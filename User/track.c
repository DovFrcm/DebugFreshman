/*
 * track.c - 循迹控制：传感器读取（4/7 路可切）+ 简单转向 + 特殊元素状态机
 *
 * 控制流程（Track_Tick 每 2 ms 走一圈）：
 *
 *   读传感器 -> 消抖 -> 偏差 error -> 状态机 -> 左右轮速度 -> TIM4
 *                          |             |
 *                          |             +-- 全部压线  -> CROSS  锁定直行 / 计圈
 *                          |             +-- 圆环"丁字"-> RING   进圆环
 *                          |             +-- 只剩最外侧 -> SHARP  原地强转
 *                          |             +-- 全白      -> GAP -> LOST（断路 / 找线）
 *                          |             +-- 其余      -> RUN    简单转向判断
 *                          |
 *                          +-- error 只当状态机判据（"刚才车正不正"）和串口显示，
 *                              **不参与控制**
 *
 * 正常循迹不用 PD：中间几路直行，线偏到哪一侧就把另一侧的轮子加快一段时间
 * （见 Control_Turn 和 TRACK_TURN_MS / TRACK_TURN_BOOST）。
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

/* ⚠ 下面这几个 PD 参数**当前没有在用**。
   正常循迹已经换成"中间直行 + 偏了就加速另一侧轮子"的规则（见 Control_Turn
   和 TRACK_TURN_MS / TRACK_TURN_BOOST）。这几个宏先留着，是想改回 PD 的时候
   方便；真要回去的话：把 Control_Turn() 换回原来的 PD 实现，并把 s_dError
   这个静态变量一起加回来（它已经从工程里删掉了，留着会被编译器报
   "was set but never used"）。

   原来的 PD 增益：误差范围 ±300，Kp = 5000 时最大贡献 ±1500 的转向量。 */
#define TRACK_KP                5000
#define TRACK_KD                1300

/* 微分项一阶低通：d = ((2^n - 1) * d + 新值) / 2^n。
   刷一次 OLED 会把某一轮控制周期从 2 ms 拖到十几毫秒，那一拍的 Δerror
   会大得离谱，不滤掉车就会莫名其妙猛打一次方向。
   n = 2 时就是 d = (3d + 新值) / 4。 */
#define TRACK_D_FILTER_SHIFT    2u
#define TRACK_D_FILTER_DEN      (1u << TRACK_D_FILTER_SHIFT)

/* 基础速度（原始 PWM 值，满量程 MOTOR_PWM_MAX = 7199）。
   当前调成"慢速"：900 大约 12.5% 占空比，方便盯着屏幕看传感器和
   状态的变化，出问题也来得及反应。
   （先前试过 700，太接近这类减速电机的起步下限，容易起步发抖/原地不动，
   所以抬到 900。再慢就要先确认车还能不能动。）
   下面这张表是巡线页按 K3 能现场切的四个档位，不用重新编译烧录。
   默认值一定要是表里的一项，否则第一次按 K3 会直接跳到表头那一档。 */
#define TRACK_SPEED_DEFAULT     900u
#define TRACK_SPEED_MIN         400u
#define TRACK_SPEED_MAX         3600u

static const uint16_t s_speedLevels[] = { 700u, 900u, 1200u, 1500u };
#define TRACK_SPEED_LEVELS      (sizeof(s_speedLevels) / sizeof(s_speedLevels[0]))

/* ---- 正常循迹的转向修正（中间直行 + 偏了就加速另一侧轮子）---- */

/* 一次修正**至少**持续多久。这是整套策略里最关键的参数：
   给太小 -> 刚加速就停，修正量不够，车会慢慢地一直偏；
   给太大 -> 一次修过头，然后反向修，车开始画龙。
   经验值：让这一段时间内车走过大约 20~40mm。
   按实测车速 v 算：TRACK_TURN_MS ≈ 30mm / v × 1000。
   （例：v = 250 mm/s -> 约 120ms） */
#define TRACK_TURN_MS           80u

