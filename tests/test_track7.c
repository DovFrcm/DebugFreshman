/*
 * test_track7.c - 七路灰度版的专项测试
 *
 * 和 test_track.c 一样，链接的是固件里真实的 track.c / app.c / motor.c /
 * oled.c，只把硬件层换成 tests/ 下的内存替身。
 *
 * 用 -DTRACK_SENSOR_COUNT=7 编译（tools/run_tests.ps1 会自动带上）。
 *
 * 掩码位序：bit0 = S1(最左) .. bit6 = S7(最右)，1 = 压到黑线。
 */

#include <stdio.h>
#include <string.h>

#include "fake_board.h"
#include "oled.h"
#include "menu.h"
#include "key.h"
#include "app.h"
#include "track.h"
#include "motor.h"

/* 七路常用图形 */
#define M_ALL     0x7Fu     /* 七路全黑：十字 / 起跑线 */
#define M_NONE    0x00u     /* 全白：断口 / 丢线       */
#define M_CEN     0x08u     /* 只有 S4（正中间）压线   */
#define M_S3      0x04u     /* 只有 S3（正中间的左邻） */
#define M_S5      0x10u     /* 只有 S5（正中间的右邻） */
#define M_S34     0x0Cu     /* S3+S4 = bit2+bit3：线在正中偏左（实测最常见的图形） */
#define M_S45     0x18u     /* S4+S5 = bit3+bit4：线在正中偏右（实测最常见的图形） */
#define M_MID     0x1Cu     /* S3+S4+S5：中间三路      */
#define M_L3      0x07u     /* S1+S2+S3：偏左          */
#define M_R3      0x70u     /* S5+S6+S7：偏右          */
#define M_S1      0x01u     /* 只剩最左一路            */
#define M_S2      0x02u     /* 只有 S2                 */
#define M_S6      0x20u     /* 只有 S6                 */
#define M_S7      0x40u     /* 只剩最右一路            */
#define M_RING_L  0x0Fu     /* S1..S4：多出来的一支在左 */
#define M_RING_R  0x78u     /* S4..S7：多出来的一支在右 */

static int g_failures;
static int g_checks;

static void expect(int condition, const char *what)
{
    g_checks++;
    if (condition == 0) {
        g_failures++;
    }
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
}

static void step_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0u; i < ms; i += 2u) {
        Delay_Ms(2u);
        App_Tick();
    }
}

static void drive_with(uint8_t bits, uint32_t ms)
{
    fake_sensor_set(bits);
    step_ms(ms);
}

static void launch(uint8_t startMask, uint8_t ringOn)
{
    Track_Init();
    Track_SetBaseSpeed(900u);
    Track_SetLaps(9u);              /* 测试期间不要因为计圈停车 */
    Track_SetRingEnable(ringOn);
    Track_SetLineMark(1u);
    Track_Stop();

    fake_sensor_set(startMask);
    step_ms(20u);                   /* 让消抖器认可 */
    Track_Start();
    fake_sensor_set(0x07u);         /* 压一下直角图形（S1-S3）：结束出发段 */
    step_ms(20u);
    fake_sensor_set(startMask);     /* 回到正式测试的图形 */
    step_ms(20u);
}

/* ================================================================== */

static void test_pin_table(void)
{
    const SensorPin *p = g_trackSensorPins;

    printf("\n--- A. 引脚表 ------------------------------------------------\n");

    expect(TRACK_SENSOR_COUNT == 7, "7 sensors are compiled in");
    expect(p[0].port == GPIOB && p[0].pin == 12u, "S1 -> PB12 (leftmost)");
    expect(p[1].port == GPIOB && p[1].pin == 13u, "S2 -> PB13");
    expect(p[2].port == GPIOB && p[2].pin == 14u, "S3 -> PB14");
    expect(p[3].port == GPIOB && p[3].pin == 15u, "S4 -> PB15");
    expect(p[4].port == GPIOA && p[4].pin == 6u,  "S5 -> PA6");
    expect(p[5].port == GPIOA && p[5].pin == 7u,  "S6 -> PA7");
    expect(p[6].port == GPIOB && p[6].pin == 0u,  "S7 -> PB0 (rightmost)");
}

