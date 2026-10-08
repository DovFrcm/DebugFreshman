/*
 * motor.c - TB6612FNG 双路电机驱动 + TIM4 PWM
 *
 * 本文件刻意不依赖标准外设库：TIM4 和 GPIO 的寄存器定义都在 board.h 里，
 * 和工程其它部分保持同一种风格（见 board.h 开头的说明）。
 */

#include "motor.h"

/* 上一轮写出去的值，Motor_SetLR() 每次都更新。
   存下来有两个用处：一是给 OLED 显示，二是能在驱动层直接看出
   "控制器算出来的"和"真正给电机的"是不是一致。 */
static int16_t s_left  = 0;
static int16_t s_right = 0;

/* ------------------------------------------------------------------ */
/* 底层：两个通道各写一次                                             */
/* ------------------------------------------------------------------ */

/* 把有符号速度翻译成 TB6612 需要的三个动作：两个方向脚 + 一个占空比。
   pwm = 0 时特意配成 IN1=IN2=0（自由停止）而不是刹车：
   巡线过程中速度过零是常事，每次都刹车会让车一顿一顿的。 */
static void Motor_ChannelDrive(uint8_t in1Pin,
                               uint8_t in2Pin,
                               volatile uint32_t *ccr,
                               int16_t pwm)
{
    uint32_t set = 0u;
    uint32_t reset = 0u;
    uint16_t duty;

    if (pwm > (int16_t)MOTOR_PWM_MAX) {
        pwm = (int16_t)MOTOR_PWM_MAX;
    } else if (pwm < -(int16_t)MOTOR_PWM_MAX) {
        pwm = -(int16_t)MOTOR_PWM_MAX;
    }

    if (pwm > 0) {
        set   = BIT(in1Pin);
        reset = BIT(in2Pin);
        duty  = (uint16_t)pwm;
    } else if (pwm < 0) {
        set   = BIT(in2Pin);
        reset = BIT(in1Pin);
        /* 这里不能写 -pwm：pwm 是有符号的，INT16_MIN 取负会溢出。
           钳位发生在上面，所以走到这里 pwm >= -7199，取负是安全的。 */
        duty  = (uint16_t)(-pwm);
    } else {
        reset = BIT(in1Pin) | BIT(in2Pin);
        duty  = 0u;
    }

    /* BSRR 的低 16 位置位、高 16 位复位，一次 32 位写就把两个方向脚
       同时改到位，中间不会出现"两个都是 1"（刹车）的瞬间 */
    GPIOA->BSRR = set | (reset << 16);
    *ccr = duty;
}

/* ------------------------------------------------------------------ */
/* 初始化                                                              */
/* ------------------------------------------------------------------ */

