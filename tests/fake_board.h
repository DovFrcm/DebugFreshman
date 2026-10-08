/*
 * fake_board.h - board.h 的电脑端（宿主端）替身，好让真实的驱动、
 * 菜单引擎和菜单树都能在 PC 上编译并渲染出来。
 *
 * 它通过 -include 被强行包含进来，并定义了 __BOARD_H，这样 ../User 下
 * 那个基于寄存器的头文件就会被跳过。固件各模块真正用到的东西，就全部
 * 由普通的主机内存来支撑。
 *
 * ⚠ 这里定义的寄存器地址、位定义和 PWM 参数必须和 ../User/board.h 保持一致。
 *    改 board.h 里的宏时，记得同步改这里，否则电脑上测出来的行为会和板子上
 *    不一样 —— 而这种"测试全过、板子上不转"的问题特别难查。
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

/* 定时器 / RCC / AFIO 的内存替身。motor.c 是直接写寄存器的，
   所以电脑上测试时这些寄存器也得有个真实存在的落点，
   这样测试还能顺便验证"初始化时到底往寄存器里写了什么"。 */
typedef struct {
    volatile uint32_t CR1;    volatile uint32_t CR2;
    volatile uint32_t SMCR;   volatile uint32_t DIER;
    volatile uint32_t SR;     volatile uint32_t EGR;
    volatile uint32_t CCMR1;  volatile uint32_t CCMR2;
    volatile uint32_t CCER;   volatile uint32_t CNT;
    volatile uint32_t PSC;    volatile uint32_t ARR;
    volatile uint32_t RCR;    volatile uint32_t CCR1;
    volatile uint32_t CCR2;   volatile uint32_t CCR3;
    volatile uint32_t CCR4;   volatile uint32_t BDTR;
    volatile uint32_t DCR;    volatile uint32_t DMAR;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t CR;      volatile uint32_t CFGR;
    volatile uint32_t CIR;     volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR; volatile uint32_t APB1ENR;
    volatile uint32_t BDCR;    volatile uint32_t CSR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t EVCR;    volatile uint32_t MAPR;
    volatile uint32_t EXTICR[4];
    volatile uint32_t RESERVED0;
    volatile uint32_t MAPR2;
} AFIO_TypeDef;

extern TIM_TypeDef  fake_tim4;
extern RCC_TypeDef  fake_rcc;
extern AFIO_TypeDef fake_afio;

#define TIM4   (&fake_tim4)
#define RCC    (&fake_rcc)
#define AFIO   (&fake_afio)

#define BIT(n)  (1u << (n))

/* ---- GPIO 配置半字节（照抄 board.h） ---- */
#define GPIO_CFG_IN_FLOAT     0x4u
#define GPIO_CFG_IN_PUPD      0x8u
#define GPIO_CFG_OUT_PP_2M    0x2u
#define GPIO_CFG_OUT_PP_50M   0x3u
#define GPIO_CFG_OUT_OD_50M   0x7u
#define GPIO_CFG_AF_PP_50M    0xBu

/* ---- RCC / AFIO / TIM 位定义（照抄 board.h） ---- */
#define RCC_APB2ENR_AFIOEN     (1u << 0)
#define RCC_APB2ENR_IOPAEN     (1u << 2)
#define RCC_APB2ENR_IOPBEN     (1u << 3)
#define RCC_APB2ENR_IOPCEN     (1u << 4)
#define RCC_APB1ENR_TIM4EN     (1u << 2)

#define AFIO_MAPR_SWJ_CFG_MASK (0x7u << 24)
#define AFIO_MAPR_SWJ_JTAG_OFF (0x2u << 24)

#define TIM_CR1_CEN            (1u << 0)
#define TIM_CR1_ARPE           (1u << 7)
#define TIM_CR1_UG             (1u << 0)
#define TIM_CCMR_OCM_PWM1      (0x6u)
#define TIM_CCMR_OC3PE         (1u << 3)
#define TIM_CCMR_OC4PE         (1u << 11)
#define TIM_CCER_CC3E          (1u << 8)
#define TIM_CCER_CC4E          (1u << 12)

#define MOTOR_PWM_PERIOD       7200u
#define MOTOR_PWM_MAX          7199u

/* ---- 接线（照抄 board.h） ---- */
/* LED 挪到 PA12 / PA15：PB12/PB13 让给循迹传感器了 */
#define BOARD_LED_PORT        GPIOA
#define BOARD_LED1_PIN        12u
#define BOARD_LED2_PIN        15u

/* 蜂鸣器 */
#define BOARD_BEEP_PORT       GPIOB
#define BOARD_BEEP_PIN        3u

/* ---- board.c 对外接口 ---- */
void     Board_Init(void);
void     GPIO_ConfigPin(GPIO_TypeDef *port, uint8_t pin, uint8_t cfg);
void     Delay_Ms(uint32_t ms);
void     Delay_Us(uint32_t us);
uint32_t Tick_Ms(void);
uint32_t Tick_Missed(void);
uint32_t Board_ClockHz(void);
void     Board_LedInit(void);
void     Board_LedWrite(uint8_t led1On, uint8_t led2On);
void     Board_BeepInit(void);
void     Board_BeepWrite(uint8_t on);
void     Board_ReleaseJtagPins(void);

/* 测试钩子：记录固件最后一次把两个灯驱动成什么状态 */
extern uint8_t fake_led1;
extern uint8_t fake_led2;
void     fake_led_reset(void);

/* 测试钩子：蜂鸣器。fake_beep_pulses 数的是"响了几声"，
   题目 4b 要求发车响一声、4c 要求第一圈再响一声，就靠它验证。 */
extern uint32_t fake_beep_pulses;
extern uint8_t  fake_beep_on;
void     fake_beep_reset(void);

/* 测试钩子：把四路循迹传感器的输入摆成指定值。
   bit0 = S1(最左) .. bit3 = S4(最右)，某位为 1 表示"这一路压到黑线"。
   内部会按"看见黑线拉低"翻译成 IDR 的电平，所以固件里那段取反逻辑
   也一并被测到了。 */
void     fake_sensor_set(uint8_t bits);
/* 直接写 PB12..PB15 的原始电平，用来单独验证极性 */
void     fake_sensor_set_levels(uint8_t levels);

/* 假 USART 的测试钩子（见 fake_serial.c） */
void        fake_serial_feed(const char *text);
const char *fake_serial_tx(void);
void        fake_serial_tx_reset(void);

#endif /* FAKE_BOARD_H */
