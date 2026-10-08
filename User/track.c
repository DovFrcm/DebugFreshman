/*
 * track.c - 4 路灰度循迹：传感器读取 + PD 控制器 + 特殊元素状态机
 *
 * 控制流程（Track_Tick 每 2 ms 走一圈）：
 *
 *   读传感器(PB12..15) -> 消抖 -> 偏差 error -> 状态机 -> 左右轮速度 -> TIM4
 *                                    |
 *                                    +-- 四路全黑  -> CROSS  锁定直行 / 计圈
 *                                    +-- 三路"丁字"-> RING   进圆环
 *                                    +-- 只剩最外侧一路 -> SHARP 原地强转
 *                                    +-- 全白      -> GAP -> LOST（断路 / 找线）
 *                                    +-- 其余      -> RUN    PD 控制
 */

#include "track.h"
#include "motor.h"
#include "board.h"
#include "oled_font.h"      /* CN_* 汉字码点宏，Track_StateText() 要用 */

/* ================================================================== */
/* 可调参数                                                            */
/* ================================================================== */

/* 控制周期。2 ms = 500 Hz，比电机机械响应快得多，足够了。
   再快只是在浪费 CPU，不会让车跑得更稳。 */
#define TRACK_CTRL_MS           2u

/* 传感器消抖：连续多少次读到同样的值才承认它变了。
   3 次 × 2 ms = 6 ms，比任何真实的路面变化都快，但足以滤掉毛刺。 */
#define TRACK_SETTLE_TICKS      3u

/* PD 增益。误差范围是 ±300（最外侧一路压线时的极限），
   所以 Kp = 5000 时最大能贡献 ±1500 的转向量。
   调参口诀：
     车画龙（左右摆）      -> Kp 调小，或 Kd 调大
     过弯跟不上、切内线    -> Kp 调大
     进弯瞬间冲出去        -> Kd 调大（提前开始打方向）
     出弯后回正慢 / 发抖   -> Kd 调小 */
#define TRACK_KP                5000
#define TRACK_KD                1300

/* 微分项一阶低通：d = ((2^n - 1) * d + 新值) / 2^n。
   刷一次 OLED 会把某一轮控制周期从 2 ms 拖到十几毫秒，那一拍的 Δerror
   会大得离谱，不滤掉车就会莫名其妙猛打一次方向。
   n = 2 时就是 d = (3d + 新值) / 4。 */
#define TRACK_D_FILTER_SHIFT    2u
#define TRACK_D_FILTER_DEN      (1u << TRACK_D_FILTER_SHIFT)

/* 基础速度（原始 PWM 值，满量程 MOTOR_PWM_MAX = 7199）。
   1800 大约是 25% 占空比，是"能跑得起来又刹得住"的起点。
   下面这张表是巡线页按 K3 能现场切的四个档位，不用重新编译烧录。 */
#define TRACK_SPEED_DEFAULT     1800u
#define TRACK_SPEED_MIN         900u
#define TRACK_SPEED_MAX         3600u

static const uint16_t s_speedLevels[] = { 1200u, 1800u, 2400u, 3000u };
#define TRACK_SPEED_LEVELS      (sizeof(s_speedLevels) / sizeof(s_speedLevels[0]))

/* 内轮允许的最大反转量。直角弯、折线要靠它原地转过去。
   给太大车会转过头来回摆，给太小急弯过不去。 */
#define TRACK_REV_LIMIT         1500

/* 原地强转的速度 */
#define TRACK_SPIN_SPEED        1500

/* 四路全黑 -> 认为压在十字/起跑线上，锁定直行通过。
   这个时间要够车开过整条横线（横线一般比传感器阵列宽），
   太长会在真的十字上"傻冲"一段，太短则刚出横线就又被 PD 带偏。 */
#define TRACK_CROSS_MS          90u

/* 丢线后先直行冲刺的时间 —— 直线上的断路（缺口）靠它过去。 */
#define TRACK_GAP_MS            170u
#define TRACK_GAP_SPEED         1500

/* 丢线时"刚才基本是正的"的判据（偏差绝对值小于它）。
   这一条用来区分两种"全白"：
     刚才车是正的、线突然没了  -> 多半是直线上的缺口，直冲过去
     刚才车正在大角度转弯      -> 是直角/急弯，线拐走了，必须原地转
   不区分的话，直角弯上会先傻冲 170 ms，直接冲出赛道。 */