static void test_read_one_by_one(void)
{
    uint8_t i;

    printf("\n--- B. 逐路读取：每一路都能单独被认出来 -----------------------\n");

    for (i = 0u; i < TRACK_SENSOR_COUNT; i++) {
        char msg[64];

        fake_sensor_set((uint8_t)(1u << i));
        sprintf(msg, "S%d alone -> mask bit %d", (int)(i + 1u), (int)i);
        expect(Track_Read() == (uint8_t)(1u << i), msg);
    }

    /* 位序：S1 在最左 = bit0，对着屏幕从左到右 */
    fake_sensor_set(M_S1);
    expect(Track_Read() == 0x01u, "leftmost sensor is bit0 (S1)");
    fake_sensor_set(M_S7);
    expect(Track_Read() == 0x40u, "rightmost sensor is bit6 (S7)");
}

static void test_bits_text(void)
{
    printf("\n--- C. 串口位串：7 位、S1 在左 ---------------------------------\n");

    /* 注意：Track_BitsText() 读的是消抖之后的 s_stable，
       所以每换一个图形都要推进几十毫秒让消抖器认可 */
    fake_sensor_set(M_CEN);
    step_ms(20u);
    expect(strlen(Track_BitsText()) == 7u, "bit string is 7 characters");
    expect(strcmp(Track_BitsText(), "0001000") == 0,
           "only S4 -> \"0001000\" (S1 printed leftmost)");

    fake_sensor_set(M_S1);
    step_ms(20u);
    expect(strcmp(Track_BitsText(), "1000000") == 0, "only S1 -> \"1000000\"");

    fake_sensor_set(M_S7);
    step_ms(20u);
    expect(strcmp(Track_BitsText(), "0000001") == 0, "only S7 -> \"0000001\"");
}

static void test_error_signs(void)
{
    printf("\n--- D. 偏差符号：中间为 0，越靠外越大 -------------------------\n");

    fake_sensor_set(M_CEN);
    step_ms(20u);
    expect(Track_Error() == 0, "centre sensor -> error 0");

    fake_sensor_set(M_ALL);
    step_ms(20u);
    expect(Track_Error() == 0, "all seven black -> error 0 (symmetric)");

    fake_sensor_set(M_S1);
    step_ms(20u);
    expect(Track_Error() == -300, "S1 alone -> error -300 (line on the left)");

    fake_sensor_set(M_S7);
    step_ms(20u);
    expect(Track_Error() == 300, "S7 alone -> error +300 (line on the right)");

    fake_sensor_set(M_L3);
    step_ms(20u);
    expect(Track_Error() < 0, "S1+S2+S3 -> error negative");

    fake_sensor_set(M_R3);
    step_ms(20u);
    expect(Track_Error() > 0, "S5+S6+S7 -> error positive");

    fake_sensor_set(M_NONE);
    step_ms(20u);
    expect(Track_Error() == -1000, "all white -> -1000 sentinel (line lost)");
}

static void test_steering(void)
{
    printf("\n--- E. 转向：左偏左转、右偏右转、居中直行 ---------------------\n");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    expect(Track_State() == TRACK_ST_RUN, "centred -> RUN");
    expect(Motor_GetLeft() == Motor_GetRight(),
           "centred -> both wheels at the same speed");
    expect(Motor_GetLeft() > 0, "and both are driving forward");

    launch(M_MID, 0u);
    drive_with(M_MID, 200u);
    expect(Motor_GetLeft() == Motor_GetRight(),
           "middle three black -> still straight");

    /* 注意：这里用的是**漂移**图形（S3+S4 / S4+S5），不是整排全黑。
       整排全黑（0x07 / 0x70）现在算"直角路口"，会进原地强转，
       那一组在 P 段单独测。 */
    launch(M_S34, 0u);                            /* 0x0C = S3+S4 */
    drive_with(M_S34, 200u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "S3+S4 (line drifting LEFT) -> right wheel faster");

    launch(M_S45, 0u);                            /* 0x18 = S4+S5 */
    drive_with(M_S45, 200u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "S4+S5 (line drifting RIGHT) -> left wheel faster");
}

