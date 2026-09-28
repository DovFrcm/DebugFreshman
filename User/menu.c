/*
 * menu.c - 多级菜单引擎
 */

#include "menu.h"
#include "oled.h"

#define MENU_MAX_DEPTH  8u
#define MENU_TEXT_X     12u     /* 文字从箭头右边开始           */
#define MENU_VALUE_X    126u    /* 实时数值右对齐到这里         */
#define MENU_BAR_X      126u    /* 滚动条占用 126..127 列       */
#define MENU_BAR_W      2u

static const MenuPage *s_stack[MENU_MAX_DEPTH];
static uint8_t s_savedCursor[MENU_MAX_DEPTH];
static uint8_t s_depth;         /* 栈上的页面数，>= 1          */
static uint8_t s_cursor;        /* 当前页面里选中的行          */
static uint8_t s_top;           /* 窗口里第一个可见的行        */
static uint8_t s_dirty;

static const MenuPage *Menu_Current(void)
{
    return s_stack[s_depth - 1u];
}

static void Menu_ClampWindow(void);

static void Menu_ResetView(void)
{
    s_cursor = 0u;
    s_top = 0u;
    s_dirty = 1u;
}

/* 离开当前页面。带 ops 的页面会先跑它的 onLeave 钩子 -- LED 页就是靠这个
   保证自己退出时把灯全部关掉的。 */
static void Menu_PopOne(void)
{
    const MenuPage *page;

    /* 已经到了根页面，没人可退，直接忽略 */
    if (s_depth <= 1u) {
        return;
    }

    page = s_stack[s_depth - 1u];
    if ((page->ops != 0) && (page->ops->onLeave != 0)) {
        page->ops->onLeave();
    }

    s_depth--;
    s_cursor = s_savedCursor[s_depth];
    s_top = 0u;
    Menu_ClampWindow();
    s_dirty = 1u;
}

void Menu_Init(const MenuPage *root)
{
    s_stack[0] = root;
    s_savedCursor[0] = 0u;
    s_depth = 1u;

    /* 上电初始状态：箭头指向第一行 */
    Menu_ResetView();
}

void Menu_Invalidate(void)
{
    s_dirty = 1u;
}

void Menu_ClearDirty(void)
{
    s_dirty = 0u;
}

uint8_t Menu_NeedsRedraw(void)
{
    return s_dirty;
}

uint8_t Menu_Depth(void)
{
    return s_depth;
}

uint8_t Menu_PageHasValue(void)
{
    const MenuPage *page = Menu_Current();
    uint8_t i;

    /* 自定义页面自己负责绘制，而且很可能显示实时数据，所以除了每次变化
       时重绘，也得让定时器周期性地刷新它 */
    if (page->ops != 0) {
        return 1u;
    }

    /* 列表页则只要有一个条目带 getValue 回调，就得跟着定时器刷 */
    for (i = 0u; i < page->count; i++) {
        if (page->items[i].getValue != 0) {
            return 1u;
        }
    }

    return 0u;
}

/* 让可见窗口始终罩住光标 */
static void Menu_ClampWindow(void)
{
    uint8_t count = Menu_Current()->count;

    /* 光标跑到窗口上边或下边之外时，把窗口拉回来 */
    if (s_cursor < s_top) {
        s_top = s_cursor;
    }
    if (s_cursor >= (uint8_t)(s_top + MENU_ROWS)) {
        s_top = (uint8_t)(s_cursor - MENU_ROWS + 1u);
    }
    /* 条目快到底时不许窗口再往下滚，否则下面会露出一片空白 */
    if ((count > MENU_ROWS) && (s_top > (uint8_t)(count - MENU_ROWS))) {
        s_top = (uint8_t)(count - MENU_ROWS);
    }
}

void Menu_Back(void)
{
    Menu_PopOne();
}

static void Menu_Enter(const MenuPage *page)
{
    /* 栈满了就放弃，宁可不进也不能把静态数组写穿 */
    if (s_depth >= MENU_MAX_DEPTH) {
        return;
    }

    s_savedCursor[s_depth] = s_cursor;      /* 记住位置，返回时才能落回原处 */
    s_stack[s_depth] = page;
    s_depth++;
    Menu_ResetView();
}

