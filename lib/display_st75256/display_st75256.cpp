#include "display_st75256.h"

static const SPISettings LCD_SPI_SETTINGS(20000000, MSBFIRST, SPI_MODE0);

// u8g2实例（使用软件SPI，但不直接操作硬件 - 仅用于字体渲染）
U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2(U8G2_R0,
                                        /* clock=*/SCLK_PIN,
                                        /* data=*/SID_PIN,
                                        /* cs=*/CS_PIN,
                                        /* dc=*/RS_PIN,
                                        /* reset=*/RES_PIN);

void sendCommand(uint8_t cmd)
{
    digitalWrite(RS_PIN, LOW);  // RS拉低，表示指令
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(cmd);
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

void sendData(uint8_t data)
{
    digitalWrite(RS_PIN, HIGH);  // RS拉高，表示数据
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

// 批量写入数据字节（RS=HIGH，CS 保持低位整个传输期间，效率最高）
static void sendDataBulk(const uint8_t *buf, size_t len)
{
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(RS_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    SPI.writeBytes(buf, len);
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

// 用单一值填充指定长度（分块避免大栈分配）
static void sendDataFill(uint8_t value, size_t len)
{
    static uint8_t fillBuf[256];
    memset(fillBuf, value, sizeof(fillBuf));
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(RS_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    while (len > 0)
    {
        size_t chunk = (len > sizeof(fillBuf)) ? sizeof(fillBuf) : len;
        SPI.writeBytes(fillBuf, chunk);
        len -= chunk;
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

// 屏幕初始化序列 (与并行/I2C版本的控制指令完全一致)
void initLCD()
{
    // 初始化硬件 SPI（MISO 不需要，传 -1；CS 手动控制，传 -1）
    SPI.begin(SCLK_PIN, -1, SID_PIN, -1);

    // 其余控制引脚设为输出
    pinMode(RES_PIN, OUTPUT);
    pinMode(CS_PIN, OUTPUT);
    pinMode(RS_PIN, OUTPUT);

    digitalWrite(CS_PIN, HIGH);

    // 硬件复位时序
    digitalWrite(RES_PIN, LOW);
    delay(100);
    digitalWrite(RES_PIN, HIGH);
    delay(100);

    // 开始按手册初始化寄存器
    sendCommand(0x30);  // 扩展指令 1
    sendCommand(0x94);  // 退出睡眠模式
    sendCommand(0x31);  // 扩展指令 2
    sendCommand(0xD7);  // 自动读取控制
    sendData(0x9F);

    sendCommand(0x32);  // 偏压比设置
    sendData(0x00);
    sendData(0x01);  // 升压电容频率
    sendData(0x02);  // Bias=1/12

    sendCommand(0x20);  // 灰度级设置
    sendData(0x03);
    sendData(0x05);
    sendData(0x07);
    sendData(0x09);
    sendData(0x0b);
    sendData(0x0d);
    sendData(0x10);
    sendData(0x11);
    sendData(0x11);
    sendData(0x13);
    sendData(0x15);
    sendData(0x17);
    sendData(0x19);
    sendData(0x1b);
    sendData(0x1d);
    sendData(0x1f);

    sendCommand(0x30);  // 回到扩展指令 1

    sendCommand(0x75);  // 页地址设置
    sendData(0x00);     // XS=0
    sendData(0x1F);     // XE=31 (128行在灰度模式下占32页)

    sendCommand(0x15);  // 列地址设置
    sendData(0x00);     // XS=0
    sendData(0xFF);     // XE=255

    sendCommand(0xBC);  // 行列扫描方向
    sendData(0x00);     // MX, MY=Normal
    sendData(0xA6);

    sendCommand(0xCA);  // 显示控制
    sendData(0x00);
    sendData(0x7F);  // Duty=128
    sendData(0x20);

    sendCommand(0xF0);  // 显示模式
    sendData(0x11);     // 核心设置：0x11为 4 灰阶模式

    sendCommand(0x81);  // 液晶内部电压设置 (对比度)
    sendData(0x39);     // 微调
    sendData(0x04);     // 粗调

    sendCommand(0x20);  // 电源控制
    sendData(0x0B);     // 开启内部电源电路

    delay(100);
    sendCommand(0xAF);  // 显示开
    clean();
}

// 初始化u8g2（仅用于字体渲染buffer）
void initU8g2()
{
    u8g2.begin();
    u8g2.setFont(u8g2_font_ncenB14_tr);  // 设置默认字体
    u8g2.setDrawColor(1);                // 1=前景色
}

// 设置写入窗口
void setWindow(uint8_t xs, uint8_t xe, uint8_t ys, uint8_t ye)
{
    sendCommand(0x15);  // 列地址
    sendData(xs);
    sendData(xe);
    sendCommand(0x75);  // 页地址
    sendData(ys);
    sendData(ye);
    sendCommand(0x30);
    sendCommand(0x5C);  // 写数据指令
}

// 测试 4 阶灰阶 (屏幕分为四个垂直条块)
void testGrayScale()
{
    setWindow(0, 255, 0, 31);

    // 预先生成一行数据，然后批量写入（32 页 × 1 次 writeBytes）
    static uint8_t rowBuf[256];
    for (int col = 0; col < 256; col++)
    {
        if (col < 64)
            rowBuf[col] = 0x00;  // 灰度阶 1 (纯白/全亮)
        else if (col < 128)
            rowBuf[col] = 0x55;  // 灰度阶 2
        else if (col < 192)
            rowBuf[col] = 0xAA;  // 灰度阶 3
        else
            rowBuf[col] = 0xFF;  // 灰度阶 4 (纯黑/全灭)
    }
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(RS_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    for (int page = 0; page < 32; page++)
    {
        SPI.writeBytes(rowBuf, 256);
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

void clean()
{
    setWindow(0, 255, 0, 31);       // 设置全屏窗口，并进入写数据模式（0x5C）
    sendDataFill(0x00, 256 * 128);  // 全白（灰度0）
}

void drawBlock()
{
    int col_begin = 0;
    int col_end = 10;
    int page_begin = 0;
    int page_end = 30;
    setWindow(col_begin, col_end, page_begin, page_end);
    for (int page = page_begin; page < page_end + 1; page++)
    {
        for (int col = col_begin; col < col_end + 1; col++)
        {
            sendData(0xFF);
        }
    }
}

void drawTextWithGrayscale(const char *text, int x, int y, uint8_t fgGray = 0xFF,
                           uint8_t bgGray = 0x55, uint8_t fontSize = 14,
                           const uint8_t *font = u8g2_font_ncenB14_tr)
{
    // 1. u8g2渲染到内部buffer
    u8g2.clearBuffer();
    u8g2.setFont(font);
    u8g2.drawStr(x, y, text);

    // 2. 获取u8g2的buffer
    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;  // tile宽度转像素
    int      bufHeight = u8g2.getBufferTileHeight() * 8;

    // 3. 转换为ST75256的灰度格式
    static uint8_t grayBuf[256 * 32];        // ST75256: 256列 x 32页
    memset(grayBuf, 0x00, sizeof(grayBuf));  // 背景填透明（0x00），由 draw 的 bgColor OR 填充

    // 4. 逐像素转换（u8g2是按列存储的1bpp格式）
    for (int page = 0; page < 32; page++)
    {
        for (int col = 0; col < 256; col++)
        {
            uint8_t grayPixels = 0x00;  // 4个2bit像素（ST75256每字节=4像素）

            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int y = page * 4 + subPixel;  // ST75256: 1页=4像素高
                if (y < 128 && col < bufWidth)
                {
                    // 从u8g2 buffer读取1bit
                    int u8g2Page = y / 8;
                    int u8g2Bit = y % 8;
                    int u8g2Idx = u8g2Page * bufWidth + col;

                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                    {
                        // 如果u8g2该位是1(白色)，设置为前景色
                        grayPixels &= ~(0x03 << (subPixel * 2));
                        grayPixels |= (fgGray & 0x03) << (subPixel * 2);
                    }
                }
            }
            grayBuf[page * 256 + col] = grayPixels;
        }
    }

    // 5. 写入ST75256（bgGray 作为衬底颜色，通过 OR 填充背景区域）
    draw(grayBuf, 0, 0, 256, 128, bgGray);
}

void draw(const uint8_t *data, int x, int y, int w, int h, uint8_t bgColor)
{
    int startPage = y / 4;
    int numPages = (h + 3) / 4;
    int endPage = startPage + numPages - 1;

    setWindow((uint8_t)x, (uint8_t)(x + w - 1), (uint8_t)startPage, (uint8_t)endPage);

    if (bgColor == 0x00)
    {
        // 无衬底/透明：直接整块发送，效率最高
        sendDataBulk(data, (size_t)w * numPages);
    }
    else
    {
        // 将每字节与 bgColor 进行 OR，使背景（0x00）区域呈现衬底颜色
        static uint8_t rowBuf[256];
        SPI.beginTransaction(LCD_SPI_SETTINGS);
        digitalWrite(RS_PIN, HIGH);
        digitalWrite(CS_PIN, LOW);
        for (int p = 0; p < numPages; p++)
        {
            const uint8_t *src = data + p * w;
            for (int c = 0; c < w; c++)
            {
                rowBuf[c] = src[c] | bgColor;
            }
            SPI.writeBytes(rowBuf, w);
        }
        digitalWrite(CS_PIN, HIGH);
        SPI.endTransaction();
    }
}

void drawGrayChessboard(uint8_t bias)
{
    const uint8_t blockSize = 16;              // 每个方块边长(像素)
    const uint8_t blockPages = blockSize / 4;  // 4灰阶下1页=4像素高
    const uint8_t grayTable[4] = {
        0x00,  // 白(最亮)
        0x55,  // 浅灰
        0xAA,  // 深灰
        0xFF   // 黑(最暗)
    };

    setWindow(0, 255, 0, 31);

    static uint8_t rowBuf[256];
    SPI.beginTransaction(LCD_SPI_SETTINGS);
    digitalWrite(RS_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    for (int page = 0; page < 32; page++)
    {
        const uint8_t rowBlock = page / blockPages;
        for (int col = 0; col < 256; col++)
        {
            const uint8_t colBlock = col / blockSize;
            rowBuf[col] = grayTable[(colBlock + rowBlock + bias) % 4];
        }
        SPI.writeBytes(rowBuf, 256);
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

// 将u8g2 2bit灰度值转换为ST75256的2bit格式
static inline uint8_t mapGray2bit(uint8_t u8g2Gray)
{
    // u8g2灰度: 0=黑, 1=深灰, 2=浅灰, 3=白
    // ST75256: 0xFF=黑, 0xAA=深灰, 0x55=浅灰, 0x00=白
    const uint8_t grayMap[4] = {0xFF, 0xAA, 0x55, 0x00};
    return grayMap[u8g2Gray & 0x03];
}

// 局部刷新：只在指定矩形区域内渲染文字
void drawTextInRect(const char *text, int textX, int textY, int rectX, int rectY, int rectW,
                    int rectH, uint8_t fgGray, uint8_t bgGray)
{
    // 1. 使用u8g2裁剪窗口渲染文字
    u8g2.clearBuffer();
    u8g2.setClipWindow(rectX, rectY, rectX + rectW - 1, rectY + rectH - 1);
    u8g2.setDrawColor(1);  // 前景色
    u8g2.drawStr(textX, textY, text);
    u8g2.setMaxClipWindow();  // 恢复全屏

    // 2. 获取u8g2的buffer
    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;  // tile宽度转像素

    // 3. 计算ST75256的页范围（4像素/页）
    int startPage = rectY / 4;
    int endPage = (rectY + rectH - 1) / 4;
    int startCol = rectX;
    int endCol = rectX + rectW - 1;

    // 边界检查
    if (startPage < 0)
        startPage = 0;
    if (endPage > 31)
        endPage = 31;
    if (startCol < 0)
        startCol = 0;
    if (endCol > 255)
        endCol = 255;

    // 4. 设置ST75256写入窗口
    setWindow(startCol, endCol, startPage, endPage);

    // 5. 逐页转换并发送
    static uint8_t pageBuf[256];
    for (int page = startPage; page <= endPage; page++)
    {
        for (int col = startCol; col <= endCol; col++)
        {
            uint8_t grayByte = 0x00;  // ST75256: 1字节=4个2bit像素

            // 处理这一页的4个像素
            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int     pixelY = page * 4 + subPixel;
                uint8_t pixelGray = bgGray;  // 默认背景色

                // 检查是否在矩形范围内
                if (pixelY >= rectY && pixelY < rectY + rectH && col >= rectX
                    && col < rectX + rectW)
                {
                    // 从u8g2 buffer读取1bit
                    if (col < bufWidth && pixelY < 128)
                    {
                        int u8g2Page = pixelY / 8;
                        int u8g2Bit = pixelY % 8;
                        int u8g2Idx = u8g2Page * bufWidth + col;

                        if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                        {
                            pixelGray = fgGray;  // u8g2显示1，使用前景色
                        }
                    }
                }
                else
                {
                    // 超出矩形范围，保持原内容（这里简化为背景色）
                    // 如果需要保留原内容，需要先读取屏幕数据
                    pixelGray = bgGray;
                }

                // 将2bit灰度值放入字节
                grayByte |= (pixelGray & 0x03) << (subPixel * 2);
            }

            pageBuf[col - startCol] = grayByte;
        }
        sendDataBulk(pageBuf, endCol - startCol + 1);
    }
}

// 全屏渲染文字（带灰度背景）
void drawTextWithGrayscale(const char *text, int x, int y, uint8_t fgGray, uint8_t bgGray)
{
    // 1. u8g2渲染到内部buffer
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    u8g2.drawStr(x, y, text);

    // 2. 获取u8g2的buffer
    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;

    // 3. 转换为ST75256的灰度格式
    static uint8_t grayBuf[256 * 32];
    memset(grayBuf, 0x00, sizeof(grayBuf));  // 背景填透明（0x00）

    // 4. 逐像素转换
    for (int page = 0; page < 32; page++)
    {
        for (int col = 0; col < 256; col++)
        {
            uint8_t grayByte = 0x00;

            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int     pixelY = page * 4 + subPixel;
                uint8_t pixelGray = 0x00;  // 默认透明

                if (pixelY < 128 && col < bufWidth)
                {
                    int u8g2Page = pixelY / 8;
                    int u8g2Bit = pixelY % 8;
                    int u8g2Idx = u8g2Page * bufWidth + col;

                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                    {
                        pixelGray = fgGray;
                    }
                }

                grayByte |= (pixelGray & 0x03) << (subPixel * 2);
            }

            grayBuf[page * 256 + col] = grayByte;
        }
    }

    // 5. 写入ST75256（bgGray 作为衬底颜色，通过 OR 填充背景区域）
    draw(grayBuf, 0, 0, 256, 128, bgGray);
}