# OLED 多级菜单 + LED 控制 + 串口（STM32F103C8 + SSD1306）

- **题目 1（6 分）OLED 菜单**：上电直接进入主菜单，箭头默认在首行，按键 1/2 移动箭头，
  按键 3 进入子菜单，按键 4 返回主菜单。
- **题目 2（6 分）LED 的使用**：在 LED 控制子菜单里，按键 1 让两个 LED 常亮/常灭切换，
  按键 2 让两个 LED 交替闪烁，按键 4 返回主菜单并熄灭两个 LED，
  屏幕上实时显示两个 LED 的亮灭状态。
- **题目 3（8 分）人机交互与串口**：信息显示页显示姓名（拼音）、学号、变量 a；
  上位机通过串口写 `a`（支持负数），也可以发 `GET A` 把当前值读回去。

所有菜单文字都是**中文**（宋体 16×16 点阵）加半角 ASCII（宋体 8×16 点阵）。

**工程根目录：`F:\Stm32\Project`**（`1.uvprojx` 就在这里，用 Keil 或 VSCode 打开这一层）。

---

## 1. 接线

需要的东西：Blue Pill 一块、0.96 寸 4 针 I2C OLED（SSD1306）一个、轻触按键 4 个、
**LED 2 个 + 限流电阻 2 个**、**USB-TTL 串口模块 1 个**、杜邦线若干、ST-Link 一个。

### 引脚对照

| 功能 | STM32 引脚 | 说明 |
|------|-----------|------|
| OLED SCL | **PB6** | 软件 I2C |
| OLED SDA | **PB7** | 软件 I2C，模块自带 4.7k 上拉，不用外加 |
| OLED VCC | **3.3V** | ⚠️ 见下面的警告，不要接 5V |
| OLED GND | GND | |
| **LED1** | **PB12** | 串 220Ω~1kΩ 电阻，极性见下 |
| **LED2** | **PB13** | 串 220Ω~1kΩ 电阻，极性见下 |
| **串口 TX** | **PA9** | USART1_TX → 接 USB-TTL 的 **RX** |
| **串口 RX** | **PA10** | USART1_RX ← 接 USB-TTL 的 **TX** |
| 按键 1（上移） | **PA0** | 按下接地，内部上拉 |
| 按键 2（下移） | **PA3** | 按下接地，内部上拉 |
| 按键 3（进入） | **PA5** | 按下接地，内部上拉 |
| 按键 4（返回） | **PB0** | 按下接地，内部上拉，可长按 |
| 板载 LED | PC13 | **本工程不用**，程序把它保持熄灭 |

> ⚠️ **OLED 的 VCC 一定接 3.3V，不要接 5V。** 大部分 4 针 OLED 模块的 I2C 上拉电阻
> 是接到 VCC 的，接 5V 就会把 SCL/SDA 也拉到 5V。STM32 的 IO 不是 5V 容忍的，
> 长期这样接可能打坏引脚。
>
> ⚠️ **OLED 模块的 4 个针顺序不统一**，有 `GND VCC SCL SDA` 的，也有 `VCC GND SCL SDA` 的。
> **按模块上印的丝印接**，不要按线的颜色或者排列顺序猜。

### Blue Pill 上引脚在哪

以 **USB 口朝上**为准，板子两排针的丝印：

```
   左排（从上往下）              右排（从上往下）
   VBAT                        ┌─ 3V3
   PC13   ← 板载 LED            ├─ GND
   PC14                        ├─ 5V    ← 别接
   PC15                        ├─ PB9
   PA0    ← KEY1               ├─ PB8
   PA1                         ├─ PB7   ← OLED SDA
   PA2                         ├─ PB6   ← OLED SCL
   PA3    ← KEY2               ├─ PB5
   PA4                         ├─ PB4
   PA5    ← KEY3               ├─ PB3
   PA6                         ├─ PA15
   PA7                         ├─ PA12
   PB0    ← KEY4               ├─ PA11
   PB1                         ├─ PA10
   PB10                        ├─ PA9
   PB11                        ├─ PA8
   RESET                       ├─ PB15
   3V3                         ├─ PB14
   GND                         ├─ PB13
   GND                         └─ PB12
```

