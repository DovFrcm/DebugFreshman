/*
 * oled.c - SSD1306 128x64 驱动，自带一个软件 I2C 主机
 *
 * 总线在 PB6 (SCL) 和 PB7 (SDA) 上用位操作 (bit banging) 模拟。这两个引脚
 * 都配成开漏输出 (open drain)，所以写 1 只是把线释放掉，靠模块上的上拉
 * 电阻把电平拉高 —— 读 ACK 位用的也是这个办法。
 */

#include "oled.h"
#include "oled_font.h"
#include "board.h"

#define OLED_I2C_ADDR   0x78u   /* 0x3C 左移一位 */

/* BSRR 写 1 是置位引脚、BRR 写 1 是复位引脚；都是单条写操作，不会被中断
   切成半个时钟脉冲，所以软件 I2C 的时序反而更稳 */
#define SCL_HIGH()      (GPIOB->BSRR = BIT(6))
#define SCL_LOW()       (GPIOB->BRR  = BIT(6))
#define SDA_HIGH()      (GPIOB->BSRR = BIT(7))
#define SDA_LOW()       (GPIOB->BRR  = BIT(7))
#define SDA_LEVEL()     ((GPIOB->IDR & BIT(7)) != 0u)

/* 1 KB 的显存 (frame buffer)，8 个 page、每 page 128 列 */
static uint8_t s_gram[OLED_PAGES][OLED_WIDTH];

/* ------------------------------------------------------------------ */
/* 软件 I2C                                                            */
/* ------------------------------------------------------------------ */

static void I2C_Delay(void)
{
    volatile uint8_t i = 8u;
    /* volatile 是为了让这个空循环不被编译器优化掉；I2C 的速度就靠它拖出来。
       屏不亮或者花屏时，先来调这个循环次数 */
    while (i != 0u) {
        i--;
    }
}

static void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    I2C_Delay();
    SDA_LOW();          /* SCL 为高时 SDA 下降 = 起始条件 */
    I2C_Delay();
    SCL_LOW();
    I2C_Delay();
}

static void I2C_Stop(void)
{
    SDA_LOW();
    SCL_HIGH();
    I2C_Delay();
    SDA_HIGH();         /* SCL 为高时 SDA 上升 = 停止条件 */
    I2C_Delay();
}

static uint8_t I2C_WriteByte(uint8_t value)
{
    uint8_t i;
    uint8_t ack;

    /* 从最高位开始一位一位往外送；SCL 高电平期间数据必须保持稳定，
       所以先摆好 SDA，再拉高 SCL */
    for (i = 0u; i < 8u; i++) {
        if ((value & 0x80u) != 0u) {
            SDA_HIGH();
        } else {
            SDA_LOW();
        }
        I2C_Delay();
        SCL_HIGH();
        I2C_Delay();
        SCL_LOW();
        I2C_Delay();
        value = (uint8_t)(value << 1);
    }

    /* 第 9 个时钟：释放 SDA，然后采样从机拉低表示的应答 (ACK) */
    SDA_HIGH();
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    ack = SDA_LEVEL() ? 0u : 1u;
    SCL_LOW();
    I2C_Delay();

    return ack;
}

/* control 字节 0x00 = 命令流，0x40 = 显示数据流 */
static uint8_t OLED_WriteStream(uint8_t control, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t ok;

    /* ok 一路按位与累积：只要有一个字节没收到 ACK，返回值就是 0 */
    I2C_Start();
    ok = I2C_WriteByte(OLED_I2C_ADDR);
    ok = ok & I2C_WriteByte(control);
    for (i = 0u; i < len; i++) {
        ok = ok & I2C_WriteByte(data[i]);
    }
    I2C_Stop();

    return ok;
}

static void OLED_WriteCmd2(uint8_t a, uint8_t b)
{
    uint8_t cmd[2];
    cmd[0] = a;
    cmd[1] = b;
    (void)OLED_WriteStream(0x00u, cmd, 2u);
}

/* ------------------------------------------------------------------ */
/* 对外接口 (public API)                                               */
/* ------------------------------------------------------------------ */