#define TRACK_GAP_CENTER_ERR    130

/* 原地找线的速度，以及找多久还没找到就放弃停车（0 = 不超时） */
#define TRACK_SEARCH_SPEED      1300u
#define TRACK_SEARCH_TIMEOUT_MS 3000u

/* 强转超时：直角/折线最多转这么久。超时说明线真的没了，转去找线。 */
#define TRACK_SHARP_TIMEOUT_MS  1200u

/* ---- 计圈 / 起跑线 ---- */

/* 两次计圈之间的最小间隔。这一条很关键：
   复杂地图上十字路口遍地都是，而它们和起跑线的传感器特征一模一样
   （都是四路全黑）。用一个"最短圈时"把它们滤掉 —— 真的跑完一圈至
   少要这么久，中间的十字不可能隔这么近。
   调法：量一下自己的赛道跑一圈要几秒，填一个明显小于它的值。 */
#define TRACK_LAP_MIN_MS        5000u

/* ---- 圆环 ---- */

/* 进环后至少待这么久才认出口，防止刚进环就把入口当成出口又冲出来 */
#define TRACK_RING_MIN_MS       700u
/* 绕这么久还没出去就强行退出，别在环里转到天荒地老 */
#define TRACK_RING_TIMEOUT_MS   8000u
/* 绕环时朝环内侧额外加的转向偏置，让车贴着圆弧走 */
#define TRACK_RING_BIAS         650

/* ================================================================== */
/* 传感器位置权重（单位 1/100 个传感器间距）                            */
/* ================================================================== */
/* S1 在 -300，S4 在 +300，中间两路各占 ±100。
   四个都压线时平均是 0（车正对着交叉点），符合直觉。 */
static const int16_t s_pos[4] = { -300, -100, 100, 300 };

/* ================================================================== */
/* 内部状态                                                            */
/* ================================================================== */

static volatile TrackState s_state = TRACK_ST_IDLE;

static uint8_t  s_raw;              /* 本次读到的原始值                 */
static uint8_t  s_stable;           /* 消抖之后的值                     */
static uint8_t  s_cand;             /* 正在被消抖确认的候选值           */
static uint8_t  s_candCount;

static int16_t  s_error;            /* 最近一次偏差                     */
static int16_t  s_lastError;
static int32_t  s_dError;           /* 低通之后的微分                   */

static uint8_t  s_lastSide;         /* 最后压线的一侧：0=左 1=右        */
static uint8_t  s_turnDir;          /* 强转方向：0=左 1=右              */
static uint8_t  s_ringDir;          /* 绕环方向：0=左 1=右              */
static uint8_t  s_ringTicks;        /* 丁字图形连续出现的拍数           */

static uint32_t s_lastCtrlMs;
static uint32_t s_stateMs;          /* 进入当前状态的时刻               */

static uint32_t s_startMs;          /* 本圈起点时刻                     */
static uint32_t s_elapsedMs;        /* 停车后冻结的用时                 */
static uint32_t s_lastLapMs;        /* 上一圈用时                       */
static uint32_t s_lastMarkMs;       /* 上一次计圈的时刻                 */
static uint8_t  s_laps;             /* 设定圈数                         */
static uint8_t  s_lapsDone;
static uint8_t  s_markArmed;        /* 已经离开黑线，可以接受下一次计圈 */

static uint16_t s_baseSpeed;

static uint8_t  s_ringEnable;
static uint8_t  s_lineMark;
static uint8_t  s_finishStop;

/* ================================================================== */
/* 传感器读取                                                          */
/* ================================================================== */

uint8_t Track_Read(void)
{
    uint32_t idr = TRACK_PORT->IDR;
    uint8_t  m;

    /* PB12..15 是连续的四个脚，一次读 IDR 就能全拿到。
       先右移到 bit0..3，再按"看见黑线是低电平"取反。 */
    m = (uint8_t)((idr >> TRACK_PIN_S1) & 0x0Fu);

#if TRACK_BLACK_IS_LOW
    m = (uint8_t)(~m & 0x0Fu);
#endif

#if TRACK_REVERSE
    /* 把 bit0<->bit3、bit1<->bit2 对调 */
    m = (uint8_t)(((m & 0x1u) << 3) | ((m & 0x2u) << 1)
                | ((m & 0x4u) >> 1) | ((m & 0x8u) >> 3));
#endif

    return m;
}

