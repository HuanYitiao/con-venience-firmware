#include "display_st75256.h"

static const SPISettings LCD_SPI_SETTINGS(20000000, MSBFIRST, SPI_MODE0);

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
            rowBuf[col] = 0x00;  // 灰度阶 1 (纯黑/全灭)
        else if (col < 128)
            rowBuf[col] = 0x55;  // 灰度阶 2
        else if (col < 192)
            rowBuf[col] = 0xAA;  // 灰度阶 3
        else
            rowBuf[col] = 0xFF;  // 灰度阶 4 (纯白/全亮)
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
    sendDataFill(0x00, 256 * 128);  // 全黑（灰度0）
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

void drawImage(const uint8_t *data)
{
    setWindow(0, 255, 0, 31);
    sendDataBulk(data, 256 * 32);
}

void drawGrayChessboard(uint8_t bias)
{
    const uint8_t blockSize = 16;              // 每个方块边长(像素)
    const uint8_t blockPages = blockSize / 4;  // 4灰阶下1页=4像素高
    const uint8_t grayTable[4] = {
        0x00,  // 深灰(最暗)
        0x55,  // 浅灰
        0xAA,  // 浅白
        0xFF   // 白(最亮)
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