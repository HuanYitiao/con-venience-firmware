#include "display_st75256.h"

static const SPISettings LCD_SPI_SETTINGS(20000000, MSBFIRST, SPI_MODE0);

// u8g2实例（使用软件SPI，但不直接操作硬件 - 仅用于字体渲染）
U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2(U8G2_R0,
                                        /* clock=*/SCLK_PIN,
                                        /* data=*/SID_PIN,
                                        /* cs=*/CS_PIN,
                                        /* dc=*/RS_PIN,
                                        /* reset=*/RES_PIN);

const uint8_t *font_variant[] = {
    u8g2_font_6x10_tf,
    u8g2_font_7x13B_tf,
};

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

void initU8g2()
{
    u8g2.begin();
    u8g2.setDrawColor(1);  // 1=前景色
}

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

void draw(const uint8_t *canvas, int x, int y, int w, int h, uint8_t bg)
{
    int startPage = y / 4;
    int endPage = (y + h - 1) / 4;
    int numPages = endPage - startPage + 1;

    setWindow((uint8_t)x, (uint8_t)(x + w - 1), (uint8_t)startPage, (uint8_t)endPage);

    for (int p = 0; p < numPages; p++)
    {
        const uint8_t *cur_col = canvas + p * w;
        for (int c = 0; c < w; c++)
        {
            sendData(cur_col[c] | bg);
        }
    }
}

void clean()
{
    uint8_t *canvas = new uint8_t[256 * 32];
    draw(canvas, 0, 0, 256, 128, DISPLAY_WHITE);
    delete[] canvas;
}

void drawText(const char *text, int textX, int textY, int rectX, int rectY, int rectW, int rectH,
              uint8_t fgGray, uint8_t bgGray, const uint8_t *font)
{
    // 1. 裁剪窗口渲染
    u8g2.clearBuffer();
    u8g2.setClipWindow(rectX, rectY, rectX + rectW - 1, rectY + rectH - 1);
    u8g2.setFont(font);
    u8g2.drawStr(textX, textY, text);
    u8g2.setMaxClipWindow();

    // 2. 获取u8g2 buffer
    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;

    // 3. 计算ST75256页/列范围，并裁剪到屏幕边界
    int startPage = rectY / 4;
    int endPage = (rectY + rectH - 1) / 4;
    int startCol = rectX;
    int endCol = rectX + rectW - 1;

    if (startPage < 0)
        startPage = 0;
    if (endPage > 31)
        endPage = 31;
    if (startCol < 0)
        startCol = 0;
    if (endCol > 255)
        endCol = 255;

    int numPages = endPage - startPage + 1;
    int numCols = endCol - startCol + 1;

    // 4. 动态分配仅覆盖rect的画布；零初始化 = 全透明背景
    uint8_t *canvas = new uint8_t[numCols * numPages]();

    for (int page = startPage; page <= endPage; page++)
    {
        for (int col = startCol; col <= endCol; col++)
        {
            uint8_t grayByte = 0x00;

            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int pixelY = page * 4 + subPixel;

                // 仅处理rect高度范围内的像素（页边界可能超出）
                if (pixelY >= rectY && pixelY < rectY + rectH)
                {
                    int u8g2Page = pixelY / 8;
                    int u8g2Bit = pixelY % 8;
                    int u8g2Idx = u8g2Page * bufWidth + col;

                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                        grayByte |= (fgGray & 0x03) << (subPixel * 2);
                }
            }

            canvas[(page - startPage) * numCols + (col - startCol)] = grayByte;
        }
    }

    // 5. draw() 通过 bgGray OR 填充背景区域
    draw(canvas, startCol, startPage * 4, numCols, numPages * 4, bgGray);
    delete[] canvas;
}

void test_GrayScale()
{
    static uint8_t buf[256 * 32];
    for (int col = 0; col < 256; col++)
    {
        uint8_t val;
        if (col < 64)
            val = 0x00;  // 灰度阶 1 (纯白/全亮)
        else if (col < 128)
            val = 0x55;  // 灰度阶 2
        else if (col < 192)
            val = 0xAA;  // 灰度阶 3
        else
            val = 0xFF;  // 灰度阶 4 (纯黑/全灭)
        for (int page = 0; page < 32; page++)
            buf[page * 256 + col] = val;
    }
    draw(buf, 0, 0, 256, 128, DISPLAY_WHITE);
}

void test_drawBlock()
{
    int            col_begin = 0;
    int            col_end = 10;
    int            page_begin = 0;
    int            page_end = 40;
    int            w = col_end - col_begin + 1;
    int            numPages = page_end - page_begin + 1;
    static uint8_t buf[11 * 41];
    memset(buf, 0xFF, sizeof(buf));
    draw(buf, col_begin, page_begin * 4, w, numPages * 4, DISPLAY_WHITE);
}