uint8_t Track_ReadStable(void)   { return s_stable; }
int16_t Track_Error(void)        { return s_error; }
TrackState Track_State(void)     { return s_state; }

uint8_t Track_IsRunning(void)
{
    return (uint8_t)((s_state != TRACK_ST_IDLE) && (s_state != TRACK_ST_FINISH));
}

/* 消抖：只有连续 TRACK_SETTLE_TICKS 次读到同样的新值才承认它变了。
   没这一步的话，传感器停在黑白边界上抖动会让 PD 的微分项疯狂跳。 */
static void Sensor_Update(void)
{
    s_raw = Track_Read();

    if (s_raw == s_stable) {
        s_candCount = 0u;
        return;
    }

    if (s_raw != s_cand) {
        s_cand = s_raw;
        s_candCount = 1u;
        return;
    }

    if (s_candCount < TRACK_SETTLE_TICKS) {
        s_candCount++;
    }

    if (s_candCount >= TRACK_SETTLE_TICKS) {
        s_stable = s_cand;
        s_candCount = 0u;
    }
}

/* 压线的路数 */
static uint8_t Popcount(uint8_t mask)
{
    uint8_t n = 0u;
    uint8_t i;

    for (i = 0u; i < 4u; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0u) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 偏差：四个传感器的加权平均                                          */
/* ------------------------------------------------------------------ */
/* 没压线时返回 -1000（一个不可能出现的值），调用方靠它判断丢线。 */
static int16_t ComputeError(uint8_t mask)
{
    int32_t sum = 0;
    uint8_t cnt = 0u;
    uint8_t i;

    for (i = 0u; i < 4u; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0u) {
            sum += s_pos[i];
            cnt++;
        }
    }

    if (cnt == 0u) {
        return -1000;
    }

    return (int16_t)(sum / (int32_t)cnt);
}

/* 压线偏多的一侧是左还是右。平手时保持上一次的判断，
   免得车正对着线的时候判断在左右之间来回跳。 */
static void UpdateLastSide(uint8_t mask)
{
    uint8_t left  = Popcount((uint8_t)(mask & 0x03u));   /* S1、S2 压了几路 */
    uint8_t right = Popcount((uint8_t)(mask & 0x0Cu));   /* S3、S4 压了几路 */

    if (left > right) {
        s_lastSide = 0u;
    } else if (right > left) {
        s_lastSide = 1u;
    }
    /* 相等就保持不变 */
}

/* ================================================================== */
/* 电机动作                                                            */
/* ================================================================== */

static void EnterState(TrackState st)
{
    s_state   = st;
    s_stateMs = Tick_Ms();
}

static void DriveForward(uint16_t speed)
{
    Motor_SetLR((int16_t)speed, (int16_t)speed);
}

static void SpinLeft(void)
{
    /* 左轮后退、右轮前进 = 原地左转（车头往左偏） */
    Motor_SetLR(-(int16_t)TRACK_SPIN_SPEED, (int16_t)TRACK_SPIN_SPEED);
}

static void SpinRight(void)
{
    Motor_SetLR((int16_t)TRACK_SPIN_SPEED, -(int16_t)TRACK_SPIN_SPEED);
}

static void SpinDir(uint8_t dir)
{
    if (dir == 0u) {
        SpinLeft();
    } else {
        SpinRight();
    }
}

/* ================================================================== */
/* 蜂鸣器 / 计圈                                                       */
/* ================================================================== */

/* 蜂鸣器响一声。发车提示和每圈提示都用它。
   这里是阻塞式的（约 45 ms）。巡线途中车正在跑，但这 45 ms 里 PWM 硬件
   还在按上一拍的占空比输出，车不会停；换来的是"响一声"百分之百做得到，
   不会因为主循环正忙而漏掉 —— 而这一声是有分的。 */