> **ST-Link 接哪里？** PA13/PA14 **不在这两排 2×20 的排针上**。Blue Pill 顶端
> （RESET 键旁边）有一个独立的 **4 针 SWD 排针**，丝印一般写着
> `3V3 / SWDIO / SWCLK / GND`（个别批次顺序不同，**照丝印接**）：
> SWDIO→PA13、SWCLK→PA14、GND→GND、3V3→3V3。ST-Link 就插这里。

最稳妥的办法是**看板子上每个针脚旁边印的字**（都印了 `PB6`、`PA0` 这样的丝印），
照丝印接就不会错。

### 想换按键引脚怎么办

按键引脚只在 **`User/key.c` 顶部的那张 `s_keys[]` 表**里出现一次，改那一处就行，
不用动别的地方：

```c
static const KeyPin s_keys[KEY_COUNT] = {
    { GPIOA, 0u },      /* KEY_1 - arrow up          */
    { GPIOA, 3u },      /* KEY_2 - arrow down        */
    { GPIOA, 5u },      /* KEY_3 - enter sub menu    */
    { GPIOB, 0u }       /* KEY_4 - back / long press */
};
```

`Key_Init()` 会按这张表把引脚配成内部上拉输入，`Key_Scan()` 也按这张表读。
跨端口（有的在 A 口、有的在 B 口）也没问题。改完重新编译烧录即可。

### 串口（USB-TTL）怎么接

```
   Blue Pill          USB-TTL 模块
   ─────────          ────────────
   PA9  (TX)  ──────▶ RXD
   PA10 (RX)  ◀────── TXD
   GND        ─────── GND          ← 必须共地
```

- **TX 和 RX 必须交叉接**：单片机的 TX 接模块的 RX，模块的 TX 接单片机的 RX。
  接成 TX-TX 是最常见的错误，表现为完全收不到数据。
- **GND 一定要接**，否则电平没有参考，收到的是乱码。
- **供电**：如果板子已经由 ST-Link 供电，就**不要再把模块的 VCC 接上去**，
  两路电源顶在一起可能损坏。要用模块供电就拔掉 ST-Link 的 3.3V。
- **电平**：STM32F1 的 PA9/PA10 是 5V 容忍的，所以 5V 的 USB-TTL 也能收；
  但如果模块上有 3.3V/5V 跳线，**拨到 3.3V** 更保险。
- 波特率 **9600**，8 数据位，无校验，1 停止位（8N1）。
  模块的 TX 和 RX 之间不要接反丝印，按模块上印的字接。

### LED 怎么接

两个 LED 接到 **PB12** 和 **PB13**，每个串一个限流电阻。**有两种接法，代码里要用宏选对**：

**接法 A：引脚拉高点亮（源电流）**

```
   PB12 ──[220Ω~1kΩ]──▶|── GND        ▶| 是 LED，长脚(正极)在左边
   PB13 ──[220Ω~1kΩ]──▶|── GND
```
电阻接在**引脚和 LED 之间**。对应 `board.h` 里：
```c
#define BOARD_LED_ACTIVE_HIGH   1
```

**接法 B：引脚拉低点亮（灌电流）** ← **本工程当前用的**

```
   3.3V ──[220Ω~1kΩ]──▶|── PB12       ▶| 是 LED，长脚(正极)在左边
   3.3V ──[220Ω~1kΩ]──▶|── PB13
```
电阻接在 **3.3V 和 LED 之间**，LED 另一头接引脚。对应：
```c
#define BOARD_LED_ACTIVE_HIGH   0
```

> **怎么一眼判断自己是哪种？** 看电阻接在哪一头：
> 电阻接在**引脚**这一侧 → 接法 A（填 1）；
> 电阻接在 **3.3V** 这一侧 → 接法 B（填 0）。
>
> **填错了会怎样？** 灯还是能亮能闪，但**屏幕上的"亮/灭"会和实际灯相反**。
> 改这一个宏重新编译烧录即可，不用重新接线。

其它注意：

