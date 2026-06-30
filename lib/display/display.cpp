#include "display.h"

#include "qrcode.h"

#define PAIRING_FRAME_INTERVAL 200

static const SPISettings DISPLAY_SPI_SETTINGS(20000000, MSBFIRST, SPI_MODE0);

// U8g2 在这里仅用于字体取模计算，从未通过此构造函数实际发送数据到屏幕；
// 真正的显示数据由下方 sendCommand/sendData/draw 走硬件 SPI 总线发出。
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

static uint8_t avatarCanvas[DISPLAY_AVATAR_WIDTH * DISPLAY_NUM_PAGES];
static uint8_t uiCanvas[DISPLAY_UI_WIDTH * DISPLAY_NUM_PAGES];

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

// canvas: numPages 行 × w 列, 每字节为一组 4 个像素(2bpp 灰阶纵向打包)
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

static void cleanRegion(uint8_t *canvas, int x, int w)
{
    memset(canvas, 0x00, w * DISPLAY_NUM_PAGES);
    draw(canvas, x, 0, w, DISPLAY_HEIGHT, NOR);
}

static void cleanAvatar()
{
    cleanRegion(avatarCanvas, 0, DISPLAY_AVATAR_WIDTH);
}

static void cleanUI()
{
    cleanRegion(uiCanvas, DISPLAY_UI_X, DISPLAY_UI_WIDTH);
}

// ── scrolling text helper ───────────────────────────────────
static void scrollTextInit(ScrollTextState &s)
{
    s.offset = 0;
    s.paused = true;
    s.pauseStart = millis();
    s.lastText[0] = '\0';
    s.lastMaxChars = 0;
}

// 在 uiCanvas 内某个矩形区域绘制文字(可滚动)，通过 U8g2 取模后转写入 uiCanvas
static void drawTextUI(const char *text, int regionX, int regionY, int regionW, int regionH,
                       const uint8_t *font, uint8_t textX, uint8_t textY, uint8_t maxChars,
                       ScrollTextState *scrollState, DrawMode mode = NOR)
{
    ScrollTextState  fallback;
    ScrollTextState &s = scrollState ? *scrollState : fallback;
    if (!scrollState)
        scrollTextInit(s);

    bool doScroll = (maxChars > 0 && strlen(text) > maxChars);
    int  renderX = regionX + textX;

    if (doScroll)
    {
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
        int availWidth = regionW - textX;

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
    if (startCol < DISPLAY_UI_X)
        startCol = DISPLAY_UI_X;
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
                    int u8g2Idx = u8g2Page * bufWidth + (col - DISPLAY_UI_X);
                    if (u8g2Buf[u8g2Idx] & (1 << u8g2Bit))
                        grayByte |= DISPLAY_BLACK << ((3 - subPixel) * 2);
                }
            }
            int localCol = col - DISPLAY_UI_X;
            uiCanvas[page * DISPLAY_UI_WIDTH + localCol] = grayByte;
        }
    }

    draw(uiCanvas + startPage * DISPLAY_UI_WIDTH + (startCol - DISPLAY_UI_X), startCol,
         startPage * DISPLAY_PAGE_HEIGHT, endCol - startCol + 1,
         (endPage - startPage + 1) * DISPLAY_PAGE_HEIGHT, mode);
}

// ── avatar (left region) ─────────────────────────────────────
// avatar.bin: ST75256 DDRAM 原生格式, 2bpp, 一字节=同一page内4个横向像素(aabbccdd)
// 当 avatarResolution == DISPLAY_AVATAR_WIDTH(128) 时可直接整块透传给 draw()
// 当分辨率更小(居中显示)时, 需要按 2bit 像素逐个搬运到画布对应位置
static void drawAvatarBitmap(const Contact &contact, int regionX)
{
    int avatarRes = contact.avatarResolution;

    if (avatarRes == DISPLAY_AVATAR_WIDTH)
    {
        draw(contact.avatar, regionX, 0, DISPLAY_AVATAR_WIDTH, DISPLAY_AVATAR_HEIGHT, NOR);
        return;
    }

    int offsetX = (DISPLAY_AVATAR_WIDTH - avatarRes) / 2;
    int offsetY = (DISPLAY_AVATAR_HEIGHT - avatarRes) / 2;
    int srcBytesPerRow = avatarRes / 4;

    memset(avatarCanvas, 0x00, sizeof(avatarCanvas));

    for (int y = 0; y < avatarRes; y++)
    {
        int srcPage = y / DISPLAY_PAGE_HEIGHT;
        int srcSub = y % DISPLAY_PAGE_HEIGHT;

        for (int x = 0; x < avatarRes; x++)
        {
            int     srcByteIdx = srcPage * srcBytesPerRow + (x / DISPLAY_PAGE_HEIGHT);
            uint8_t srcByte = contact.avatar[srcByteIdx];
            int     srcSubX = x % DISPLAY_PAGE_HEIGHT;
            uint8_t pixel = (srcByte >> ((3 - srcSubX) * 2)) & 0x03;

            int px = offsetX + x;
            int py = offsetY + y;
            if (px < 0 || px >= DISPLAY_AVATAR_WIDTH || py < 0 || py >= DISPLAY_AVATAR_HEIGHT)
                continue;

            int dstPage = py / DISPLAY_PAGE_HEIGHT;
            int dstSub = py % DISPLAY_PAGE_HEIGHT;
            avatarCanvas[dstPage * DISPLAY_AVATAR_WIDTH + px] |= pixel << ((3 - dstSub) * 2);
            (void)srcSub;
        }
    }

    draw(avatarCanvas, regionX, 0, DISPLAY_AVATAR_WIDTH, DISPLAY_AVATAR_HEIGHT, NOR);
}

