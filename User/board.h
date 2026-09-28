/*
 * board.h - STM32F103C8 最小板级支持包 (board support)
 *
 * 本工程刻意做成完全自包含的：除 Keil 的器件 startup 文件之外什么都不需要。
 * 它不依赖 Standard Peripheral Library（标准外设库），也不依赖 STM32Cube HAL，
 * 所以这里用到的寄存器定义全部就地声明。而且只描述本工程真正碰到的外设
 * （RCC、GPIOA/B/C、FLASH_ACR、SysTick）—— 少写几个寄存器，既省编译时间，
 * 也方便读代码时直接对着参考手册的寄存器表核对。
 */

#ifndef __BOARD_H
#define __BOARD_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 寄存器布局 (register layout)                                        */
/* ------------------------------------------------------------------ */
/* 成员顺序必须和参考手册一致：结构体的首地址由下面那几个宏写死，顺序错一个或者
   少写一个成员，后面所有寄存器的地址就全都错位了。类型统一用 volatile uint32_t，
   否则编译器会把对寄存器的读写当成普通变量优化掉（读一次就不再读、写被合并）。 */

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;
    volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t APB1ENR;
    volatile uint32_t BDCR;
    volatile uint32_t CSR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_TypeDef;

/* 外设基地址，照参考手册的 memory map 直接写，不经过任何库函数 */
#define RCC          ((RCC_TypeDef *)     0x40021000u)
#define GPIOA        ((GPIO_TypeDef *)    0x40010800u)
#define GPIOB        ((GPIO_TypeDef *)    0x40010C00u)
#define GPIOC        ((GPIO_TypeDef *)    0x40011000u)
#define SYSTICK      ((SysTick_TypeDef *) 0xE000E010u)
#define FLASH_ACR    (*(volatile uint32_t *)0x40022000u)

/* RCC_CR 时钟控制寄存器各位：
 *   HSEON  = 打开外部高速晶振 (HSE)，HSERDY = 晶振已稳定
 *   PLLON  = 打开 PLL，PLLRDY = PLL 已锁定
 * 写 1 是"开始启动"，之后必须轮询对应的 RDY 位，确认硬件真的起来了再往下走。 */
#define RCC_CR_HSEON        (1u << 16)
#define RCC_CR_HSERDY       (1u << 17)
#define RCC_CR_PLLON        (1u << 24)
#define RCC_CR_PLLRDY       (1u << 25)

/* RCC_APB2ENR 外设时钟使能寄存器（APB2 那一侧）各位 */
#define RCC_APB2ENR_AFIOEN  (1u << 0)
#define RCC_APB2ENR_IOPAEN  (1u << 2)
#define RCC_APB2ENR_IOPBEN  (1u << 3)
#define RCC_APB2ENR_IOPCEN  (1u << 4)
#define RCC_APB2ENR_USART1EN (1u << 14)

/* RCC_AHBENR 外设时钟使能寄存器（AHB 那一侧）各位 */
#define RCC_AHBENR_FLITFEN  (1u << 4)

/* GPIO 配置半字节 (nibble)，取值 = (CNF << 2) | MODE
 *
 * 每个引脚在 CRL/CRH 里占 4 位：低 2 位 MODE 表示输入模式或输出速度，高 2 位
 * CNF 表示输入输出类型。这里把两段拼成一个数传给 GPIO_ConfigPin()，一个引脚
 * 一次 32 位读-改-写就配好了。 */
#define GPIO_CFG_IN_ANALOG    0x0u
#define GPIO_CFG_IN_FLOAT     0x4u   /* 同时也是 USART RX 要用的设置 */
#define GPIO_CFG_IN_PUPD      0x8u   /* ODR 对应位 = 1 选上拉，= 0 选下拉 */
/* 输出档位怎么选：2 MHz 足够驱动 LED 这种慢信号；50 MHz 档给 I2C 的 OLED 和
   USART 用。推挽 (push-pull) 能主动输出高和低；开漏 (open drain) 只能拉低，
   高电平靠外部上拉电阻，I2C 这种总线必须开漏，否则两个器件同时输出就是短路。 */
#define GPIO_CFG_OUT_PP_2M    0x2u
#define GPIO_CFG_OUT_PP_50M   0x3u
#define GPIO_CFG_OUT_OD_50M   0x7u
#define GPIO_CFG_AF_PP_50M    0xBu   /* 复用功能推挽 (alternate function)，USART TX */

/* 位掩码小工具：BIT(3) 展开就是 (1u << 3)，给 BSRR、CR 这类需要拼位的寄存器用 */
#define BIT(n)  (1u << (n))

