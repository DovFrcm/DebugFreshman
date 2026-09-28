/*
 * menu.h - 可复用的多级菜单引擎
 *
 * 一个菜单由两张普通表格描述：
 * MenuPage 里放一个 MenuItem 数组，而每个 item 要么指向一个子页面
 * （子菜单），要么执行一个回调（叶子动作），
 * 要么只在行右侧显示一个实时数值。
 *
 * 引擎内部维护一个页面栈，所以子菜单想嵌套多少层都行。
 * 四行 16 像素的行正好铺满 128x64 的屏；条目多于一屏的菜单
 * 会用一扇跟着光标走的滚动窗口来显示，
 * 并在右边缘画一根小滚动条。
 *
 *   KEY_1       ：光标上移（到顶后绕回末尾）
 *   KEY_2       ：光标下移（到底后绕回开头）
 *   KEY_3       ：进入选中的子菜单，或执行它的动作
 *   KEY_4       ：返回上一级页面（第 1 级就是主菜单）
 *   KEY_4_LONG  ：不管在第几层，直接回到主菜单
 */

#ifndef __MENU_H
#define __MENU_H

#include <stdint.h>
#include "key.h"

#define MENU_ROWS       4u      /* 可见行数，4 x 16 像素 = 64 像素 */
#define MENU_ROW_PAGES  2u      /* 每行占的 page 数               */

typedef struct MenuPage MenuPage;

typedef struct {
    const uint16_t *label;                  /* 这一行上画的文字       */
    const MenuPage *child;                  /* 子菜单，没有就填 0     */
    void (*onSelect)(void);                 /* 叶子行的动作回调       */
    const uint16_t *(*getValue)(void);      /* 右列的实时数值，没有填 0 */
} MenuItem;

/* 页面也可以不是列表，而是一块自定义控制面板 - LED 控制页就是这种。
   只要 ops 不为 0，引擎就把按键处理和绘制全交给它，
   items[] 列表完全被忽略。三个钩子都可以不填；
   页面自己想退出时，直接调 Menu_Back()。 */
typedef struct {
    void (*onKey)(KeyEvent event);          /* 本页的按键处理         */
    void (*onDraw)(void);                   /* 绘制页面主体           */
    void (*onLeave)(void);                  /* 本页即将出栈时调用     */
} MenuPageOps;

struct MenuPage {
    const MenuItem *items;
    uint8_t count;
    const MenuPageOps *ops;                 /* 普通列表页填 0         */
};

/* 复位引擎，显示 "root" 页面，光标停在第一行 */
void    Menu_Init(const MenuPage *root);

/* 送入一个按键事件，移动和翻页立即生效 */
void    Menu_HandleKey(KeyEvent event);

/* 返回上一层，菜单里的 "back" 行用它 */
void    Menu_Back(void);

/* 把当前页面渲染进 OLED 显存 (frame buffer) */
void    Menu_Draw(void);

/* 给主循环用的重绘标记 */
uint8_t Menu_NeedsRedraw(void);
void    Menu_ClearDirty(void);
void    Menu_Invalidate(void);

/* 当前页面至少有一个实时数值时返回真 */
uint8_t Menu_PageHasValue(void);

/* 当前嵌套深度，1 = 主菜单 */
uint8_t Menu_Depth(void);

#endif /* __MENU_H */