- **LED 有极性**：长脚是正极（阳极），短脚是负极（阴极）。接反了不亮，但不会烧；
- 电阻 220Ω~1kΩ 都行。220Ω 亮一些（约 7mA），1kΩ 暗一些（约 1.5mA）。
  **不要不接电阻**，会烧 LED 也可能拉坏引脚；
- 两颗 LED 的另一头按上面接法接引脚或 GND。

> **为什么特意选 PB12/PB13 这两个相邻的脚？** 因为这两个 LED 在同一个端口（GPIOB），
> 代码里可以用**一次 BSRR 写入同时控制两个灯**，两个灯在同一时刻翻转，
> 交替闪烁时绝不会出现"两个同时亮"或"两个同时灭"的瞬间。
> 如果你要换引脚，**尽量换成同一个端口的两个脚**。

### 按键怎么接

轻触按键是 4 个脚的，**同一侧的两个脚内部是连通的**，实际只有两个电气节点。

```
          ┌───────┐
   PA0 ───┤1     4├─── GND
          │       │        ← 1 和 2 连通，3 和 4 连通
          │2     3├─── (空着)
          └───────┘
```

- **一脚接 PAx，另一脚接 GND**，两个脚无所谓正负；
- **不要接同一侧的两个脚**（比如 1 和 2），那样等于把 PA0 直接短到 GND，按键永远处于"按下"状态；
- **不需要外接上拉电阻**，`board.c` 里已经把 PA0~PA3 配成内部上拉输入了；
- 不确定哪两个脚是一组，用万用表蜂鸣档量一下，或者直接接**斜对角**的两个脚（一定是一组）。

### 面包板怎么摆

**第一步：把 Blue Pill 插上去**

板子的排针要**朝下**才能插进面包板。跨在面包板**中间的凹槽**上，两排针各占一列
（通常是左排落在 `c` 列、右排落在 `h` 列，具体看你插进去后落在哪一列）。

> 如果板子的排针是**朝上**焊的，就插不进去。两个办法：
> ① 把排针吹下来重新焊成朝下；② 用母对母杜邦线从排针引出来，板子放旁边。

**第二步：拉两条电源轨**

```
Blue Pill 3V3 ──→ 面包板红色轨 (+)     ← 后面 OLED 的 VCC 从这里取
Blue Pill GND ──→ 面包板蓝色轨 (-)     ← 所有 GND 都汇总到这里
```

> ⚠️ **面包板的电源轨常常是中间断开的**（上下半段不连通）。用万用表量一下，
> 不通就用一根线把两段短接起来。这个坑很常见。

**第三步：接 OLED（先只接这一个，能显示主菜单再继续）**

| OLED | 接到 |
|------|------|
| GND | 蓝色轨 (−) |
| VCC | 红色轨 (+3.3V) |
| SCL | 一根线到 Blue Pill 的 **PB6** 那一列 |
| SDA | 一根线到 Blue Pill 的 **PB7** 那一列 |

**第四步：接 4 个按键**

轻触按键**横跨中间凹槽**插，一侧的脚接信号，另一侧的脚接 GND：

```
   蓝色轨 GND ══╤═══════╤═══════╤═══════╤══
                │       │       │       │
             ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐
             │KEY1 │ │KEY2 │ │KEY3 │ │KEY4 │
             └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘
                │       │       │       │
               PA0     PA3     PA5     PB0
              (上移)  (下移)  (进入)  (返回)
```

4 个按键的 GND 侧可以先用线串成一条，最后只用一根线接到蓝色轨，省线。

**导线颜色建议**：红=3.3V、黑或蓝=GND、其它颜色走信号。接错了扫一眼就能发现。

**上电前检查**：

- [ ] OLED 的 VCC 接的是 **3.3V**，不是 5V
- [ ] SCL 在 PB6、SDA 在 PB7，**没有接反**
- [ ] OLED、LED、4 个按键的 GND **都接到同一条蓝轨**（必须共地）
- [ ] 每个按键都只有**一侧**接 GND，没有把同一侧两个脚同时接到 GND
- [ ] 两个 LED 都**串了电阻**，而且 LED 的长短脚没有接反

### 上电顺序建议

先别急着全接上，按这个顺序排查最快：

