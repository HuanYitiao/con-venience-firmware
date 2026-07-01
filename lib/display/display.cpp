#include "display.h"

#include "qrcode.h"

#define PAIRING_FRAME_INTERVAL 200

static const SPISettings DISPLAY_SPI_SETTINGS(20000000, MSBFIRST, SPI_MODE0);

// U8g2 仅用于字体取模计算，从未通过此构造函数实际发送数据到屏幕。
// 不调用 u8g2.begin()：已验证其内部会通过软件SPI真正发送ST75256初始化序列，
// 与我们手写的寄存器序列冲突。clearBuffer/setFont/drawUTF8/getBufferPtr 均为纯内存操作，
// 不依赖 begin()。
static U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2(U8G2_R0,
                                               /* clock=*/PIN_SPI_SCK,
                                               /* data=*/PIN_SPI_MOSI,
                                               /* cs=*/PIN_DISPLAY_CS,
                                               /* dc=*/PIN_DISPLAY_DC,
                                               /* reset=*/PIN_DISPLAY_RST);

static uint8_t       pairingFrame = 0;
static unsigned long lastFrameTime = 0;

static ScrollTextState scrollName;
static ScrollTextState scrollLink;
static ScrollTextState scrollMisc;

static uint8_t fullCanvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];

// ── low level ST75256 bus ───────────────────────────────────
static void sendCommand(uint8_t cmd)
{
    digitalWrite(PIN_DISPLAY_DC, LOW);
    SPI.beginTransaction(DISPLAY_SPI_SETTINGS);
    digitalWrite(PIN_DISPLAY_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(PIN_DISPLAY_CS, HIGH);
    SPI.endTransaction();
}

static void sendData(uint8_t data)
{
    digitalWrite(PIN_DISPLAY_DC, HIGH);
    SPI.beginTransaction(DISPLAY_SPI_SETTINGS);
    digitalWrite(PIN_DISPLAY_CS, LOW);
    SPI.transfer(data);
    digitalWrite(PIN_DISPLAY_CS, HIGH);
    SPI.endTransaction();
}

static void setWindow(uint8_t xs, uint8_t xe, uint8_t ys, uint8_t ye)
{
    sendCommand(0x15);
    sendData(xs);
    sendData(xe);
    sendCommand(0x75);
    sendData(ys);
    sendData(ye);
    sendCommand(0x30);
    sendCommand(0x5C);
}

// canvas 布局：numPages 行 × w 列，每字节包含4个像素（2bpp，纵向打包，MSB在上）
static void draw(const uint8_t *canvas, int x, int y, int w, int h, DrawMode mode = NOR)
{
    int startPage = y / DISPLAY_PAGE_HEIGHT;
    int endPage = (y + h - 1) / DISPLAY_PAGE_HEIGHT;
    int numPages = endPage - startPage + 1;

    setWindow((uint8_t)x, (uint8_t)(x + w - 1), (uint8_t)startPage, (uint8_t)endPage);

    for (int p = 0; p < numPages; p++)
    {
        const uint8_t *curCol = canvas + p * w;
        for (int c = 0; c < w; c++)
        {
            uint8_t byteVal = curCol[c];
            switch (mode)
            {
                case NOR:
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

// 从 fullCanvas（DISPLAY_WIDTH 宽）中抠出列范围 [colX, colX+w) 并发送到屏幕对应位置
// 用于左右分屏场景：头像和文字分别画在 fullCanvas 的不同列范围后独立发送
static void drawFromFullCanvas(int colX, int y, int w, int h)
{
    int startPage = y / DISPLAY_PAGE_HEIGHT;
    int endPage = (y + h - 1) / DISPLAY_PAGE_HEIGHT;
    int numPages = endPage - startPage + 1;

    setWindow((uint8_t)colX, (uint8_t)(colX + w - 1), (uint8_t)startPage, (uint8_t)endPage);

    for (int p = 0; p < numPages; p++)
    {
        for (int c = colX; c < colX + w; c++)
            sendData(fullCanvas[p * DISPLAY_WIDTH + c]);
    }
}

// ── canvas helpers ──────────────────────────────────────────
static void canvasClear(uint8_t *canvas, int w, int numPages)
{
    memset(canvas, 0x00, w * numPages);
}

static void canvasSetPixel(uint8_t *canvas, int canvasW, int x, int y, uint8_t grayVal)
{
    if (x < 0 || x >= canvasW || y < 0 || y >= DISPLAY_HEIGHT)
        return;
    int page = y / DISPLAY_PAGE_HEIGHT;
    int sub = y % DISPLAY_PAGE_HEIGHT;
    canvas[page * canvasW + x] |= grayVal << ((3 - sub) * 2);
}

// ── dither ──────────────────────────────────────────────────
static const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void canvasDitherLeft(uint8_t *canvas, int canvasW, int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
    {
        for (int px = x; px < x + w; px++)
        {
            int col = px - x;
            int threshold = (col * 16) / w;
            if (kBayer4[py % 4][px % 4] >= threshold)
                canvasSetPixel(canvas, canvasW, px, py, DISPLAY_BLACK);
        }
    }
}

static void canvasDitherRight(uint8_t *canvas, int canvasW, int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
    {
        for (int px = x; px < x + w; px++)
        {
            int col = (x + w - 1) - px;
            int threshold = (col * 16) / w;
            if (kBayer4[py % 4][px % 4] >= threshold)
                canvasSetPixel(canvas, canvasW, px, py, DISPLAY_BLACK);
        }
    }
}

// ── avatar ──────────────────────────────────────────────────
// avatar.bin: ST75256 DDRAM 原生格式，2bpp，page-major column-minor
// 将头像从 (startX, startY) 开始绘制到 canvas 上
// avatarRes==128 且 startX==0 时可直接整块透传（最高效且验证正确）
static void canvasBlitAvatar(uint8_t *canvas, int canvasW, int startX, int startY,
                             const Contact &contact)
{
    int avatarRes = contact.avatarResolution;

    if (avatarRes == DISPLAY_AVATAR_WIDTH && startX == 0 && startY == 0 && canvasW == DISPLAY_WIDTH)
    {
        // 直接把 avatar.bin 写入 canvas 左半区，每行步进 canvasW
        int srcBytesPerPage = avatarRes;
        for (int page = 0; page < DISPLAY_NUM_PAGES; page++)
        {
            memcpy(&canvas[page * canvasW], &contact.avatar[page * srcBytesPerPage],
                   srcBytesPerPage);
        }
        return;
    }

    // 逐像素搬运（非标准尺寸或偏移位置）
    // avatar.bin 格式：page-major, column-minor, 每字节4个sub-pixel(2bpp)
    // 每page有 avatarRes 列，每列1字节，字节内从高位到低位依次是 sub-pixel 0..3
    int srcBytesPerPage = avatarRes;

    for (int y = 0; y < avatarRes; y++)
    {
        int srcPage = y / DISPLAY_PAGE_HEIGHT;
        int srcSub = y % DISPLAY_PAGE_HEIGHT;
        for (int x = 0; x < avatarRes; x++)
        {
            int     srcByteIdx = srcPage * srcBytesPerPage + x;
            uint8_t srcByte = contact.avatar[srcByteIdx];
            uint8_t pixel = (srcByte >> ((3 - srcSub) * 2)) & 0x03;
            if (pixel == DISPLAY_WHITE)
                continue;
            int px = startX + x;
            int py = startY + y;
            if (px < 0 || px >= canvasW || py < 0 || py >= DISPLAY_HEIGHT)
                continue;
            int dstPage = py / DISPLAY_PAGE_HEIGHT;
            int dstSub = py % DISPLAY_PAGE_HEIGHT;
            canvas[dstPage * canvasW + px] |= pixel << ((3 - dstSub) * 2);
        }
    }
}

// ── text (via U8g2 取模 → canvas) ──────────────────────────
static void scrollTextInit(ScrollTextState &s)
{
    s.offset = 0;
    s.paused = true;
    s.pauseStart = millis();
    s.lastText[0] = '\0';
    s.lastMaxChars = 0;
}

static void canvasDrawText(uint8_t *canvas, int canvasW, const char *text, int regionX, int regionY,
                           int regionW, int regionH, const uint8_t *font, int textX, int textY,
                           ScrollTextState *scrollState, DrawMode mode = NOR)
{
    ScrollTextState  fallback;
    ScrollTextState &s = scrollState ? *scrollState : fallback;
    if (!scrollState)
        scrollTextInit(s);

    u8g2.setFont(font);
    int textWidth = u8g2.getUTF8Width(text);
    int availWidth = regionW - textX;
    int renderX = regionX + textX;

    bool doScroll = (textWidth > availWidth);

    if (doScroll)
    {
        if (strcmp(s.lastText, text) != 0 || s.lastMaxChars != (uint8_t)availWidth)
        {
            strncpy(s.lastText, text, sizeof(s.lastText) - 1);
            s.lastText[sizeof(s.lastText) - 1] = '\0';
            s.lastMaxChars = (uint8_t)availWidth;
            s.offset = 0;
            s.paused = true;
            s.pauseStart = millis();
        }

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
    else
    {
        scrollTextInit(s);
    }

    u8g2.clearBuffer();
    u8g2.setClipWindow(regionX, regionY, regionX + regionW - 1, regionY + regionH - 1);
    u8g2.setFont(font);
    u8g2.drawUTF8(renderX, regionY + textY + u8g2.getAscent(), text);
    u8g2.setMaxClipWindow();

    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;

    int startPage = regionY / DISPLAY_PAGE_HEIGHT;
    int endPage = (regionY + regionH - 1) / DISPLAY_PAGE_HEIGHT;
    int startCol = regionX;
    int endCol = regionX + regionW - 1;

    if (startPage < 0)
        startPage = 0;
    if (endPage > DISPLAY_NUM_PAGES - 1)
        endPage = DISPLAY_NUM_PAGES - 1;
    if (startCol < 0)
        startCol = 0;
    if (endCol > DISPLAY_WIDTH - 1)
        endCol = DISPLAY_WIDTH - 1;

    for (int page = startPage; page <= endPage; page++)
    {
        for (int col = startCol; col <= endCol; col++)
        {
            uint8_t grayByte = 0x00;
            for (int subPixel = 0; subPixel < 4; subPixel++)
            {
                int pixelY = page * DISPLAY_PAGE_HEIGHT + subPixel;
                if (pixelY >= regionY && pixelY < regionY + regionH)
                {
                    int u8g2Page = pixelY / 8;
                    int u8g2Bit = pixelY % 8;
                    int u8g2Idx = u8g2Page * bufWidth + col;
                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                        grayByte |= DISPLAY_BLACK << ((3 - subPixel) * 2);
                }
            }

            if (mode == INV)
            {
                // 高亮反色：有文字像素的位置显示白色，其余保留已有内容（黑底）
                uint8_t existing = canvas[page * canvasW + col];
                uint8_t result = existing;
                for (int sp = 0; sp < 4; sp++)
                {
                    uint8_t textPixel = (grayByte >> (sp * 2)) & 0x03;
                    if (textPixel != DISPLAY_WHITE)
                        result = (result & ~(0x03 << (sp * 2))) | (DISPLAY_WHITE << (sp * 2));
                }
                canvas[page * canvasW + col] = result;
            }
            else
            {
                canvas[page * canvasW + col] = grayByte;
            }
        }
    }
}

// ── QR ──────────────────────────────────────────────────────
static void drawQR(const char *text)
{
    QRCode  qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(8)];
    qrcode_initText(&qrcode, qrcodeData, 8, ECC_LOW, text);

    int moduleSize = DISPLAY_QR_SIZE / qrcode.size;
    if (moduleSize < 1)
        moduleSize = 1;
    int qrPixelSize = qrcode.size * moduleSize;
    int offsetX = (DISPLAY_WIDTH - qrPixelSize) / 2;
    int offsetY = (DISPLAY_HEIGHT - qrPixelSize) / 2;

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    for (int y = 0; y < qrcode.size; y++)
    {
        for (int x = 0; x < qrcode.size; x++)
        {
            if (!qrcode_getModule(&qrcode, x, y))
                continue;
            for (int dy = 0; dy < moduleSize; dy++)
                for (int dx = 0; dx < moduleSize; dx++)
                    canvasSetPixel(fullCanvas, DISPLAY_WIDTH, offsetX + x * moduleSize + dx,
                                   offsetY + y * moduleSize + dy, DISPLAY_BLACK);
        }
    }

    canvasDitherLeft(fullCanvas, DISPLAY_WIDTH, 0, 0, DISPLAY_DITHER_WIDTH, DISPLAY_HEIGHT);
    canvasDitherRight(fullCanvas, DISPLAY_WIDTH, DISPLAY_WIDTH - DISPLAY_DITHER_WIDTH, 0,
                      DISPLAY_DITHER_WIDTH, DISPLAY_HEIGHT);

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

// ── highlight box on canvas ─────────────────────────────────
static void canvasDrawHighlight(uint8_t *canvas, int canvasW, int x, int y, int w, int h)
{
    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++)
    {
        int     page = py / DISPLAY_PAGE_HEIGHT;
        int     sub = py % DISPLAY_PAGE_HEIGHT;
        uint8_t pix = DISPLAY_BLACK << ((3 - sub) * 2);
        for (int px = x; px < x + w && px < canvasW; px++)
            canvas[page * canvasW + px] |= pix;
    }
}

// ── public API ─────────────────────────────────────────────
void displayInit()
{
    u8g2.setDrawColor(1);

    pinMode(PIN_DISPLAY_RST, OUTPUT);
    pinMode(PIN_DISPLAY_CS, OUTPUT);
    pinMode(PIN_DISPLAY_DC, OUTPUT);

    digitalWrite(PIN_DISPLAY_CS, HIGH);

    digitalWrite(PIN_DISPLAY_RST, LOW);
    delay(100);
    digitalWrite(PIN_DISPLAY_RST, HIGH);
    delay(100);

    sendCommand(0x30);
    sendCommand(0x94);
    sendCommand(0x31);
    sendCommand(0xD7);
    sendData(0x9F);

    sendCommand(0x32);
    sendData(0x00);
    sendData(0x01);
    sendData(0x02);

    sendCommand(0x20);
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

    sendCommand(0x30);
    sendCommand(0x75);
    sendData(0x00);
    sendData(0x1F);
    sendCommand(0x15);
    sendData(0x00);
    sendData(0xFF);
    sendCommand(0xBC);
    sendData(0x00);
    sendData(0xA6);
    sendCommand(0xCA);
    sendData(0x00);
    sendData(0x7F);
    sendData(0x20);
    sendCommand(0xF0);
    sendData(0x11);
    sendCommand(0x81);
    sendData(0x39);
    sendData(0x04);
    sendCommand(0x20);
    sendData(0x0B);

    delay(100);
    sendCommand(0xAF);

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void displayClearAll()
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void displayResetScroll()
{
    scrollTextInit(scrollName);
    scrollTextInit(scrollLink);
    scrollTextInit(scrollMisc);
}

// ── draw functions ──────────────────────────────────────────
void drawHomepage(const Contact &self)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    int startX = (DISPLAY_WIDTH - self.avatarResolution) / 2;
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, startX, 0, self);

    canvasDitherLeft(fullCanvas, DISPLAY_WIDTH, 0, 0, DISPLAY_DITHER_WIDTH, DISPLAY_HEIGHT);
    canvasDitherRight(fullCanvas, DISPLAY_WIDTH, DISPLAY_WIDTH - DISPLAY_DITHER_WIDTH, 0,
                      DISPLAY_DITHER_WIDTH, DISPLAY_HEIGHT);

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawPairing(const Contact &self)
{
    unsigned long now = millis();
    if (now - lastFrameTime > PAIRING_FRAME_INTERVAL)
    {
        pairingFrame = (pairingFrame + 1) % 4;
        lastFrameTime = now;
    }

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, self);

    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;

    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    for (int i = 0; i < 3; i++)
    {
        int frameOffset = (pairingFrame + i) % 4;
        int r = 20 + frameOffset * 6;
        if (r > 0 && r < 40)
            u8g2.drawCircle(cx, cy, r, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
    }

    uint8_t *u8g2Buf = u8g2.getBufferPtr();
    int      bufWidth = u8g2.getBufferTileWidth() * 8;

    for (int page = 0; page < DISPLAY_NUM_PAGES; page++)
    {
        for (int col = cx; col < DISPLAY_WIDTH; col++)
        {
            for (int subPixel = 0; subPixel < DISPLAY_PAGE_HEIGHT; subPixel++)
            {
                int pixelY = page * DISPLAY_PAGE_HEIGHT + subPixel;
                if (pixelY >= DISPLAY_HEIGHT)
                    continue;
                int u8g2Page = pixelY / 8;
                int u8g2Bit = pixelY % 8;
                int u8g2Idx = u8g2Page * bufWidth + col;
                if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                    fullCanvas[page * DISPLAY_WIDTH + col] |= DISPLAY_BLACK << ((3 - subPixel) * 2);
            }
        }
    }

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawContactCard(const Contact &contact)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, contact);
    drawFromFullCanvas(0, 0, DISPLAY_AVATAR_WIDTH, DISPLAY_AVATAR_HEIGHT);

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.name, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 16,
                   u8g2_font_7x13B_tf, 4, 0, &scrollName);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.links[0].url, DISPLAY_UI_X, 16,
                   DISPLAY_UI_WIDTH, 14, u8g2_font_6x10_tf, 4, 0, &scrollLink);
    drawFromFullCanvas(DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, DISPLAY_HEIGHT);
}

void drawMenu(bool menuSelection)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    if (menuSelection)
        canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, 20, DISPLAY_WIDTH, 14);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Friends", 0, 20, DISPLAY_WIDTH, 14,
                   u8g2_font_6x10_tf, 8, 2, nullptr, menuSelection ? INV : NOR);

    if (!menuSelection)
        canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, 36, DISPLAY_WIDTH, 14);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "My Profile", 0, 36, DISPLAY_WIDTH, 14,
                   u8g2_font_6x10_tf, 8, 2, nullptr, !menuSelection ? INV : NOR);

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawContactList(const char names[][NAME_LEN], int count, int index)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    const int lineH = 14;
    const int visibleLines = DISPLAY_HEIGHT / lineH;
    int       listOffset = 0;
    if (index >= visibleLines)
        listOffset = index - visibleLines + 1;

    for (int i = 0; i < visibleLines && (i + listOffset) < count; i++)
    {
        int  ci = i + listOffset;
        int  y = i * lineH;
        bool selected = (ci == index);

        if (selected)
            canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, y, DISPLAY_WIDTH, lineH);

        canvasDrawText(fullCanvas, DISPLAY_WIDTH, names[ci], 0, y, DISPLAY_WIDTH, lineH,
                       u8g2_font_6x10_tf, 4, 2, selected ? nullptr : &scrollMisc,
                       selected ? INV : NOR);
    }

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawProfileAvatar(const Contact &contact)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, contact);
    drawFromFullCanvas(0, 0, DISPLAY_AVATAR_WIDTH, DISPLAY_AVATAR_HEIGHT);

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 20);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.name, DISPLAY_UI_X, 3, DISPLAY_UI_WIDTH, 20,
                   u8g2_font_8x13B_tf, 4, 2, &scrollName, INV);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.species, DISPLAY_UI_X, 26, DISPLAY_UI_WIDTH,
                   14, u8g2_font_7x13B_tf, 4, 2, &scrollLink);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.from, DISPLAY_UI_X, 42, DISPLAY_UI_WIDTH, 14,
                   u8g2_font_7x13B_tf, 4, 2, &scrollMisc);
    drawFromFullCanvas(DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, DISPLAY_HEIGHT);
}

