/*
 * app.c - 菜单树以及每个菜单项背后的功能实现
 *
 * 菜单结构（上电后看到的是第 1 级）：
 *
 *   main（主菜单）
 *    |- LED control（LED 控制）-> LED 开关 / 闪烁模式 / 返回
 *    |- Information（信息）    -> 芯片、时钟、运行时间、返回
 *    |- Line follow（循迹）     -> 启动 / 停止 / 状态、返回
 *    |- Extras（扩展功能）      -> 蜂鸣器测试 / 背光 / 设置 / 返回
 *    |                           '- 设置 -> 版本 / 恢复默认值 / 返回
 *    '- About（关于）           -> 芯片型号 / 演示 / 返回
 *
 * 所有要显示的字符串都以“Unicode 码点数组”的形式写在这个文件里。
 * CN_* 这些宏来自 oled_font.h，代表字库位图表里的那一个个汉字。
 * 之所以把字符串写成数字而不是直接写中文，是为了让本文件保持纯 ASCII：
 * 这样不管编辑器和编译器把源文件当成 UTF-8 还是 GBK，编译结果都一样，
 * 不会出现“在我机器上显示正常、换个环境就变乱码”的问题。
 */

#include "app.h"
#include "board.h"
#include "oled.h"
#include "oled_font.h"
#include "menu.h"
#include "serial.h"

/* ------------------------------------------------------------------ */
/* 你自己的信息，会显示在 Information（信息）页面上                      */
/* ------------------------------------------------------------------ */

/* 拼音姓名和学号，换人的话只改下面这两行就够了。
   注意第 0 行整行只有 128 像素宽，ASCII 字符是 8 像素一个，
   所以拼音姓名最多只能放 12 个字符，再长就会被截掉。 */
static const uint16_t T_MY_NAME[]    = { 'Y','a','n','g',' ','S','h','o','u','q','i','n', 0 };
static const uint16_t T_MY_ID[]      = { '3','2','6','0','2','5','3','6', 0 };             /* 学号 */

/* ------------------------------------------------------------------ */
/* 字符串表（全部是码点数组，末尾的 0 相当于字符串结束符）              */
/* ------------------------------------------------------------------ */

/* 主菜单各项的文字 */
static const uint16_t T_LED[]        = { 'L', 'E', 'D', CN_KONG, CN_ZHI, 0 };
static const uint16_t T_INFO[]       = { CN_XIN, CN_XI, CN_XIAN, CN_SHI, 0 };
static const uint16_t T_TRACE[]      = { CN_XUN, CN_XIAN2, CN_GONG, CN_NENG, 0 };
static const uint16_t T_EXTRA[]      = { CN_TUO, CN_ZHAN, CN_GONG, CN_NENG, 0 };
static const uint16_t T_ABOUT[]      = { CN_GUAN, CN_YU, CN_BEN, CN_JI, 0 };

/* 各个子菜单共用的“返回” */
static const uint16_t T_BACK[]       = { CN_FAN, CN_HUI, 0 };

/* LED 控制页 */
static const uint16_t T_LED_ROW1[]   = { 'L', 'E', 'D', '1', ':', 0 };
static const uint16_t T_LED_ROW2[]   = { 'L', 'E', 'D', '2', ':', 0 };
static const uint16_t T_MODE[]       = { CN_MO, CN_SHI2, ':', 0 };
static const uint16_t T_IS_ON[]      = { CN_LIANG, 0 };
static const uint16_t T_IS_OFF[]     = { CN_MIE, 0 };
static const uint16_t T_IS_ALT[]     = { CN_JIAO, CN_TI, 0 };
static const uint16_t T_HINT_BACK[]  = { CN_AN, CN_JIAN2, '4', CN_FAN, CN_HUI, 0 };

/* Information 信息页 */
static const uint16_t T_LBL_NAME[]   = { CN_XING2, CN_MING2, 0 };   /* 姓名 */
static const uint16_t T_LBL_ID[]     = { CN_XUE, CN_HAO, 0 };       /* 学号 */
static const uint16_t T_LBL_A[]      = { 'a', ' ', '=', ' ', 0 };