/* 修正时把那一侧的轮子加快多少（原始 PWM 值）。
   基础速度 TRACK_SPEED_DEFAULT 是 900，这里给 1800 就是那一侧跑到 2700（三倍），
   差速很猛。车反应太慢、压不住线 -> 再调大；一修就甩过头、左右摆 -> 调小。
   ⚠ 这个值要跟着基础速度一起看：基础速度改了，差速的比例也就变了。 */
#define TRACK_TURN_BOOST        1800

/* ⚠ TRACK_REV_LIMIT 当前没有在用：正常循迹已经不允许内轮反转了
   （Control_Turn 只封上限，不倒转），急弯的原地转全部交给 SHARP 状态。
   留着是为了以后想恢复"内轮反向帮过弯"时有现成的值。 */
#define TRACK_REV_LIMIT         1500

/* 原地强转的速度（只剩最外侧一路压线时用，见 Control_Sharp） */
#define TRACK_SPIN_SPEED        1500

/* **全部传感器都压线**（7 路全黑 = 0x7F）-> 认为压在十字/起跑线上，锁定直行通过。
   ⚠ 这条判据优先级最高，一旦成立就完全不转向 —— 所以如果因为传感器高度/阈值
     没调好导致白底被读成全黑，车会被一直卡在这个状态里"直着冲"，
     表现就是"小车根本不修正"。排查时先看屏幕上第 0 行是不是一直显示"十字"。
   这个时间要够车开过整条横线（横线一般比传感器阵列宽），
   太长会在真的十字上"傻冲"一段，太短则刚出横线就又被转向修正带偏。 */
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
/* 左右对称，最外侧固定为 ±300（和四路时一致，所以原来的 Kp/Kd 和
   TRACK_GAP_CENTER_ERR 这些阈值都不用重调）。
     4 路：间距归一化成 200 -> { -300, -100, +100, +300 }
     7 路：间距归一化成 100 -> { -300, -200, -100, 0, +100, +200, +300 }
   全压线时平均是 0（车正对着交叉点），符合直觉。 */
#if TRACK_SENSOR_COUNT >= 7
static const int16_t s_pos[TRACK_SENSOR_COUNT] =
    { -300, -200, -100, 0, 100, 200, 300 };
#else
static const int16_t s_pos[TRACK_SENSOR_COUNT] = { -300, -100, 100, 300 };
#endif

/* ================================================================== */
/* 掩码图形：各元素判据用的位组合                                      */
/* ================================================================== */
/* 掩码位序：bit0 = S1（最左）.. bit(N-1) = SN（最右），1 = 压到黑线。
   下面这几组图形换传感器路数时要跟着改，改完到现场拿「巡线功能」页
   或串口 SENSORS 对着实际元素读一遍就能校准。 */

/* "咬住线了"的中间几路。4 路是中间两路，7 路是中间三路。 */
#if TRACK_SENSOR_COUNT >= 7
#define TRACK_MASK_MID      0x1Cu   /* S3+S4+S5 */
#else
#define TRACK_MASK_MID      0x06u   /* S2+S3     */
#endif

/* 直角 / 折线的判据：**中线一侧"那一侧全部"几路都黑、而正中间是空的**。
   含义是"前面已经没线了，线拐到一边去了" —— 该原地 90° 强转了。

   为什么要求"正中间是空的"：正中间也黑（0x0F / 0x78）说明主线还在，
   那是 T 字路口 / 圆环入口，由 TRACK_RING_PAT_* 那套管，不归这里。

   为什么不是"一侧任意一路黑"：线在直线上正常漂移时也会压到侧面的路，
   那种情况只要温和修正就行（Control_Turn），原地 90° 转就过头了。
   要**整侧全黑**才说明是拐角。 */
#if TRACK_SENSOR_COUNT >= 7
#define TRACK_MASK_LEFT3    0x07u   /* S1+S2+S3：左侧整排 */
#define TRACK_MASK_RIGHT3   0x70u   /* S5+S6+S7：右侧整排 */
#define TRACK_MASK_CENTER   0x08u   /* S4：正中间       */
#else
/* 四路没有"一侧三路"这回事，退化成"只剩最外侧一路"，
   和原来的判据完全一致，行为不变。 */