static void Track_Beep(void)
{
    Board_BeepWrite(1u);
    Delay_Ms(45u);
    Board_BeepWrite(0u);
}

/* 这次"四路全黑"算不算一次过起跑线？两道闸门：
     1) 必须已经离开过黑线（s_markArmed），否则压在一条宽横线上会被连数好几次
     2) 距离上一次计圈要超过 TRACK_LAP_MIN_MS，用来滤掉地图中间的十字路口 */
static uint8_t Mark_IsLap(uint32_t now)
{
    if (s_markArmed == 0u) {
        return 0u;
    }
    if ((uint32_t)(now - s_lastMarkMs) < TRACK_LAP_MIN_MS) {
        return 0u;
    }
    return 1u;
}

/* 跑完一圈。注意它可能把状态改成 FINISH（最后一圈跑完），调用方要检查。 */
static void Track_OnLapDone(uint32_t now)
{
    s_lastLapMs  = (uint32_t)(now - s_startMs);
    s_lapsDone++;
    s_lastMarkMs = now;
    s_markArmed  = 0u;

    if (s_lapsDone >= s_laps) {
        /* 最后一圈跑完：冻结用时 */
        s_elapsedMs = s_lastLapMs;

        if (s_finishStop != 0u) {
            Motor_Stop();
            EnterState(TRACK_ST_FINISH);
            Track_Beep();
            return;
        }
        /* 关掉了自动停车：当普通一圈处理，继续跑 */
    }

    /* 还有下一圈：响一声提示，但**不停车**，计时从这一刻重新开始 */
    Track_Beep();
    s_startMs = now;
}

/* ================================================================== */
/* 各个状态的控制器                                                    */
/* ================================================================== */

/* 正常循迹：算 PD，给左右轮差速 */
static void Control_Pd(uint8_t mask)
{
    int32_t err = (int32_t)s_error;
    int32_t dRaw;
    int32_t turn;
    int32_t left;
    int32_t right;

    (void)mask;

    /* 微分项先做一阶低通，滤掉控制周期抖动带来的假微分 */
    dRaw = err - (int32_t)s_lastError;
    s_lastError = (int16_t)err;
    s_dError = (((int32_t)(TRACK_D_FILTER_DEN - 1u) * s_dError) + dRaw)
             / (int32_t)TRACK_D_FILTER_DEN;

    turn = (((int32_t)TRACK_KP * err) + ((int32_t)TRACK_KD * s_dError)) / 1000;

    left  = (int32_t)s_baseSpeed + turn;
    right = (int32_t)s_baseSpeed - turn;

    /* 外侧不超过满量程；内侧允许反转，急弯才转得过来 */
    if (left > (int32_t)MOTOR_PWM_MAX)   { left  = (int32_t)MOTOR_PWM_MAX; }
    if (right > (int32_t)MOTOR_PWM_MAX)  { right = (int32_t)MOTOR_PWM_MAX; }
    if (left < -(int32_t)TRACK_REV_LIMIT)  { left  = -(int32_t)TRACK_REV_LIMIT; }
    if (right < -(int32_t)TRACK_REV_LIMIT) { right = -(int32_t)TRACK_REV_LIMIT; }

    Motor_SetLR((int16_t)left, (int16_t)right);
}

/* 十字 / 起跑线：锁定直行，别让 PD 在路口被某个读数带偏 */
static void Control_Cross(uint32_t now)
{
    if ((uint32_t)(now - s_stateMs) > TRACK_CROSS_MS) {
        s_dError    = 0;
        s_lastError = 0;
        EnterState(TRACK_ST_RUN);
        return;
    }
    DriveForward((uint16_t)s_baseSpeed);
}

/* 强转：朝指定方向原地转，直到中间两路重新压线 */
static void Control_Sharp(uint32_t now, uint8_t mask)
{
    /* 中间两路任意一路重新压线，就说明已经咬住线了 */
    if ((mask & 0x06u) != 0u) {
        s_dError    = 0;
        s_lastError = s_error;
        EnterState(TRACK_ST_RUN);
        return;
    }

    if ((uint32_t)(now - s_stateMs) > TRACK_SHARP_TIMEOUT_MS) {
        EnterState(TRACK_ST_LOST);
        return;
    }

    SpinDir(s_turnDir);
}