1. 只接 ST-Link + OLED，**按键先不接** → 上电应该能看到主菜单（箭头能显示但按不动）；
2. 再接 KEY1，试一下箭头往上；接一个试一个；
3. 最后接 KEY4。

这样出问题时能立刻定位是哪个环节。

### 常见现象对照

| 现象 | 原因 |
|------|------|
| OLED 全黑，背光都不亮 | VCC/GND 没接对，或屏坏了 |
| OLED 全黑但 ST-Link 能烧录 | SCL/SDA 接反（PB6/PB7 对调试试）、或接触不良 |
| 屏幕只有一条亮线 / 花屏 | I2C 通信不稳：线太长、杜邦线接触不良、没共地 |
| 屏幕正常但按键没反应 | 按键没接到 GND、或接成同一侧两个脚短路了 |
| 某个按键一直接触 | 按键的一脚接到了 VCC 而不是 GND |
| 两个 LED 都不亮 | 没串电阻/LED 接反/接错脚；先在 LED 控制页按按键 1 试常亮 |
| 两个 LED 常亮不灭 | 板载 PC13 那颗不算；如果外接的常亮，检查是不是接到了 3.3V |

**不接 OLED 也能验证程序在跑**：进入 LED 控制子菜单（主菜单第一项按按键 3），
再按按键 1，两个外接 LED 应该一起亮起来。

## 2. 操作方式

**主菜单和普通子菜单**（题目 1）：

| 按键 | 主菜单 | 普通子菜单 |
|------|--------|-----------|
| 按键 1 | 箭头上移一行（到顶后循环到最后一项） | 同上 |
| 按键 2 | 箭头下移一行（到底后循环回第一项） | 同上 |
| 按键 3 | 进入当前选中的子菜单 | 执行该项功能 |
| 按键 4 | 无动作（已经在主菜单） | 返回上一层 |
| 按键 4 长按 | — | 直接回到主菜单（任意层数） |

**LED 控制子菜单**（题目 2）—— 这一页是控制面板，按键含义不一样：

| 按键 | 作用 |
|------|------|
| 按键 1 | 两个 LED **常亮 ↔ 常灭** 切换（灭→亮→灭……） |
| 按键 2 | 两个 LED 进入**交替闪烁**（每个亮 500ms，1 秒一个来回） |
| 按键 4 | **返回主菜单**，并把两个 LED 熄灭（长按同样有效） |
| 按键 3 | 这一页无作用 |

OLED 上这一页显示 4 行：

```
LED1: 亮        ← 实时状态，跟着灯一起变
LED2: 灭
模式: 交替       ← 亮 / 灭 / 交替
按键4返回
```

状态一变，程序立刻重绘（不是等定时器），所以屏幕和实际灯光几乎同步，
远快于题目要求的 1 秒。

### 串口协议（题目 3）

波特率 **9600**，8 数据位，无校验，1 停止位。上位机发**文本行**就行，行尾用 `\r\n`
或者 `\n` 都可以；**不发换行也能用**——单片机在收到最后一个字符约 60ms 后会自动认为
这一行结束了。

| 上位机发送 | 单片机行为 | 单片机回复 |
|-----------|-----------|-----------|
| `111` | `a = 111` | 无 |
| `-25` | `a = -25`（**支持负数**） | 无 |
| `0` | `a = 0` | 无 |
| `GET A` | 不改 `a` | `111\r\n`（当前值 + 换行） |
| `abc` 之类的乱码 | 忽略，`a` 不变 | 无 |

- 大小写不敏感：`GET A` / `get a` / `GETA` 都认；
- 数字前面允许有空格和 `+` / `-` 号；
- **一整行必须是合法数字才生效**：`12ab` 这种会被整行丢弃，
  不会把 `a` 改成 12（防止半截数据落进来）；
- 超出 int32 范围的数会被**钳位**到 ±2147483647，不会溢出回绕；
- `a` 是**有符号 32 位整数**，上电初值 0。

**用串口助手测**：sscom / XCOM / MobaXterm 都行，波特率 9600。
发 `111` → OLED 上 `a = 0` 立刻变成 `a = 111`；发 `GET A` → 接收区显示 `111`。