/* 系统信息（时钟、运行时间） */
static const uint16_t T_CLOCK[]      = { CN_ZHU, CN_PIN, 0 };
static const uint16_t T_UPTIME[]     = { CN_YUN, CN_XING, 0 };

/* 循迹页面 */
static const uint16_t T_TRACE_GO[]   = { CN_QI, CN_DONG, CN_XUN, CN_XIAN2, 0 };
static const uint16_t T_TRACE_STOP[] = { CN_TING, CN_ZHI2, CN_XUN, CN_XIAN2, 0 };
static const uint16_t T_STATE[]      = { CN_ZHUANG, CN_TAI, 0 };

/* 扩展功能页 */
static const uint16_t T_BUZZER[]     = { CN_FENG, CN_MING, CN_QI2, CN_CE, CN_SHI4, 0 };
static const uint16_t T_BACKLIGHT[]  = { CN_BEI, CN_GUANG, CN_LIANG, CN_DU, 0 };
static const uint16_t T_SETTINGS[]   = { CN_XI2, CN_TONG, CN_SHE, CN_ZHI3, 0 };

/* 设置页 */
static const uint16_t T_VERSION[]    = { CN_BAN, CN_BEN, CN_XIN, CN_XI, 0 };
static const uint16_t T_RESTORE[]    = { CN_HUI2, CN_FU, CN_MO2, CN_REN, 0 };

/* 关于页 */
static const uint16_t T_CHIP_NAME[]  = { 'S','T','M','3','2','F','1','0','3','C','8', 0 };

/* 直接当作“值”来显示的字符串 */
static const uint16_t V_RUN[]        = { 'R', 'U', 'N', 0 };
static const uint16_t V_STOP[]       = { 'S', 'T', 'O', 'P', 0 };
static const uint16_t V_VERSION[]    = { 'V', '1', '.', '0', 0 };

/* ------------------------------------------------------------------ */
/* 全局状态（都是本文件私有的 static，外部只能通过 App_* 接口访问）    */
/* ------------------------------------------------------------------ */

#define BRIGHTNESS_LEVELS   4u
#define DEFAULT_BRIGHTNESS  3u

/* 交替闪烁时每个灯保持亮的时长。取 500 ms 就是每秒完整交换一次：
   肉眼看着足够明显，而 OLED（每次变化都会刷新）也完全来得及在
   1 秒的节奏里跟上报数，不会出现画面和灯不一致的情况。 */
#define ALT_HALF_MS         500u

static const uint8_t s_contrast[BRIGHTNESS_LEVELS] = {
    OLED_CONTRAST_DIM, OLED_CONTRAST_LOW, OLED_CONTRAST_MID, OLED_CONTRAST_HIGH
};

/* ---- 两个 LED ---- */

typedef enum {
    LED_MODE_OFF = 0,       /* 两个灯都灭                  */
    LED_MODE_ON,            /* 两个灯都常亮                */
    LED_MODE_ALT            /* 交替闪烁，先从 LED1 亮起     */
} LedMode;

static LedMode  s_ledMode;
static uint32_t s_altBase;      /* 交替闪烁的计时起点（tick）             */
static uint8_t  s_led1On;       /* 实际驱动出去的电平，存下来是为了       */
static uint8_t  s_led2On;       /* 保证屏幕显示的和灯的真实状态永远一致   */

static uint8_t  s_brightness;
static uint8_t  s_traceRun;
static uint8_t  s_buzzerCount;

/* ---- 上位机可以读写的变量 a（题目要求 3） ---- */

static int32_t  s_varA;             /* 初值 0，有符号                     */

#define LINK_LINE_MAX   24u         /* 上位机一行最长接受多少字符        */
#define LINK_IDLE_MS    60u         /* 上位机没发换行就静默了，多久之后  */
                                    /* 就把这半行当成完整一行处理        */

static char     s_line[LINK_LINE_MAX];
static uint8_t  s_lineLen;
static uint32_t s_lineLastMs;