/* 丢线冲刺：保持直行一小段时间，把直线上的缺口冲过去 */
static void Control_Gap(uint32_t now)
{
    if ((uint32_t)(now - s_stateMs) > TRACK_GAP_MS) {
        EnterState(TRACK_ST_LOST);
        return;
    }
    DriveForward((uint16_t)TRACK_GAP_SPEED);
}

/* 原地找线：朝最后看到线的那一侧转 */
static void Control_Lost(uint32_t now)
{
#if TRACK_SEARCH_TIMEOUT_MS != 0u
    if ((uint32_t)(now - s_stateMs) > TRACK_SEARCH_TIMEOUT_MS) {
        Motor_Stop();
        s_elapsedMs = (uint32_t)(now - s_startMs);
        EnterState(TRACK_ST_FINISH);
        return;
    }
#endif
    SpinDir(s_lastSide);
}

/* 绕圆环：正常 PD + 朝环内的固定偏置，直到再次看到"丁字"就是出口 */
static void Control_Ring(uint32_t now, uint8_t mask)
{
    uint32_t inRing = (uint32_t)(now - s_stateMs);

    /* 出口判据：进环待够 TRACK_RING_MIN_MS 之后，再次出现"丁字"图形。
       四路全黑的情形在 Track_Tick 里已经被拦下并转到 CROSS 了，
       那条路径同样会直行冲出去，正好也是我们要的动作。 */
    if ((inRing > TRACK_RING_MIN_MS) && ((mask == 0x0Eu) || (mask == 0x07u))) {
        EnterState(TRACK_ST_CROSS);
        Control_Cross(now);
        return;
    }

    /* 兜底：绕太久说明判据失效了，回正常循迹，别一直转 */
    if (inRing > TRACK_RING_TIMEOUT_MS) {
        EnterState(TRACK_ST_RUN);
        return;
    }

    Control_Pd(mask);

    /* 在 PD 的结果上再加一点朝环内的固定偏置，让车老实贴着圆弧走。
       方向不能搞反：
         环在右边 -> 要一直往右拐 -> 左轮加快、右轮减慢
         环在左边 -> 要一直往左拐 -> 左轮减慢、右轮加快
       偏置反了的话，车会在环里走直线然后从对面穿出去。 */
    if (s_ringDir == 0u) {
        /* 左环：往左拐 */
        Motor_SetLR((int16_t)(Motor_GetLeft() - TRACK_RING_BIAS),
                    (int16_t)(Motor_GetRight() + TRACK_RING_BIAS));
    } else {
        /* 右环：往右拐 */
        Motor_SetLR((int16_t)(Motor_GetLeft() + TRACK_RING_BIAS),
                    (int16_t)(Motor_GetRight() - TRACK_RING_BIAS));
    }
}

/* 圆环入口判据：正好三路压线的"丁字"图形。
   返回 0 = 往左进环，1 = 往右进环，0xFF = 不是环入口。
   位序是 bit0=S1(最左) .. bit3=S4(最右)：
     0x07 = S1+S2+S3 压线、S4 空 -> 多出来的一支在左边 -> 左环
     0x0E = S2+S3+S4 压线、S1 空 -> 多出来的一支在右边 -> 右环 */
static uint8_t Ring_EntryDir(uint8_t mask)
{
    if (mask == 0x07u) {
        return 0u;
    }
    if (mask == 0x0Eu) {
        return 1u;
    }
    return 0xFFu;
}

/* ================================================================== */
/* 主控制器                                                            */
/* ================================================================== */

