/*
 * app.h - 菜单树（menu tree）以及它背后的功能实现
 */

#ifndef __APP_H
#define __APP_H

#include <stdint.h>

void App_Init(void);

/* 由 main 的主循环反复调用：一方面驱动 LED 的闪烁，另一方面把上位机
   串口发来的数据取走处理掉。之所以做成 Tick 形式而不是在这里死等，
   是为了主循环里的按键扫描和 OLED 刷新不会被串口拖住。 */
void App_Tick(void);

/* 返回上位机可读写的变量 a 的当前值（题目要求 3）。
   做成只读的 getter，外部就只能通过串口来改它，变量本身留在 app.c 内部 */
int32_t App_GetVarA(void);

#endif /* __APP_H */
