/*
 * board.c - STM32F103C8 OLED 菜单工程的时钟、GPIO 与延时
 *
 * 目标板：STM32F103C8T6「Blue Pill」，板上焊的是 8 MHz 晶振。
 * SYSCLK 被拉到 72 MHz。如果晶振始终不起振，代码就退回内部 8 MHz HSI 继续跑
 * （只是慢一点），菜单照样能用，同时 Board_ClockHz() 会把真实频率如实报给
 * 信息页面，而不是硬编码一个 72 MHz 去骗界面。
 */

#include "board.h"

/* 等硬件就绪用的空转超时计数。如果无脑死等 HSERDY / PLLRDY，一旦板上没焊晶振
   或者晶振坏了，程序就永远卡在这里，连"退回 HSI 继续跑"这条兜底路径都走不到。 */
#define HSE_TIMEOUT     0x00040000u
#define PLL_TIMEOUT     0x00040000u
#define SWS_TIMEOUT     0x00040000u

/* 当前 SYSCLK 的实际频率，初值按复位后的内部 HSI 8 MHz 填 */
static uint32_t s_sysclk_hz = 8000000u;
/* 从复位开始累计的毫秒数。本项目没开 SysTick 中断，这个数是 Delay_Ms() 里数完
   一次 1 ms 顺手加一记的账，所以只有调用过延时，它才会往前推进。 */
static volatile uint32_t s_ms_tick = 0u;

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */
/* 配置一个引脚，本质就是改 CRL/CRH 里的 4 位。这里刻意把整个 32 位寄存器读出来、
   改完再写回去，而不是用位带 (bit-band) 操作，少一层心智负担；代价是这条
   读-改-写不是原子的，所以不要在主循环和中断里同时配置同一个 port。 */

void GPIO_ConfigPin(GPIO_TypeDef *port, uint8_t pin, uint8_t cfg)
{
    volatile uint32_t *reg;
    uint32_t shift;
    uint32_t value;

    if (pin < 8u) {
        reg = &port->CRL;
        shift = (uint32_t)pin * 4u;
    } else {
        reg = &port->CRH;
        shift = (uint32_t)(pin - 8u) * 4u;
    }

    value = *reg;
    value &= ~(0xFu << shift);
    value |= ((uint32_t)cfg << shift);
    *reg = value;
}

void Board_LedInit(void)
{
    GPIO_ConfigPin(BOARD_LED_PORT, BOARD_LED1_PIN, GPIO_CFG_OUT_PP_2M);
    GPIO_ConfigPin(BOARD_LED_PORT, BOARD_LED2_PIN, GPIO_CFG_OUT_PP_2M);
    Board_LedWrite(0u, 0u);
}

void Board_LedWrite(uint8_t led1On, uint8_t led2On)
{
    uint32_t bsrr = 0u;
    uint8_t  drive1;
    uint8_t  drive2;

    /* 先把"逻辑亮灭"翻译成"引脚电平"，后面拼 BSRR 的代码就与接线极性无关了 */
#if BOARD_LED_ACTIVE_HIGH
    /* 拉电流 (sourcing)：灯亮 = 引脚输出高 */
    drive1 = (led1On != 0u) ? 1u : 0u;
    drive2 = (led2On != 0u) ? 1u : 0u;
#else
    /* 灌电流 (sinking)：灯亮 = 引脚被拉低 */
    drive1 = (led1On != 0u) ? 0u : 1u;
    drive2 = (led2On != 0u) ? 0u : 1u;
#endif

    /* BSRR 的低 16 位置位、高 16 位复位，所以两个灯靠一次 32 位写就同时改完，
       中途不会出现"一个已翻、另一个还没翻"的中间状态 */
    bsrr |= (drive1 != 0u) ? BIT(BOARD_LED1_PIN) : (BIT(BOARD_LED1_PIN) << 16);
    bsrr |= (drive2 != 0u) ? BIT(BOARD_LED2_PIN) : (BIT(BOARD_LED2_PIN) << 16);

    BOARD_LED_PORT->BSRR = bsrr;
}

/* ------------------------------------------------------------------ */
/* 时钟                                                                */
/* ------------------------------------------------------------------ */
/* 复位后芯片跑在内部 HSI 8 MHz 上。这里按「配好 FLASH 等待周期 -> 起 HSE ->
   配 PLL 与总线分频 -> 把 SYSCLK 切到 PLL」的顺序做，任何一步失败都退回 HSI，
   绝不卡死。 */

