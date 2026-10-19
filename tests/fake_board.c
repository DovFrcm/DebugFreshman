/* fake_board.c - 电脑端（宿主端）实现的 board 服务函数 */

#include "fake_board.h"

/* 三个 GPIO 口的内存替身。
   GPIOB 的输入寄存器初值给全 1，是为了模拟真板子的上拉：没接东西、
   或者传感器输出高（没看见黑线）的时候，引脚读到的就是高电平。
   如果这里留 0，PB12..15 会被当成"四路全黑"，一上电就进十字状态，
   测试结果会和板子上完全对不上。 */
GPIO_TypeDef fake_gpio[3] = {
    { 0u, 0u, 0x00000000u, 0u, 0u, 0u, 0u },
    { 0u, 0u, 0x0000FFFFu, 0u, 0u, 0u, 0u },
    { 0u, 0u, 0x00000000u, 0u, 0u, 0u, 0u }
};
TIM_TypeDef  fake_tim4;
RCC_TypeDef  fake_rcc;
AFIO_TypeDef fake_afio;

static uint32_t s_tick;

void Board_Init(void)
{
}

void GPIO_ConfigPin(GPIO_TypeDef *port, uint8_t pin, uint8_t cfg)
{
    (void)port;
    (void)pin;
    (void)cfg;
}

/* 电脑上"延时"就是把时间基准往前推。
   真板子上 Tick_Ms() 是 SysTick 的 1 ms 中断在推进，
   这里由测试主动调用 Delay_Ms() 来模拟时间流逝，效果一样。 */
void Delay_Ms(uint32_t ms)
{
    s_tick += ms;
}

void Delay_Us(uint32_t us)
{
    (void)us;
}

uint32_t Tick_Ms(void)
{
    return s_tick;
}

uint32_t Tick_Missed(void)
{
    return 0u;
}

uint32_t Board_ClockHz(void)
{
    return 72000000u;
}

void Board_ReleaseJtagPins(void)
{
    /* 电脑上没有 JTAG 这回事。真板子上这一步用来把 PB3/PB4/PA15
       从 JTAG 手里抢回来当普通 IO 用。 */
}

uint8_t fake_led1;
uint8_t fake_led2;

void fake_led_reset(void)
{
    fake_led1 = 0u;
    fake_led2 = 0u;
}

void Board_LedInit(void)
{
    fake_led_reset();
}

void Board_LedWrite(uint8_t led1On, uint8_t led2On)
{
    /* 真实的实现是用一次 BSRR 写入同时更新两个灯的电平；在这里只要
       把"灯被告知要做什么"记下来就够了 */
    fake_led1 = (led1On != 0u) ? 1u : 0u;
    fake_led2 = (led2On != 0u) ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* 蜂鸣器                                                              */
/* ------------------------------------------------------------------ */
/* 数"响了几声"要数上升沿：拍一下蜂鸣器会先写 1 再写 0，
   如果把两次写都算上，一声就会被数成两下。 */
uint32_t fake_beep_pulses;
uint8_t  fake_beep_on;

void fake_beep_reset(void)
{
    fake_beep_pulses = 0u;
    fake_beep_on = 0u;
}

void Board_BeepInit(void)
{
    fake_beep_reset();
}

void Board_BeepWrite(uint8_t on)
{
    uint8_t level = (on != 0u) ? 1u : 0u;

    if ((level != 0u) && (fake_beep_on == 0u)) {
        fake_beep_pulses++;         /* 只在上升沿计数 */
    }
    fake_beep_on = level;
}

/* ------------------------------------------------------------------ */
/* 循迹传感器输入                                                      */
/* ------------------------------------------------------------------ */
/* ⚠ 这两个函数必须和 track.h 里的极性宏保持一致。
 *
 * 真板子上"压到黑线时引脚是高还是低"由传感器模块决定（有的是黑线拉低，
 * 有的是黑线拉高），代码里用 TRACK_BLACK_IS_LOW / TRACK_REVERSE 两个宏
 * 去适配。电脑端测试要模拟的是"车压到黑线"这件事本身，而不是某个电平，
 * 所以这里反过来把固件那套变换再逆一遍再写进 IDR ——
 * 这样无论你把极性宏改成什么，测试的语义都还是"这一路压线了/没压线"，
 * 不会因为你换了个模块就整套测试全红。 */

#include "track.h"

void fake_sensor_set(uint8_t bits)
{
    uint8_t  v = bits;
    uint32_t idr;

#if TRACK_REVERSE
    /* 逆变换：先把固件里那次 bit 对调还原回去 */
    v = (uint8_t)(((v & 0x1u) << 3) | ((v & 0x2u) << 1)
                | ((v & 0x4u) >> 1) | ((v & 0x8u) >> 3));
#endif

#if TRACK_BLACK_IS_LOW
    /* 固件把"低电平"取反成"压线"，这里就先取反写回去 */
    v = (uint8_t)(~v & 0x0Fu);
#endif

    idr = ((uint32_t)v & 0x0Fu) << 12u;
    GPIOB->IDR = (GPIOB->IDR & ~(0x0Fu << 12u)) | idr;
}

/* 直接写 PB12..PB15 的原始电平。只有"验证极性宏本身对不对"那一组测试用得到，
   其它测试一律用上面的 fake_sensor_set()。 */
void fake_sensor_set_levels(uint8_t levels)
{
    uint32_t v = ((uint32_t)levels & 0x0Fu) << 12u;

    GPIOB->IDR = (GPIOB->IDR & ~(0x0Fu << 12u)) | v;
}