void Track_Tick(void)
{
    uint32_t now = Tick_Ms();
    uint8_t  mask;
    uint8_t  ringDir;
    uint8_t  allBlack;
    uint8_t  allWhite;

    /* 限频到 TRACK_CTRL_MS，让控制周期固定，PD 的微分项才有意义 */
    if ((uint32_t)(now - s_lastCtrlMs) < TRACK_CTRL_MS) {
        return;
    }
    s_lastCtrlMs = now;

    Sensor_Update();
    mask = s_stable;

    /* 只要不是全黑，就说明车已经离开了横线，可以接受下一次计圈 */
    if (mask != 0x0Fu) {
        s_markArmed = 1u;
    }

    s_error = ComputeError(mask);

    /* 没发车（或已经跑完）：只更新传感器显示，一根线都不碰电机 */
    if ((s_state == TRACK_ST_IDLE) || (s_state == TRACK_ST_FINISH)) {
        return;
    }

    UpdateLastSide(mask);

    allBlack = (uint8_t)(mask == 0x0Fu);
    allWhite = (uint8_t)(mask == 0x00u);

    /* ---------------- 全局优先事件：四路全黑 ---------------- */
    /* 十字路口和起跑线的传感器特征完全一样，处理办法也一样：
       先判要不要计圈，然后锁定直行冲过去。 */
    if ((allBlack != 0u) && (s_state != TRACK_ST_CROSS)) {
        /* 只有"正常循迹中"压到全黑才算过起跑线。
           绕环、强转、找线途中压到全黑，那是元素本身，不能算一圈。 */
        if ((s_lineMark != 0u) && (s_state == TRACK_ST_RUN) && (Mark_IsLap(now) != 0u)) {
            Track_OnLapDone(now);
            if (s_state == TRACK_ST_FINISH) {
                return;         /* 跑完了，车已经停了，别再往下控电机 */
            }
            /* 计圈成功会响一声蜂鸣器，那是个阻塞操作，一走就是几十毫秒。
               上面那个 now 已经过期了，必须重新取一次时间再进 CROSS ——
               否则 EnterState() 记下的时刻会比 now 还新，
               Control_Cross() 里 (now - s_stateMs) 一减就变成一个巨大的
               无符号数，十字直行会被立刻判定为超时，车还没过路口就交还
               给 PD 了。 */
            now = Tick_Ms();
        }
        EnterState(TRACK_ST_CROSS);
        Control_Cross(now);
        return;
    }

    /* ---------------- 状态机 ---------------- */

    switch (s_state) {
    case TRACK_ST_CROSS:
        Control_Cross(now);
        break;

    case TRACK_ST_GAP:
        Control_Gap(now);
        break;

    case TRACK_ST_SHARP:
        Control_Sharp(now, mask);
        break;

    case TRACK_ST_RING:
        Control_Ring(now, mask);
        break;

    case TRACK_ST_LOST:
        if (allWhite == 0u) {
            s_dError    = 0;
            s_lastError = s_error;
            EnterState(TRACK_ST_RUN);
        } else {
            Control_Lost(now);
        }
        break;

    case TRACK_ST_RUN:
    default:
        if (allWhite != 0u) {
            /* 全白。分两种情况，判据是"刚才车是不是基本正的"：
                 刚才基本正 -> 直线上的缺口，直冲过去（断路）
                 刚才在转弯 -> 是直角/急弯，线拐走了，立即原地转
               不区分的话直角弯上会先傻冲一段，直接冲出赛道。 */
            if ((s_lastError > -TRACK_GAP_CENTER_ERR)
                && (s_lastError < TRACK_GAP_CENTER_ERR)) {
                EnterState(TRACK_ST_GAP);
                Control_Gap(now);
            } else {
                s_turnDir = s_lastSide;
                EnterState(TRACK_ST_SHARP);
                Control_Sharp(now, mask);
            }
            break;
        }

        /* 圆环入口：连续几拍都是"丁字"图形才认，避免斜着压线误判 */
        if (s_ringEnable != 0u) {
            ringDir = Ring_EntryDir(mask);
            if (ringDir != 0xFFu) {
                s_ringTicks++;
                if (s_ringTicks >= 3u) {
                    s_ringDir   = ringDir;
                    s_ringTicks = 0u;
                    EnterState(TRACK_ST_RING);
                    Control_Ring(now, mask);
                    break;
                }
            } else {
                s_ringTicks = 0u;
            }
        }

        /* 只剩最外侧一路压线：线急转走了，原地强转跟上 */
        if ((mask == 0x01u) || (mask == 0x08u)) {
            s_turnDir = (uint8_t)((mask == 0x01u) ? 0u : 1u);
            EnterState(TRACK_ST_SHARP);
            Control_Sharp(now, mask);
            break;
        }

        Control_Pd(mask);
        break;
    }
}

/* ================================================================== */
/* 发车 / 停车                                                         */
/* ================================================================== */