/* 每个会变化的“值”各用一个缓冲区。虽然 Menu_Draw() 拿到指针后会马上把内容
   拷走，但缓冲区还是分开更保险：否则一个页面里同时显示两个值（比如 About 页
   的时钟和运行时间）时，后一个 getter 就会把前一个的内容覆盖掉 */
static uint16_t s_bufClock[12];
static uint16_t s_bufUptime[12];
static uint16_t s_bufBright[8];
static uint16_t s_bufBuzzer[8];

/* ------------------------------------------------------------------ */
/* 会动的数值：把数字格式化成字符串，返回指向内部缓冲区的指针          */
/* ------------------------------------------------------------------ */

/* 把 value 的十进制数字追加到 dst[pos] 后面，返回新的写入位置。
   返回值特意做成 uint8_t：这些缓冲区最长也就十几个字符，不会超过 255，
   用 uint8_t 当游标既省内存，也省得每次再强转一遍 */
static uint8_t AppendU32(uint16_t *dst, uint8_t pos, uint32_t value)
{
    return (uint8_t)(pos + OLED_FormatU32(&dst[pos], value));
}

static const uint16_t *Val_TraceState(void)
{
    return (s_traceRun != 0u) ? V_RUN : V_STOP;
}

static const uint16_t *Val_Clock(void)
{
    uint8_t p = AppendU32(s_bufClock, 0u, Board_ClockHz() / 1000000u);

    s_bufClock[p] = (uint16_t)'M'; p++;
    s_bufClock[p] = (uint16_t)'H'; p++;
    s_bufClock[p] = (uint16_t)'z'; p++;
    s_bufClock[p] = 0u;

    return s_bufClock;
}

static const uint16_t *Val_Uptime(void)
{
    uint32_t tenths = Tick_Ms() / 100u;
    uint8_t p = AppendU32(s_bufUptime, 0u, tenths / 10u);

    s_bufUptime[p] = (uint16_t)'.'; p++;
    s_bufUptime[p] = (uint16_t)('0' + (tenths % 10u)); p++;
    s_bufUptime[p] = (uint16_t)'s'; p++;
    s_bufUptime[p] = 0u;

    return s_bufUptime;
}

static const uint16_t *Val_Brightness(void)
{
    uint8_t p = AppendU32(s_bufBright, 0u, (uint32_t)s_brightness);

    s_bufBright[p] = 0u;

    return s_bufBright;
}

static const uint16_t *Val_Buzzer(void)
{
    uint8_t p = 0u;

    s_bufBuzzer[p] = (uint16_t)'x'; p++;
    p = AppendU32(s_bufBuzzer, p, (uint32_t)s_buzzerCount);
    s_bufBuzzer[p] = 0u;

    return s_bufBuzzer;
}

/* 版本号是固定字符串，不需要缓冲区，直接返回常量表就行 */
static const uint16_t *Val_Version(void)
{
    return V_VERSION;
}

/* ------------------------------------------------------------------ */
/* 上位机串口协议：变量 a 可以通过串口写，也可以读回来                 */
/*                                                                     */
/*   上位机发   111        -> a = 111                                  */
/*   上位机发   -25        -> a = -25      （负数也支持）              */
/*   上位机发   GET A      -> 板子把当前值回发过去，                    */
/*                            例如 "111\r\n"                          */
/*                                                                     */
/* 一行的结束可以是 CR、LF，也可以只是上位机停顿了一下（静默超时）。   */
/* 所以 "111\r\n" 和光秃秃一个 "111" 都能被正确识别。                 */
/* ------------------------------------------------------------------ */

/* 把有符号十进制数格式化成字符串。故意不写 -value，因为在 INT32_MIN
   这个值上取负会溢出，所以下面用 -(value + 1) 再 +1 的写法绕开 */
static uint8_t FormatI32(uint16_t *dst, int32_t value)
{
    uint8_t  n = 0u;
    uint32_t mag;

    if (value < 0) {
        dst[n] = (uint16_t)'-';
        n++;
        mag = (uint32_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint32_t)value;
    }

    n = (uint8_t)(n + OLED_FormatU32(&dst[n], mag));

    return n;
}