static void test_cross(void)
{
    printf("\n--- F. 十字 / 起跑线：全白（现场实测，判据已对调）-------------\n");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_NONE, 200u);
    expect(Track_State() == TRACK_ST_CROSS,
           "all seven WHITE -> CROSS state (swapped by field test)");
    expect(Motor_GetLeft() == Motor_GetRight(),
           "CROSS drives straight, no steering at a junction");
}

static void test_sharp(void)
{
    printf("\n--- G. 直角 / 折线：只剩最外侧一路 ----------------------------\n");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S1, 200u);
    expect(Track_State() == TRACK_ST_SHARP, "S1 alone -> SHARP (spin left)");
    expect(Motor_GetLeft() < 0, "left wheel reverses -> spins left");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S7, 200u);
    expect(Track_State() == TRACK_ST_SHARP, "S7 alone -> SHARP (spin right)");
    expect(Motor_GetRight() < 0, "right wheel reverses -> spins right");
}

static void test_gap_and_lost(void)
{
    printf("\n--- H. 断路与丢线：全黑（现场实测，判据已对调）----------------\n");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 300u);
    drive_with(M_ALL, 100u);
    expect(Track_State() == TRACK_ST_GAP,
           "all seven BLACK -> GAP state (swapped by field test)");
    expect(Motor_GetLeft() > 0 && Motor_GetRight() > 0,
           "in GAP both wheels still drive FORWARD");

    drive_with(M_ALL, 400u);
    expect(Track_State() == TRACK_ST_LOST, "gap not cleared -> LOST");

    drive_with(M_CEN, 200u);
    expect(Track_State() == TRACK_ST_RUN, "line found -> back to RUN");
}

static void test_ring_entry(void)
{
    printf("\n--- I. 圆环：进环 / 出环判据（现场实测，全部左环）------------\n");

    /* 进环：0x0F（S1..S4）满足"S1+S4 且 S2/S3" -> 左环 */
    launch(M_CEN, 1u);              /* 圆环打开 */
    drive_with(M_CEN, 400u);
    drive_with(M_RING_L, 200u);
    expect(Track_State() == TRACK_ST_RING, "0x0F (S1..S4) -> RING (left ring)");

    /* 0x78 现在只是**出环**图形，不再作为进环判据（全部是左环） */
    launch(M_CEN, 1u);
    drive_with(M_CEN, 400u);
    drive_with(M_RING_R, 200u);
    expect(Track_State() != TRACK_ST_RING,
           "0x78 is the EXIT pattern now, NOT an entry (all rings are left)");

    /* 出环：环内待够 TRACK_RING_MIN_MS 后，读到 (S7+S4 且 S5/S6) -> 出环 */
    launch(M_CEN, 1u);
    drive_with(M_CEN, 400u);
    drive_with(M_RING_L, 40u);
    expect(Track_State() == TRACK_ST_RING, "entered the left ring");
    drive_with(M_CEN, 800u);                    /* 环内巡线，超过 RING_MIN_MS */
    drive_with((uint8_t)(M_S7 | M_CEN | M_S5), 40u);   /* S7+S4+S5 = 0x58 出环图形 */
    expect(Track_State() == TRACK_ST_CROSS,
           "exit pattern (S7+S4 and S5) -> leaves the ring (CROSS)");

    /* 圆环关掉时同样的图形不能进环，而是按普通转向处理 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 400u);
    drive_with(M_RING_L, 200u);
    expect(Track_State() != TRACK_ST_RING,
           "ring disabled -> the same pattern must NOT enter RING");
    expect(Track_State() == TRACK_ST_RUN && Motor_GetRight() > Motor_GetLeft(),
           "ring disabled -> treated as a normal left turn");
}

static void test_lap_still_works(void)
{
    printf("\n--- J. 计圈没被路数改动破坏 -----------------------------------\n");

    Track_Init();
    Track_SetBaseSpeed(900u);
    Track_SetLaps(1u);
    Track_SetFinishStop(1u);        /* 跑完停车（题目 4b） */
    Track_SetRingEnable(0u);
    Track_SetLineMark(1u);
    Track_Stop();
    fake_sensor_set(M_CEN);
    step_ms(20u);
    Track_Start();
    step_ms(20u);

    drive_with(0x07u, 40u);         /* 第一个路口（直角图形）：结束出发段 */
    drive_with(M_CEN, Track_LapMinMs() + 2000u);   /* 跑过最短圈时 */
    drive_with(M_NONE, 200u);       /* 压过起跑线（现场实测读成全白） */
    expect(Track_LapsDone() == 1u, "one lap counted with 7 sensors");
    expect(Track_State() == TRACK_ST_FINISH, "stops after the lap");
    expect(Motor_GetLeft() == 0 && Motor_GetRight() == 0, "motors off");
}