void Menu_HandleKey(KeyEvent event)
{
    const MenuPage *page = Menu_Current();
    const MenuItem *item;

    /* 控制面板页面自己处理按键，引擎不再插手 */
    if ((page->ops != 0) && (page->ops->onKey != 0)) {
        page->ops->onKey(event);
        return;
    }

    if (page->count == 0u) {
        return;
    }

    switch (event) {
    case KEY_1:                             /* 光标上移 */
        /* 用取模式的绕回：在第一行再往上就跳到最后一行 */
        s_cursor = (s_cursor == 0u) ? (uint8_t)(page->count - 1u)
                                    : (uint8_t)(s_cursor - 1u);
        Menu_ClampWindow();
        s_dirty = 1u;
        break;

    case KEY_2:                             /* 光标下移 */
        s_cursor = (uint8_t)((s_cursor + 1u) % page->count);
        Menu_ClampWindow();
        s_dirty = 1u;
        break;

    case KEY_3:                             /* 进入 */
        item = &page->items[s_cursor];
        /* 有子页面就进去，是叶子行就执行动作；两样都没有就什么也不做 */
        if (item->child != 0) {
            Menu_Enter(item->child);
        } else if (item->onSelect != 0) {
            item->onSelect();
            s_dirty = 1u;
        }
        break;

    case KEY_4:                             /* 返回上一层 */
        Menu_Back();
        break;

    case KEY_4_LONG:                        /* 直接回主菜单 */
        /* 一层一层地正常出栈，而不是把 depth 直接清零，这样每一层的
           onLeave 钩子都有机会执行 */
        while (s_depth > 1u) {
            Menu_PopOne();
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 渲染                                                                */
/* ------------------------------------------------------------------ */

static void Menu_DrawArrow(uint8_t x, uint8_t y, uint8_t on)
{
    uint8_t i;
    uint8_t j;

    /* 画一个朝右的 5x9 三角形：第 i 列从第 i 行填到第 8-i 行，中间最宽 */
    for (i = 0u; i < 5u; i++) {
        for (j = 0u; j < 9u; j++) {
            if ((j >= i) && (j <= (uint8_t)(8u - i))) {
                OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + j), on);
            }
        }
    }
}

static void Menu_DrawScrollBar(uint8_t count)
{
    uint8_t slot;

    if (count <= MENU_ROWS) {
        return;
    }

    /* 每个可见行给一个槽位，滑块 (thumb) 就永远不会走出屏幕 */
    slot = (uint8_t)(((uint32_t)s_cursor * (uint32_t)MENU_ROWS) / (uint32_t)count);
    if (slot >= MENU_ROWS) {
        slot = (uint8_t)(MENU_ROWS - 1u);
    }

    OLED_FillRect(MENU_BAR_X, (uint8_t)(slot * MENU_ROW_PAGES),
                  MENU_BAR_W, MENU_ROW_PAGES, 1u);
}

void Menu_Draw(void)
{
    const MenuPage *page = Menu_Current();
    uint8_t count;
    uint8_t row;

    OLED_Clear();

    /* 控制面板页面自己绘制，画完就返回，不再走下面的列表逻辑 */
    if ((page->ops != 0) && (page->ops->onDraw != 0)) {
        page->ops->onDraw();
        return;
    }

    count = page->count;

    for (row = 0u; row < MENU_ROWS; row++) {
        uint8_t index = (uint8_t)(s_top + row);
        uint8_t itemPage = (uint8_t)(row * MENU_ROW_PAGES);
        const MenuItem *item;

        if (index >= count) {
            break;
        }

        item = &page->items[index];
        OLED_DrawText(MENU_TEXT_X, itemPage, item->label);

        if (item->getValue != 0) {
            const uint16_t *value = item->getValue();
            /* 回调可能返回 0，表示这次没内容可显示 */
            if (value != 0) {
                OLED_DrawTextRight(MENU_VALUE_X, itemPage, value);
            }
        }
    }

    /* 高亮选中行：先把整行反色，再在上面"抠"出一个黑色箭头 */
    if ((s_cursor >= s_top) && ((uint8_t)(s_cursor - s_top) < MENU_ROWS)) {
        uint8_t selectedRow = (uint8_t)(s_cursor - s_top);
        uint8_t selectedPage = (uint8_t)(selectedRow * MENU_ROW_PAGES);

        OLED_InvertRect(0u, selectedPage, OLED_WIDTH, MENU_ROW_PAGES);
        /* 箭头用 "on = 0" 画，等于从白色高亮条里挖出一个黑色形状；
           1 个 page = 8 个像素行，所以 y 要按 page 换算 */
        Menu_DrawArrow(3u, (uint8_t)(selectedPage * 8u + 4u), 0u);
    }

    /* 滚动条放在高亮之后画，压在上面才不会被反色吃掉 */
    Menu_DrawScrollBar(count);
}
