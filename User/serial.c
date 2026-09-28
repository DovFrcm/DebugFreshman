/*
 * serial.c - USART1 中断驱动的上位机链路
 *
 * PA9 = USART1_TX，PA10 = USART1_RX，8N1，波特率 SERIAL_BAUD。
 *
 * 接收侧是一个单生产者 / 单消费者环形缓冲区 (ring buffer)：USART1 中断写
 * head，主循环读 tail。Cortex-M3 上字节宽度的访问本身就是原子的，所以两边
 * 不需要为了互相保护而关中断。
 */

#include "serial.h"
#include "board.h"

/* 长度取 2 的幂，取模就能用按位与 RX_BUF_MASK 代替除法（除法在单片机上很贵） */
#define RX_BUF_SIZE     64u
#define RX_BUF_MASK     (RX_BUF_SIZE - 1u)

static volatile uint8_t  s_rxBuf[RX_BUF_SIZE];
static volatile uint8_t  s_rxHead;      /* 由中断推进   */
static volatile uint8_t  s_rxTail;      /* 由主循环推进 */
/* 只在中断里递增，主循环只读；32 位的读写在 Cortex-M3 上同样是原子的 */
static volatile uint32_t s_rxOverruns;

void Serial_Init(uint32_t baud)
{
    uint32_t brr;

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = USART1_TX，复用功能推挽输出
       PA10 = USART1_RX，浮空输入（这是 USART1 默认的引脚映射） */
    GPIO_ConfigPin(GPIOA, 9u, GPIO_CFG_AF_PP_50M);
    GPIO_ConfigPin(GPIOA, 10u, GPIO_CFG_IN_FLOAT);

    /* BRR 里存的是 12.4 定点格式的分频系数。72 MHz 下常用波特率都除得尽：
       9600 -> 7500，115200 -> 625，所以这里的四舍五入只是给怪波特率兜底。 */
    brr = (Board_ClockHz() + (baud / 2u)) / baud;
    USART1->BRR = brr;

    s_rxHead = 0u;
    s_rxTail = 0u;
    s_rxOverruns = 0u;

    /* UE | RXNEIE | TE | RE：打开 USART、开接收中断、开发送、开接收 */
    USART1->CR1 = USART_CR1_UE | USART_CR1_RXNEIE
                | USART_CR1_TE | USART_CR1_RE;

    /* USART1 是 37 号中断，也就是 NVIC_ISER1 的第 5 位：37 - 32 = 5 */
    NVIC_ISER1 = (1u << USART1_IRQ_BIT);
}

/* 覆盖 startup 文件里的弱定义中断向量，这样 USART1 中断会跳到这个函数 */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    /* 先读 SR、再读 DR，RXNE 和 overrun 标志会一起被清掉。
       就算只是溢出而没有新数据，也必须把 DR 读一次，否则标志不清，中断会反复重进。 */
    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) != 0u) {
        uint8_t ch = (uint8_t)(USART1->DR & 0xFFu);

        if ((sr & USART_SR_RXNE) != 0u) {
            uint8_t next = (uint8_t)((s_rxHead + 1u) & RX_BUF_MASK);

            if (next == s_rxTail) {
                s_rxOverruns++;         /* 满了：直接丢掉，绝不覆盖还没读走的数据 */
            } else {
                s_rxBuf[s_rxHead] = ch;
                s_rxHead = next;
            }
        }
    }
}

/* 主循环这边负责消费：head == tail 就是空。head 只会被中断改、tail 只会被主循环
   改，所以判断和取值之间不需要关中断。 */
uint8_t Serial_ReadByte(uint8_t *ch)
{
    if (s_rxHead == s_rxTail) {
        return 0u;
    }

    *ch = s_rxBuf[s_rxTail];
    s_rxTail = (uint8_t)((s_rxTail + 1u) & RX_BUF_MASK);

    return 1u;
}

void Serial_WriteByte(uint8_t ch)
{
    while ((USART1->SR & USART_SR_TXE) == 0u) {
        /* 等到发送寄存器腾出空位，能再装一个字节为止 */
    }

    USART1->DR = (uint32_t)ch;
}

void Serial_WriteString(const char *text)
{
    while (*text != '\0') {
        Serial_WriteByte((uint8_t)*text);
        text++;
    }
}

uint32_t Serial_RxOverruns(void)
{
    return s_rxOverruns;
}