/* 进入"巡线功能"这一页（主菜单第 3 项）。
   必须先 App_Init() 把菜单树建起来，否则 Menu_Draw() 会踩空指针。 */
static void enter_trace_page(void)
{
    App_Init();
    Menu_Draw();
    Menu_HandleKey(KEY_2);      /* 0 -> 1 */
    Menu_HandleKey(KEY_2);      /* 1 -> 2 : 巡线功能 */
    Menu_HandleKey(KEY_3);
    Menu_Draw();
}

/* 中间那一路（S4）不该被算进"左"或"右"的任何一边。
   如果把 S4 算进右边，车在直线上（只有 S4 压线）就会被判成"线在右边"，
   以后一丢线就永远往右转 —— 这一组就是守这个的。 */
static void test_last_side_symmetry(void)
{
    printf("\n--- L. 丢线方向：中间那一路不该把最后一侧带偏 -----------------\n");

    /* ① 先在"线偏左"的状态跑一会儿，把 s_lastSide 置成"左" */
    launch((uint8_t)(M_S1 | M_S2), 0u);
    drive_with((uint8_t)(M_S1 | M_S2), 200u);

    /* ② 再回到正中（只有 S4 压线）：这一步不该改变"最后一侧" */
    drive_with(M_CEN, 200u);

    /* ③ 丢线（全黑 = 断线，现场实测判据已对调）：先进 GAP 直冲，之后进 LOST 要往**左**找 */
    drive_with(M_ALL, 700u);
    expect(Track_State() == TRACK_ST_LOST, "line lost -> LOST");
    expect(Motor_GetLeft() < 0,
           "was on the LEFT then centred -> search spins LEFT");

    /* 反过来：先在"线偏右"的状态，再回正中，丢线后应该往右找 */
    launch((uint8_t)(M_S6 | M_S7), 0u);
    drive_with((uint8_t)(M_S6 | M_S7), 200u);
    drive_with(M_CEN, 200u);
    drive_with(M_ALL, 700u);
    expect(Track_State() == TRACK_ST_LOST, "line lost -> LOST (other side)");
    expect(Motor_GetRight() < 0,
           "was on the RIGHT then centred -> search spins RIGHT");
}

/* 新的转向策略：中间几路直行；偏到哪一侧就把另一侧的轮子加快"一段时间"。
   这里重点验证三件事：中间直行、偏了就加速对侧、以及**最短持续时间**
   （回到中间之后修正还会做完，不会一回到中间就撒手）。 */
/* 新的转向策略：正中直行；偏到哪一侧就把另一侧的轮子加快"一段时间"。
   这里验证四件事：正中直行、偏了就加速对侧、**最短持续时间**
   （回到正中之后修正还会做完），以及两侧的加速量对称。

   ⚠ 测试**不写死 TRACK_TURN_BOOST 的数值** —— 那是拿来调的参数，
     写死的话你每次调参测试都会红。这里只验关系（>0、两侧相等）。 */
