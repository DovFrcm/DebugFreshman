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

/* AFIO 是复用功能重映射/外部中断那套东西的控制块。本工程只用它一个功能：
   关掉 JTAG、把 PB3/PB4/PA15 这几个 JTAG 专用脚腾出来当普通 IO 用。
   整个寄存器组里我们只碰 MAPR 一个，所以只需要它的地址。 */
typedef struct {
    volatile uint32_t EVCR;
    volatile uint32_t MAPR;
    volatile uint32_t EXTICR[4];
    volatile uint32_t RESERVED0;
    volatile uint32_t MAPR2;
} AFIO_TypeDef;

/* 通用定时器。本工程用 TIM4 的两个通道输出 PWM 驱动电机。
   成员顺序必须严格照参考手册（RM0008）的偏移排，
   写错一个成员的顺序，后面所有寄存器地址就全错了。
   CCR3/CCR4 就是左右两个电机的占空比寄存器。 */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SMCR;
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    volatile uint32_t CCMR1;
    volatile uint32_t CCMR2;
    volatile uint32_t CCER;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
    volatile uint32_t ARR;
    volatile uint32_t RCR;
    volatile uint32_t CCR1;
    volatile uint32_t CCR2;
    volatile uint32_t CCR3;
    volatile uint32_t CCR4;
    volatile uint32_t BDTR;
    volatile uint32_t DCR;
    volatile uint32_t DMAR;
} TIM_TypeDef;

/* 外设基地址，照参考手册的 memory map 直接写，不经过任何库函数 */
#define RCC          ((RCC_TypeDef *)     0x40021000u)
#define AFIO         ((AFIO_TypeDef *)    0x40010000u)
#define GPIOA        ((GPIO_TypeDef *)    0x40010800u)
#define GPIOB        ((GPIO_TypeDef *)    0x40010C00u)
#define GPIOC        ((GPIO_TypeDef *)    0x40011000u)
#define TIM4         ((TIM_TypeDef *)     0x40000800u)
#define SYSTICK      ((SysTick_TypeDef *) 0xE000E010u)
#define FLASH_ACR    (*(volatile uint32_t *)0x40022000u)

/* Cortex-M3 的系统控制块里跟本工程有关的那几个寄存器 */
#define SCB_SHPR3    (*(volatile uint32_t *)0xE000ED20u)   /* SysTick 优先级 */
#define SCB_AIRCR    (*(volatile uint32_t *)0xE000ED0Cu)

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

/* RCC_APB1ENR 外设时钟使能寄存器（APB1 那一侧）各位。
   TIM4 挂在 APB1 上，所以它的时钟使能位在这里，不是在 APB2ENR。
   注意 APB1 的分频系数是 2（见 board.c），此时定时器时钟会自动 x2，
   所以 TIM4 数数的频率仍然是 72 MHz —— 这是 STM32F1 的一个特例，
   只有"APB 分频 != 1"时定时器时钟才翻倍。 */
#define RCC_APB1ENR_TIM4EN  (1u << 2)

/* AFIO_MAPR 的 SWJ_CFG[2:0]（bit26..24）：JTAG/SWD 引脚怎么分配
 *   0b000 全功能 SWJ（JTAG + SWD），复位后的默认值
 *   0b010 关掉 JTAG，只留 SWD  ← 本工程要的就是这个
 *   0b100 JTAG 和 SWD 全关（关了就再也连不上 ST-Link 了，千万别用）
 *
 * 为什么必须关 JTAG：小车主板上 PB3 接蜂鸣器、PB4/PB5 接按键、PA15 接
 * 循迹/外设。这五个脚复位后默认归 JTAG 用（PB3=JTDO、PB4=NJTRST、
 * PA15=JTDI），不关掉 JTAG 的话，普通 GPIO 寄存器写了也不起作用。
 * 关掉 JTAG 之后 PA13/PA14（SWDIO/SWCLK）仍然是调试口，ST-Link 照常能烧录。 */
#define AFIO_MAPR_SWJ_CFG_MASK  (0x7u << 24)
#define AFIO_MAPR_SWJ_JTAG_OFF  (0x2u << 24)

/* TIM4 寄存器位。只列本工程用得到的：
 *   CR1.CEN   = 1 启动计数器
 *   CR1.ARPE  = 1 让 ARR 也走预装载，改周期时不会出现半周期的怪波形
 *   CCMRy.OCxM = 0b110 就是 PWM 模式 1（计数值 < CCRx 时输出有效电平）
 *   CCMRy.OCxPE = 1 让 CCRx 走预装载，改占空比时不会切出毛刺
 *   CCER.CCxE = 1 把该通道的输出接到引脚上（不使能的话引脚不动） */
#define TIM_CR1_CEN         (1u << 0)
#define TIM_CR1_ARPE        (1u << 7)
#define TIM_CR1_UG          (1u << 0)   /* EGR 里的"产生更新事件"位 */
#define TIM_CCMR_OCM_PWM1   (0x6u)      /* 要左移 4(CH3) 或 12(CH4) 位 */
#define TIM_CCMR_OC3PE      (1u << 3)
#define TIM_CCMR_OC4PE      (1u << 11)
#define TIM_CCER_CC3E       (1u << 8)
#define TIM_CCER_CC4E       (1u << 12)