/* 只有 "GET A"、"get a"、"GETA"、"GET  A" 这类写法才返回真：
   比较时忽略空格和制表符，并且统一转成大写，其它内容一律不认 */
static uint8_t Link_IsGetCommand(const char *line)
{
    static const char cmd[] = "GETA";
    uint8_t i = 0u;

    while (*line != '\0') {
        char c = *line;

        line++;

        if ((c == ' ') || (c == '\t')) {
            continue;
        }
        if ((c >= 'a') && (c <= 'z')) {
            c = (char)(c - 'a' + 'A');
        }
        if ((i >= 4u) || (c != cmd[i])) {
            return 0u;
        }

        i++;
    }

    return (uint8_t)(i == 4u);
}

/* 整行必须是一个合法的有符号十进制数，否则拒绝。
   注意前后允许有空格，但除了空格之外不允许出现别的字符，
   这样 "111abc"、"1 2 3" 这类输入就不会被误当成 111 或 123 */
static uint8_t Link_ParseI32(const char *line, int32_t *out)
{
    int32_t  sign = 1;
    uint32_t mag = 0u;
    uint8_t  digits = 0u;

    while ((*line == ' ') || (*line == '\t')) {
        line++;
    }

    if (*line == '-') {
        sign = -1;
        line++;
    } else if (*line == '+') {
        line++;
    }

    while ((*line >= '0') && (*line <= '9')) {
        if (mag > 214748364u) {
            mag = 2147483647u;              /* 超范围就卡在最大值，不回绕 */
        } else {
            mag = (mag * 10u) + (uint32_t)(*line - '0');
            if (mag > 2147483647u) {
                mag = 2147483647u;
            }
        }
        digits++;
        line++;
    }

    while ((*line == ' ') || (*line == '\t')) {
        line++;
    }

    if ((digits == 0u) || (*line != '\0')) {
        return 0u;
    }

    *out = (sign < 0) ? -(int32_t)mag : (int32_t)mag;

    return 1u;
}

/* 把变量 a 的当前值回发给上位机。末尾的 CR LF 是给串口助手分行用的，
   手工在窗口里看更清楚，用脚本读的时候也正好按行读 */
static void Link_SendValue(void)
{
    uint16_t buf[16];
    uint8_t  n = FormatI32(buf, s_varA);
    uint8_t  i;

    for (i = 0u; i < n; i++) {
        Serial_WriteByte((uint8_t)buf[i]);
    }

    Serial_WriteByte((uint8_t)'\r');
    Serial_WriteByte((uint8_t)'\n');
}

static void Link_ProcessLine(void)
{
    int32_t value;

    s_line[s_lineLen] = '\0';

    if (s_lineLen != 0u) {
        if (Link_ParseI32(s_line, &value) != 0u) {
            if (value != s_varA) {
                s_varA = value;
                Menu_Invalidate();      /* 让页面马上重画一次             */
            }
        }
        /* 解析失败的输入直接丢掉：宁可什么都不做，也不能把 a 改成乱值 */
    }

    s_lineLen = 0u;
}

static void Link_FeedChar(char ch)
{
    if ((ch == '\r') || (ch == '\n')) {
        Link_ProcessLine();
        s_lineLastMs = Tick_Ms();
        return;
    }

    if (s_lineLen < (LINK_LINE_MAX - 1u)) {
        s_line[s_lineLen] = ch;
        s_lineLen++;
        s_line[s_lineLen] = '\0';

        /* "GET A" 在收到 A 的那一刻就已经完整了，所以这里立刻就回，
           不必再等上位机发换行——很多串口助手不勾“发送新行” */
        if (Link_IsGetCommand(s_line) != 0u) {
            Link_SendValue();
            s_lineLen = 0u;
        }
    } else {
        s_lineLen = 0u;                 /* 这一行太长了：丢掉重新同步 */
    }

    s_lineLastMs = Tick_Ms();
}

/* 轮询式收数据：每轮主循环问一次串口有没有新字节。
   这么做的好处是不用进中断、不用管 ISR 和主循环的共享变量，
   24 字节的行缓冲区也不会被两边同时动到 */