static void test_turn_pulse(void)
{
    const int16_t base = 900;       /* launch() 里设定的基础速度 */
    int16_t boostWhenLeft;          /* 线偏左时给右轮加的量 */
    int16_t boostWhenRight;         /* 线偏右时给左轮加的量 */

    printf("\n--- M. 转向修正：正中直行 + 偏了加速另一侧轮子 -----------------\n");

    /* ① 只有正中 S4 压线 -> 两轮等速直行 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    expect(Track_State() == TRACK_ST_RUN, "dead centre -> RUN");
    expect(Motor_GetLeft() == Motor_GetRight(),
           "dead centre -> both wheels at the same speed (straight)");
    expect(Motor_GetLeft() == base && Motor_GetRight() == base,
           "and both are exactly at the base speed");

    /* ② 线偏左 -> 右轮加快，左轮保持基础速度 */
    drive_with(M_S3, 20u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "line drifts LEFT -> right wheel speeds up");
    expect(Motor_GetLeft() == base, "and the left wheel stays at the base speed");
    boostWhenLeft = (int16_t)(Motor_GetRight() - base);

    /* ③ 关键：立刻回到正中，修正仍要把最短时间做完 */
    drive_with(M_CEN, 20u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "back to centre: the correction pulse is still running");

    /* ④ 脉冲走完之后回到两轮等速 */
    drive_with(M_CEN, 250u);
    expect(Motor_GetLeft() == Motor_GetRight(),
           "after the pulse expires -> straight again");

    /* ⑤ 线偏右 -> 左轮加快（镜像） */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S5, 20u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "line drifts RIGHT -> left wheel speeds up");
    expect(Motor_GetRight() == base,
           "and the right wheel stays at the base speed");
    boostWhenRight = (int16_t)(Motor_GetLeft() - base);

    /* ⑥ 两侧必须完全对称，而且都是"加速"不是"减速" */
    expect(boostWhenLeft > 0 && boostWhenRight > 0,
           "both sides actually SPEED UP (the boost is positive)");
    expect(boostWhenLeft == boostWhenRight,
           "both sides get exactly the same boost (symmetric)");

    printf("      boost: line-on-left = +%d, line-on-right = +%d\n",
           (int)boostWhenLeft, (int)boostWhenRight);
}

/* 这一组是本策略最关键的地方：**线一离开正中就开始修**。
   正中间是 S4，它的左右邻居 S3/S5 必须算作"偏了" ——
   不然线刚离开正中时根本不修，车会先漂出去一大截才纠正
   （看着就是"慢慢偏、然后猛拉一把"）。 */
static void test_centre_neighbours(void)
{
    printf("\n--- N. 正中间的左右邻居（S3 / S5）一压线就修 -------------------\n");

    /* ① 只有 S4（正中）压线 -> 直行 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    expect(Motor_GetLeft() == Motor_GetRight(),
           "S4 only (dead centre) -> straight");

    /* ② 只有 S3（正中间的**左邻**）压线 -> 线在车的左边 -> 往左摆 = 右轮加快 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S3, 20u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "S3 only (left neighbour) -> line is LEFT -> right wheel speeds up");
    expect(Motor_GetLeft() == 900,
           "and the left wheel stays at the base speed");

    /* ③ 只有 S5（正中间的**右邻**）压线 -> 线在车的右边 -> 往右摆 = 左轮加快 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S5, 20u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "S5 only (right neighbour) -> line is RIGHT -> left wheel speeds up");
    expect(Motor_GetRight() == 900,
           "and the right wheel stays at the base speed");

    /* ④ 中间三路一起压（S3+S4+S5）本身就是对称的 -> 直行 */
    launch(M_MID, 0u);
    drive_with(M_MID, 200u);
    expect(Motor_GetLeft() == Motor_GetRight(),
           "S3+S4+S5 together is symmetric -> straight");

    /* ⑤ 更外侧的 S2 / S6 方向必须和 S3 / S5 一致（不能反过来） */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S2, 20u);                        /* 只有 S2 */
    expect(Motor_GetRight() > Motor_GetLeft(),
           "S2 only (further left) -> still corrects LEFT, same as S3");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S6, 20u);                        /* 只有 S6 */
    expect(Motor_GetLeft() > Motor_GetRight(),
           "S6 only (further right) -> still corrects RIGHT, same as S5");
}