// ── QR code (full screen, centered, 128x128) ─────────────────
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

    static uint8_t qrCanvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];
    memset(qrCanvas, 0x00, sizeof(qrCanvas));

    for (int y = 0; y < qrcode.size; y++)
    {
        for (int x = 0; x < qrcode.size; x++)
        {
            if (!qrcode_getModule(&qrcode, x, y))
                continue;
            for (int dy = 0; dy < moduleSize; dy++)
            {
                for (int dx = 0; dx < moduleSize; dx++)
                {
                    int px = offsetX + x * moduleSize + dx;
                    int py = offsetY + y * moduleSize + dy;
                    if (px < 0 || px >= DISPLAY_WIDTH || py < 0 || py >= DISPLAY_HEIGHT)
                        continue;
                    int page = py / DISPLAY_PAGE_HEIGHT;
                    int sub = py % DISPLAY_PAGE_HEIGHT;
                    qrCanvas[page * DISPLAY_WIDTH + px] |= DISPLAY_BLACK << ((3 - sub) * 2);
                }
            }
        }
    }

    draw(qrCanvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
}

// ── public API ─────────────────────────────────────────────
void displayInit()
{
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

    cleanAvatar();
    cleanUI();

    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setDrawColor(1);
}

// 临时验证函数：直接用真实 Contact 数据测试头像渲染路径是否正确
// 确认无误后可以删除此函数及 main.cpp 中对它的调用
void displayTestAvatar(const Contact &self)
{
    Serial0.println("Display test: rendering real avatar");
    drawAvatarBitmap(self, 0);
    delay(3000);
    Serial0.println("Display test: avatar render done");
}

// 临时验证函数：整屏填充 0x00 (理论上应为全白)
// 用于验证是否卡在单色模式 —— 若单色模式下非零bit即显示为黑,
// 那么全0填充应该是唯一能验证"真的能显示白"的方式
void displayTestAllWhite()
{
    Serial0.println("Display test: filling white (0x00)");
    static uint8_t canvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];
    memset(canvas, 0x00, sizeof(canvas));
    draw(canvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
    delay(3000);
    Serial0.println("Display test: white fill done");
}

void displayTestAllBlack()
{
    Serial0.println("Display test: filling black (0x11)");
    static uint8_t canvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];
    memset(canvas, 0x11, sizeof(canvas));
    draw(canvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
    delay(3000);
    Serial0.println("Display test: black fill done");
}

// 临时验证函数：4级灰度棋盘测试
// 屏幕分成左右两半(各128宽), 每半再分上下两半(各64高)
// 左上=白(00) 右上=浅灰(01) 左下=深灰(10) 右下=黑(11)
// 用于肉眼核对实际显示的灰度顺序是否和代码假设一致
void displayTestGrayChessboard()
{
    Serial0.println("Display test: gray chessboard");

    static uint8_t canvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];

    for (int page = 0; page < DISPLAY_NUM_PAGES; page++)
    {
        int  y = page * DISPLAY_PAGE_HEIGHT;
        bool topHalf = (y < DISPLAY_HEIGHT / 2);

        for (int col = 0; col < DISPLAY_WIDTH; col++)
        {
            bool leftHalf = (col < DISPLAY_WIDTH / 2);

            uint8_t pixel;
            if (leftHalf && topHalf)
                pixel = DISPLAY_WHITE;
            else if (!leftHalf && topHalf)
                pixel = DISPLAY_LIGHT_GRAY;
            else if (leftHalf && !topHalf)
                pixel = DISPLAY_DARK_GRAY;
            else
                pixel = DISPLAY_BLACK;

            uint8_t byteVal = 0;
            byteVal |= pixel << 6;
            byteVal |= pixel << 4;
            byteVal |= pixel << 2;
            byteVal |= pixel << 0;

            canvas[page * DISPLAY_WIDTH + col] = byteVal;
        }
    }

    draw(canvas, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, NOR);
    Serial0.println(
        "Display test: chessboard drawn (TL=white TR=light-gray BL=dark-gray BR=black)");
}