void OLED_Init(void)
{
    static const uint8_t init_seq[] = {
        0xAEu,              /* 关闭显示 (display off)                     */
        0xD5u, 0x80u,       /* 时钟分频比 / 振荡器频率                    */
        0xA8u, 0x3Fu,       /* 多路复用比 (multiplex ratio) = 64          */
        0xD3u, 0x00u,       /* 显示偏移 (display offset) = 0              */
        0x40u,              /* 显示起始行 = 0                             */
        0x8Du, 0x14u,       /* 打开电荷泵 (charge pump)                   */
        0x20u, 0x02u,       /* 显存寻址模式 = page                        */
        0xA1u,              /* 段重映射 (segment remap)，列 127 排到最左  */
        0xC8u,              /* COM 扫描方向反向                           */
        0xDAu, 0x12u,       /* COM 引脚硬件配置                           */
        0x81u, 0xCFu,       /* 对比度 (contrast)                          */
        0xD9u, 0xF1u,       /* 预充电周期 (pre-charge period)             */
        0xDBu, 0x40u,       /* VCOMH 取消选择电平                         */
        0xA4u,              /* 输出跟随 RAM 的内容                        */
        0xA6u,              /* 正常显示（不反色）                         */
        0x2Eu,              /* 关闭滚动 (scrolling)                       */
        0xAFu               /* 打开显示                                   */
    };

    /* 等屏自己做完上电复位 (power on reset) */
    Delay_Ms(120);

    /* 确保总线一开始是空闲状态 */
    SCL_HIGH();
    SDA_HIGH();
    Delay_Ms(2);

    (void)OLED_WriteStream(0x00u, init_seq, (uint16_t)sizeof(init_seq));

    OLED_Clear();
    OLED_Refresh();
}

void OLED_Clear(void)
{
    uint8_t page;
    uint8_t col;

    for (page = 0u; page < OLED_PAGES; page++) {
        for (col = 0u; col < OLED_WIDTH; col++) {
            s_gram[page][col] = 0x00u;
        }
    }
}

void OLED_Refresh(void)
{
    uint8_t page;
    uint8_t cmd[3];

    /* 逐 page 刷：先告诉屏"我要写第几个 page、从第 0 列开始"，
       再把这一 page 的 128 字节数据一次推过去 */
    for (page = 0u; page < OLED_PAGES; page++) {
        cmd[0] = (uint8_t)(0xB0u | page);   /* 设置 page 起始地址 */
        cmd[1] = 0x00u;                     /* 列地址低 4 位 = 0  */
        cmd[2] = 0x10u;                     /* 列地址高 4 位 = 0  */
        (void)OLED_WriteStream(0x00u, cmd, 3u);
        (void)OLED_WriteStream(0x40u, &s_gram[page][0], OLED_WIDTH);
    }
}

void OLED_SetContrast(uint8_t contrast)
{
    OLED_WriteCmd2(0x81u, contrast);
}

void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on)
{
    /* 越界的点直接丢掉。x/y 是 uint8_t，不做这一刀的话 y>>3 会算到
       数组外面去，踩坏别的变量 */
    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT)) {
        return;
    }

    /* 显存按 page 组织：一个字节管同一列上纵向 8 个像素，bit0 在最上、
       bit7 在最下（SSD1306 的约定）。所以 y>>3 选 page，y&7 选 bit */
    if (on != 0u) {
        s_gram[y >> 3][x] |= (uint8_t)(1u << (y & 7u));
    } else {
        s_gram[y >> 3][x] &= (uint8_t)~(1u << (y & 7u));
    }
}

void OLED_FillRect(uint8_t x, uint8_t page, uint8_t w, uint8_t pages, uint8_t on)
{
    uint8_t p;
    uint8_t i;

    /* 每走一步都重新裁剪一次，允许调用方传一个超出屏幕的宽矩形而不用
       自己算边界 */
    for (p = 0u; p < pages; p++) {
        uint8_t pg = (uint8_t)(page + p);
        if (pg >= OLED_PAGES) {
            break;
        }
        for (i = 0u; i < w; i++) {
            uint8_t col = (uint8_t)(x + i);
            if (col >= OLED_WIDTH) {
                break;
            }
            s_gram[pg][col] = (on != 0u) ? 0xFFu : 0x00u;
        }
    }
}

void OLED_InvertRect(uint8_t x, uint8_t page, uint8_t w, uint8_t pages)
{
    uint8_t p;
    uint8_t i;

    for (p = 0u; p < pages; p++) {
        uint8_t pg = (uint8_t)(page + p);
        if (pg >= OLED_PAGES) {
            break;
        }
        for (i = 0u; i < w; i++) {
            uint8_t col = (uint8_t)(x + i);
            if (col >= OLED_WIDTH) {
                break;
            }
            s_gram[pg][col] = (uint8_t)~s_gram[pg][col];
        }
    }
}

