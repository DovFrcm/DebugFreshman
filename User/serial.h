/*
 * serial.h - USART1 与上位机的串口链路
 *
 * 接线（见 README）：
 *   PA9  = USART1_TX  ->  接到 USB-TTL 转接板的 RX
 *   PA10 = USART1_RX  <-  来自 USB-TTL 转接板的 TX
 * 也就是 TX 接对方的 RX、RX 接对方的 TX（交叉相接），并且两边必须共地。
 *
 * 接收走 USART1 中断，收进环形缓冲区 (ring buffer)。这不是为了好看：
 * 一次完整的 OLED 刷新会把主循环堵住很久，远超 9600 baud 下一个字符的传输
 * 时间，所以轮询接收一定会悄悄丢字节。让中断来填缓冲区，就一个字节都不会丢。
 */

#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdint.h>

/* 8 位数据位、无校验、1 位停止位 (8N1)。必须和 PC 端的设置一致。 */
#define SERIAL_BAUD     9600u

/* 请在 Board_Init() 之后调用，因为 GPIO 和 USART 的时钟是它打开的 */
void     Serial_Init(uint32_t baud);

/* 非阻塞：有字节在等就返回 1 并把它存进 *ch；没有就返回 0 */
uint8_t  Serial_ReadByte(uint8_t *ch);

/* 阻塞式发送，用来回复上位机 */
void     Serial_WriteByte(uint8_t ch);
void     Serial_WriteString(const char *text);

/* 因为接收缓冲区满而被丢掉的字节数 */
uint32_t Serial_RxOverruns(void);

#endif /* __SERIAL_H */
