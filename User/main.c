/*
 * main.c - STM32F103C8 上的 OLED 菜单演示程序
 *
 * 上电后显示主菜单，箭头停在第一行。
 *   KEY 1 / KEY 2 : 箭头上移 / 下移
 *   KEY 3         : 进入当前选中的子菜单
 *   KEY 4         : 返回主菜单（长按可以直接跳回主菜单）
 *
 * 编译、下载等说明见 README.md。
 */

#include "board.h"
#include "oled.h"
#include "key.h"
#include "menu.h"
#include "app.h"
#include "serial.h"

#define LIVE_REFRESH_MS     250u    /* 会自己变的数值的重画周期，单位 ms     */
/* 主循环每轮末尾的延时；2 ms 既能保证按键手感，又不会把 CPU 占满 */
#define MAIN_LOOP_MS        2u

int main(void)
{
    uint32_t lastRefresh;

    /* 这几句的先后顺序是有讲究的：先把时钟、GPIO 和两个 LED 配好，再开串口，
       然后才初始化 OLED（里面有屏的复位时序），最后才建菜单树。反过来的话，
       菜单初始化时就会去画屏，而那时屏还没准备好 */
    Board_Init();           /* 72 MHz 时钟、GPIO、两个 LED           */
    Serial_Init(SERIAL_BAUD);/* USART1 上位机串口（PA9/PA10）        */
    OLED_Init();            /* 屏的复位时序，同时清空显存 RAM        */
    Key_Init();
    App_Init();             /* 建菜单树，箭头落在第一行              */

    lastRefresh = Tick_Ms();

    for (;;) {
        KeyEvent event = Key_Scan();
        uint32_t now;

        if (event != KEY_NONE) {
            Menu_HandleKey(event);
        }

        App_Tick();         /* LED 闪烁 + 收上位机串口数据           */

        now = Tick_Ms();

        /* 两种情况都要重画：一是按键把画面置脏了，这时要立刻响应、不能等周期；
           二是当前页面显示的是会自己变化的数值（时钟、运行时间……），
           这种只能靠定时重画。整屏刷一次要走 I2C，比较费时间，所以做限频。
           另外这里的 (now - lastRefresh) 是无符号相减，即使 Tick_Ms()
           回绕了结果依然正确 */
        if ((Menu_NeedsRedraw() != 0u)
            || ((Menu_PageHasValue() != 0u)
                && ((uint32_t)(now - lastRefresh) >= LIVE_REFRESH_MS))) {
            Menu_Draw();
            OLED_Refresh();
            Menu_ClearDirty();
            lastRefresh = Tick_Ms();
        }

        Delay_Ms(MAIN_LOOP_MS);
    }
}