#define TRACK_MASK_LEFT3    0x01u   /* S1 */
#define TRACK_MASK_RIGHT3   0x08u   /* S4 */
#define TRACK_MASK_CENTER   0x06u   /* S2+S3 */
#endif

/* 注意：判断"线偏左还是偏右"**不是**用"左边那几路有没有压到"这种宏，
   而是用 MaskLean() 数中线左右各压了几路再比大小。原因见 MaskLean() 上面
   那段注释 —— 直线上一般两路同时压线，常常是 S3+S4 / S4+S5 这种跨中线的
   图形，用"有没有"判会把它们误判成正中。 */

/* 圆环入口的"丁字"图形：主线还在（中间那路压着），一侧多出一支。
   4 路时是"正好三路压线"：0x07 = S1+S2+S3（左）、0x0E = S2+S3+S4（右）。
   7 路时中间是 S4，所以左边一支亮 S1~S3、右边一支亮 S5~S7。
   ⚠ 这两个值最好现场校准：把车停在六边形环的入口，看串口 SENSORS 读出来
     的图形，把它填到这里。填错了表现为"进不去环"或"直线也误入环"。 */
#if TRACK_SENSOR_COUNT >= 7
#define TRACK_RING_PAT_LEFT     0x0Fu   /* S1+S2+S3+S4：多出来的一支在左 */
#define TRACK_RING_PAT_RIGHT    0x78u   /* S4+S5+S6+S7：多出来的一支在右 */
#else
#define TRACK_RING_PAT_LEFT     0x07u   /* S1+S2+S3 */
#define TRACK_RING_PAT_RIGHT    0x0Eu   /* S2+S3+S4 */
#endif

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

static uint8_t  s_lastSide;         /* 最后压线的一侧：0=左 1=右        */
static uint8_t  s_turnDir;          /* 强转方向：0=左 1=右              */
static uint8_t  s_ringDir;          /* 绕环方向：0=左 1=右              */
static uint8_t  s_ringTicks;        /* 丁字图形连续出现的拍数           */

/* 转向修正的状态：正在修哪一侧、修到什么时候。
   s_turnSide: 0xFF = 没在修（直行）, 0 = 线偏左正在加快右轮, 1 = 线偏右加快左轮 */
static uint8_t  s_turnSide;
static uint32_t s_turnUntilMs;      /* 这次修正的最早结束时刻           */

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
/* 引脚表：顺序就是车头方向从左到右 S1..SN。
   ⚠ 这张表必须和实际接线一一对应 —— 以后改接线只改这里。
   4 路和 7 路的前四路完全相同（PB12~PB15），所以两种接法之间来回切换
   不用重新焊线。
   这张表在 track.h 里导出，宿主端测试的假硬件也用它。 */
const SensorPin g_trackSensorPins[TRACK_SENSOR_COUNT] = {
#if TRACK_SENSOR_COUNT >= 7
    { GPIOB, 12u },     /* S1  最左 */
    { GPIOB, 13u },     /* S2       */
    { GPIOB, 14u },     /* S3       */
    { GPIOB, 15u },     /* S4       */
    { GPIOA,  6u },     /* S5       */
    { GPIOA,  7u },     /* S6       */
    { GPIOB,  0u }      /* S7  最右 */
#else
    { GPIOB, 12u },     /* S1  最左 */
    { GPIOB, 13u },     /* S2       */
    { GPIOB, 14u },     /* S3       */
    { GPIOB, 15u }      /* S4  最右 */
#endif
};

/* 掩码全是 1 的值：4 路是 0x0F，7 路是 0x7F */
#define TRACK_MASK_ALL      ((uint8_t)((1u << TRACK_SENSOR_COUNT) - 1u))

