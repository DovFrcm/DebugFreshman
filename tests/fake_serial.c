/*
 * fake_serial.c - serial.c 的电脑端（宿主端）替身
 *
 * 它把固件发出的所有内容都记录下来，并允许测试像从 PC 发来一样把字节
 * 塞进去，这样整套"host 写 / host 读"协议不用开发板就能跑通。
 */

#include <stddef.h>

#include "serial.h"
#include "fake_board.h"

#define RX_MAX  256u
#define TX_MAX  256u

static uint8_t  s_rx[RX_MAX];
static uint16_t s_rxHead;
static uint16_t s_rxTail;

static char     s_tx[TX_MAX];
static uint16_t s_txLen;

void Serial_Init(uint32_t baud)
{
    (void)baud;
    s_rxHead = 0u;
    s_rxTail = 0u;
    s_txLen = 0u;
    s_tx[0] = '\0';
}

uint8_t Serial_ReadByte(uint8_t *ch)
{
    if (s_rxHead == s_rxTail) {
        return 0u;
    }

    *ch = s_rx[s_rxTail];
    s_rxTail++;

    return 1u;
}

void Serial_WriteByte(uint8_t ch)
{
    if (s_txLen < (TX_MAX - 1u)) {
        s_tx[s_txLen] = (char)ch;
        s_txLen++;
        s_tx[s_txLen] = '\0';
    }
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
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 测试钩子                                                            */
/* ------------------------------------------------------------------ */

void fake_serial_feed(const char *text)
{
    while (*text != '\0') {
        if (s_rxHead < RX_MAX) {
            s_rx[s_rxHead] = (uint8_t)(*text);
            s_rxHead++;
        }
        text++;
    }
}

const char *fake_serial_tx(void)
{
    return s_tx;
}

void fake_serial_tx_reset(void)
{
    s_txLen = 0u;
    s_tx[0] = '\0';
}