static void Link_Poll(void)
{
    uint8_t ch;

    while (Serial_ReadByte(&ch) != 0u) {
        Link_FeedChar((char)ch);
    }

    /* 上位机只发了 "111" 而没发换行时，这一行也必须生效，
       所以在它静默了一小会儿之后，这里补一次收尾处理 */
    if ((s_lineLen != 0u)
        && ((uint32_t)(Tick_Ms() - s_lineLastMs) >= LINK_IDLE_MS)) {
        Link_ProcessLine();
    }
}

/* ------------------------------------------------------------------ */
/* Information 信息页（题目要求 3a）                                    */
/*                                                                     */
/*   第 0 行 : 姓名Yang Shouqin                                        */
/*   第 1 行 : 学号32602536                                            */
/*   第 2 行 : a = 111                                                 */
/*   第 3 行 : 按键4返回                                                */
/*                                                                     */
/* 标签和值都是紧挨着画出来的，因为拼音姓名正好占满整行，              */
/* 如果改成右对齐，名字和右边的值就会叠在一起看不清。                  */
/* ------------------------------------------------------------------ */

static void InfoPage_Draw(void)
{
    uint16_t buf[16];
    uint8_t  n;
    uint8_t  x;

    x = OLED_TextWidth(T_LBL_NAME);
    OLED_DrawText(0u, 0u, T_LBL_NAME);
    OLED_DrawText(x, 0u, T_MY_NAME);

    x = OLED_TextWidth(T_LBL_ID);
    OLED_DrawText(0u, 2u, T_LBL_ID);
    OLED_DrawText(x, 2u, T_MY_ID);

    x = OLED_TextWidth(T_LBL_A);
    OLED_DrawText(0u, 4u, T_LBL_A);
    n = FormatI32(buf, s_varA);
    buf[n] = 0u;
    OLED_DrawText(x, 4u, buf);

    OLED_DrawText(0u, 6u, T_HINT_BACK);
}

static void InfoPage_Key(KeyEvent event)
{
    if ((event == KEY_4) || (event == KEY_4_LONG)) {
        Menu_Back();
    }
    /* 这一页是只读的，除了 KEY_4 返回之外按键都不做任何事 */
}

/* 第三个字段是“离开页面”的回调：信息页没有什么需要收尾的，
   所以填 0（空指针），menu.c 见到 0 就不调用 */
static const MenuPageOps kInfoPageOps = {
    InfoPage_Key,
    InfoPage_Draw,
    0
};

/* ------------------------------------------------------------------ */
/* 各个菜单项的“动作”函数                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * LED 控制页
 *
 * 这一页是“控制面板”，不是普通的菜单列表，所以它自己带一套按键处理和
 * 绘制函数（对应 menu.h 里的 MenuPageOps），完全不走菜单项那一套：
 *
 *   KEY_1 : 两个灯常亮  <->  两个灯全灭
 *   KEY_2 : 交替闪烁
 *   KEY_4 : 退回主菜单，同时强制两个灯都灭
 *
 * 不管用哪种方式离开这一页，都会走到 LedPage_Leave()，所以“回到主菜单时
 * LED 必须是灭的”这条要求，即使用户是长按 KEY_4 溜出去的也一样成立。
 * ------------------------------------------------------------------ */

/* 真正把两个灯的电平写出去，同时把电平记下来。
   只有真的发生变化时才把画面置脏，避免无意义的重画；而把电平存在这里，
   屏幕上显示的状态就永远不可能和灯的实际状态对不上。 */
static void Led_Apply(uint8_t led1On, uint8_t led2On)
{
    if ((led1On == s_led1On) && (led2On == s_led2On)) {
        return;
    }

    s_led1On = led1On;
    s_led2On = led2On;
    Board_LedWrite(led1On, led2On);

    /* 立刻重画，让屏幕在一帧之内就跟着灯变，而不是干等那个慢吞吞的
       周期性刷新，否则按一下键要过一会儿屏幕才更新 */
    Menu_Invalidate();
}

