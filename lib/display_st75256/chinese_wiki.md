# display_st75256 — ST75256 4 级灰阶 LCD 驱动库

## 概述

`display_st75256` 是一个面向 **ST75256** 驱动芯片的 **4 级灰阶** LCD 驱动库，屏幕分辨率为 **256×128 像素**。该库封装了屏幕初始化、局部刷新、图文绘制等功能，并集成了 **U8g2** 的字体渲染管线以实现灵活的文本输出。

> 本项目硬件基于 JLX256128G-337 或其他使用 ST75256 控制器的 256×128 4 灰阶 LCD 模块。

---

## 像素格式

每个像素用 **2 bit** 表示灰阶等级：

| 二进制 | 宏定义               | 灰阶     |
|--------|----------------------|----------|
| `0b00` | `DISPLAY_WHITE`      | 白色     |
| `0b01` | `DISPLAY_LIGHT_GRAY` | 浅灰     |
| `0b10` | `DISPLAY_DARK_GRAY`  | 深灰     |
| `0b11` | `DISPLAY_BLACK`      | 黑色     |

像素 2bit 在字节中的排列（**高位优先**）：

```
bit [7:6] → 像素 0（最上方的像素）
bit [5:4] → 像素 1
bit [3:2] → 像素 2
bit [1:0] → 像素 3（最下方的像素）
```

即 **1 个字节 = 4 个垂直排列的像素**。

---

## Page（页）系统

**Page** 是向屏幕传输的最小单位。

- 屏幕的 **128 行** 被划分为 **32 个 page**，每个 page 管理 **4 行像素**。
- 1 个 page 包含从左到右的 **256 列**，每列 1 字节（即 4 个垂直像素）。
- 屏幕显存总大小为 `256 列 × 32 页 = 8192 字节`。

```
Page 0: 行  0 ~ 行  3
Page 1: 行  4 ~ 行  7
...
Page 31: 行 124 ~ 行 127
```

### Canvas（画布）数据结构

Canvas 是一个**一维数组**，按 **page-major（页主序）、column-minor（列次序）** 排列。

对于一个 **w 像素宽、h 像素高** 的画布：
- 所需 page 数：`numPages = ceil(h / 4)`
- 总字节数：`numPages × w`
- 索引公式：`canvas[page * w + col]`

```
Canvas 内存布局（w=4, h=8, numPages=2）:
         Col 0  Col 1  Col 2  Col 3
Page 0: [byte] [byte] [byte] [byte]   ← 行 0-3
Page 1: [byte] [byte] [byte] [byte]   ← 行 4-7
```

**Canvas 不存储宽高信息**，因此调用 `draw()` 时必须同时传入 `w` 和 `h`。

---

## 硬件引脚定义

| 引脚宏      | 默认 GPIO | 说明                     |
|-------------|-----------|--------------------------|
| `SCLK_PIN`  | 10        | 串行时钟（屏幕 D0）      |
| `SID_PIN`   | 11        | 串行数据（屏幕 D1~D3）   |
| `CS_PIN`    | 7         | 片选（屏幕 CS）          |
| `RS_PIN`    | 6         | 数据/命令选择（屏幕 A0） |
| `RES_PIN`   | 9         | 硬件复位（屏幕 RES）     |

SPI 通信参数：`20 MHz, MSBFIRST, SPI_MODE0`

> 可在 `display_st75256.h` 中按需修改引脚编号。

---

## API 参考

### 初始化

#### `void initLCD()`

完整初始化 LCD 屏幕，包含：
1. 调用 `initU8g2()` 初始化 U8g2 库
2. 初始化硬件 SPI
3. 配置控制引脚为输出模式
4. 执行硬件复位时序
5. 发送完整的寄存器初始化序列（退出睡眠、偏压设置、灰度等级、行列地址、显示模式、对比度等）
6. 打开显示并清屏

**必须在所有绘制操作前调用一次。**

#### `void initU8g2()`

初始化 U8g2 库实例，启用 UTF-8 打印，设置绘制颜色为 1。

---

### 绘制函数

#### `void draw(const uint8_t *canvas, int x, int y, int w, int h, DrawMode mode = NOR)`

将 canvas 位图数据绘制到屏幕指定位置。

| 参数     | 说明                                              |
|----------|---------------------------------------------------|
| `canvas` | 位图数据缓冲区（page-major, column-minor, 2bpp）  |
| `x`      | 起始列坐标（0 = 最左侧）                          |
| `y`      | 起始行坐标（0 = 最顶部）                          |
| `w`      | 画布宽度（像素）                                  |
| `h`      | 画布高度（像素）                                  |
| `mode`   | 绘制模式（NOR / BG / INV）                        |

**核心特性：支持局部刷新。** 只有被覆盖的区域才会更新，不影响屏幕其他已有内容。

