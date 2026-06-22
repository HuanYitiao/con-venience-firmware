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
    u8g2_font_7x13B_tf,
    u8g2_font_8x13B_tf,
    u8g2_font_tenfatguys_tf,
    u8g2_font_pressstart2p_8f,
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
    initU8g2();
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
    sendData(0x01);
    sendData(0x03);
    sendData(0x05);
    sendData(0x07);
    sendData(0x09);
    sendData(0x0b);
    sendData(0x0d);
    sendData(0x0f);
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
    u8g2.enableUTF8Print();
    u8g2.setDrawColor(1);
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

void draw(const uint8_t *canvas, int x, int y, int w, int h, DrawMode mode)
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
            uint8_t byteVal = cur_col[c];
            switch (mode)
            {
                case NOR:  // DRAW_NORMAL
                    sendData(byteVal);
                    break;
                case INV:
                    sendData(~byteVal);
                    break;
                case BG:
                {
                    uint8_t result = 0;
                    for (int sp = 0; sp < 4; sp++)
                    {
                        uint8_t sub = (byteVal >> (sp * 2)) & 0x03;
                        if (sub == DISPLAY_WHITE)
                            sub = DISPLAY_LIGHT_GRAY;
                        result |= (sub << (sp * 2));
                    }
                    sendData(result);
                    break;
                }
            }
        }
    }
}

void clean()
{
    uint8_t *canvas = new uint8_t[256 * 32];
    memset(canvas, 0x00, 256 * 32);
    draw(canvas, 0, 0, 256, 128, NOR);
    delete[] canvas;
}

void scrollTextInit(ScrollTextState &s)
{
    s.offset = 0;
    s.paused = true;
    s.pauseStart = millis();
    s.lastText[0] = '\0';
    s.lastMaxChars = 0;
}

void drawText(const char *text, int canvasX, int canvasY, int canvasW, int canvasH,
              const uint8_t *font, DrawMode mode, uint8_t textX, uint8_t textY, uint8_t maxChars,
              ScrollTextState *scrollState)
{
    if (!scrollState)
    {
        text = "ERROR STATE";
    };

    size_t textLen = strlen(text);

    // ── Determine scroll state ─────────────────────────
    bool doScroll = (maxChars > 0 && textLen > maxChars);

    static ScrollTextState s_internal;  // fallback for single-line scroll
    ScrollTextState       &s = scrollState ? *scrollState : s_internal;

    int renderX = canvasX + textX;

    if (doScroll)
    {
        // Detect text or maxChars change → reset scroll
        if (strcmp(s.lastText, text) != 0 || s.lastMaxChars != maxChars)
        {
            strncpy(s.lastText, text, sizeof(s.lastText) - 1);
            s.lastText[sizeof(s.lastText) - 1] = '\0';
            s.lastMaxChars = maxChars;
            s.offset = 0;
            s.paused = true;
            s.pauseStart = millis();
        }

        u8g2.setFont(font);
        int textWidth = u8g2.getUTF8Width(text);
        int availWidth = canvasW - textX;

        if (textWidth > availWidth)
        {
            unsigned long now = millis();

            if (s.paused)
            {
                if (now - s.pauseStart >= SCROLL_TEXT_PAUSE)
                {
                    s.paused = false;
                    s.offset = 0;
                    s.lastTime = now;
                }
            }
            else
            {
                if (now - s.lastTime >= SCROLL_TEXT_INTERVAL)
                {
                    s.offset++;
                    s.lastTime = now;
                    if (s.offset >= textWidth - availWidth)
                    {
                        s.paused = true;
                        s.pauseStart = now;
                    }
                }
            }
            renderX -= s.offset;
        }
    }
    else
    {
        // No scroll needed → reset state if one was provided
        if (scrollState)
            scrollTextInit(s);
    }

    // ── Render via u8g2 ────────────────────────────────
    u8g2.clearBuffer();
    u8g2.setClipWindow(canvasX, canvasY, canvasX + canvasW - 1, canvasY + canvasH - 1);
    u8g2.setFont(font);
    u8g2.drawUTF8(renderX, canvasY + textY + u8g2.getAscent(), text);
    u8g2.setMaxClipWindow();

    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;

    int startPage = canvasY / 4;
    int endPage = (canvasY + canvasH - 1) / 4;
    int startCol = canvasX;
    int endCol = canvasX + canvasW - 1;

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

    uint8_t *canvas = new uint8_t[numCols * numPages]();

    for (int page = startPage; page <= endPage; page++)
    {
        for (int col = startCol; col <= endCol; col++)
        {
            uint8_t grayByte = 0x00;

            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int pixelY = page * 4 + subPixel;

                if (pixelY >= canvasY && pixelY < canvasY + canvasH)
                {
                    int u8g2Page = pixelY / 8;
                    int u8g2Bit = pixelY % 8;
                    int u8g2Idx = u8g2Page * bufWidth + col;

                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                        grayByte |= DISPLAY_BLACK << ((3 - subPixel) * 2);
                }
            }

            canvas[(page - startPage) * numCols + (col - startCol)] = grayByte;
        }
    }

    draw(canvas, startCol, startPage * 4, numCols, numPages * 4, mode);
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
    draw(buf, 0, 0, 256, 128, NOR);
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
    draw(buf, col_begin, page_begin * 4, w, numPages * 4, NOR);
}

uint8_t *gen_GrayScale()
{
    static uint8_t buf[128 * 32];  // 128列 × 32页 = 4096 bytes
    for (int page = 0; page < 32; page++)
    {
        uint8_t val;
        if (page < 8)
            val = 0x00;
        else if (page < 16)
            val = 0x55;
        else if (page < 24)
            val = 0xAA;
        else
            val = 0xFF;

        for (int col = 0; col < 128; col++)
        {
            if (col < 64)
            {
                buf[page * 128 + col] = 0x00;
            }
            else
            {
                buf[page * 128 + col] = val;
            }
        }
    }
    return buf;
}