void Track_Start(void)
{
    if (Track_IsRunning() != 0u) {
        return;         /* 已经在跑，别把计时清零 */
    }

    /* 先响一声再动车。题目要求"发车时蜂鸣器响一声"，
       先响后动也比"先冲出去再响"安全。 */
    Track_Beep();

    s_startMs    = Tick_Ms();
    s_lastMarkMs = s_startMs;
    s_elapsedMs  = 0u;
    s_lastLapMs  = 0u;
    s_lapsDone   = 0u;
    s_markArmed  = 0u;      /* 起跑线本身不该被算成一圈 */
    s_error      = 0;
    s_lastError  = 0;
    s_dError     = 0;
    s_ringTicks  = 0u;

    EnterState(TRACK_ST_RUN);
}

void Track_Stop(void)
{
    Motor_Stop();

    if (Track_IsRunning() != 0u) {
        s_elapsedMs = (uint32_t)(Tick_Ms() - s_startMs);
    }

    s_state     = TRACK_ST_IDLE;
    s_error     = 0;
    s_lastError = 0;
    s_dError    = 0;
    s_ringTicks = 0u;
}

/* ================================================================== */
/* 参数                                                                */
/* ================================================================== */

void    Track_SetFinishStop(uint8_t on) { s_finishStop = (uint8_t)(on != 0u); }
uint8_t Track_GetFinishStop(void)       { return s_finishStop; }

void Track_SetLaps(uint8_t laps)
{
    if (laps < 1u) { laps = 1u; }
    if (laps > 9u) { laps = 9u; }
    s_laps = laps;
}
uint8_t Track_GetLaps(void)             { return s_laps; }

void Track_SetBaseSpeed(uint16_t s)
{
    if (s < TRACK_SPEED_MIN) { s = TRACK_SPEED_MIN; }
    if (s > TRACK_SPEED_MAX) { s = TRACK_SPEED_MAX; }
    s_baseSpeed = s;
}
uint16_t Track_GetBaseSpeed(void)       { return s_baseSpeed; }

void    Track_SetRingEnable(uint8_t on) { s_ringEnable = (uint8_t)(on != 0u); }
uint8_t Track_GetRingEnable(void)       { return s_ringEnable; }

/* 在 s_speedLevels[] 里往上切一档，到头绕回最慢。
   巡线页的 K3 和"巡线设置"页的速度项都调它，两边行为永远一致。
   当前速度不在表里（比如用串口 SPEED 设了个 2500），就从最慢那档重新开始。 */
void Track_CycleSpeed(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)TRACK_SPEED_LEVELS; i++) {
        if (s_baseSpeed == s_speedLevels[i]) {
            Track_SetBaseSpeed(s_speedLevels[(uint8_t)((i + 1u) % TRACK_SPEED_LEVELS)]);
            return;
        }
    }

    Track_SetBaseSpeed(s_speedLevels[0]);
}

/* 圈数在 1 和 2 之间来回切（题目 4b 一圈 / 4c 两圈） */
void Track_CycleLaps(void)
{
    Track_SetLaps((uint8_t)((s_laps >= 2u) ? 1u : 2u));
}

void    Track_SetLineMark(uint8_t on)   { s_lineMark = (uint8_t)(on != 0u); }
uint8_t Track_GetLineMark(void)         { return s_lineMark; }

uint32_t Track_ElapsedMs(void)
{
    if (Track_IsRunning() != 0u) {
        return (uint32_t)(Tick_Ms() - s_startMs);
    }
    return s_elapsedMs;
}

uint32_t Track_LastLapMs(void)  { return s_lastLapMs; }

/* 当前正在跑第几圈，从 1 开始。
   跑完之后不要再往上加，否则屏幕上会显示"圈 2/1"这种读不通的东西。 */
uint8_t  Track_LapIndex(void)
{
    uint8_t n = (uint8_t)(s_lapsDone + 1u);

    return (n > s_laps) ? s_laps : n;
}

uint8_t  Track_LapsDone(void)   { return s_lapsDone; }

/* ================================================================== */
/* 文字                                                                */
/* ================================================================== */
/* 状态名用码点数组写（和工程其它地方一样，保持源文件纯 ASCII）。
   这些汉字都在 oled_font.h 的字库里，加字方法见 README 第 7 章。 */

