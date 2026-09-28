/*
 * oled.h - SSD1306 128x64 OLED 驱动，跑在软件模拟 (bit banged) 的 I2C 总线上
 *
 * STM32F103C8 板子上的接线：
 *   PB6 -> OLED SCL
 *   PB7 -> OLED SDA
 *   VCC -> 3.3V, GND -> GND
 *
 * 整幅 128x64 的画面先在 1 KB 的显存 (frame buffer) 里拼好，再由
 * OLED_Refresh() 一次性推给屏，所以画图过程中屏幕不会闪。文本统一用
 * Unicode 码点 (code point) 数组 (uint16_t) 传递，好处是每个源文件
 * 都是纯 ASCII，不依赖编辑器或编译器的文本编码。
 * 可用的 CN_* 宏见 oled_font.h。
 */

#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#define OLED_WIDTH      128u
#define OLED_HEIGHT     64u
#define OLED_PAGES      8u      /* 64 行 / 每个 page 8 行 */

/* "backlight" 菜单项用的对比度预设值 */
#define OLED_CONTRAST_DIM     0x0Fu
#define OLED_CONTRAST_LOW     0x3Fu
#define OLED_CONTRAST_MID     0x8Fu
#define OLED_CONTRAST_HIGH    0xCFu

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_SetContrast(uint8_t contrast);

/* 绘图基础函数，全都只改显存 (frame buffer)，不动屏幕 */
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
void OLED_FillRect(uint8_t x, uint8_t page, uint8_t w, uint8_t pages, uint8_t on);
void OLED_InvertRect(uint8_t x, uint8_t page, uint8_t w, uint8_t pages);

/* 文本辅助函数；page 是 16 像素高的一行所占两个 page 中的第一个 */
uint8_t OLED_TextWidth(const uint16_t *text);
void    OLED_DrawText(uint8_t x, uint8_t page, const uint16_t *text);
void    OLED_DrawTextRight(uint8_t xRight, uint8_t page, const uint16_t *text);

/* 把数字格式化进码点缓冲区，返回新的长度 */
uint8_t OLED_FormatU32(uint16_t *dst, uint32_t value);

/* 显存 (frame buffer) 的只读视图，共 8 个 page、每 page 128 字节；上位机
   模拟器用它，在真机上调试时也很顺手 */
const uint8_t *OLED_Buffer(void);

#endif /* __OLED_H */