/* ★ 这一组是"车根本不修正"那个 bug 的回归测试 ★

   实车在直线上一般是**两路**同时压线，而且这两路常常正好跨在中线两侧：
   S3+S4（线略偏左）、S4+S5（线略偏右）。

   最早的写法是"左边只要有压 + 右边只要有压 = 对称 = 直行"，
   于是这两种最常见的图形全被判成"正中"，**车一点都不修**。
   正确的判据是"数中线左右各压了几路，再比大小"。 */
static void test_straddling_pairs(void)
{
    printf("\n--- O. 两路跨中线压线（S3+S4 / S4+S5）必须修正 ---------------\n");

    /* S3+S4 -> 左边 1 路、右边 0 路 -> 偏左 -> 右轮加快 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S34, 40u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "S3+S4 (line just LEFT of centre) -> right wheel speeds up");
    expect(Motor_GetLeft() == 900,
           "and the left wheel is still at the base speed");
    expect(Track_State() == TRACK_ST_RUN, "and the state is still RUN");

    /* S4+S5 -> 左边 0 路、右边 1 路 -> 偏右 -> 左轮加快 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S45, 40u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "S4+S5 (line just RIGHT of centre) -> left wheel speeds up");
    expect(Motor_GetRight() == 900,
           "and the right wheel is still at the base speed");

    /* 反过来也要立刻能改：先偏左再偏右，方向必须跟着翻过来 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S34, 40u);
    expect(Motor_GetRight() > Motor_GetLeft(), "first: S3+S4 -> corrects left");
    drive_with(M_S45, 40u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "then S4+S5 -> correction flips to the other side");

    /* S3+S4+S5（真对称）仍然要直行 */
    launch(M_MID, 0u);
    drive_with(M_MID, 40u);
    expect(Motor_GetLeft() == Motor_GetRight(),
           "S3+S4+S5 (genuinely symmetric) -> straight");
}

/* 直角路口：**一侧整排全黑、正中间是空的** -> 原地 90° 强转。
   含义是"前面已经没线了，线拐到一边去了"。 */
static void test_right_angle(void)
{
    printf("\n--- P. 直角路口：一侧整排压线 -> 原地 90° 强转 -----------------\n");

    /* 左侧整排 S1+S2+S3 全黑、正中间空 -> 左 90° */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_L3, 40u);                       /* 0x07 = S1+S2+S3 */
    expect(Track_State() == TRACK_ST_SHARP,
           "S1+S2+S3 whole left row black, centre clear -> SHARP");

    drive_with(M_L3, 100u);                      /* 越过可能存在的直行阶段 */
    expect(Motor_GetLeft() < 0,
           "then it pivots LEFT in place (left wheel reverses)");

    /* 转到位：中间几路重新压到线 -> 交还给巡线 */
    drive_with(M_MID, 40u);
    expect(Track_State() == TRACK_ST_RUN, "grabbed the line again -> back to RUN");

    /* 镜像：右侧整排 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_R3, 40u);                       /* 0x70 = S5+S6+S7 */
    expect(Track_State() == TRACK_ST_SHARP,
           "S5+S6+S7 whole right row black -> SHARP");

    drive_with(M_R3, 100u);
    expect(Motor_GetRight() < 0, "then it pivots RIGHT in place");

    /* 只压一两路是**漂移**不是拐角 -> 必须还是温和修正，不能原地转 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S3, 40u);
    expect(Track_State() == TRACK_ST_RUN,
           "only S3 (a drift, not a corner) -> stays in RUN");
    expect(Motor_GetRight() > Motor_GetLeft(), "and it is a gentle correction");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with((uint8_t)(M_S1 | M_S2), 40u);     /* S1+S2，不是整排 */
    expect(Track_State() == TRACK_ST_RUN,
           "S1+S2 only (not the whole row) -> stays in RUN");

    /* 正中间也黑的图形不是拐角：主线还在，那是 T 字/圆环口 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_RING_L, 40u);                   /* 0x0F = S1..S4 */
    expect(Track_State() != TRACK_ST_SHARP,
           "0x0F (centre still black = T junction) -> NOT a right-angle corner");
}