static const uint16_t T_IDLE[]   = { CN_JIU, CN_XU, 0 };                /* 就绪 */
static const uint16_t T_RUN[]    = { CN_YUN, CN_XING, 0 };              /* 运行 */
static const uint16_t T_CROSS[]  = { CN_SHI5, CN_ZI, 0 };               /* 十字 */
static const uint16_t T_SHARP[]  = { CN_ZHUAN, CN_WAN, 0 };             /* 转弯 */
static const uint16_t T_GAP[]    = { CN_DUAN, CN_XIAN2, 0 };            /* 断线 */
static const uint16_t T_LOST[]   = { CN_ZHAO, CN_XIAN2, 0 };            /* 找线 */
static const uint16_t T_RING[]   = { CN_YUAN, CN_HUAN, 0 };             /* 圆环 */
static const uint16_t T_FINISH[] = { CN_WAN2, CN_CHENG, 0 };            /* 完成 */

const uint16_t *Track_StateText(void)
{
    switch (s_state) {
    case TRACK_ST_RUN:    return T_RUN;
    case TRACK_ST_CROSS:  return T_CROSS;
    case TRACK_ST_SHARP:  return T_SHARP;
    case TRACK_ST_GAP:    return T_GAP;
    case TRACK_ST_LOST:   return T_LOST;
    case TRACK_ST_RING:   return T_RING;
    case TRACK_ST_FINISH: return T_FINISH;
    case TRACK_ST_IDLE:
    default:              return T_IDLE;
    }
}

const char *Track_StateTag(void)
{
    switch (s_state) {
    case TRACK_ST_RUN:    return "RUN";
    case TRACK_ST_CROSS:  return "CROSS";
    case TRACK_ST_SHARP:  return "SHARP";
    case TRACK_ST_GAP:    return "GAP";
    case TRACK_ST_LOST:   return "LOST";
    case TRACK_ST_RING:   return "RING";
    case TRACK_ST_FINISH: return "FINISH";
    case TRACK_ST_IDLE:
    default:              return "IDLE";
    }
}

/* 串口用的位串，bit3 打在左边，也就是 "S1S2S3S4" 的顺序 ——
   和 OLED 上从左到右看到的顺序一致，对着屏幕就能核对。 */
const char *Track_BitsText(void)
{
    static char str[5];
    uint8_t i;

    for (i = 0u; i < 4u; i++) {
        str[i] = ((s_stable & (uint8_t)(0x8u >> i)) != 0u) ? '1' : '0';
    }
    str[4] = '\0';

    return str;
}

/* ================================================================== */
/* 初始化                                                              */
/* ================================================================== */

void Track_Init(void)
{
    uint8_t i;

    /* PB12..PB15 配成上拉输入：有些灰度模块的输出是开集电极的，
       没有上拉会一直读到 0，四个全"压线"，非常误导。 */
    for (i = 0u; i < 4u; i++) {
        uint8_t pin = (uint8_t)(TRACK_PIN_S1 + i);

        GPIO_ConfigPin(TRACK_PORT, pin, GPIO_CFG_IN_PUPD);
        TRACK_PORT->BSRR = BIT(pin);        /* ODR 置 1 = 选上拉 */
    }

    s_state      = TRACK_ST_IDLE;
    s_raw        = 0u;
    s_stable     = 0u;
    s_cand       = 0u;
    s_candCount  = 0u;
    s_error      = 0;
    s_lastError  = 0;
    s_dError     = 0;
    s_lastSide   = 0u;
    s_turnDir    = 0u;
    s_ringDir    = 0u;
    s_ringTicks  = 0u;
    s_lastCtrlMs = 0u;
    s_stateMs    = 0u;
    s_startMs    = 0u;
    s_elapsedMs  = 0u;
    s_lastLapMs  = 0u;
    s_lastMarkMs = 0u;
    s_laps       = 1u;
    s_lapsDone   = 0u;
    s_markArmed  = 0u;
    s_baseSpeed  = TRACK_SPEED_DEFAULT;
    s_ringEnable = 1u;
    s_lineMark   = 1u;
    s_finishStop = 1u;
}