void Motor_Init(void)
{
    /* TIM4 挂在 APB1 上，时钟使能位在 APB1ENR。
       APB1 分频是 2，按 STM32F1 的规矩定时器时钟自动 x2，所以还是 72 MHz。 */
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    /* 四个方向脚：通用推挽输出 */
    GPIO_ConfigPin(GPIOA, 4u,  GPIO_CFG_OUT_PP_50M);    /* AIN1 */
    GPIO_ConfigPin(GPIOA, 5u,  GPIO_CFG_OUT_PP_50M);    /* AIN2 */
    GPIO_ConfigPin(GPIOA, 8u,  GPIO_CFG_OUT_PP_50M);    /* BIN1 */
    GPIO_ConfigPin(GPIOA, 11u, GPIO_CFG_OUT_PP_50M);    /* BIN2 */

    /* 两个 PWM 脚：复用推挽。不配成复用的话，定时器照样在数，
       CCR 也照样在变，但引脚根本不理会 —— 表现为"程序全对、电机不转"。 */
    GPIO_ConfigPin(GPIOB, 8u, GPIO_CFG_AF_PP_50M);      /* TIM4_CH3 */
    GPIO_ConfigPin(GPIOB, 9u, GPIO_CFG_AF_PP_50M);      /* TIM4_CH4 */

    /* 时基：PSC=0 -> 计数频率 72 MHz；ARR=7199 -> 每 7200 个数溢出一次
       => 7200 个数 / 72 MHz = 100 us = 10 kHz */
    TIM4->PSC = 0u;
    TIM4->ARR = MOTOR_PWM_PERIOD - 1u;
    /* ARPE=1：ARR 也走预装载。这样运行中改周期不会切出一个畸变的周期。
       CEN 先不开，等下面都配好了再一起启动。 */
    TIM4->CR1 = TIM_CR1_ARPE;

    /* CCMR2 同时管通道 3 和通道 4：
         bit 6:4   = OC3M = 110  -> 通道3 PWM 模式 1
         bit 3     = OC3PE = 1   -> 通道3 的 CCR 预装载
         bit 14:12 = OC4M = 110  -> 通道4 PWM 模式 1
         bit 11    = OC4PE = 1   -> 通道4 的 CCR 预装载 */
    TIM4->CCMR2 = (TIM_CCMR_OCM_PWM1 << 4)  | TIM_CCMR_OC3PE
                | (TIM_CCMR_OCM_PWM1 << 12) | TIM_CCMR_OC4PE;

    /* 把两个通道的输出接到引脚上。CCxP 保持 0 = 高电平有效，
       也就是"计数值 < CCR 时引脚输出高" */
    TIM4->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;

    /* 占空比先清零，免得定时器一启动电机就猛地转一下 */
    TIM4->CCR3 = 0u;
    TIM4->CCR4 = 0u;

    /* 方向脚也先清零（自由停止） */
    Motor_Stop();

    /* 手动产生一次更新事件：PSC/ARR/CCRx 这些带预装载的寄存器，
       要等一次更新事件才会把值搬进真正干活的影子寄存器。
       不做这一步的话，第一次溢出之前占空比和周期都还是旧值。 */
    TIM4->EGR = TIM_CR1_UG;
    TIM4->SR  = 0u;             /* 把 UG 顺带置起来的更新标志清掉 */

    TIM4->CR1 |= TIM_CR1_CEN;   /* 开跑 */
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

void Motor_SetLR(int16_t left, int16_t right)
{
#if MOTOR_A_IS_LEFT
    int16_t a = left;
    int16_t b = right;
#else
    int16_t a = right;
    int16_t b = left;
#endif

    s_left  = left;
    s_right = right;

#if MOTOR_A_INVERT
    a = (int16_t)(-a);
#endif
#if MOTOR_B_INVERT
    b = (int16_t)(-b);
#endif

    /* 通道 A：PA4/PA5 + CCR3（TIM4_CH3 / PB8）
       通道 B：PA8/PA11 + CCR4（TIM4_CH4 / PB9） */
    Motor_ChannelDrive(4u, 5u, &TIM4->CCR3, a);
    Motor_ChannelDrive(8u, 11u, &TIM4->CCR4, b);
}

void Motor_Stop(void)
{
    /* 自由停止：占空比清零 + 方向脚全 0。
       不能只清占空比不清方向脚，那样电机是"被短路"的状态，
       推车的时候能明显感觉到阻力。 */
    GPIOA->BSRR = (BIT(4) | BIT(5) | BIT(8) | BIT(11)) << 16;
    TIM4->CCR3 = 0u;
    TIM4->CCR4 = 0u;
    s_left  = 0;
    s_right = 0;
}

void Motor_Brake(void)
{
    /* 刹车：四个方向脚全拉高，占空比清零。
       TB6612 会把电机两端短起来，反电动势被吃掉，停得比滑行快。 */
    GPIOA->BSRR = BIT(4) | BIT(5) | BIT(8) | BIT(11);
    TIM4->CCR3 = 0u;
    TIM4->CCR4 = 0u;
    s_left  = 0;
    s_right = 0;
}

int16_t Motor_GetLeft(void)
{
    return s_left;
}

int16_t Motor_GetRight(void)
{
    return s_right;
}
