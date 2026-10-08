/*
 * test_menu.c - OLED 菜单固件的电脑端（宿主端）测试程序。
 *
 * 它链接真实的 oled.c、oled_font.c、menu.c 和 app.c，只把硬件层替换掉，
 * 然后用按键事件驱动菜单，并检查真实的 128x64 frame buffer。frame buffer
 * 可以打印成 ASCII 画，所以不用开发板也能核对面板上实际会显示的画面。
 *
 * 编译与运行：见 run_tests.ps1
 */

#include <stdio.h>
#include <string.h>

#include "fake_board.h"
#include "oled.h"
#include "menu.h"
#include "key.h"
#include "app.h"

static int g_failures;
static int g_checks;

/* ------------------------------------------------------------------ */
/* frame buffer 辅助函数                                               */
/* ------------------------------------------------------------------ */

static int pixel(int x, int y)
{
    const uint8_t *fb = OLED_Buffer();
    return (fb[(y >> 3) * 128 + x] >> (y & 7)) & 1;
}

/* 被选中的那一行画成反色的高亮条，所以该行第 0 列是 x == 0 处唯一点亮的
   像素；别的地方从来不会画到这里 */
static int highlight_row(void)
{
    int row;
    int found = -1;

    for (row = 0; row < 4; row++) {
        if (pixel(0, row * 16) != 0) {
            if (found >= 0) {
                return -2;              /* 高亮的行不止一行，说明画错了 */
            }
            found = row;
        }
    }

    return found;
}

static int arrow_inside_row(int row)
{
    /* 箭头是"挖"在高亮条里的黑像素（高亮条反色，箭头保持不亮） */
    return (pixel(3, row * 16 + 4) == 0) && (pixel(3, row * 16 + 8) == 0);
}

static void dump_screen(const char *title)
{
    int y;
    int x;

    printf("\n==== %s   (depth=%u, highlight row=%d)\n", title,
           (unsigned)Menu_Depth(), highlight_row());

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++) {
            putchar(pixel(x, y) ? '#' : '.');
        }
        putchar('\n');
    }
}

/* ------------------------------------------------------------------ */
/* 断言                                                                */
/* ------------------------------------------------------------------ */

static void expect(int condition, const char *what)
{
    g_checks++;
    if (condition == 0) {
        g_failures++;
    }
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
}

static void press(KeyEvent event, const char *name)
{
    Menu_HandleKey(event);
    Menu_Draw();
    printf("  key %-10s -> depth=%u highlightRow=%d\n", name,
           (unsigned)Menu_Depth(), highlight_row());
}

/* ------------------------------------------------------------------ */