/* ------------------------------------------------------------------ */
/* USART1 - 与上位机通信的串口，由 serial.c 驱动                        */
/* ------------------------------------------------------------------ */
/* USART1 挂在 APB2 上，所以它的时钟使能位在 RCC_APB2ENR 里。初始化代码在
   serial.c，这里只放收发用得着的那几个寄存器位。 */

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

#define USART1               ((USART_TypeDef *) 0x40013800u)

/* USART_SR 状态寄存器各位：
 *   ORE  = overrun，上一个字节还没被读走又收满了一个，数据已经丢了
 *   RXNE = 接收数据寄存器非空，说明有字节可读
 *   TXE  = 发送数据寄存器空，说明可以写入下一个字节 */
#define USART_SR_ORE         (1u << 3)
#define USART_SR_RXNE        (1u << 5)
#define USART_SR_TXE         (1u << 7)

/* USART_CR1 控制寄存器 1 各位：
 *   RE     = 接收使能        TE     = 发送使能
 *   RXNEIE = 接收非空中断使能  UE     = USART 总使能 */
#define USART_CR1_RE         (1u << 2)
#define USART_CR1_TE         (1u << 3)
#define USART_CR1_RXNEIE     (1u << 5)
#define USART_CR1_UE         (1u << 13)

/* Cortex-M3 中断控制器 (NVIC)。USART1 是 37 号中断，32~63 号中断归 ISER1 管，
   所以它的使能位是 ISER1 的第 5 位：37 - 32 = 5。 */
#define NVIC_ISER1           (*(volatile uint32_t *)0xE000E104u)
#define USART1_IRQ_BIT       5u

/* ------------------------------------------------------------------ */
/* board 对外接口 (API)                                                */
/* ------------------------------------------------------------------ */

/* 配置系统时钟、GPIO 时钟，以及板子上真正用到的那些引脚。
   上电后第一件事就该调它：时钟没起来，后面所有外设的寄存器都写不进去。 */
void     Board_Init(void);

/* 往 CRL（pin < 8）或 CRH 里写一个 4 位的配置半字节。
   内部是整个寄存器读-改-写，所以别在主循环和中断里同时配同一个 port。 */
void     GPIO_ConfigPin(GPIO_TypeDef *port, uint8_t pin, uint8_t cfg);

/* 基于 SysTick 递减计数器的阻塞延时 */
void     Delay_Ms(uint32_t ms);
void     Delay_Us(uint32_t us);

/* 从复位到现在经过的毫秒数，由 Delay_Ms() 推进。
   它靠的是"延时顺便记账"，所以没调用过延时的地方它不会自己走。 */
uint32_t Tick_Ms(void);

/* SYSCLK 的实际频率 (Hz)。晶振没起振时也能把这个真实值显示出来，免得骗人 */
uint32_t Board_ClockHz(void);

/* ------------------------------------------------------------------ */
/* LED 控制页面驱动的两个灯                                             */
/*                                                                     */
/* 两个灯故意挂在同一个 port 上，这样一次写 BSRR 就能把它们一起更新。      */
/* 于是两个灯在同一个总线周期里翻转，交替闪烁时绝不可能被拍到同时亮或       */
/* 同时灭 —— 而交替闪烁测试要看的正是这一点。                            */
/* 以后如果要挪引脚，请仍然把它们留在同一个 port 上。                     */
/* ------------------------------------------------------------------ */

#define BOARD_LED_PORT      GPIOB
#define BOARD_LED1_PIN      12u
#define BOARD_LED2_PIN      13u

/* ------------------------------------------------------------------ */
/* LED 驱动极性 —— 按实际接线方式设置这一项                              */
/*                                                                     */
/*   1  引脚 -> 限流电阻 -> LED 阳极，LED 阴极 -> GND                    */
/*      引脚输出高电平点亮（拉电流 sourcing）                            */
/*                                                                     */
/*   0  3.3V -> 限流电阻 -> LED 阳极，LED 阴极 -> 引脚                   */
/*      引脚输出低电平点亮（灌电流 sinking）                             */
/*                                                                     */
/* OLED 状态页面会自动跟着这个宏走：Led_Apply() 保存的是逻辑上的亮/灭      */
/* 状态，屏幕打印的也是这个逻辑状态，所以只要改这一个宏，界面和实物灯       */
/* 就始终一致。                                                          */
/* ------------------------------------------------------------------ */
#define BOARD_LED_ACTIVE_HIGH   0

/* 把两个 LED 引脚都配成推挽输出，并且先都熄灭 */
void     Board_LedInit(void);

/* 驱动这一对灯；ledNOn = 1 表示"这个灯应该亮"（逻辑状态，和接线极性无关） */
void     Board_LedWrite(uint8_t led1On, uint8_t led2On);

#endif /* __BOARD_H */