void drawProfileLinks(const Contact &contact, int linkIndex)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    const int lineH = 14;
    int       clampedIndex = min(linkIndex, (int)contact.linkCount - 1);

    for (int i = 0; i < contact.linkCount; i++)
    {
        int  y = i * lineH;
        bool selected = (i == clampedIndex);

        if (selected)
            canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, y, DISPLAY_WIDTH, lineH);

        canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.links[i].tag, 0, y, DISPLAY_WIDTH, lineH,
                       u8g2_font_6x10_tf, 4, 2, nullptr, selected ? INV : NOR);
    }

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawProfileQR(const Contact &contact, int linkIndex)
{
    drawQR(contact.links[linkIndex].url);
}

void drawStandby()
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void drawLowBattery()
{
    u8g2.setFont(u8g2_font_7x13B_tf);
    int w1 = u8g2.getUTF8Width("Low Battery");
    u8g2.setFont(u8g2_font_6x10_tf);
    int w2 = u8g2.getUTF8Width("Please charge");

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Low Battery", (DISPLAY_WIDTH - w1) / 2, 40,
                   DISPLAY_WIDTH, 18, u8g2_font_7x13B_tf, 0, 0, nullptr);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Please charge", (DISPLAY_WIDTH - w2) / 2, 60,
                   DISPLAY_WIDTH, 16, u8g2_font_6x10_tf, 0, 0, nullptr);

    draw(fullCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

void displayRender(state_t state, const Contact &self, const Contact &currentContact,
                   const char contactNames[][NAME_LEN], int contactCount, int contactIndex,
                   bool menuSelection, bool idleShowQR, const Contact &profileContact,
                   int linkIndex)
{
    switch (state)
    {
        case STATE_IDLE:
            if (idleShowQR)
                drawProfileQR(self, 0);
            else
                drawHomepage(self);
            break;
        case STATE_PAIRING:
            drawPairing(self);
            break;
        case STATE_CONTACT_CARD:
            drawContactCard(currentContact);
            break;
        case STATE_MENU:
            drawMenu(menuSelection);
            break;
        case STATE_CONTACT_LIST:
            drawContactList(contactNames, contactCount, contactIndex);
            break;
        case STATE_PROFILE_AVATAR:
            drawProfileAvatar(profileContact);
            break;
        case STATE_PROFILE_LINKS:
            drawProfileLinks(profileContact, linkIndex);
            break;
        case STATE_PROFILE_QR:
            drawProfileQR(profileContact, linkIndex);
            break;
        case STATE_STANDBY:
            drawStandby();
            break;
        case STATE_LOW_BATTERY:
            drawLowBattery();
            break;
    }
}