int main(void)
{
    int row;

    fake_led_reset();
    App_Init();                 /* 真实的菜单树，箭头从第 0 行开始 */
    Menu_Draw();

    printf("\n--- 1. power on -------------------------------------------------\n");
    expect(Menu_Depth() == 1u, "power on shows the main menu (depth 1)");
    expect(highlight_row() == 0, "the arrow points at the first line");
    expect(arrow_inside_row(0), "the arrow is drawn inside the highlight bar");
    dump_screen("power on");

    printf("\n--- 2. key 2 walks the arrow down -------------------------------\n");
    press(KEY_2, "KEY_2");
    expect(highlight_row() == 1, "KEY_2 -> second line");
    press(KEY_2, "KEY_2");
    expect(highlight_row() == 2, "KEY_2 -> third line");
    press(KEY_2, "KEY_2");
    expect(highlight_row() == 3, "KEY_2 -> fourth line");
    press(KEY_2, "KEY_2");
    expect(highlight_row() == 3, "fifth entry scrolls the window, arrow stays on the last visible line");
    dump_screen("after 4 x KEY_2 (window scrolled to entry 5)");

    printf("\n--- 3. wrapping ------------------------------------------------\n");
    press(KEY_2, "KEY_2");
    expect(highlight_row() == 0, "KEY_2 past the last entry wraps to the first");
    press(KEY_1, "KEY_1");
    expect(highlight_row() == 3, "KEY_1 before the first entry wraps to the last");
    press(KEY_1, "KEY_1");
    expect(highlight_row() == 2, "KEY_1 -> up one line");

    printf("\n--- 4. KEY_4 on the main menu does nothing ----------------------\n");
    press(KEY_4, "KEY_4");
    expect(Menu_Depth() == 1u, "still on the main menu");

    printf("\n--- 5. enter the LED sub menu ----------------------------------\n");
    press(KEY_1, "KEY_1");              /* 光标 3 -> 2 */
    press(KEY_1, "KEY_1");              /* 光标 2 -> 1 */
    press(KEY_1, "KEY_1");              /* 光标 1 -> 0，也就是 LED control 那一项 */
    expect(highlight_row() == 0, "arrow back on the first line");
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 2u, "KEY_3 entered the LED sub menu");
    expect(fake_led1 == 0u && fake_led2 == 0u, "both lamps are dark on entry");
    dump_screen("LED control page (both dark)");

    printf("\n--- 6. requirement a: KEY_1 = steady on <-> steady off ----------\n");
    press(KEY_1, "KEY_1");
    expect(fake_led1 == 1u && fake_led2 == 1u, "KEY_1 lights BOTH lamps");
    expect(Menu_Depth() == 2u, "and stays on the LED page");
    dump_screen("both lamps steady on");

    press(KEY_1, "KEY_1");
    expect(fake_led1 == 0u && fake_led2 == 0u, "KEY_1 again darkens BOTH lamps");
    dump_screen("both lamps dark again");

    printf("\n--- 7. requirement b: KEY_2 = alternating blink ----------------\n");
    press(KEY_2, "KEY_2");
    expect(fake_led1 != fake_led2, "alternating starts with the lamps in opposite states");

    {
        int rounds;
        int overlap = 0;
        int stuck = 0;
        int swaps = 0;
        uint8_t previousLed1 = fake_led1;

        /* 模拟 90 秒时间，每 10 ms 采样一次：相当于评分标准里那个
           30 秒测试跑 3 轮 */
        for (rounds = 0; rounds < 9000; rounds++) {
            uint8_t diff;

            Delay_Ms(10u);
            App_Tick();

            diff = (uint8_t)(fake_led1 != fake_led2);
            if (diff == 0u) {
                overlap++;              /* 两灯同时亮或同时灭：交替闪烁不允许 */
            }
            if (fake_led1 != previousLed1) {
                swaps++;                /* LED1 状态翻转了一次：一次真正的互换  */
                previousLed1 = fake_led1;
            }
            if (fake_led1 == 0u && fake_led2 == 0u) {
                stuck++;                /* 闪烁卡住了，两灯都灭 */
            }
        }

        printf("  sampled 90 s: %d swaps, %d overlapping samples, %d dark samples\n",
               swaps, overlap, stuck);
        expect(overlap == 0, "the two lamps are never in the same state (no overlap)");
        expect(stuck == 0, "the blink never stops (a lamp is always lit)");
        expect(swaps >= 170 && swaps <= 190, "roughly one swap per 500 ms over 90 s");
    }
    dump_screen("alternating blink");

    printf("\n--- 8. requirement c: KEY_4 back to the main menu, lamps off ---\n");
    press(KEY_4, "KEY_4");
    expect(Menu_Depth() == 1u, "KEY_4 returned to the main menu");
    expect(fake_led1 == 0u && fake_led2 == 0u, "both lamps are dark on the main menu");
    expect(highlight_row() == 0, "the arrow is restored on the entry we came from");
    dump_screen("back on the main menu");

    printf("\n--- 8b. long KEY_4 must also leave the lamps off ---------------\n");
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 2u, "entered the LED page again");
    press(KEY_2, "KEY_2");
    expect(fake_led1 != fake_led2, "alternating again");
    press(KEY_4_LONG, "KEY_4_LONG");
    expect(Menu_Depth() == 1u, "long KEY_4 jumped to the main menu");
    expect(fake_led1 == 0u && fake_led2 == 0u, "and it forced both lamps off too");

    printf("\n--- 9. information page ----------------------------------------\n");
    press(KEY_2, "KEY_2");
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 2u, "entered the information page");
    dump_screen("information sub menu");

    printf("\n--- 10. deep nesting, level 3 -----------------------------------\n");
    /* 下面两处注释里的 extras / settings 是菜单项的英文标签，保留原词 */
    press(KEY_4, "KEY_4");              /* 回到主菜单，光标停在 "information" 上 */
    press(KEY_2, "KEY_2");
    press(KEY_2, "KEY_2");              /* -> extras（菜单项标签，下同） */
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 2u, "entered the extras page");
    dump_screen("extras page (buzzer / backlight / trace cfg / settings / back)");

    /* extras 现在是 5 项：蜂鸣器测试、背光亮度、巡线设置、系统设置、返回。
       先下去看看新增的"巡线设置"页。 */
    press(KEY_2, "KEY_2");
    press(KEY_2, "KEY_2");              /* -> 巡线设置 */
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 3u, "entered the trace-settings page (level 3)");
    dump_screen("trace settings page (ring / laps / speed / finish-stop / back)");
    press(KEY_4, "KEY_4");
    expect(Menu_Depth() == 2u, "back on the extras page");

    /* 再往下走到"系统设置" */
    press(KEY_2, "KEY_2");              /* -> 系统设置 */
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 3u, "entered the settings page (level 3)");
    dump_screen("settings page, level 3");

    printf("\n--- 10. KEY_4 walks back up, KEY_4_LONG jumps to the main menu ---\n");
    press(KEY_4, "KEY_4");
    expect(Menu_Depth() == 2u, "KEY_4 went back to the extras page");
    press(KEY_4_LONG, "KEY_4_LONG");
    expect(Menu_Depth() == 1u, "KEY_4_LONG jumped straight to the main menu");
    dump_screen("back on the main menu");

    printf("\n--- 11. every screen keeps exactly one highlighted row -----------\n");
    for (row = 0; row < 6; row++) {
        int found;
        Menu_Draw();
        found = highlight_row();
        if (found < 0) {
            expect(0, "exactly one row highlighted");
            break;
        }
        Menu_HandleKey(KEY_2);
    }
    expect(highlight_row() >= 0, "a single row is always highlighted");

    printf("\n--- 12. scrolling keeps the arrow inside the window --------------\n");
    press(KEY_4_LONG, "KEY_4_LONG");
    for (row = 0; row < 10; row++) {
        Menu_HandleKey(KEY_2);
        Menu_Draw();
        if ((highlight_row() < 0) || (highlight_row() > 3)) {
            break;
        }
    }
    expect(row == 10, "10 x KEY_2 kept the arrow on screen the whole time");

    printf("\n--- 13. requirement 3a: name, id and a = 0 after power on ------\n");
    fake_led_reset();
    App_Init();                 /* 模拟一次重新上电 */
    fake_serial_tx_reset();
    Menu_Draw();
    expect(App_GetVarA() == 0, "a is 0 straight after power on");
    expect(Menu_Depth() == 1u, "main menu is showing");

    press(KEY_2, "KEY_2");      /* 光标 0 -> 1，也就是 information 那一项 */
    press(KEY_3, "KEY_3");
    expect(Menu_Depth() == 2u, "entered the information page");
    dump_screen("information page (Yang Shouqin / 32602536 / a = 0)");

    printf("\n--- 14. requirement 3b: the host writes a ----------------------\n");
    fake_serial_feed("111\r\n");
    App_Tick();
    expect(App_GetVarA() == 111, "sending 111 sets a to 111");
    Menu_Draw();
    dump_screen("a = 111");

    /* 不带回车换行的裸数字也必须能被接收 */
    fake_serial_feed("222");
    App_Tick();
    expect(App_GetVarA() == 111, "a number with no ending is not taken yet");
    Delay_Ms(200u);
    App_Tick();
    expect(App_GetVarA() == 222, "a bare number lands once the host goes quiet");

    fake_serial_feed("-25\r\n");
    App_Tick();
    expect(App_GetVarA() == -25, "negative values are accepted");

    fake_serial_feed("abc\r\n");
    App_Tick();
    expect(App_GetVarA() == -25, "an unparseable line leaves a untouched");

    fake_serial_feed("12ab\r\n");
    App_Tick();
    expect(App_GetVarA() == -25, "trailing junk is rejected too");

    fake_serial_feed("0\r\n");
    App_Tick();
    expect(App_GetVarA() == 0, "a can be set back to 0");

    printf("\n--- 15. requirement 3c: the host reads a back with GET A ------\n");
    {
        /* 评分标准要求分别验证 0、一个正数和一个负数 */
        static const int32_t probe[3] = { 0, 111, -4269 };
        int i;

        for (i = 0; i < 3; i++) {
            char cmd[24];
            char want[24];
            char what[64];

            sprintf(cmd, "%ld\r\n", (long)probe[i]);
            fake_serial_feed(cmd);
            App_Tick();
            sprintf(what, "wrote %ld and the board stored it", (long)probe[i]);
            expect(App_GetVarA() == probe[i], what);

            fake_serial_tx_reset();
            fake_serial_feed("GET A");
            App_Tick();

            sprintf(want, "%ld\r\n", (long)probe[i]);
            sprintf(what, "GET A answered %s", want);
            expect(strcmp(fake_serial_tx(), want) == 0, what);
            printf("      a = %-6ld  host sent 'GET A'  ->  board replied '%s'",
                   (long)probe[i], fake_serial_tx());
        }

        /* 页面上必须显示最后写入的那个值 */
        Menu_Draw();
        dump_screen("information page after the host round trip");
    }

    printf("\n=================================================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);

    return (g_failures != 0) ? 1 : 0;
}
