/*
 * main.c - 小车主程序：OLED 菜单 + 巡线 + 串口
 *
 * 上电后显示主菜单，箭头停在第一行。
 *   KEY 1 / KEY 2 : 箭头上移 / 下移
 *   KEY 3         : 进入当前选中的子菜单
 *   KEY 4         : 返回主菜单（长按可以直接跳回主菜单）
 *
 * 巡线功能在主菜单的「巡线功能」页里：那一页是控制面板，
 * K1 发车/停车、K2 切圈数、K3 切速度、K4 返回并停车。
 * 详细说明和接线见 README.md。
 */

#include "board.h"
#include "oled.h"
#include "key.h"
#include "menu.h"
#include "app.h"
#include "serial.h"
#include "motor.h"
#include "track.h"

#define LIVE_REFRESH_MS     250u    /* 会自己变的数值的重画周期，单位 ms     */
/* 主循环每轮末尾的延时；2 ms 既能保证按键手感，又不会把 CPU 占满。
   这个值同时决定了循迹控制器的实际执行频率（Track_Tick 自己限频 2 ms），
   所以不要往上调太多 —— 调大了过弯的反应会明显变钝。 */
#define MAIN_LOOP_MS        2u

int main(void)
{
    uint32_t lastRefresh;

    /* 这几句的先后顺序是有讲究的：先把时钟和 GPIO 配好，再开串口，
       然后才初始化 OLED（里面有屏的复位时序），最后才建菜单树。
       反过来的话，菜单初始化时就会去画屏，而那时屏还没准备好。
       电机和循迹传感器必须在 Board_Init() 之后初始化 ——
       GPIO 的时钟是 Board_Init() 打开的。 */
    Board_Init();           /* 72 MHz 时钟、关 JTAG、LED、蜂鸣器、1ms 滴答 */
    Motor_Init();           /* TB6612 + TIM4 两路 PWM             */
    Track_Init();           /* PB12..PB15 四路循迹传感器          */
    Serial_Init(SERIAL_BAUD);/* USART1 上位机串口（PA9/PA10）     */
    OLED_Init();            /* 屏的复位时序，同时清空显存 RAM     */
    Key_Init();
    App_Init();             /* 建菜单树，箭头落在第一行           */

    /* 上电时电机必须是停的。Motor_Init() 里已经清零了，
       这里再停一次是防止中途有人在初始化里动了电机。 */
    Motor_Stop();

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
