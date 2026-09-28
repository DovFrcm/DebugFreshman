/* fake_board.c - 电脑端（宿主端）实现的 board 服务函数 */

#include "fake_board.h"

GPIO_TypeDef fake_gpio[3];

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

uint32_t Board_ClockHz(void)
{
    return 72000000u;
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