/* PWM 周期：72 MHz / 7200 = 10 kHz。TB6612 支持到 100 kHz，
   10 kHz 在 audible range 之上一点点，电机不会叫得太难听，
   同时 7200 级的分辨率让低速也能平滑调速。
   占空比 = CCRx / 7200，CCRx 的合法范围是 0 ~ 7199。 */
#define MOTOR_PWM_PERIOD    7200u
#define MOTOR_PWM_MAX       7199u

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

/* 基于 SysTick 滴答计数器的阻塞延时 */
void     Delay_Ms(uint32_t ms);
void     Delay_Us(uint32_t us);

/* 从复位到现在经过的毫秒数。
 *
 * 它由 SysTick 的 1 ms 中断来推进，所以是真正的"墙上时间"：不管主循环在
 * 刷 OLED、在忙等还是闲着，它都照走。巡线的圈速计时和丢线防抖都依赖这个
 * 性质 —— 早期版本把计数放在 Delay_Ms() 里"延时顺便记账"，那样刷一次屏
 * （软件 I2C 要十几毫秒）就漏记了，圈速会偏短。
 *
 * 32 位毫秒计数会在 49.7 天回绕。用 (uint32_t)(now - start) 这种无符号
 * 相减的写法算时间差，回绕时结果照样是对的。 */
uint32_t Tick_Ms(void);

/* 自复位以来 SysTick 有没有漏掉过中断（中断被长时间关掉时会漏）。
   正常应该是 0，可以用来判断主循环是不是被某个阻塞操作卡太久 */
uint32_t Tick_Missed(void);

/* SYSCLK 的实际频率 (Hz)。晶振没起振时也能把这个真实值显示出来，免得骗人 */
uint32_t Board_ClockHz(void);

/* ------------------------------------------------------------------ */
/* LED 控制页面驱动的两个灯                                             */
/*                                                                     */
/* 灯挪到 PA12 / PA15，是因为主板上 PB12/PB13 已经给 4 路循迹传感器用了， */
/* 而这两个脚原本是"红外避障传感器 1/2"的接口，本项目不用红外避障，       */
/* 接口正好空出来插 LED。                                              */
/*                                                                     */
/* 两个灯仍然挂在同一个 port 上，一次写 BSRR 就能把它们一起更新，         */
/* 交替闪烁时绝不可能被拍到同时亮或同时灭。                              */
/*                                                                     */
/* 注意 PA15 复位后是 JTDI，必须先在 Board_Init() 里关掉 JTAG 才能当     */
/* 普通 IO 用 —— 这件事 Board_Init() 已经做了。                         */
/* ------------------------------------------------------------------ */

#define BOARD_LED_PORT      GPIOA
#define BOARD_LED1_PIN      12u
#define BOARD_LED2_PIN      15u

/* 蜂鸣器：主板上的有源蜂鸣器接在 PB3（同样是 JTAG 脚，靠关 JTAG 腾出来）。
   有源蜂鸣器给高电平就响，不需要 PWM 驱动。 */
#define BOARD_BEEP_PORT     GPIOB
#define BOARD_BEEP_PIN      3u

/* ------------------------------------------------------------------ */
/* LED 驱动极性 —— 按实际接线方式设置这一项                              */
/*                                                                     */
/*   1  引脚 -> 限流电阻 -> LED 阳极，LED 阴极 -> GND                    */
/*      引脚输出高电平点亮（拉电流 sourcing）  ← 本工程当前用的          */
/*                                                                     */
/*   0  3.3V -> 限流电阻 -> LED 阳极，LED 阴极 -> 引脚                   */
/*      引脚输出低电平点亮（灌电流 sinking）                             */
/*                                                                     */
/* OLED 状态页面会自动跟着这个宏走：Led_Apply() 保存的是逻辑上的亮/灭      */
/* 状态，屏幕打印的也是这个逻辑状态，所以只要改这一个宏，界面和实物灯       */
/* 就始终一致。                                                          */
/*                                                                     */
/* 本工程实车验证时发现灯和屏幕是反的（屏幕显示"亮"、实际灯灭），           */
/* 所以从 0 改成了 1。填错了不会烧东西，只是亮灭反相，改回来即可。          */
/* ------------------------------------------------------------------ */
#define BOARD_LED_ACTIVE_HIGH   1

/* 把两个 LED 引脚都配成推挽输出，并且先都熄灭 */
void     Board_LedInit(void);

/* 驱动这一对灯；ledNOn = 1 表示"这个灯应该亮"（逻辑状态，和接线极性无关） */
void     Board_LedWrite(uint8_t led1On, uint8_t led2On);

/* ------------------------------------------------------------------ */
/* 蜂鸣器 (buzzer)                                                     */
/* ------------------------------------------------------------------ */

/* 蜂鸣器引脚配成推挽输出并先关掉。要在 Board_Init() 之后调用 */
void     Board_BeepInit(void);

/* 立刻开/关蜂鸣器（有源蜂鸣器，给高电平就响） */
void     Board_BeepWrite(uint8_t on);

/* ------------------------------------------------------------------ */
/* JTAG 引脚释放                                                       */
/* ------------------------------------------------------------------ */

/* 关掉 JTAG、保留 SWD，把 PB3/PB4/PA15 变成普通 GPIO。
   不调用它的话蜂鸣器（PB3）、按键 1（PB4）、LED2（PA15）都不会动。
   Board_Init() 里已经调用过一次，这里再暴露出来是方便别人单独用。 */
void     Board_ReleaseJtagPins(void);

#endif /* __BOARD_H */
