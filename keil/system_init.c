/*
 * system_init.c - Keil 启动文件会调用的那个 SystemInit() 钩子函数
 *
 * 启动文件 startup_stm32f10x_md.s 里有这么三句：
 *     IMPORT  SystemInit
 *     LDR     R0, =SystemInit
 *     BLX     R0
 * 也就是说启动代码会通过函数指针跳到 SystemInit。所以这个符号必须存在，
 * 否则链接阶段就会直接报 undefined symbol。
 *
 * 本工程的时钟树是在 board.c 的 Clock_Init() 里配置的，所以这个钩子
 * 故意留空，什么都不做。另外请注意：不要在 Keil 的 Run-Time Environment
 * 里给本工程勾上 "Device: Startup" 组件，因为那会把 system_stm32f10x.c
 * 拉进编译，而那个文件里也定义了一份 SystemInit，链接时会出现重复符号
 * （duplicate symbol）而失败。
 */

#include <stdint.h>

/* 中文注释编码测试：启动文件会调用这个函数，时钟在 board.c 里配置 */
void SystemInit(void)
{
}

/* 这个变量正常是由 system_stm32f10x.c 提供的。这里自己留一份，是为了让
   任何引用它的库代码、以及调试器里查看它的窗口，都还能正常链接和取值 */
uint32_t SystemCoreClock = 8000000u;
