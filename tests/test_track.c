/*
 * test_track.c - 巡线控制器的电脑端（宿主端）测试
 *
 * 它链接真实的 track.c、motor.c、app.c 和 oled.c，只把硬件层换成内存变量，
 * 所以"传感器读进来是什么、电机被命令转多快"这两件事走的都是固件里那份
 * 真实的代码 —— 包括 PB12..PB15 的取反逻辑、TIM4 寄存器的初始化、
 * PD 控制器和整个状态机。
 *
 * 覆盖的评分点：
 *   4a 传感器显示    —— 四路任意组合都能被正确读出来
 *   4b 正常循迹一圈  —— 发车响一声、跑完一圈响一声、停车、屏幕上有用时
 *   4c 正常循迹两圈  —— 第一圈只响不停，第二圈跑完才停
 *   4d/4e 特殊元素   —— 十字、断路、直角、圆环各自进入正确的状态
 *
 * 编译与运行：见 README 第 8 章。
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

static int g_failures;
static int g_checks;

/* ------------------------------------------------------------------ */

static void expect(int condition, const char *what)
{
    g_checks++;
    if (condition == 0) {
        g_failures++;
    }
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
}

/* 推进 ms 毫秒的"墙上时间"。真板子上 Tick_Ms() 由 SysTick 中断推进；
   这里用 Delay_Ms() 模拟，同时每 2 ms 调一次 App_Tick() ——
   和真主循环里 Track_Tick() 被调用的节奏一致。 */
static void step_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0u; i < ms; i += 2u) {
        Delay_Ms(2u);
        App_Tick();
    }
}

/* 把传感器摆成某个值，等消抖过去，让控制器跑起来 */
static void drive_with(uint8_t bits, uint32_t ms)
{
    fake_sensor_set(bits);
    step_ms(ms);
}

/* 屏幕上那四行文字，行号 0..3，每行 16 像素高、占 2 个 page。
   注意这里收的是**行号**不是 page 号：第 2 行画在 page 4 上，y 是 32..47。
   传超范围的 row 直接返回 0，免得越界读到显存外面还以为测过了。 */