#### `void drawText(const char *text, int canvasX, int canvasY, int canvasW, int canvasH, const uint8_t *font, DrawMode mode = NOR, uint8_t textX = 5, uint8_t textY = 3, uint8_t maxChars = 0, ScrollTextState *scrollState = nullptr)`

在指定区域内绘制单行文本，支持超长文本自动滚动显示。

| 参数          | 说明                                                       |
|---------------|------------------------------------------------------------|
| `text`        | 文本内容（单行）                                           |
| `canvasX`     | 画布区域左上角 X                                           |
| `canvasY`     | 画布区域左上角 Y                                           |
| `canvasW`     | 画布宽度                                                   |
| `canvasH`     | 画布高度                                                   |
| `font`        | U8g2 字体指针                                              |
| `mode`        | 绘制模式                                                   |
| `textX`       | 文本在画布内的 X 偏移（默认 5）                            |
| `textY`       | 文本在画布内的 Y 偏移（默认 3）                            |
| `maxChars`    | 触发滚动的字符数阈值（0 = 不滚动，默认 0）                 |
| `scrollState` | 每行独立的滚动状态指针（`nullptr` = 使用内部单例，默认 `nullptr`） |

**滚动行为：**
- 当 `maxChars > 0` 且文本长度超过 `maxChars` 时，文本会自动向左滚动。
- 每个滚动行需要一个独立的 `ScrollTextState` 实例（声明为 `static` 或全局变量），用于记录该行的偏移量、暂停状态等。
- 多行滚动时**必须**为每行传入独立的 `scrollState` 指针；单行滚动可以不传（使用内部单例）。
- 如果传入 `nullptr`（且 `maxChars > 0`），函数会将 `text` 强制替换为 `"ERROR STATE"` 并继续执行，因此**建议始终传入有效的 `scrollState` 指针**。

**使用示例：**
```cpp
// 单行滚动（不传 scrollState，使用内部单例）
drawText("Long text that needs scrolling...", 0, 0, 64, 32,
         font_variant[0], NOR, 5, 3, 10);

// 多行独立滚动（每行一个独立的 ScrollTextState）
static ScrollTextState scrollLine1, scrollLine2;
drawText("First scrolling line...", 0, 0, 64, 32,
         font_variant[0], NOR, 5, 3, 10, &scrollLine1);
drawText("Second scrolling line...", 0, 32, 64, 32,
         font_variant[0], NOR, 5, 3, 10, &scrollLine2);

// 不滚动（maxChars = 0，不需要 scrollState）
drawText("Short", 0, 96, 64, 32, font_variant[0], NOR, 5, 3);
```

**初始化：** 在 `setup()` 中调用 `scrollTextInit()` 初始化每个 `ScrollTextState` 实例：
```cpp
static ScrollTextState scrollLine;
scrollTextInit(scrollLine);
```

**实现原理：**
1. 利用 U8g2 在内存缓冲区中渲染字体
2. 将 U8g2 的 1bpp 单色缓冲区转换为 ST75256 的 2bpp 灰阶格式
3. 调用 `draw()` 将结果输出到屏幕

可使用预定义字体数组 `font_variant[]`：

```cpp
const uint8_t *font_variant[] = {
    u8g2_font_6x10_tf,
    u8g2_font_7x13B_tf,
    u8g2_font_tenfatguys_tf,
};
```

---

### 绘制模式 `DrawMode`

| 枚举值 | 名称     | 效果                       |
|--------|----------|----------------------------|
| `NOR`  | 正常模式 | 按原样显示画布数据         |
| `BG`   | 背景模式 | 将白色（0b00）替换为浅灰   |
| `INV`  | 反转模式 | 按位取反（`~byteVal`）     |

---

### 工具函数

#### `void clean()`

清空全屏（将所有像素设置为白色）。

#### `void testGrayScale()`（声明在头文件中）

绘制 4 段垂直灰阶条，从白色到黑色依次渐变，用于验证屏幕灰度表现。

#### `void drawGrayChessboard(uint8_t bias = 0)`（声明在头文件中）

绘制灰度棋盘格图案，`bias` 参数可生成不同的棋盘样式。

#### `void drawBlock()`（声明在头文件中）

绘制一个黑色测试方块，用于验证局部刷新功能。

---

### 内部函数

#### `void sendCommand(uint8_t cmd)`

通过 SPI 向屏幕发送指令（RS 拉低）。

#### `void sendData(uint8_t data)`

通过 SPI 向屏幕发送数据（RS 拉高）。

#### `void setWindow(uint8_t xs, uint8_t xe, uint8_t ys, uint8_t ye)`

设置屏幕的写入窗口（列范围 + 页范围），之后的 `sendData()` 只影响该窗口区域。

---

## 灰度测试

### `test_GrayScale()`

全屏绘制 4 段垂直灰阶条，验证 **4 级灰阶** 的显示效果：