/* ★ 这一组守的是"手推能识别、跑起来识别不到"那个 bug ★

   消抖数的是"连续多少次**调用**"，而刷一次 OLED 会把主循环卡十几毫秒，
   所以 3 次调用实际可能横跨三五十毫秒。跑起来冲过直角口时，
   "一侧整排全黑"这个图形只存在（线宽 ÷ 车速）那么久，等消抖走完就没了。

   所以直角判据走快通道：**当拍**采信原始读数，不等消抖。
   下面用"只推进一个控制周期"来验证这一点 —— 走消抖的话这里必然抓不到。 */
static void test_corner_no_debounce(void)
{
    printf("\n--- Q. 直角判据必须当拍生效（跑起来抓不到的那个 bug）-----------\n");

    /* 只推进 2ms = 一个控制周期，图形就应该已经被认出来 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_L3, 2u);                        /* 0x07，只给一拍 */
    expect(Track_State() == TRACK_ST_SHARP,
           "0x07 detected after a SINGLE control tick (no debounce wait)");

    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_R3, 2u);                        /* 0x70，只给一拍 */
    expect(Track_State() == TRACK_ST_SHARP,
           "0x70 detected after a SINGLE control tick (mirror)");

    /* 普通的两路图形仍然要消抖：只给一拍不应该改变判断 */
    launch(M_CEN, 0u);
    drive_with(M_CEN, 200u);
    drive_with(M_S3, 2u);                        /* 只有 S3，一拍 */
    expect(Track_State() == TRACK_ST_RUN,
           "a plain 1-sensor reading still waits for the debouncer");
}

/* 直线冲刺窗：新一圈开始后 9~13 秒内，直线基础速度额外 +600，
   窗口外保持设置值不变（每圈按 s_startMs 自动重新计时）。 */
static void test_straight_boost(void)
{
    printf("\n--- S. 直线冲刺窗：新一圈 10~11.5s 速度 +1100 -----------------\n");

    launch(M_CEN, 0u);               /* 基础速度 900 */

    /* 窗口之前：设置的基础速度 900（现场冲刺窗 = 10~11.5s，+1100） */
    drive_with(M_CEN, 100u);
    expect(Motor_GetLeft() == 900 && Motor_GetRight() == 900,
           "before 10 s -> base speed 900");

    /* 进入 10~11.5s 窗口：900 + 1100 = 2000 */
    drive_with(M_CEN, 10000u);
    expect(Motor_GetLeft() == 2000 && Motor_GetRight() == 2000,
           "inside the 10..11.5 s window -> 900 + 1100 = 2000");

    /* 窗口过后：恢复设置速度 */
    drive_with(M_CEN, 2500u);
    expect(Motor_GetLeft() == 900 && Motor_GetRight() == 900,
           "after 11.5 s -> back to 900");
}

static void test_oled_layout(void)
{
    const uint8_t *fb;
    int x, y, lit = 0;

    printf("\n--- K. 屏幕：七个方框都画得下 --------------------------------\n");

    enter_trace_page();
    launch(M_CEN, 0u);
    Menu_Invalidate();
    Menu_Draw();

    /* 第 2 行（y = 16..31）应该有像素 */
    fb = OLED_Buffer();
    for (y = 16; y < 32; y++) {
        for (x = 0; x < 128; x++) {
            if (((fb[(y >> 3) * 128 + x] >> (y & 7)) & 1) != 0) {
                lit++;
            }
        }
    }
    expect(lit > 0, "sensor row (row 1) is drawn");
}

int main(void)
{
    printf("###############################################################\n");
    printf("# 七路灰度专项测试（TRACK_SENSOR_COUNT = %d）\n", TRACK_SENSOR_COUNT);
    printf("###############################################################\n");

    test_pin_table();
    test_read_one_by_one();
    test_bits_text();
    test_error_signs();
    test_steering();
    test_cross();
    test_sharp();
    test_gap_and_lost();
    test_ring_entry();
    test_lap_still_works();
    test_last_side_symmetry();
    test_turn_pulse();
    test_centre_neighbours();
    test_straddling_pairs();
    test_right_angle();
    test_corner_no_debounce();
    test_straight_boost();
    test_oled_layout();

    printf("\n===============================================================\n");
    printf("%d checks, %d failures\n\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