static void Led_SetMode(LedMode mode)
{
    s_ledMode = mode;
    s_altBase = Tick_Ms();

    switch (mode) {
    case LED_MODE_ON:
        Led_Apply(1u, 1u);
        break;
    case LED_MODE_ALT:
        Led_Apply(1u, 0u);      /* 交替时一律从 LED1 亮开始     */
        break;
    case LED_MODE_OFF:
    default:
        Led_Apply(0u, 0u);
        break;
    }
}

static void LedPage_Key(KeyEvent event)
{
    switch (event) {
    case KEY_1:
        /* 对应要求 a：灭 -> 亮 -> 灭 …… 反复切换 */
        Led_SetMode((s_ledMode == LED_MODE_ON) ? LED_MODE_OFF : LED_MODE_ON);
        break;

    case KEY_2:
        /* 对应要求 b：交替闪烁 */
        Led_SetMode(LED_MODE_ALT);
        break;

    case KEY_4:
    case KEY_4_LONG:
        /* 对应要求 c：离开页面时的回调会把两个灯都关掉 */
        Menu_Back();
        break;

    default:
        /* KEY_3 在这一页没有用途，按了也不理 */
        break;
    }
}

static void LedPage_Draw(void)
{
    const uint16_t *mode;

    OLED_DrawText(8u, 0u, T_LED_ROW1);
    OLED_DrawTextRight(126u, 0u, (s_led1On != 0u) ? T_IS_ON : T_IS_OFF);

    OLED_DrawText(8u, 2u, T_LED_ROW2);
    OLED_DrawTextRight(126u, 2u, (s_led2On != 0u) ? T_IS_ON : T_IS_OFF);

    if (s_ledMode == LED_MODE_ALT) {
        mode = T_IS_ALT;
    } else {
        mode = (s_ledMode == LED_MODE_ON) ? T_IS_ON : T_IS_OFF;
    }
    OLED_DrawText(8u, 4u, T_MODE);
    OLED_DrawTextRight(126u, 4u, mode);

    OLED_DrawText(8u, 6u, T_HINT_BACK);
}

/* 离开这一页时的钩子。MenuPageOps 的第三个字段就是它：正常返回、长按 KEY_4
   跳回主菜单，走的都是这里，所以两个灯一定会被关掉 */
static void LedPage_Leave(void)
{
    Led_SetMode(LED_MODE_OFF);
}

static const MenuPageOps kLedPageOps = {
    LedPage_Key,
    LedPage_Draw,
    LedPage_Leave
};

static void Act_TraceStart(void)
{
    s_traceRun = 1u;
}

static void Act_TraceStop(void)
{
    s_traceRun = 0u;
}

static void Act_Buzzer(void)
{
    /* 底板上没有接蜂鸣器，所以这里只是把“被触发过几次”记下来。
       等蜂鸣器接上去之后，把这个计数换成真正的驱动代码即可 */
    s_buzzerCount++;
}

/* 每按一次背光就在 4 个档位之间循环：加 1 之后对档位总数取模，
   转一圈回到最暗，不用写 if 判断到头没有 */
static void Act_Brightness(void)
{
    s_brightness = (uint8_t)((s_brightness + 1u) % BRIGHTNESS_LEVELS);
    OLED_SetContrast(s_contrast[s_brightness]);
}

/* 把能改的东西都打回上电时的样子；LED 和背光也要跟着立刻生效，
   不能只改变量等着下次重画 */
static void Act_RestoreDefaults(void)
{
    s_traceRun = 0u;
    s_buzzerCount = 0u;
    s_brightness = DEFAULT_BRIGHTNESS;

    Led_SetMode(LED_MODE_OFF);
    OLED_SetContrast(s_contrast[s_brightness]);
}

/* ------------------------------------------------------------------ */
/* 菜单树：MenuItem 四项依次是 文字 / 子菜单指针 / 动作函数 / 取值函数   */
/* ------------------------------------------------------------------ */