static int row_has_pixels(int row)
{
    const uint8_t *fb = OLED_Buffer();
    int x;
    int y;

    if ((row < 0) || (row > 3)) {
        return 0;
    }

    for (y = row * 16; y < (row * 16) + 16; y++) {
        for (x = 0; x < 128; x++) {
            if (((fb[(y >> 3) * 128 + x] >> (y & 7)) & 1) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

static void dump_screen(const char *title)
{
    const uint8_t *fb = OLED_Buffer();
    int y;
    int x;

    printf("\n==== %s\n", title);
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++) {
            putchar((((fb[(y >> 3) * 128 + x] >> (y & 7)) & 1) != 0) ? '#' : '.');
        }
        putchar('\n');
    }
}

/* 进入"巡线功能"这一页（主菜单第 3 项） */
static void enter_trace_page(void)
{
    App_Init();
    Menu_Draw();
    Menu_HandleKey(KEY_2);      /* 0 -> 1 */
    Menu_HandleKey(KEY_2);      /* 1 -> 2 : 巡线功能 */
    Menu_HandleKey(KEY_3);
    Menu_Draw();
}

/* 把车摆到一个干净的初始状态：传感器摆成 startMask（默认中间两路压线，
   也就是"车正对着线"），消抖稳定之后再发车。
   每个测试开头都走这一遍，免得上一个测试残留的传感器状态影响判断 ——
   真板子上电时引脚被上拉成高电平，等价于"全白"，所以默认值取 0x06。 */
static void reset_and_launch(uint8_t startMask)
{
    Track_Init();
    Track_SetBaseSpeed(1800u);
    Track_SetLaps(9u);              /* 测试期间不要因为计圈停车 */
    Track_SetRingEnable(0u);        /* 单独测控制律，先关掉圆环识别 */
    Track_SetLineMark(1u);
    Track_Stop();

    fake_sensor_set(startMask);
    step_ms(20u);                   /* 让消抖器认可这个初始值 */
    Track_Start();
    step_ms(20u);
}

/* ================================================================== */

static void test_motor_registers(void)
{
    printf("\n--- A. TIM4 PWM 初始化 -----------------------------------------\n");

    Motor_Init();

    expect((RCC->APB1ENR & RCC_APB1ENR_TIM4EN) != 0u,
           "TIM4 clock is enabled on APB1");
    expect(TIM4->ARR == (MOTOR_PWM_PERIOD - 1u),
           "ARR = 7199 -> 72MHz/7200 = 10 kHz PWM");
    expect(TIM4->PSC == 0u, "prescaler = 0 (no division)");
    expect((TIM4->CR1 & TIM_CR1_CEN) != 0u, "counter is running (CEN=1)");
    expect((TIM4->CR1 & TIM_CR1_ARPE) != 0u, "ARR preload is on (ARPE=1)");

    /* CCMR2 同时管通道 3 和通道 4：两路都必须是 PWM 模式 1 + 预装载 */
    expect((TIM4->CCMR2 & (0x7u << 4)) == (TIM_CCMR_OCM_PWM1 << 4),
           "channel 3 (PB8 / PWMA) is PWM mode 1");
    expect((TIM4->CCMR2 & (0x7u << 12)) == (TIM_CCMR_OCM_PWM1 << 12),
           "channel 4 (PB9 / PWMB) is PWM mode 1");
    expect((TIM4->CCMR2 & TIM_CCMR_OC3PE) != 0u, "channel 3 CCR preload on");
    expect((TIM4->CCMR2 & TIM_CCMR_OC4PE) != 0u, "channel 4 CCR preload on");

    expect((TIM4->CCER & (TIM_CCER_CC3E | TIM_CCER_CC4E))
               == (TIM_CCER_CC3E | TIM_CCER_CC4E),
           "both channel outputs are connected to the pins");

    expect(TIM4->CCR3 == 0u && TIM4->CCR4 == 0u,
           "duty cycle is 0 right after init (car must not lurch)");
}

static void test_sensor_read(void)
{
    printf("\n--- B. 四路传感器读取（题目 4a） -------------------------------\n");

    /* 这两行的期望值跟着 track.h 的极性宏走。
       别的测试用 fake_sensor_set()（它是极性无关的，说的是"压线/没压线"），
       只有这里要直接摆电平，专门验证极性宏本身是不是按预期在起作用。 */
#if TRACK_BLACK_IS_LOW
#define LVL_WHITE  0x0Fu    /* 看见黑线输出低电平的模块：全高 = 全白 */
#define LVL_BLACK  0x00u
#define POLARITY_TXT "TRACK_BLACK_IS_LOW=1 (module pulls LOW on black)"
#else
#define LVL_WHITE  0x00u    /* 高有效模块：全低 = 全白 */
#define LVL_BLACK  0x0Fu
#define POLARITY_TXT "TRACK_BLACK_IS_LOW=0 (module drives HIGH on black)"
#endif

    printf("      %s\n", POLARITY_TXT);

    Track_Init();

    fake_sensor_set_levels(LVL_WHITE);
    expect(Track_Read() == 0x00u, "all-white level -> no sensor sees a line");

    fake_sensor_set_levels(LVL_BLACK);
    expect(Track_Read() == 0x0Fu, "all-black level -> all four see the line");

    /* S1 是最左，接 PB12，应该是 bit0；S4 是最右，接 PB15，应该是 bit3。
       fake_sensor_set() 是极性无关的，所以这两条在任何极性配置下都成立。 */
    fake_sensor_set(0x01u);
    expect(Track_Read() == 0x01u, "only S1 (PB12, leftmost) -> bit0");

    fake_sensor_set(0x08u);
    expect(Track_Read() == 0x08u, "only S4 (PB15, rightmost) -> bit3");

    /* 逐路走一遍，这正是考核时"逐路改变传感器状态"要做的事 */
    {
        static const uint8_t probe[4] = { 0x01u, 0x02u, 0x04u, 0x08u };
        int i;

        for (i = 0; i < 4; i++) {
            char what[80];

            fake_sensor_set(probe[i]);
            step_ms(20u);
            sprintf(what, "channel S%d alone reads back as 0x%X",
                    i + 1, (unsigned)Track_ReadStable());
            expect(Track_ReadStable() == probe[i], what);
        }
    }
}

static void test_pd_control(void)
{
    printf("\n--- C. PD 循迹控制 ---------------------------------------------\n");

    reset_and_launch(0x06u);

    /* 中间两路压线 = 车正对着线 -> 两个轮子一样快，直行 */
    drive_with(0x06u, 40u);
    expect(Track_State() == TRACK_ST_RUN, "centred on the line -> RUN");
    expect(Motor_GetLeft() == Motor_GetRight(),
           "centred on the line -> both wheels at the same speed (straight)");
    expect(Motor_GetLeft() > 0, "and both are driving forward");

    /* 右侧两路压线 = 线在车的右边 -> 必须右转 = 左轮快、右轮慢 */
    drive_with(0x0Cu, 40u);
    expect(Motor_GetLeft() > Motor_GetRight(),
           "line to the RIGHT -> left wheel faster (turns right)");

    /* 左侧两路压线 -> 左转 = 右轮快 */
    drive_with(0x03u, 40u);
    expect(Motor_GetRight() > Motor_GetLeft(),
           "line to the LEFT -> right wheel faster (turns left)");

    /* 偏差必须是连续的：S4 单独压线应该比 S3+S4 一起压线偏得更多 */
    {
        int16_t errOuter;
        int16_t errInner;

        fake_sensor_set(0x08u);
        step_ms(20u);
        errOuter = Track_Error();

        fake_sensor_set(0x06u);
        step_ms(40u);
        fake_sensor_set(0x0Cu);
        step_ms(20u);
        errInner = Track_Error();

        printf("      error: S4 alone = %d, S3+S4 = %d\n",
               (int)errOuter, (int)errInner);
        expect(errOuter > errInner,
               "weighted error is continuous: outer sensor deviates more");
    }

    Track_Stop();
}

static void test_sharp_turn(void)
{
    printf("\n--- D. 直角 / 折线（只剩最外侧一路） ---------------------------\n");

    reset_and_launch(0x06u);

    /* 车本来在正中间，然后线突然只剩最左边一路 -> 线向左急转 */
    drive_with(0x01u, 20u);

    expect(Track_State() == TRACK_ST_SHARP,
           "only the outermost sensor -> SHARP state (spin to catch the line)");
    expect(Motor_GetLeft() < 0 && Motor_GetRight() > 0,
           "S1 only (line went left) -> spin LEFT: left wheel reverses");

    /* 线重新被中间传感器咬住 -> 自动回到正常循迹 */
    drive_with(0x06u, 40u);
    expect(Track_State() == TRACK_ST_RUN,
           "once the middle sensors see the line again -> back to RUN");

    /* 另一边 */
    drive_with(0x08u, 20u);
    expect(Track_State() == TRACK_ST_SHARP, "S4 only -> SHARP again");
    expect(Motor_GetLeft() > 0 && Motor_GetRight() < 0,
           "S4 only (line went right) -> spin RIGHT: right wheel reverses");

    Track_Stop();
}

static void test_gap_and_lost(void)
{
    printf("\n--- E. 断路（缺口）与丢线找线 ---------------------------------\n");

    reset_and_launch(0x06u);

    /* 车正正的走着，线突然没了 —— 直线上的缺口，应该直冲过去而不是原地转 */
    drive_with(0x00u, 30u);

    expect(Track_State() == TRACK_ST_GAP,
           "line lost while centred -> GAP (drive straight over the break)");
    expect(Motor_GetLeft() > 0 && Motor_GetRight() > 0,
           "in GAP both wheels still drive FORWARD (no spinning)");

    /* 一直冲不过去 -> 转成原地找线，朝最后看到线的那一侧转 */
    step_ms(300u);
    expect(Track_State() == TRACK_ST_LOST,
           "gap not cleared -> LOST (search for the line)");
    expect(Motor_GetLeft() == -Motor_GetRight(),
           "in LOST the car spins on the spot (wheels in opposite directions)");

    /* 找到线就回到正常循迹 */
    drive_with(0x06u, 40u);
    expect(Track_State() == TRACK_ST_RUN, "line found -> back to RUN");

    /* 丢线找太久要放弃停车，不能一直在那儿转 */
    drive_with(0x00u, 4000u);
    expect(Track_State() == TRACK_ST_FINISH,
           "searching too long gives up and stops (no spinning forever)");
    expect(Motor_GetLeft() == 0 && Motor_GetRight() == 0, "and the motors stop");

    Track_Stop();
}

static void test_cross(void)
{
    printf("\n--- F. 十字路口 ------------------------------------------------\n");

    reset_and_launch(0x06u);
    Track_SetLineMark(0u);          /* 先只测"直行通过"，不算圈 */

    drive_with(0x0Fu, 30u);         /* 四路全黑 = 压在十字上 */

    expect(Track_State() == TRACK_ST_CROSS,
           "all four black -> CROSS state");
    expect(Motor_GetLeft() == Motor_GetRight(),
           "CROSS drives straight (PD must not steer at a junction)");

    /* 车开过横线、传感器回到只有中间两路 -> 应该交还给正常循迹 */
    drive_with(0x06u, 200u);
    expect(Track_State() == TRACK_ST_RUN,
           "once past the junction, control goes back to normal tracking");

    Track_Stop();
}

static void test_ring(void)
{
    printf("\n--- G. 圆环 ----------------------------------------------------\n");

    reset_and_launch(0x06u);
    Track_SetRingEnable(1u);

    /* 0x0E = S2+S3+S4 压线、S1 空：多出来的一支在右边 -> 右环 */
    drive_with(0x0Eu, 30u);

    expect(Track_State() == TRACK_ST_RING,
           "three-in-a-row (T shape) -> RING state entered");

    /* 在环里绕：即便传感器显示"车正对着线"，也必须持续朝环内偏，
       否则车会直着从环里穿出去。 */
    {
        int turned = 0;
        int i;

        for (i = 0; i < 20; i++) {
            drive_with(0x06u, 10u);
            /* 右环 = 一直往右拐 = 左轮比右轮快 */
            if (Motor_GetLeft() > Motor_GetRight()) {
                turned++;
            }
        }
        printf("      %d/20 samples steering into the ring\n", turned);
        expect(turned == 20,
               "inside the ring it keeps steering into the ring (never straightens out)");
        expect(Track_State() == TRACK_ST_RING, "and it is still in the RING state");
    }

    /* 圆环识别关掉之后，同样的图形不该再进环 */
    reset_and_launch(0x06u);
    Track_SetRingEnable(0u);
    drive_with(0x0Eu, 40u);
    expect(Track_State() != TRACK_ST_RING,
           "RING recognition off -> the same pattern stays in RUN");
    Track_Stop();
    Track_SetRingEnable(1u);
}

static void test_laps_one(void)
{
    printf("\n--- H. 题目 4b：正常循迹一圈 -----------------------------------\n");

    enter_trace_page();

    Track_SetLaps(1u);
    Track_SetLineMark(1u);
    Track_SetRingEnable(0u);
    Track_SetBaseSpeed(1800u);
    Track_Stop();
    fake_beep_reset();
    fake_sensor_set(0x06u);
    step_ms(20u);

    /* 按下 K1 发车 */
    Menu_HandleKey(KEY_1);
    expect(Track_IsRunning() != 0u, "K1 on the trace page starts the car");
    expect(fake_beep_pulses == 1u, "requirement 4b: the buzzer beeps ONCE at launch");

    /* 跑一会儿（要超过 TRACK_LAP_MIN_MS，真赛道上这段是整整一圈） */
    drive_with(0x06u, 6000u);
    expect(Track_IsRunning() != 0u, "still running mid-lap");
    expect(fake_beep_pulses == 1u, "no extra beeps during the lap");
    expect(Track_ElapsedMs() >= 6000u, "the lap timer is running");

    /* 压在起跑线上（四路全黑 = 考核时贴在起点的那条横线） */
    drive_with(0x0Fu, 200u);

    expect(Track_LapsDone() == 1u, "one lap counted");
    expect(Track_State() == TRACK_ST_FINISH, "requirement 4b: stops after the lap");
    expect(Motor_GetLeft() == 0 && Motor_GetRight() == 0, "and the motors are off");
    expect(fake_beep_pulses == 2u, "a second beep announces the lap is done");

    printf("      lap time = %lu ms\n", (unsigned long)Track_LastLapMs());
    expect(Track_LastLapMs() >= 6000u, "the lap time looks sane");
    expect(Track_ElapsedMs() == Track_LastLapMs(),
           "requirement 4b: the frozen time equals the lap time");

    /* 屏幕上要有用时。四行分别是：0=状态 1=传感器 2=用时+速度 3=按键提示 */
    Menu_Invalidate();
    Menu_Draw();
    dump_screen("trace page after one lap (row 0 = state, row 2 = sensors, row 4 = time)");
    expect(row_has_pixels(0) != 0, "state row is drawn");
    expect(row_has_pixels(1) != 0, "sensor row is drawn");
    expect(row_has_pixels(2) != 0, "elapsed-time row is drawn");
    expect(row_has_pixels(3) != 0, "key-hint row is drawn");

    /* 过快地再压一次横线不该被算成第二圈 */
    fake_beep_reset();
    drive_with(0x00u, 100u);
    drive_with(0x0Fu, 100u);
    expect(Track_LapsDone() == 1u,
           "the minimum lap time filters out a second marker too soon");
}

/* ------------------------------------------------------------------
 * 计圈那一下会响蜂鸣器，而响蜂鸣器是个阻塞操作（几十毫秒）。
 * 响完之后 track.c 必须重新取一次时间再进"十字直行"状态，
 * 否则 Control_Cross() 里 (now - s_stateMs) 一减就变成一个巨大的无符号数，
 * 直行会被立刻判定成超时 —— 表现为车一过起跑线就被交还给 PD，
 * 在路口被某个传感器读数带偏。这一条就是那次时间戳过期 bug 的回归测试。
 * ------------------------------------------------------------------ */
static void test_lap_beep_timing(void)
{
    TrackState st;
    int32_t l;
    int32_t r;

    printf("\n--- H2. 计圈响铃之后必须仍然锁定直行 ---------------------------\n");

    Track_Init();
    Track_SetBaseSpeed(1800u);
    Track_SetLaps(9u);
    Track_SetRingEnable(0u);
    Track_SetLineMark(1u);
    Track_Stop();

    fake_sensor_set(0x06u);
    step_ms(20u);
    fake_beep_reset();
    Track_Start();
    step_ms(6000u);

    fake_sensor_set(0x0Fu);     /* 压上起跑线：计圈 + 响铃 + 进 CROSS */
    /* 传感器消抖要 3 拍（约 6 ms），所以这里必须走够时间，
       否则全黑还没被确认，整个计圈流程根本不会触发 */
    step_ms(20u);

    st = Track_State();
    l = Motor_GetLeft();
    r = Motor_GetRight();

    printf("      right after the lap beep: state=%s L=%ld R=%ld\n",
           Track_StateTag(), (long)l, (long)r);

    expect(fake_beep_pulses == 2u, "the lap beep did fire");
    expect(st == TRACK_ST_CROSS,
           "still in CROSS after the beep (the timestamp must be refreshed)");
    expect(l > 0 && r > 0 && l == r,
           "and it drives straight through the line instead of steering");

    Track_Stop();
}

static void test_laps_two(void)
{
    printf("\n--- I. 题目 4c：正常循迹两圈 -----------------------------------\n");

    enter_trace_page();

    Track_SetLaps(2u);
    Track_SetLineMark(1u);
    Track_SetRingEnable(0u);
    Track_Stop();
    fake_beep_reset();
    fake_sensor_set(0x06u);
    step_ms(20u);

    Menu_HandleKey(KEY_1);
    expect(Track_GetLaps() == 2u, "two laps configured");
    expect(fake_beep_pulses == 1u, "beep on launch");

    /* ---- 第一圈 ---- */
    drive_with(0x06u, 6000u);
    drive_with(0x0Fu, 200u);

    expect(Track_LapsDone() == 1u, "requirement 4c: first lap recognised");
    expect(fake_beep_pulses == 2u, "requirement 4c: buzzer beeps at the end of lap 1");
    expect(Track_IsRunning() != 0u,
           "requirement 4c: the car does NOT stop after lap 1");
    expect(Track_State() != TRACK_ST_FINISH, "state is not FINISH after lap 1");

    /* ---- 第二圈 ---- */
    drive_with(0x00u, 200u);        /* 先离开起跑线 */
    drive_with(0x06u, 6000u);
    drive_with(0x0Fu, 200u);

    expect(Track_LapsDone() == 2u, "requirement 4c: second lap counted");
    expect(fake_beep_pulses == 3u, "a third beep announces the finish");
    expect(Track_State() == TRACK_ST_FINISH, "stops after lap 2");
    expect(Motor_GetLeft() == 0 && Motor_GetRight() == 0, "motors off at the end");

    /* 第三圈不该再发生 */
    drive_with(0x00u, 200u);
    drive_with(0x06u, 6000u);
    drive_with(0x0Fu, 200u);
    expect(Track_LapsDone() == 2u, "the car stays stopped after the set number of laps");
}

static void test_serial_commands(void)
{
    printf("\n--- J. 巡线相关的串口命令 -------------------------------------\n");

    Track_Stop();
    Track_SetLaps(1u);
    Track_SetBaseSpeed(1800u);
    Track_SetRingEnable(1u);

    fake_serial_tx_reset();
    fake_serial_feed("SENSORS\r\n");
    App_Tick();
    fake_sensor_set(0x06u);
    step_ms(20u);
    fake_serial_tx_reset();
    fake_serial_feed("SENSORS\r\n");
    App_Tick();
    expect(strcmp(fake_serial_tx(), "S=0110\r\n") == 0,
           "SENSORS replies with the four bit states");

    fake_serial_tx_reset();
    fake_serial_feed("LAPS 2\r\n");
    App_Tick();
    expect(Track_GetLaps() == 2u, "LAPS 2 sets the lap count");
    expect(strcmp(fake_serial_tx(), "LAPS=2\r\n") == 0, "and it is acknowledged");

    fake_serial_tx_reset();
    fake_serial_feed("SPEED 2500\r\n");
    App_Tick();
    expect(Track_GetBaseSpeed() == 2500u, "SPEED n sets the base speed");

    fake_serial_tx_reset();
    fake_serial_feed("RING OFF\r\n");
    App_Tick();
    expect(Track_GetRingEnable() == 0u, "RING OFF disables roundabout handling");

    fake_serial_tx_reset();
    fake_serial_feed("TRACK\r\n");
    App_Tick();
    expect(Track_IsRunning() != 0u, "TRACK launches the car");

    fake_serial_tx_reset();
    fake_serial_feed("TRACK STOP\r\n");
    App_Tick();
    expect(Track_IsRunning() == 0u, "TRACK STOP halts it");

    fake_serial_tx_reset();
    fake_serial_feed("STATUS\r\n");
    App_Tick();
    printf("      STATUS -> %s", fake_serial_tx());
    expect(strncmp(fake_serial_tx(), "ST=", 3) == 0, "STATUS replies with a state tag");

    /* 原有协议不能被破坏：数字仍然写进 a，GET A 仍然读回来 */
    fake_serial_tx_reset();
    fake_serial_feed("123\r\n");
    App_Tick();
    expect(App_GetVarA() == 123, "numeric input still sets a (old protocol intact)");

    fake_serial_tx_reset();
    fake_serial_feed("GET A");
    App_Tick();
    expect(strcmp(fake_serial_tx(), "123\r\n") == 0, "GET A still answers with a");
}

static void test_page_safety(void)
{
    printf("\n--- K. 离开巡线页必须停车 --------------------------------------\n");

    enter_trace_page();
    Track_SetRingEnable(0u);
    Track_SetLaps(9u);
    fake_sensor_set(0x06u);
    step_ms(20u);

    Menu_HandleKey(KEY_1);
    expect(Track_IsRunning() != 0u, "car launched from the trace page");

    /* 长按 K4 直接跳回主菜单，onLeave 钩子必须把车停下来 */
    Menu_HandleKey(KEY_4_LONG);
    expect(Menu_Depth() == 1u, "long K4 jumps back to the main menu");
    expect(Track_IsRunning() == 0u, "and leaving the page stops the car");
    expect(Motor_GetLeft() == 0 && Motor_GetRight() == 0, "motors are off");
}

/* ------------------------------------------------------------------ */
/* 把巡线页在不同状态下画出来，随测试一起存成 PNG。                    */
/* 这些图同时也是"考核时屏幕上应该看到什么"的对照表。                  */
/* ------------------------------------------------------------------ */

static void test_screens(void)
{
    static const struct {
        uint8_t     sensors;
        const char *what;
    } cases[4] = {
        { 0x06u, "requirement 4a: sensor display, centred (S2+S3 black)" },
        { 0x01u, "requirement 4a: sensor display, only S1 black"         },
        { 0x0Cu, "requirement 4a: sensor display, S3+S4 black"           },
        { 0x00u, "requirement 4a: sensor display, all white (no line)"   }
    };
    int i;

    printf("\n--- L. 巡线页在各状态下的画面 ---------------------------------\n");

    enter_trace_page();
    Track_SetLaps(2u);
    Track_SetBaseSpeed(1800u);
    Track_SetRingEnable(1u);
    Track_Stop();
    Menu_Invalidate();

    for (i = 0; i < 4; i++) {
        char title[96];

        fake_sensor_set(cases[i].sensors);
        step_ms(20u);
        Menu_Invalidate();
        Menu_Draw();

        sprintf(title, "%s", cases[i].what);
        dump_screen(title);
    }

    /* 跑起来的样子：正在循迹 + 计时在走 */
    fake_sensor_set(0x06u);
    step_ms(20u);
    Menu_HandleKey(KEY_1);
    step_ms(1500u);
    fake_sensor_set(0x03u);         /* 线偏到左边，车正在左转 */
    step_ms(40u);
    Menu_Invalidate();
    Menu_Draw();
    dump_screen("requirement 4b: running, 1.5 s elapsed, line drifting left");

    expect(Track_IsRunning() != 0u, "screen capture 5: car is running");
    expect(row_has_pixels(0) != 0, "screen capture 5: state row drawn");
    expect(row_has_pixels(1) != 0, "screen capture 5: sensor row drawn");
    expect(row_has_pixels(2) != 0, "screen capture 5: time row drawn");
    expect(row_has_pixels(3) != 0, "screen capture 5: key-hint row drawn");

    Track_Stop();
}

/* ================================================================== */

int main(void)
{
    printf("=============================================================\n");
    printf(" line-following tests (real track.c + motor.c on the host)\n");
    printf("=============================================================\n");

    test_motor_registers();
    test_sensor_read();
    test_pd_control();
    test_sharp_turn();
    test_gap_and_lost();
    test_cross();
    test_ring();
    test_laps_one();
    test_lap_beep_timing();
    test_laps_two();
    test_serial_commands();
    test_page_safety();
    test_screens();

    printf("\n=================================================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);

    return (g_failures != 0) ? 1 : 0;
}