**用 Python 测**（就是评分规则里"写 0、一个正数、一个负数再读回来"那一轮）：

```python
import serial, time
s = serial.Serial('COM3', 9600, timeout=1)   # 改成你的串口号

def set_a(v):
    s.write(f'{v}\n'.encode())

def get_a():
    s.reset_input_buffer()
    s.write(b'GET A')
    time.sleep(0.1)
    return int(s.readline())

for v in (0, 111, -4269):        # 0、正数、负数各来一轮
    set_a(v)
    assert get_a() == v, f'{v} 读回来不一致'
print('三轮回读全部一致')
```

> **为什么用中断收而不是在主循环里轮询？** 刷新一整屏 OLED 要十来毫秒，
> 比 9600 波特率下一个字节的时间长得多。如果靠主循环轮询，屏幕一刷新就会丢字节。
> 现在 USART1 中断把收到的字节塞进环形缓冲区，主循环慢慢取，一个字节都不会丢。

菜单项超过 4 项时，显示窗口会自动跟随箭头滚动，屏幕右侧有滚动条指示当前位置。

## 3. 菜单结构

```
主菜单
 ├─ LED控制     → 控制面板：LED1/LED2 实时状态 + 模式（亮/灭/交替）
 ├─ 信息显示    → 姓名 Yang Shouqin / 学号 32602536 / a = 0 / 按下按键4返回
 ├─ 巡线功能    → 启动巡线 / 停止巡线 / 状态 RUN|STOP / 返回
 ├─ 拓展功能    → 蜂鸣器测试 / 背光亮度(0~3) / 系统设置 / 返回
 │                 └─ 系统设置 → 版本信息 V1.0 / 恢复默认 / 返回
 └─ 关于本机    → STM32F103C8 / 主频 72MHz / 运行时间 / 返回
```

- **LED控制**：题目 2 的实现。这一页不是列表菜单而是**控制面板**，用 `MenuPageOps`
  （见 `menu.h`）接管按键和绘制：按键 1 常亮/常灭切换，按键 2 交替闪烁，
  按键 4 返回主菜单并熄灭两个灯。屏幕上实时显示 LED1/LED2 的亮灭和当前模式。
- **信息显示**：题目 3a/3b 的实现。也是控制面板式的自定义页面，4 行分别是
  姓名、学号、变量 `a`、返回提示。上位机一改 `a`，页面**立刻**重绘。
  姓名和学号写在 `app.c` 顶部的 `T_MY_NAME[]` / `T_MY_ID[]` 两张表里，改成自己的即可
  （注意第 1 行 128 像素宽，拼音姓名**最多 12 个字母**，当前的 "Yang Shouqin" 正好占满）。
- **巡线功能**：启停标志位，为后续接灰度传感器预留；`状态` 行实时显示 RUN/STOP。
- **拓展功能**：蜂鸣器计数（板子上没接蜂鸣器时只是计数，接上后把 `Act_Buzzer()` 换成真实驱动即可）、
  背光亮度真实调节 SSD1306 对比度、系统设置是第三级菜单（用来演示多级嵌套）。
- **关于本机**：型号、实际主频、开机运行时间。

## 4. 编译方法（Keil MDK）

这台机器上 Keil MDK 已经装好了：

```
F:\Program Files (x86)\keil5\UV4\UV4.exe     MDK 5.24a (MDK-Lite)
F:\Program Files (x86)\keil5\ARM\PACK\Keil\STM32F1xx_DFP\2.2.0
F:\Program Files (x86)\keil5\ARM\PACK\ARM\CMSIS\5.0.1
```

工程文件 `1.uvprojx` 已经配好，**双击打开直接 `Build` 就能过**，不需要再做任何设置：

| 分组 | 内容 |
|------|------|
| `User` | 本工程的 7 个 `.c` |
| `Startup` | `keil\startup_stm32f10x_md.s`（ST 官方 armasm 启动文件）+ `keil\system_init.c` |

实测结果（`UV4 -r` 全量重建）：

```
Program Size: Code=4296 RO-data=4748 RW-data=48 ZI-data=2784
".\Objects\1.axf" - 0 Error(s), 0 Warning(s).
FromELF: creating hex file...
```