static const MenuItem kSettingsItems[] = {
    { T_VERSION, 0, 0,                   Val_Version },
    { T_RESTORE, 0, Act_RestoreDefaults, 0           },
    { T_BACK,    0, Menu_Back,           0           }
};
static const MenuPage kSettingsPage = { kSettingsItems, 3u, 0 };

static const MenuItem kExtraItems[] = {
    { T_BUZZER,    0,              Act_Buzzer,       Val_Buzzer     },
    { T_BACKLIGHT, 0,              Act_Brightness,   Val_Brightness },
    { T_SETTINGS,  &kSettingsPage, 0,                0              },
    { T_BACK,      0,              Menu_Back,        0              }
};
static const MenuPage kExtraPage = { kExtraItems, 4u, 0 };

static const MenuItem kLedItems[] = {
    { 0, 0, 0, 0 }              /* 用不到：这一页是控制面板，不走菜单项 */
};
static const MenuPage kLedPage = { kLedItems, 0u, &kLedPageOps };

static const MenuItem kInfoItems[] = {
    { 0, 0, 0, 0 }              /* 用不到：这一页是控制面板，不走菜单项 */
};
static const MenuPage kInfoPage = { kInfoItems, 0u, &kInfoPageOps };

static const MenuItem kTraceItems[] = {
    { T_TRACE_GO,   0, Act_TraceStart, 0              },
    { T_TRACE_STOP, 0, Act_TraceStop,  0              },
    { T_STATE,      0, 0,              Val_TraceState },
    { T_BACK,       0, Menu_Back,      0              }
};
static const MenuPage kTracePage = { kTraceItems, 4u, 0 };

static const MenuItem kAboutItems[] = {
    { T_CHIP_NAME, 0, 0,         0          },
    { T_CLOCK,     0, 0,         Val_Clock  },
    { T_UPTIME,    0, 0,         Val_Uptime },
    { T_BACK,      0, Menu_Back, 0          }
};
static const MenuPage kAboutPage = { kAboutItems, 4u, 0 };

static const MenuItem kMainItems[] = {
    { T_LED,   &kLedPage,   0, 0 },
    { T_INFO,  &kInfoPage,  0, 0 },
    { T_TRACE, &kTracePage, 0, 0 },
    { T_EXTRA, &kExtraPage, 0, 0 },
    { T_ABOUT, &kAboutPage, 0, 0 }
};
static const MenuPage kMainPage = { kMainItems, 5u, 0 };

/* ------------------------------------------------------------------ */
/* 对外接口（给 main.c 调用）                                          */
/* ------------------------------------------------------------------ */

void App_Init(void)
{
    s_traceRun = 0u;
    s_buzzerCount = 0u;
    s_brightness = DEFAULT_BRIGHTNESS;

    /* 对应要求 3a：上电后 a 从 0 开始 */
    s_varA = 0;
    s_lineLen = 0u;
    s_lineLastMs = 0u;
    s_line[0] = '\0';

    /* 两个灯全灭，模式记为“常灭” */
    s_led1On = 0u;
    s_led2On = 0u;
    s_ledMode = LED_MODE_OFF;
    s_altBase = 0u;
    Board_LedWrite(0u, 0u);

    OLED_SetContrast(s_contrast[s_brightness]);

    /* 上电就停在这一页：主菜单，箭头在第一行 */
    Menu_Init(&kMainPage);
}

int32_t App_GetVarA(void)
{
    return s_varA;
}

void App_Tick(void)
{
    /* 不管当前在哪一页，每一轮都要把上位机串口来的数据处理掉 */
    Link_Poll();

    /* 交替闪烁完全由毫秒 tick 推算出来，所以既不会累积误差、也不会停：
       每次进来只是重新算一遍“现在该轮到哪个灯亮”。
       另外 Led_Apply() 是用一次 BSRR 写入同时更新两个灯的，两个灯在同一个
       总线周期里完成交换，中间不会出现两个都亮或者两个都灭的一瞬间 */
    if (s_ledMode == LED_MODE_ALT) {
        uint8_t phase = (uint8_t)(((Tick_Ms() - s_altBase) / ALT_HALF_MS) & 1u);

        Led_Apply((uint8_t)(phase == 0u), phase);
    }
}
