/*
 * key.c - 四个菜单按键的消抖扫描
 *
 * Key_Scan() 每 KEY_SCAN_MS 毫秒采样一次引脚，只有连续 KEY_DEBOUNCE 次采样
 * 结果都一样，才承认电平真的变了。
 * 短按只在按下的那一刻上报一次。KEY_4 另外在按住超过 KEY_LONG_MS 之后上报
 * 一次 KEY_4_LONG。
 */

#include "key.h"
#include "board.h"

/* 这几个参数为什么取这些值：
 *   KEY_SCAN_MS   5  —— 5 ms 扫一次（200 Hz），跟手足够，占用主循环的时间可忽略
 *   KEY_DEBOUNCE  2  —— 连续 2 次一样 = 稳定 10 ms，足够滤掉机械触点的弹跳
 *   KEY_LONG_MS 800  —— 0.8 秒算长按：比手抖误触长得多，又不至于让人等太久 */
#define KEY_COUNT       4u
#define KEY_SCAN_MS     5u
#define KEY_DEBOUNCE    2u
#define KEY_LONG_MS     800u

/* ------------------------------------------------------------------ */
/* 接线 (wiring)                                                       */
/*                                                                     */
/* 这张表是按键引脚在整个工程里唯一出现的地方。想把某个键挪到别的引脚，    */
/* 只改这里就行：Key_Init() 会把新引脚配成带内部上拉的输入，              */
/* Key_Scan() 会去读它。引脚分属不同 port 也没关系。                      */
/*                                                                     */
/* 每个按键都是把引脚接到 GND，所以"按下"读到的就是 0。                   */
/* ------------------------------------------------------------------ */

typedef struct {
    GPIO_TypeDef *port;
    uint8_t       pin;
} KeyPin;

static const KeyPin s_keys[KEY_COUNT] = {
    { GPIOA, 0u },      /* KEY_1 - 光标上移               */
    { GPIOA, 3u },      /* KEY_2 - 光标下移               */
    { GPIOA, 5u },      /* KEY_3 - 进入子菜单             */
    { GPIOB, 0u }       /* KEY_4 - 返回 / 长按            */
};

static uint8_t  s_stable;                       /* 第 i 位置 1 = 按键 i 处于按下状态 */
/* 每个按键"连续读到相同电平"的计数，用来消抖；电平一变就清零 */
static uint8_t  s_count[KEY_COUNT];
/* 每个按键按下的时刻 (Tick_Ms)，判断长按要用 */
static uint32_t s_pressMs[KEY_COUNT];
/* 长按事件是否已经上报过，防止一直按着就每 5 ms 报一次 */
static uint8_t  s_longSent[KEY_COUNT];
/* 上次扫描的时刻。用 Tick_Ms 做节流，让扫描周期稳定在 KEY_SCAN_MS */
static uint32_t s_lastScanMs;

void Key_Init(void)
{
    uint8_t i;

    /* 配成带内部上拉的输入：ODR 对应位 = 1 选上拉，= 0 选下拉。
       必须先跑过 Board_Init()，否则 GPIO 的时钟还没打开。 */
    for (i = 0u; i < KEY_COUNT; i++) {
        GPIO_ConfigPin(s_keys[i].port, s_keys[i].pin, GPIO_CFG_IN_PUPD);
        /* BSRR 写 1 会把 ODR 对应位置 1，也就是选中"上拉"而不是"下拉" */
        s_keys[i].port->BSRR = BIT(s_keys[i].pin);
    }

    s_stable = 0u;
    s_lastScanMs = 0u;

    for (i = 0u; i < KEY_COUNT; i++) {
        s_count[i] = 0u;
        s_pressMs[i] = 0u;
        s_longSent[i] = 0u;
    }
}

uint8_t Key_IsDown(uint8_t index)
{
    if (index >= KEY_COUNT) {
        return 0u;
    }
    return (uint8_t)((s_stable >> index) & 1u);
}

/* 一次调用最多返回一个事件。这样主循环的状态机每一步只处理一件事，"按下"和
   紧随其后的"长按"也不会在同一帧里互相干扰。 */
KeyEvent Key_Scan(void)
{
    uint32_t now = Tick_Ms();
    uint8_t raw = 0u;
    uint8_t i;
    KeyEvent event = KEY_NONE;

    /* 用无符号减法算时间差：即使 Tick_Ms() 将来回绕，算出来的差仍然是对的 */
    if ((uint32_t)(now - s_lastScanMs) < KEY_SCAN_MS) {
        return KEY_NONE;
    }
    s_lastScanMs = now;

    /* 低电平有效的输入：读到 0 才算按下，所以这里取反 —— raw 里置 1 表示按下 */
    for (i = 0u; i < KEY_COUNT; i++) {
        if ((s_keys[i].port->IDR & BIT(s_keys[i].pin)) == 0u) {
            raw |= (uint8_t)BIT(i);
        }
    }

    for (i = 0u; i < KEY_COUNT; i++) {
        uint8_t mask = (uint8_t)BIT(i);

        if ((raw & mask) != 0u) {
            /* 这一次读到"按下"，稳定计数往上加，最多加到 KEY_DEBOUNCE 就停 */
            if (s_count[i] < KEY_DEBOUNCE) {
                s_count[i]++;
            }

            /* 计数够了，而且之前记的是"松开"：这算一次新的按下，只在按下的沿上报一次 */
            if ((s_count[i] >= KEY_DEBOUNCE) && ((s_stable & mask) == 0u)) {
                s_stable |= mask;
                s_pressMs[i] = now;
                s_longSent[i] = 0u;
                if (event == KEY_NONE) {
                    event = (KeyEvent)((int)KEY_1 + (int)i);
                }
            }

            /* 还按着、且已经按够 KEY_LONG_MS、且这个长按还没报过，就报一次；
               s_longSent 就是用来封住重复上报的，否则会每 5 ms 报一次长按 */
            if (((s_stable & mask) != 0u) && (s_longSent[i] == 0u)
                && ((uint32_t)(now - s_pressMs[i]) >= KEY_LONG_MS)) {
                if (event == KEY_NONE) {
                    s_longSent[i] = 1u;
                    if (i == 3u) {
                        event = KEY_4_LONG;
                    }
                }
            }
        } else {
            /* 读到松开：消抖计数清零，稳定状态也回到松开，长按标志复位好迎接下一次 */
            s_count[i] = 0u;
            s_stable &= (uint8_t)~mask;
            s_longSent[i] = 0u;
        }
    }

    return event;
}