占用 Flash 约 **8.9 KB**、RAM 约 **2.8 KB**，远低于 MDK-Lite 的 32 KB 限制。
产物是 `Objects\1.hex`（已自动勾选 `Create HEX File`），用 ST-Link 下载即可。

> ⚠️ **不要**再去 `Project → Manage → Run-Time Environment` 里勾 `Device → Startup`。
> 那个组件会引入 `system_stm32f10x.c`，它和本工程的 `keil\system_init.c` 都会定义
> `SystemInit()`，链接会报重复符号。启动文件已经放进工程了，不需要 RTE。

> 本工程**不依赖标准外设库，也不依赖 HAL**。`User\board.h` 里自带用到的那几个寄存器定义，
> 所以不用额外添加 `stm32f10x_gpio.c` 之类的库文件。

### 用命令行编译

Keil Assistant 插件就是靠这个方式编译的，也可以自己在 PowerShell 里跑：

```powershell
& 'F:\Program Files (x86)\keil5\UV4\UV4.exe' -r 'F:\Stm32\Project\1.uvprojx' -j0 -o 'F:\Stm32\Project\keil_build.log'
```

`-b` 是增量编译，`-r` 是全量重建，`-f` 是下载到芯片。返回码 0 = 无错误无警告。

### 下载到板子

**① 下载器已配成 ST-Link**

工程里已经写好 ST-Link 配置，不用再手动设：

| 文件 | 字段 | 值 |
|------|------|-----|
| `1.uvoptx` | `nTsel` / `pMon` | `5` / `STLink\ST-LINKIII-KEIL_SWO.dll` |
| `1.uvoptx` | `TargetDriverDllRegistry` | ST-Link 的 Flash 算法条目 |
| `1.uvprojx` | `Utilities/Flash2` | `STLink\ST-LINKIII-KEIL_SWO.dll` |

Flash 算法是 `STM32F10x_128`（**名字带 128 但没错**：查过器件包的 PDSC，
ST 给 STM32F103C8 指定的就是这个，实际范围由芯片型号限定在 64KB）。

**建议开机核对一次**（30 秒）：`Alt+F7` → **Debug** 页应显示 `ST-Link Debugger`，
点 **Settings** 后 `SW Device` 里能看到 IDCODE 就说明连上芯片了；**Utilities** 页确认
`Use Target Driver for Flash Programming` 勾着且是 ST-Link。
如果 Debug 页显示的是别的，就在下拉框里重选一次 `ST-Link Debugger`——
Keil 会自己把参数写正确，这招永远管用，不依赖手改的配置文件。

**② 接线（ST-Link ↔ Blue Pill）**

| ST-Link | 开发板 |
|---------|--------|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| 3.3V | 3V3 |

OLED 和按键的接线见第 1 章。注意 OLED 的 VCC 接 **3.3V**，接 5V 有些模块会烧。

**③ 下载**

- Keil uVision：按 **F8**，或点工具栏的下载图标；
- VSCode + Keil Assistant：`Ctrl+Alt+D`；
- 命令行：

```powershell
& 'F:\Program Files (x86)\keil5\UV4\UV4.exe' -f 'F:\Stm32\Project\1.uvprojx' -j0 -t 'Target 1' -o 'F:\Stm32\Project\keil_flash.log'
```

**④ 上电应该看到**

按一下板子的 RESET 键。OLED 第一行是反白高亮的 `▶ LED控制`，
下面依次 `信息显示`、`巡线功能`、`拓展功能`。按 KEY1/KEY2 箭头上下移动，
移到第 5 项 `关于本机` 时窗口会整体上滚，右侧出现滚动条。

**⑤ 下载报错对照**

| 报错 | 原因 |
|------|------|
| `No ULINK2/ME Device found` | 第 ① 步没做，Debug 里还是 ULINK2 |
| `No Cortex-M Device found` | SWDIO/SWCLK 接反或没接，或板子没供电 |
| `Flash Download failed - Target DLL has been cancelled` | 接触不良，或芯片被读保护（用 ST-Link Utility 解除） |
| `Cannot Load Flash Programming Algorithm` | Utilities 里没配 Flash 算法 |
| 下载成功但屏幕不亮 | I2C 接线（PB6/PB7）、OLED 供电，或对比度；先查接线 |