/* ⚠ 现场实测（2026-10，七路模块）：十字口读出来是**全白**、断线口读出来是
   **全黑** —— 和代码原本假设的正好相反，所以把"十字/断线"两个判据对调：
     全白 -> 十字（锁定直行 + 计圈）
     全黑 -> 断线（直冲过去）
   这是按实测做的**判据对调**，只影响这两个"全黑/全白"元素的归属，
   转向修正、直角、圆环的判据一概不动。
   以后传感器（高度/阈值）调正常了，把这里改回 0 即可。
   四路配置保持原样（那是老模块的老实测）。 */
#if TRACK_SENSOR_COUNT >= 7
#define TRACK_CROSS_GAP_SWAPPED  1
#else
#define TRACK_CROSS_GAP_SWAPPED  0
#endif

#if TRACK_REVERSE
/* 把整条掩码左右对调，对任意路数都成立 */
static uint8_t ReverseMask(uint8_t m)
{
    uint8_t r = 0u;
    uint8_t i;

    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        if ((m & (uint8_t)(1u << i)) != 0u) {
            r |= (uint8_t)(1u << (TRACK_SENSOR_COUNT - 1u - i));
        }
    }
    return r;
}
#endif

uint8_t Track_Read(void)
{
    uint8_t m = 0u;
    uint8_t i;

    /* 逐路查表读。7 路跨了 GPIOA / GPIOB 两个口，所以不能像四路那样
       一次 IDR 移位搞定；每个控制周期读 7 个脚，开销可以忽略。 */
    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        if ((g_trackSensorPins[i].port->IDR & BIT(g_trackSensorPins[i].pin)) != 0u) {
            m |= (uint8_t)(1u << i);
        }
    }

#if TRACK_BLACK_IS_LOW
    /* 模块是"看见黑线拉低"，取反成"1 = 压线" */
    m = (uint8_t)(~m & TRACK_MASK_ALL);
#endif

#if TRACK_REVERSE
    m = ReverseMask(m);
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

/* 直角 / 折线入口判据：**一侧整排全黑、正中间是空的**。
   返回 0 = 往左 90° 转，1 = 往右 90° 转，0xFF = 不是拐角。

   7 路时就是 0x07（S1+S2+S3 全黑）和 0x70（S5+S6+S7 全黑）。
   4 路时退化成"只剩最外侧一路"（0x01 / 0x08），和原来的判据一致。

   触发后走的是 SHARP 状态：原地朝那一侧转，直到正中间那一路重新压到线 ——
   对直角来说正好就是转够 90° 之后咬住新方向的那条线。 */
static uint8_t Sharp_EntryDir(uint8_t mask)
{
    /* 左侧整排黑 + 右边一路都没黑 + 正中间空 -> 线拐到左边去了 */
    if (((mask & TRACK_MASK_LEFT3) == TRACK_MASK_LEFT3)
        && ((mask & TRACK_MASK_RIGHT3) == 0u)
        && ((mask & TRACK_MASK_CENTER) == 0u)) {
        return 0u;
    }

    /* 镜像 */
    if (((mask & TRACK_MASK_RIGHT3) == TRACK_MASK_RIGHT3)
        && ((mask & TRACK_MASK_LEFT3) == 0u)
        && ((mask & TRACK_MASK_CENTER) == 0u)) {
        return 1u;
    }

    return 0xFFu;
}

/* 消抖：只有连续 TRACK_SETTLE_TICKS 次读到同样的新值才承认它变了。
   没这一步的话，传感器停在黑白边界上抖动会让转向判断来回猛跳。 */
static void Sensor_Update(void)
{
    s_raw = Track_Read();

    /* ---------------- 快通道：直角/折线不等消抖 ----------------
       消抖数的是"连续多少次**调用**"，不是多少毫秒。而刷一次 OLED 会把主循环
       整整卡住十几毫秒（见 app.c 的说明），所以这几次调用实际可能横跨
       三五十毫秒。

       车跑起来冲过直角口时，"一侧整排全黑"这个图形本身只存在
       （线宽 ÷ 车速）那么久 —— 18mm 的线、几百 mm/s 的车速，也就几十毫秒。
       等消抖走完图形早没了，于是**用手慢慢推能识别、跑起来识别不到**。

       这类图形要求一侧三路同时成立，本来就不容易被毛刺伪造，
       所以直接采信未经消抖的原始读数，让它当拍生效。 */
    if (Sharp_EntryDir(s_raw) != 0xFFu) {
        s_stable    = s_raw;
        s_cand      = s_raw;
        s_candCount = 0u;
        return;
    }

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

    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0u) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 线偏在中线的哪一侧                                                  */
