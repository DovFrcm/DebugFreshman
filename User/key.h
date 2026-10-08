/*
 * key.h - 四个按键 (push button)
 *
 * 按键把引脚直接接到 GND，芯片内部的上拉把引脚平时拉在高电平，
 * 所以按下时读到的是逻辑 0。不需要外接电阻。
 *
 *   KEY_1 (PB4) -> 光标上移 / 板载 KEY1
 *   KEY_2 (PB5) -> 光标下移 / 板载 KEY2
 *   KEY_3 (PA0) -> 进入选中的子菜单 / 发车（原舵机 1 接口，需外接按键）
 *   KEY_4 (PA1) -> 返回主菜单       （原舵机 2 接口，需外接按键）
 *
 * 小车主板上只焊了 KEY1/KEY2 两个按钮，所以 KEY_3/KEY_4 要自己接到
 * 舵机接口上（PA0/PA1）：一脚接信号、一脚接接口上的 GND 就行。
 * 只接板载两键也能用，但进不了子菜单、也返回不了 —— 菜单至少要三键。
 *
 * 长按 KEY_4 超过 KEY_LONG_MS 还会额外报出 KEY_4_LONG，
 * 它可以从任意层级直接跳回主菜单。
 *
 * 引脚没有硬编码散落在驱动各处：整套接线都放在 key.c 顶部的 s_keys[] 表里。
 * 在那里改一下，Key_Init() 就会去配置新引脚，Key_Scan() 就会去读它们 ——
 * 工程里其他文件一行都不用动。
 */

#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>

/* 按键事件码。KEY_1..KEY_4 刻意排成连续的，key.c 里就能用 KEY_1 + i 直接
   算出是哪个键，不必写一大串 switch。 */
typedef enum {
    KEY_NONE = 0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_4_LONG
} KeyEvent;

/* 配置按键引脚，请在 Board_Init() 之后调用 */
void     Key_Init(void);
KeyEvent Key_Scan(void);        /* 尽量勤调用；它自己限制成 5 ms 才扫一次 */
/* 查询某个按键当前是否按下；index 就是 s_keys[] 的下标，越界返回 0 */
uint8_t  Key_IsDown(uint8_t index);

#endif /* __KEY_H */