**⑥ 如果没有仿真器，只有 USB-TTL**

用串口 ISP 下载：`BOOT0` 拨到 1 → 复位 → 用 STM32CubeProgrammer 或
Flash Loader Demonstrator 通过 PA9/PA10 烧 `Objects\1.hex` → 烧完把 `BOOT0` 拨回 0 → 再复位。

## 5. 用 VSCode 编译

VSCode 里的 **Keil Assistant** 插件本身不带编译器，它只是去调用 Keil 的 `UV4.exe`。
Keil 已经装好了，所以把路径告诉它就能在 VSCode 里编译 —— 用的还是 Keil 的 ARMCC 编译器，
和上面第 4 章完全一样。

**路径已经写进 `.vscode\settings.json` 了**：

```json
"KeilAssistant.MDK.Uv4Path": "F:/Program Files (x86)/keil5/UV4/UV4.exe"
```

用法：

1. 用 VSCode 打开 **`F:\Stm32\Project`** 文件夹；
2. 左侧点 **Keil Assistant** 图标（如果没看到，在扩展里确认它已启用），点 **打开项目**，
   选 `1.uvprojx`；
3. 面板里会出现 `Target 1`，右键可以 **Build** / **Rebuild** / **Download**，
   也可以按 `F7` 编译。

IntelliSense 用的 `.vscode\c_cpp_properties.json` 是 Keil Assistant 自动生成的
（里面定义了 `__CC_ARM` 等 ARMCC 宏），保持原样即可。

> 工程的根目录是 `F:\Stm32\Project`。旁边的 `.eide\` 和 `gcc\` 是之前尝试 EIDE + GNU 工具链时
> 和 Keil 的编译互不干扰（Keil 只编译 `User\` 和 `keil\` 里列出的文件）。
> 想删可以直接删，不影响 Keil 工程。

## 6. 文件说明

| 文件 | 作用 |
|------|------|
| `User/main.c` | 主循环：扫描按键 → 交给菜单 → 需要时重绘并刷新 OLED |
| `User/app.c` / `app.h` | 菜单树（各级菜单项）和各菜单项的具体功能 |
| `User/menu.c` / `menu.h` | 通用多级菜单引擎：页面栈、箭头、滚动窗口、高亮、滚动条 |
| `User/key.c` / `key.h` | 4 个按键的消抖扫描，支持长按；按键引脚表在 `key.c` 顶部 |
| `User/serial.c` / `serial.h` | USART1 上位机链路：中断收 + 环形缓冲区，PA9/PA10 |
| `User/oled.c` / `oled.h` | SSD1306 驱动：软件 I2C、1KB 显存、画点/矩形/反白、中英文混排 |
| `User/oled_font.c` / `oled_font.h` | 字库：ASCII 8×16（95 个）+ 汉字 16×16（64 个） |
| `User/board.c` / `board.h` | 72MHz 时钟、GPIO 初始化、SysTick 延时、板载 LED |
| `keil/` | Keil 编译用的 ST 官方 armasm 启动文件 + `SystemInit()` 实现 |
| `gcc/` | 之前尝试 EIDE + GCC 时留下的启动文件和链接脚本，Keil 用不到 |
| `tools/genfont.ps1` | 字库生成脚本（用系统宋体渲染点阵，可重新生成/扩充） |
| `tests/` | 在电脑上跑的菜单逻辑测试，不需要开发板 |

### 源码的编码和中文注释

**所有 `.c` / `.h` 都是 UTF-8 无 BOM、LF 换行，注释是中文。**

实测过 armcc（Keil 的编译器）能正常吃下 UTF-8 中文注释——加注释前后
`Program Size` 完全相同（`Code=5764 RO-data=5056`），说明注释没影响任何代码。

- **在 VSCode 里看**：默认就是 UTF-8，中文注释显示正常，不用设置。
- **在 Keil uVision 里看**：Keil 在中文系统上默认按 ANSI（GBK）读源文件，
  中文注释可能显示成乱码——**这只影响显示，不影响编译**。想让它正常显示：
  `Edit → Configuration → Editor → Encoding` 选 `Encode in UTF-8`（或类似选项）。

> 为什么菜单文字不直接用中文字符串，而是写成 `{ CN_KONG, CN_ZHI, 0 }` 这种码点数组？
> 就是为了不受编辑器编码影响：不管 Keil 把源文件当成 UTF-8 还是 GBK，
> 编译进单片机的字节都一样，屏幕上不会出现乱码。中文注释没有这个顾虑，
> 因为注释根本不进二进制。

### 显示效果（测试程序渲染出来的真实显存内容）

`tests/screens/` 里的 PNG 就是菜单在 128×64 屏上的实际画面，
例如 `01.png` 主菜单、`06.png` LED 交替闪烁、`11.png` 信息显示（姓名/学号/a）。

## 7. 怎么改菜单

### 加一个菜单项

在 `app.c` 里加字符串和菜单项即可。菜单项四个字段分别是
`{ 文字, 子菜单, 按键3的动作, 右侧实时值 }`，不用的填 `0`：

```c
static const uint16_t T_MYITEM[] = { 'T', 'E', 'S', 'T', 0 };   /* 纯英文 */