/* ------------------------------------------------------------------ */
/* 把阵列以中线劈成左右两半，**数各压了几路再比大小**。
   正中间那一路（奇数路数时才有）**不算进任何一边** —— 它是"在不在正中"
   的标志，不是"偏哪边"的证据。

   返回 -1 = 偏左, +1 = 偏右, 0 = 左右一样多（正中或对称）。

   ⚠ 这里必须是"比大小"而不是"看有没有"：直线上一般两路同时压线，
     而这两路常常正好跨在中线两侧（S3+S4 / S4+S5）。写成"左边有 + 右边有
     = 对称"的话，这两种最普通的图形都会被判成正中而不修正。 */
static int8_t MaskLean(uint8_t mask)
{
    uint8_t half  = (uint8_t)(TRACK_SENSOR_COUNT / 2u);       /* 7->3, 4->2 */
    uint8_t odd   = (uint8_t)(TRACK_SENSOR_COUNT & 1u);       /* 7->1, 4->0 */
    uint8_t lmask = (uint8_t)((1u << half) - 1u);             /* 左边那几路 */
    uint8_t left  = Popcount((uint8_t)(mask & lmask));
    uint8_t right = Popcount((uint8_t)(mask >> (uint8_t)(half + odd)));

    if (left > right) { return -1; }
    if (right > left) { return  1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 偏差：压线那几路的加权平均（权重表 s_pos[] 在文件顶部）              */
/* ------------------------------------------------------------------ */
/* 没压线时返回 -1000（一个不可能出现的值），调用方靠它判断丢线。 */
static int16_t ComputeError(uint8_t mask)
{
    int32_t sum = 0;
    uint8_t cnt = 0u;
    uint8_t i;

    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
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
   免得车正对着线的时候判断在左右之间来回跳。
   这个结果只在"丢线之后往哪边找"和"全白后往哪边强转"时用到。

   以阵列中线为界把掩码劈成左右两半，**两种情况不一样**：
     偶数路（4 路）中线落在两路之间：左 = bit0..1，右 = bit2..3
     奇数路（7 路）中线正好是一路（S4）：它必须**不算进任何一边**。
       算进右边的话右边会凭空多出一路 —— 车在直线上（只有 S4 压线）
       就会被判成"线在右边"，以后一丢线就永远往右转。 */
static void UpdateLastSide(uint8_t mask)
{
    int8_t lean = MaskLean(mask);

    if (lean < 0) {
        s_lastSide = 0u;
    } else if (lean > 0) {
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

    /* 换状态就把"正在修正"清掉：十字、强转、找线这些由各自的状态接管，
       回来重新循迹时要从"直行"重新判断，别带着上一次的修正脉冲。 */
    s_turnSide = 0xFFu;
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

/* ------------------------------------------------------------------ */
/* 正常循迹：正中间直行，线一离开正中就把另一侧的轮子加快一段时间        */
/* ------------------------------------------------------------------ */
/* 规则（7 路，从车头看从左往右 S1..S7，正中间是 S4）：
     中线左边压得比右边多 -> 线偏在车的左边 -> 右轮加快
     中线右边压得比左边多 -> 线偏在车的右边 -> 左轮加快
     左右一样多           -> 对称/正中 -> 直行

   ⚠ **必须"数路数比大小"，不能"看有没有"**。实测直线上一般是**两路**同时压线，
   而这两路往往正好跨在中线两侧：

       S3+S4（线略偏左）  左 1 路、右 0 路  -> 偏左 -> 右轮加快
       S4+S5（线略偏右）  左 0 路、右 1 路  -> 偏右 -> 左轮加快
       S3+S4+S5（真正中） 左 1 路、右 1 路  -> 平手 -> 直行

   如果写成"左边只要有压 + 右边只要有压 = 对称"，上面第一种和第二种都会被
   误判成"正中"而**完全不修正** —— 那车就只会直着冲出去。

   正中间那一路（S4）**不算进任何一边**（它是"在不在正中"的标志，不是"偏哪边"）。

   "一段时间"是最短持续时间：一旦判出偏了就把这次修正锁定 TRACK_TURN_MS，
   这期间**即使传感器已经回到中间也继续修完** —— 这一步很重要，不然会出现
   "一修就过头、一过头又反向修"的高频抖动。偏得久就每拍续期（一直修），
   回到中间之后最多再修 TRACK_TURN_MS 就收手。

   方向约定：线偏左 -> 右轮快 -> 车头往左摆（追着线走）。 */
static void Control_Turn(uint8_t mask)
{
    uint32_t now  = Tick_Ms();
    int8_t   lean = MaskLean(mask);        /* -1 偏左, +1 偏右, 0 正中 */
    int32_t  left  = (int32_t)s_baseSpeed;
    int32_t  right = (int32_t)s_baseSpeed;

    /* 丢线之后判"刚才车正不正"要用它，跟用什么控制律无关，照旧维护 */
    s_lastError = s_error;

    if (lean < 0) {
        s_turnSide    = 0u;                       /* 线偏左 -> 修右轮 */
        s_turnUntilMs = now + TRACK_TURN_MS;
    } else if (lean > 0) {
        s_turnSide    = 1u;                       /* 线偏右 -> 修左轮 */
        s_turnUntilMs = now + TRACK_TURN_MS;
    } else if ((s_turnSide != 0xFFu)
               && ((int32_t)(now - s_turnUntilMs) < 0)) {
        /* 已经回到正中，但这次修正还没做满最短时间 -> 做完它（否则会抖） */
    } else {
        s_turnSide = 0xFFu;                       /* 正中：直行 */
    }

    if (s_turnSide == 0u) {
        right += TRACK_TURN_BOOST;                /* 加快右轮 */
    } else if (s_turnSide == 1u) {
        left  += TRACK_TURN_BOOST;                /* 加快左轮 */
    }

    /* 只封上限：这套规则里两轮都不会出现负速度（倒转交给 SHARP 状态） */
    if (left  > (int32_t)MOTOR_PWM_MAX) { left  = (int32_t)MOTOR_PWM_MAX; }
    if (right > (int32_t)MOTOR_PWM_MAX) { right = (int32_t)MOTOR_PWM_MAX; }

    Motor_SetLR((int16_t)left, (int16_t)right);
}

/* 十字 / 起跑线：锁定直行，别让转向修正在路口被某个读数带偏 */
static void Control_Cross(uint32_t now)
{
    if ((uint32_t)(now - s_stateMs) > TRACK_CROSS_MS) {
        s_lastError = 0;
        EnterState(TRACK_ST_RUN);
        return;
    }
    DriveForward((uint16_t)s_baseSpeed);
}

/* 强转：朝指定方向原地转，直到**正中间那一路**重新压到线 */
static void Control_Sharp(uint32_t now, uint8_t mask)
{
    /* 退出判据是"正中间看到线了"，**不能**用 TRACK_MASK_MID（中间三路任意一路）。
       因为进入强转的图形本身就含中间三路里的 S3 或 S5：
       直角口那一路是 0x07（含 S3），用"任意一路"判的话，刚进强转就立刻退出，
       车根本来不及转 —— 直角就转不过去。

       4 路时 TRACK_MASK_CENTER 就是中间两路（S2+S3），和原来的行为一致。 */
    if ((mask & TRACK_MASK_CENTER) != 0u) {
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

/* 绕圆环：正常转向修正 + 朝环内的固定偏置，直到再次看到"丁字"就是出口 */
static void Control_Ring(uint32_t now, uint8_t mask)
{
    uint32_t inRing = (uint32_t)(now - s_stateMs);

    /* 出口判据：进环待够 TRACK_RING_MIN_MS 之后，再次出现"丁字"图形。
       四路全黑的情形在 Track_Tick 里已经被拦下并转到 CROSS 了，
       那条路径同样会直行冲出去，正好也是我们要的动作。 */
    if ((inRing > TRACK_RING_MIN_MS)
        && ((mask == TRACK_RING_PAT_RIGHT) || (mask == TRACK_RING_PAT_LEFT))) {
        EnterState(TRACK_ST_CROSS);
        Control_Cross(now);
        return;
    }

    /* 兜底：绕太久说明判据失效了，回正常循迹，别一直转 */
    if (inRing > TRACK_RING_TIMEOUT_MS) {
        EnterState(TRACK_ST_RUN);
        return;
    }

    Control_Turn(mask);

    /* 在转向修正的结果上再加一点朝环内的固定偏置，让车老实贴着圆弧走。
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

/* 圆环入口判据：正好三路压线（4 路）/ 中间加一侧三路（7 路）。
   返回 0 = 往左进环，1 = 往右进环，0xFF = 不是环入口。
   位序是 bit0=S1(最左) .. bit3=S4(最右)：
     0x07 = S1+S2+S3 压线、S4 空 -> 多出来的一支在左边 -> 左环
     0x0E = S2+S3+S4 压线、S1 空 -> 多出来的一支在右边 -> 右环 */
static uint8_t Ring_EntryDir(uint8_t mask)
{
    if (mask == TRACK_RING_PAT_LEFT) {
        return 0u;
    }
    if (mask == TRACK_RING_PAT_RIGHT) {
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
    uint8_t  sharpDir;
    uint8_t  allBlack;
    uint8_t  allWhite;

    /* 限频到 TRACK_CTRL_MS，让控制周期固定，转向修正才有一致的节奏 */
    if ((uint32_t)(now - s_lastCtrlMs) < TRACK_CTRL_MS) {
        return;
    }
    s_lastCtrlMs = now;

    Sensor_Update();
    mask = s_stable;

    /* 只要不是"十字图形"（横线/起跑线），就说明车已经离开了横线，
       可以接受下一次计圈。十字图形读成什么由 TRACK_CROSS_GAP_SWAPPED 决定
       （七路实测是全白）。 */
#if TRACK_CROSS_GAP_SWAPPED
    if (mask != 0x00u) {
#else
    if (mask != TRACK_MASK_ALL) {
#endif
        s_markArmed = 1u;
    }

    s_error = ComputeError(mask);

    /* 没发车（或已经跑完）：只更新传感器显示，一根线都不碰电机 */
    if ((s_state == TRACK_ST_IDLE) || (s_state == TRACK_ST_FINISH)) {
        return;
    }

    UpdateLastSide(mask);

    allBlack = (uint8_t)(mask == TRACK_MASK_ALL);
    allWhite = (uint8_t)(mask == 0x00u);

#if TRACK_CROSS_GAP_SWAPPED
    /* ⚠ 七路实测："十字"读成全白、"断线"读成全黑，把两个变量的含义对调。
       下面的触发代码一行都不用改：allBlack 现在代表"十字图形（全白）"，
       allWhite 代表"断线图形（全黑）"。 */
    {
        uint8_t t = allBlack;
        allBlack  = allWhite;
        allWhite  = t;
    }
#endif

    /* ---------------- 全局优先事件：十字 / 起跑线 ---------------- */
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
               给转向修正了。 */
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

        /* 圆环入口：连续几拍都是"丁字"图形才认，避免斜着压线误判。
           这里数的是**未经消抖的原始读数** —— 和直角那条一样的原因：
           消抖后的值要 3 次调用才更新，而刷 OLED 会把主循环卡十几毫秒，
           跑起来时"丁字"图形根本活不到那时候，圆环就永远进不去。
           数原始读数仍然要求连续 3 拍，抗毛刺的能力没有丢。 */
        if (s_ringEnable != 0u) {
            ringDir = Ring_EntryDir(s_raw);
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

        /* 直角 / 折线：**一侧整排全黑、正中间空** -> 原地 90° 强转跟上。
           位置要在圆环判据**后面**：0x0F / 0x78 那种"正中间还压着"的图形
           说明主线还在，那是 T 字路口/圆环入口，归圆环那套管；
           这里只管"前面已经没线了、线拐到一边去"的拐角。 */
        sharpDir = Sharp_EntryDir(mask);
        if (sharpDir != 0xFFu) {
            s_turnDir = sharpDir;
            EnterState(TRACK_ST_SHARP);
            Control_Sharp(now, mask);
            break;
        }

        /* 只剩最外侧一路压线：比整排全黑还偏，同样原地强转。
           路数变了这里自动跟着走：bit0 = S1(最左)，bit(N-1) = SN(最右)。 */
        if ((mask == (uint8_t)BIT(0)) || (mask == (uint8_t)BIT(TRACK_SENSOR_COUNT - 1u))) {
            s_turnDir = (uint8_t)((mask == (uint8_t)BIT(0)) ? 0u : 1u);
            EnterState(TRACK_ST_SHARP);
            Control_Sharp(now, mask);
            break;
        }

        Control_Turn(mask);
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
    s_ringTicks  = 0u;
    s_turnSide   = 0xFFu;
    s_turnUntilMs = 0u;

    EnterState(TRACK_ST_RUN);
}

void Track_Stop(void)
{
    Motor_Stop();

    if (Track_IsRunning() != 0u) {
        s_elapsedMs = (uint32_t)(Tick_Ms() - s_startMs);
    }

    s_state      = TRACK_ST_IDLE;
    s_error      = 0;
    s_lastError  = 0;
    s_ringTicks  = 0u;
    s_turnSide   = 0xFFu;
    s_turnUntilMs = 0u;
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

/* 串口用的位串，S1(最左) 打在左边，也就是 "S1S2..SN" 的顺序 ——
   和 OLED 上从左到右看到的顺序一致，对着屏幕就能核对。
   4 路时是 4 个字符（例如 "0110"），7 路时是 7 个（例如 "0011100"）。 */
const char *Track_BitsText(void)
{
    static char str[TRACK_SENSOR_COUNT + 1u];
    uint8_t i;

    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        str[i] = ((s_stable & (uint8_t)(1u << i)) != 0u) ? '1' : '0';
    }
    str[TRACK_SENSOR_COUNT] = '\0';

    return str;
}

/* ================================================================== */
/* 初始化                                                              */
/* ================================================================== */

void Track_Init(void)
{
    uint8_t i;

    /* 循迹传感器全部配成上拉输入：有些灰度模块的输出是开集电极的，
       没有上拉会一直读到 0，全都"压线"，非常误导。
       引脚来自 g_trackSensorPins[]，所以 4 路 / 7 路、以后改脚都只改那一张表。 */
    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        GPIO_ConfigPin(g_trackSensorPins[i].port, g_trackSensorPins[i].pin, GPIO_CFG_IN_PUPD);
        g_trackSensorPins[i].port->BSRR = BIT(g_trackSensorPins[i].pin);   /* ODR 置 1 = 上拉 */
    }

    s_state      = TRACK_ST_IDLE;
    s_raw        = 0u;
    s_stable     = 0u;
    s_cand       = 0u;
    s_candCount  = 0u;
    s_error      = 0;
    s_lastError  = 0;
    s_lastSide   = 0u;
    s_turnDir    = 0u;
    s_ringDir    = 0u;
    s_ringTicks  = 0u;
    s_turnSide   = 0xFFu;
    s_turnUntilMs = 0u;
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
    /* 上电默认"完成不停车"：圈数只用来计时和报圈，不会把车停下来，
       所以一按发车就会一直跑下去。菜单里的"完成停车"仍然能改回开。 */
    s_finishStop = 0u;
}