static void Clock_Init(void)
{
    uint32_t timeout;

    /* FLASH：打开预取 (prefetch)、2 个等待周期 —— 超过 48 MHz 必须这么配，
       否则指令取指跟不上，程序会跑飞 */
    FLASH_ACR = (1u << 4) | 0x2u;

    /* 让 FLASH 接口时钟一直开着，不然刚写的 FLASH_ACR 不生效 */
    RCC->AHBENR |= RCC_AHBENR_FLITFEN;

    /* 启动外部 8 MHz 晶振 (HSE)。晶振起振要几毫秒，所以下面轮询 HSERDY */
    RCC->CR |= RCC_CR_HSEON;
    timeout = HSE_TIMEOUT;
    while (((RCC->CR & RCC_CR_HSERDY) == 0u) && (timeout != 0u)) {
        timeout--;
    }

    if ((RCC->CR & RCC_CR_HSERDY) == 0u) {
        /* 晶振没起振：留在 HSI 上，下面的延时函数会按实际频率自动换算 */
        s_sysclk_hz = 8000000u;
        return;
    }

    /* AHB = SYSCLK，APB2 = SYSCLK，APB1 = SYSCLK/2，PLL = HSE x 9。
       APB1 最高只能跑 36 MHz，所以必须给它分频，不能跟着 72 MHz 一起上。 */
    RCC->CFGR = (4u << 8)      /* PPRE1  = HCLK / 2  -> 36 MHz（APB1 上限） */
              | (0u << 11)     /* PPRE2  = HCLK / 1  -> 72 MHz（APB2 可跑满） */
              | (0x7u << 18)   /* PLLMUL = x9，8 MHz x 9 = 72 MHz */
              | (1u << 16);    /* PLLSRC = HSE，PLL 的输入源选外部晶振 */

    RCC->CR |= RCC_CR_PLLON;
    timeout = PLL_TIMEOUT;
    while (((RCC->CR & RCC_CR_PLLRDY) == 0u) && (timeout != 0u)) {
        timeout--;
    }

    if ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
        s_sysclk_hz = 8000000u;
        return;
    }

    /* 把 SYSCLK 切到 PLL：SWS 变成 0b10 才算真的切过去了 */
    RCC->CFGR = (RCC->CFGR & ~0x3u) | 0x2u;
    timeout = SWS_TIMEOUT;
    while ((((RCC->CFGR >> 2) & 0x3u) != 0x2u) && (timeout != 0u)) {
        timeout--;
    }

    if (((RCC->CFGR >> 2) & 0x3u) == 0x2u) {
        s_sysclk_hz = 72000000u;
    } else {
        s_sysclk_hz = 8000000u;
    }
}

uint32_t Board_ClockHz(void)
{
    return s_sysclk_hz;
}

/* ------------------------------------------------------------------ */
/* 延时                                                                */
/* ------------------------------------------------------------------ */
/* SysTick 的递减计数器：LOAD 装初始值，写 CTRL 让它以内核时钟往下数，数到 0 时
   COUNTFLAG 置 1。本项目不用 SysTick 中断，纯轮询，这样延时逻辑不依赖中断，
   在主循环还是别的地方调用都一样。 */

void Delay_Ms(uint32_t ms)
{
    uint32_t reload = (s_sysclk_hz / 1000u) - 1u;

    while (ms != 0u) {
        SYSTICK->LOAD = reload;
        SYSTICK->VAL  = 0u;
        SYSTICK->CTRL = 0x00000005u;    /* ENABLE | CLKSOURCE = core clock，不分频 */
        while ((SYSTICK->CTRL & (1u << 16)) == 0u) {
            /* 轮询 COUNTFLAG，等这 1 ms 数完 */
        }
        SYSTICK->CTRL = 0u;
        s_ms_tick++;
        ms--;
    }
}

void Delay_Us(uint32_t us)
{
    /* 72 MHz 下大约 8 次循环 1 微秒；只用于很短的总线时序，不需要精确标定。
       n 声明为 volatile，否则编译器会把这个空循环整个优化掉。 */
    volatile uint32_t n = us * 8u;
    while (n != 0u) {
        n--;
    }
}

uint32_t Tick_Ms(void)
{
    return s_ms_tick;
}

/* ------------------------------------------------------------------ */
/* 板级初始化                                                          */
/* ------------------------------------------------------------------ */

void Board_Init(void)
{
    Clock_Init();

    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN
                  | RCC_APB2ENR_IOPAEN
                  | RCC_APB2ENR_IOPBEN
                  | RCC_APB2ENR_IOPCEN;

    /* 按键引脚由 key.c 里的 Key_Init() 配置，引脚接线表 s_keys[] 也归它管。
       所以 Key_Init() 必须在本函数之后调用，否则上面刚打开的 GPIO 时钟还没生效，
       按键引脚的配置写不进去。 */

    /* PB6 = OLED 的 SCL，PB7 = OLED 的 SDA。开漏输出，两条线都先释放成高电平。
       0.96 寸 OLED 模块自带上拉电阻（一般 4.7k），所以这里不用外接。 */
    GPIO_ConfigPin(GPIOB, 6u, GPIO_CFG_OUT_OD_50M);
    GPIO_ConfigPin(GPIOB, 7u, GPIO_CFG_OUT_OD_50M);
    GPIOB->BSRR = BIT(6) | BIT(7);

    /* LED 控制页面用的那两个灯 */
    Board_LedInit();

    /* 板载 LED (PC13，低电平点亮) 不属于本练习的内容。
       把它按住熄灭，免得被误当成上面那两个灯中的一个。 */
    GPIO_ConfigPin(GPIOC, 13u, GPIO_CFG_OUT_PP_2M);
    GPIOC->BSRR = BIT(13);
}