static const MenuItem kMyItems[] = {
    { T_MYITEM, 0, My_Action, 0 },      /* 按键3 调用 My_Action() */
    { T_BACK,   0, Menu_Back, 0 }
};
static const MenuPage kMyPage = { kMyItems, 2 };
```

再把 `kMyPage` 挂到上一级菜单某个菜单项的第二个字段上即可。菜单项超过 4 项会自动滚动，
不需要额外处理。

### 加一个汉字

汉字点阵是脚本生成的，**源文件里不出现中文**，汉字用 Unicode 码点表示
（例如 `CN_KONG` 就是 `0x63A7`，即"控"）。这样做的好处是：
无论 Keil 或编辑器把源文件当成 UTF-8 还是 GBK，编译出来都不会乱码。

加字步骤：

1. 打开 `tools/genfont.ps1`，在 `$Cjk` 表里按**码点从小到大**的位置插入一行：
   `@(0x8BBE, 'CN_SHE', 'she = set up')`（第一个是该字的 Unicode 码点）；
2. 运行脚本重新生成 `User/oled_font.c` 和 `User/oled_font.h`；
3. 在源码里直接用 `CN_SHE` 拼字符串。

查汉字码点：Windows 计算器切到"程序员"模式，选"Unicode"输入汉字即可看到码点；
或者用 `[char]0x8BBE` 在 PowerShell 里反查。

## 8. 电脑上跑测试（不用开发板）

`tests/` 里有一套测试，它把**真实的** `oled.c`、`oled_font.c`、`menu.c`、`app.c`
编译到电脑上运行，只把硬件层换成内存变量，然后把真实的 128×64 显存打印成字符画，
用来验证按键逻辑和画面是否符合预期。

需要 MinGW 的 gcc。**先 `cd F:\Stm32\Project`**，下面都是相对路径：

```powershell
cd F:\Stm32\Project
gcc -std=c99 -Wall -Wextra -O1 -I User -include tests/fake_board.h `
    tests/test_menu.c tests/fake_board.c tests/fake_serial.c `
    User/oled.c User/oled_font.c User/menu.c User/app.c -o tests/test_menu.exe
.\tests\test_menu.exe
```

覆盖的内容：上电箭头在首行、按键 1/2 上下移动与循环、超过 4 项时窗口滚动、
按键 3 进入子菜单、按键 4 返回且箭头回到原来的位置、三级嵌套、长按返回主菜单。

## 9. 说明与注意

- OLED 刷新一次要发 1KB 数据（软件 I2C 约 10ms），所以只在需要时才重绘：
  按键后立即重绘，带实时数值的页面每 250ms 重绘一次。
- 如果晶振没起振，程序会自动退回内部 8MHz 继续跑（菜单仍然能用），
  信息显示页显示的也是真实的 8MHz，不会死机。
- 如果换了引脚，只改 `board.c` 里 `Board_Init()` 的两处配置和 `key.c` 里的
  `s_pin[]` 数组即可，其它文件不用动。