/* ------------------------------------------------------------------ */
/* 字模与文本渲染                                                      */
/* ------------------------------------------------------------------ */

static void OLED_DrawAscii(uint8_t x, uint8_t page, uint8_t ch)
{
    const uint8_t *glyph;
    uint8_t i;

    /* 8x16 的 ASCII 字模要占两个 page，画在最后一个 page 上会越界 */
    if (page > (OLED_PAGES - 2u)) {
        return;
    }
    if ((ch < FONT_ASCII_FIRST) || (ch > FONT_ASCII_LAST)) {
        ch = (uint8_t)'?';
    }

    glyph = font_ascii_8x16[ch - FONT_ASCII_FIRST];
    /* 用 |= 叠加而不是直接赋值：文字才能画在已有图形或高亮块上面 */
    for (i = 0u; i < 8u; i++) {
        s_gram[page][x + i] |= glyph[i];
        s_gram[page + 1u][x + i] |= glyph[8u + i];
    }
}

static void OLED_DrawCjkGlyph(uint8_t x, uint8_t page, const uint8_t *glyph)
{
    uint8_t i;

    /* 汉字字模 16x16，同样跨两个 page，每 page 用 16 字节 */
    if (page > (OLED_PAGES - 2u)) {
        return;
    }

    for (i = 0u; i < 16u; i++) {
        s_gram[page][x + i] |= glyph[i];
        s_gram[page + 1u][x + i] |= glyph[16u + i];
    }
}

uint8_t OLED_TextWidth(const uint16_t *text)
{
    uint16_t width = 0u;

    /* 半角 ASCII 占 8 列，全角汉字占 16 列。这里只看码点范围，不去查字库，
       所以对字库里没有的字也能算出宽度 */
    while (*text != 0u) {
        width = (uint16_t)(width + ((*text < 0x80u) ? 8u : 16u));
        text++;
    }

    return (uint8_t)width;
}

void OLED_DrawText(uint8_t x, uint8_t page, const uint16_t *text)
{
    while (*text != 0u) {
        uint16_t code = *text;

        if (code < 0x80u) {
            if (x > (OLED_WIDTH - 8u)) {
                break;
            }
            OLED_DrawAscii(x, page, (uint8_t)code);
            x = (uint8_t)(x + 8u);
        } else {
            int index;
            if (x > (OLED_WIDTH - 16u)) {
                break;
            }
            index = Font_FindCjk(code);
            if (index >= 0) {
                OLED_DrawCjkGlyph(x, page, font_cjk_16x16[index]);
            } else {
                OLED_FillRect(x, page, 16u, 2u, 0u);    /* 字库里没这个字 */
            }
            x = (uint8_t)(x + 16u);
        }
        text++;
    }
}

void OLED_DrawTextRight(uint8_t xRight, uint8_t page, const uint16_t *text)
{
    uint8_t width = OLED_TextWidth(text);

    /* 右对齐就是反推左边界。文字比可用宽度还长时就干脆从 0 列开始画，
       右边溢出的部分交给 OLED_DrawText 自己截断 */
    if (width >= xRight) {
        OLED_DrawText(0u, page, text);
    } else {
        OLED_DrawText((uint8_t)(xRight - width), page, text);
    }
}

uint8_t OLED_FormatU32(uint16_t *dst, uint32_t value)
{
    /* uint32_t 最大 4294967295，正好 10 位十进制数 */
    uint16_t tmp[10];
    uint8_t n = 0u;
    uint8_t i;

    /* 单独处理 0：下面的取余循环对 0 一次都不会执行 */
    if (value == 0u) {
        dst[0] = (uint16_t)'0';
        return 1u;
    }

    /* 取余拿到的是"个位在前"，所以先存进 tmp，最后再倒着抄进 dst */
    while ((value != 0u) && (n < 10u)) {
        tmp[n] = (uint16_t)('0' + (value % 10u));
        value /= 10u;
        n++;
    }

    for (i = 0u; i < n; i++) {
        dst[i] = tmp[n - 1u - i];
    }

    return n;
}

const uint8_t *OLED_Buffer(void)
{
    /* 返回首地址，按 8*128 连续字节解读即可 */
    return &s_gram[0][0];
}
