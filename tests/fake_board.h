/*
 * fake_board.h - board.h 的电脑端（宿主端）替身，好让真实的驱动、
 * 菜单引擎和菜单树都能在 PC 上编译并渲染出来。
 *
 * 它通过 -include 被强行包含进来，并定义了 __BOARD_H，这样 ../User 下
 * 那个基于寄存器的头文件就会被跳过。固件各模块真正用到的东西，就全部
 * 由普通的主机内存来支撑。
 */

#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include <stdint.h>

#define __BOARD_H       /* 不能把真实的 board.h 也包含进来 */

typedef struct {
    volatile uint32_t CRL;  volatile uint32_t CRH;
    volatile uint32_t IDR;  volatile uint32_t ODR;
    volatile uint32_t BSRR; volatile uint32_t BRR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

extern GPIO_TypeDef fake_gpio[3];

#define GPIOA   (&fake_gpio[0])
#define GPIOB   (&fake_gpio[1])
#define GPIOC   (&fake_gpio[2])

#define BIT(n)  (1u << (n))

#define GPIO_CFG_IN_PUPD      0x8u
#define GPIO_CFG_OUT_PP_2M    0x2u
#define GPIO_CFG_OUT_OD_50M   0x7u

/* 接线和真实板子一致：两个灯挂在同一个 port 上 */
#define BOARD_LED_PORT        GPIOB
#define BOARD_LED1_PIN        12u
#define BOARD_LED2_PIN        13u

void     Board_Init(void);
void     GPIO_ConfigPin(GPIO_TypeDef *port, uint8_t pin, uint8_t cfg);
void     Delay_Ms(uint32_t ms);
void     Delay_Us(uint32_t us);
uint32_t Tick_Ms(void);
uint32_t Board_ClockHz(void);
void     Board_LedInit(void);
void     Board_LedWrite(uint8_t led1On, uint8_t led2On);

/* 测试钩子：记录固件最后一次把两个灯驱动成什么状态 */
extern uint8_t fake_led1;
extern uint8_t fake_led2;
void     fake_led_reset(void);

/* 假 USART 的测试钩子（见 fake_serial.c） */
void        fake_serial_feed(const char *text);
const char *fake_serial_tx(void);
void        fake_serial_tx_reset(void);

#endif /* FAKE_BOARD_H */