```
Col 0-63:   0x00 (全白)
Col 64-127: 0x55 (浅灰，每个子像素 01)
Col 128-191: 0xAA (深灰，每个子像素 10)
Col 192-255: 0xFF (全黑)
```

### `gen_GrayScale()`

生成一个 **128×128** 的灰阶测试画布，左半 64 列为白色，右半 64 列按 page 分为白色/浅灰/深灰/黑色四段。

---

## U8g2 集成

该库借助 **U8g2** 实现字体渲染，但**不依赖 U8g2 的硬件输出**：

1. U8g2 实例使用软件 SPI 构建，但仅用于在**内存缓冲区**中渲染文字
2. 通过 `u8g2.getBufferPtr()` 读取 U8g2 的 1bpp 缓冲区
3. 将 1bpp 数据转换为 ST75256 需要的 2bpp 灰阶格式
4. 通过自定义的 `draw()` 函数写入屏幕硬件

这种方式兼顾了 U8g2 丰富的字体生态和 ST75256 的灰阶能力。

---

## 图片转换工具

`tools/img2st75256.py` 用于将 **128×128 像素、4 灰阶 (2bpp)** 的二进制图片转换为 C 头文件格式，可直接在项目中使用。

### 用法

```bash
python tools/img2st75256.py input.bin output.h
```

### 输入格式

- 尺寸：**128×128 像素**
- 位深：**2bpp**（每像素 2 bit）
- 布局：**行主序**，每字节 4 像素，高位优先
- 总大小：**4096 字节**

### 输出格式

生成的 `.h` 文件可直接 `#include` 使用，数据已在 `PROGMEM` 中：

```cpp
#include "G.h"
draw(G_data, 64, 0, 128, 128, NOR);  // 居中显示
```

### 选项

| 参数       | 说明                                         |
|------------|----------------------------------------------|
| `--mode 128` | 输出 128 列宽（默认），全宽显示              |
| `--mode 256` | 输出 256 列宽，图片居中（左右各 64 列黑边） |

---

## 快速使用示例

```cpp
#include "display_st75256.h"

void setup() {
    initLCD();                              // 1. 初始化屏幕

    // 2. 测试灰度显示
    test_GrayScale();
    delay(2000);

    // 3. 绘制文字
    drawText("Hello World!", 0, 0, 256, 20,
             font_variant[1], NOR, 5, 3);

    // 3b. 滚动文字（超长文本自动滚动）
    static ScrollTextState scrollLine;
    scrollTextInit(scrollLine);
    drawText("This is a long text that will scroll automatically!",
             0, 24, 256, 20, font_variant[0], NOR, 5, 3, 15, &scrollLine);
    delay(2000);

    // 4. 绘制自定义画布
    static uint8_t buf[32 * 10];            // 10 列 × 8 页 = 32 行高
    memset(buf, 0xFF, sizeof(buf));         // 全黑
    draw(buf, 100, 50, 10, 32, NOR);        // 在 (100,50) 处绘制 10×32 黑色矩形

    // 5. 局部清除
    clean();
}
```

---

## 数据格式图解

以 `w=4, h=4`（1 个 page）为例，数据表示为：

```
像素阵列（实际像素）:
  (0,0) (1,0) (2,0) (3,0)      ← 行 0
  (0,1) (1,1) (2,1) (3,1)      ← 行 1
  (0,2) (1,2) (2,2) (3,2)      ← 行 2
  (0,3) (1,3) (2,3) (3,3)      ← 行 3

Canvas 编码（1 个 page = 4 字节）:
  canvas[0] = [px(0,0):2b][px(1,0):2b][px(2,0):2b][px(3,0):2b]
              = bits[7:6][5:4][3:2][1:0]
  canvas[1] = [px(0,1):2b][px(1,1):2b][px(2,1):2b][px(3,1):2b]
  canvas[2] = [px(0,2):2b][px(1,2):2b][px(2,2):2b][px(3,2):2b]
  canvas[3] = [px(0,3):2b][px(1,3):2b][px(2,3):2b][px(3,3):2b]
```

若图片高度 > 4 像素，则依次排列多个 page 的数据。

---

## 设计要点

| 特性               | 说明                                                  |
|--------------------|-------------------------------------------------------|
| 分辨率             | 256 × 128 像素                                        |
| 灰阶               | 4 级（白、浅灰、深灰、黑）                             |
| 像素编码           | 2 bpp，每字节 4 个垂直像素                             |
| 显存格式           | Page-major, column-minor                               |
| 局部刷新           | 支持，通过 `setWindow()` 限定写入区域                   |
| 字体渲染           | 基于 U8g2，支持多种字体                                |
| 绘制模式           | 正常（NOR）、背景（BG）、反转（INV）                    |
| 通信接口           | 4-wire Software SPI（SCLK, SID, CS, RS）+ RES          |