void displayResetScroll()
{
    scrollTextInit(scrollName);
    scrollTextInit(scrollLink);
    scrollTextInit(scrollMisc);
}

void drawHomepage(const Contact &self)
{
    drawAvatarBitmap(self, 0);
    cleanUI();
}

void drawPairing(const Contact &self)
{
    unsigned long now = millis();
    if (now - lastFrameTime > PAIRING_FRAME_INTERVAL)
    {
        pairingFrame = (pairingFrame + 1) % 4;
        lastFrameTime = now;
    }

    drawAvatarBitmap(self, 0);

    char buf[16];
    snprintf(buf, sizeof(buf), "Pairing%.*s", pairingFrame, "...");
    drawTextUI(buf, DISPLAY_UI_X, 50, DISPLAY_UI_WIDTH, 20, u8g2_font_7x13B_tf, 8, 0, 0, nullptr);
}

void drawContactCard(const Contact &contact)
{
    drawAvatarBitmap(contact, 0);
    drawTextUI(contact.name, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 20, u8g2_font_7x13B_tf, 4, 0, 12,
               &scrollName);
    drawTextUI(contact.links[0].url, DISPLAY_UI_X, 20, DISPLAY_UI_WIDTH, 16, u8g2_font_6x10_tf, 4,
               0, 18, &scrollLink);
}

void drawMenu(bool menuSelection)
{
    cleanUI();
    drawTextUI("Friends", DISPLAY_UI_X, 20, DISPLAY_UI_WIDTH, 16, u8g2_font_6x10_tf, 8, 0, 0,
               nullptr, menuSelection ? BG : NOR);
    drawTextUI("My Profile", DISPLAY_UI_X, 36, DISPLAY_UI_WIDTH, 16, u8g2_font_6x10_tf, 8, 0, 0,
               nullptr, !menuSelection ? BG : NOR);
}

void drawContactList(const char names[][NAME_LEN], int count, int index)
{
    cleanUI();
    const int lineH = 12;
    const int visibleLines = DISPLAY_UI_HEIGHT / lineH;
    int       listOffset = 0;
    if (index >= visibleLines)
        listOffset = index - visibleLines + 1;

    for (int i = 0; i < visibleLines && (i + listOffset) < count; i++)
    {
        int  ci = i + listOffset;
        int  y = i * lineH;
        bool selected = (ci == index);
        drawTextUI(names[ci], DISPLAY_UI_X, y, DISPLAY_UI_WIDTH, lineH, u8g2_font_6x10_tf, 4, 1,
                   selected ? 0 : 18, selected ? nullptr : &scrollMisc, selected ? BG : NOR);
    }
}

void drawProfileAvatar(const Contact &contact)
{
    drawAvatarBitmap(contact, 0);
    drawTextUI(contact.name, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 16, u8g2_font_7x13B_tf, 4, 0, 12,
               &scrollName);
    drawTextUI(contact.species, DISPLAY_UI_X, 16, DISPLAY_UI_WIDTH, 14, u8g2_font_6x10_tf, 4, 0, 18,
               &scrollLink);
    drawTextUI(contact.from, DISPLAY_UI_X, 30, DISPLAY_UI_WIDTH, 14, u8g2_font_6x10_tf, 4, 0, 18,
               &scrollMisc);
}

void drawProfileLinks(const Contact &contact, int linkIndex)
{
    cleanUI();
    const int lineH = 12;
    int       clampedIndex = min(linkIndex, (int)contact.linkCount - 1);

    for (int i = 0; i < contact.linkCount; i++)
    {
        int  y = i * lineH;
        bool selected = (i == clampedIndex);
        drawTextUI(contact.links[i].tag, DISPLAY_UI_X, y, DISPLAY_UI_WIDTH, lineH,
                   u8g2_font_6x10_tf, 4, 1, 0, nullptr, selected ? BG : NOR);
    }
}

void drawProfileQR(const Contact &contact, int linkIndex)
{
    drawQR(contact.links[linkIndex].url);
}

void drawStandby()
{
    cleanAvatar();
    cleanUI();
}

void drawLowBattery()
{
    cleanAvatar();
    drawTextUI("Low Battery", DISPLAY_UI_X, 40, DISPLAY_UI_WIDTH, 18, u8g2_font_7x13B_tf, 4, 0, 0,
               nullptr);
    drawTextUI("Please charge", DISPLAY_UI_X, 60, DISPLAY_UI_WIDTH, 16, u8g2_font_6x10_tf, 4, 0, 0,
               nullptr);
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