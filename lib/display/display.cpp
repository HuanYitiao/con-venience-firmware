#include "display.h"

#include "io_expander.h"
#include "qrcode.h"
#include "wifi_config.h"

#define PAIRING_FRAME_INTERVAL 200

// Datasheet table 7: 4-wire SPI clock period tSCYC >= 80ns (<= 12.5MHz); 80MHz / 8 = 10MHz
static const SPISettings DISPLAY_SPI_SETTINGS(10000000, MSBFIRST, SPI_MODE0);

// U8g2 is only used to rasterize fonts into its RAM buffer; it never talks to the panel.
// Do not call u8g2.begin(): it bit-bangs the full ST75256 init sequence over software SPI,
// which conflicts with our own register setup. clearBuffer/setFont/drawUTF8/getBufferPtr
// are pure memory operations and do not need begin().
static U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2(U8G2_R0,
                                               /* clock=*/PIN_SPI_SCK,
                                               /* data=*/PIN_SPI_MOSI,
                                               /* cs=*/PIN_DISPLAY_CS,
                                               /* dc=*/PIN_DISPLAY_DC,
                                               /* reset=*/U8X8_PIN_NONE);

static uint8_t       pairingFrame = 0;
static unsigned long lastFrameTime = 0;

static ScrollTextState scrollName;
static ScrollTextState scrollLink;
static ScrollTextState scrollMisc;

static uint8_t fullCanvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];

// Mirror of the panel's DDRAM. The ST75256 refreshes the glass from DDRAM on its own,
// so only the parts of fullCanvas that differ from this mirror need to be sent.
static uint8_t shadowCanvas[DISPLAY_WIDTH * DISPLAY_NUM_PAGES];

// When true, flushCanvas ignores the mirror and resends everything (init and page changes,
// so a corrupted DDRAM heals itself)
static bool forceFlush = false;

// ── low level ST75256 bus ───────────────────────────────────
// CS stays low for a whole busBegin/busEnd span; command vs. data is selected by A0 (DC)
// alone (datasheet fig. 3). SPI.write/writeBytes block until the last bit is shifted out,
// so DC can be toggled right after they return.
static void busBegin()
{
    SPI.beginTransaction(DISPLAY_SPI_SETTINGS);
    digitalWrite(PIN_DISPLAY_CS, LOW);
}

static void busEnd()
{
    digitalWrite(PIN_DISPLAY_CS, HIGH);
    SPI.endTransaction();
}

static void busCommand(uint8_t cmd)
{
    digitalWrite(PIN_DISPLAY_DC, LOW);
    SPI.write(cmd);
}

static void busData(const uint8_t *buf, size_t len)
{
    digitalWrite(PIN_DISPLAY_DC, HIGH);
    SPI.writeBytes(buf, len);
}

static void busData(uint8_t data)
{
    busData(&data, 1);
}

static void sendCommand(uint8_t cmd)
{
    busBegin();
    busCommand(cmd);
    busEnd();
}

static void sendData(uint8_t data)
{
    busBegin();
    busData(data);
    busEnd();
}

// Must be called between busBegin() and busEnd()
static void setWindow(uint8_t xs, uint8_t xe, uint8_t ys, uint8_t ye)
{
    busCommand(0x15);
    busData(xs);
    busData(xe);
    busCommand(0x75);
    busData(ys);
    busData(ye);
    busCommand(0x30);
    busCommand(0x5C);
}

// Sync columns [colX, colX+w) of fullCanvas to the panel. Each page is compared with
// shadowCanvas and only the span from the first to the last changed column is sent;
// an unchanged frame never touches the bus.
// Canvas layout: DISPLAY_NUM_PAGES rows x DISPLAY_WIDTH columns, 4 pixels per byte
// (2bpp, packed vertically, MSB on top).
static void flushCanvas(int colX, int w)
{
    int  colEnd = colX + w - 1;
    bool busOpen = false;

    for (int page = 0; page < DISPLAY_NUM_PAGES; page++)
    {
        const uint8_t *cur = &fullCanvas[page * DISPLAY_WIDTH];
        uint8_t       *shadow = &shadowCanvas[page * DISPLAY_WIDTH];
        int            first = colX;
        int            last = colEnd;

        if (!forceFlush)
        {
            while (first <= colEnd && cur[first] == shadow[first])
                first++;
            if (first > colEnd)
                continue;
            while (cur[last] == shadow[last])
                last--;
        }

        if (!busOpen)
        {
            busBegin();
            busOpen = true;
        }
        int len = last - first + 1;
        setWindow((uint8_t)first, (uint8_t)last, (uint8_t)page, (uint8_t)page);
        busData(&cur[first], len);
        memcpy(&shadow[first], &cur[first], len);
    }

    if (busOpen)
        busEnd();
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
// avatar.bin is in native ST75256 DDRAM format: 2bpp, page-major, column-minor.
// Draws the avatar onto the canvas with its top-left corner at (startX, startY).
// A 128px avatar at the origin is copied page by page (fastest path, verified on hardware).
static void canvasBlitAvatar(uint8_t *canvas, int canvasW, int startX, int startY,
                             const Contact &contact)
{
    int avatarRes = contact.avatarResolution;

    if (avatarRes == DISPLAY_AVATAR_WIDTH && startX == 0 && startY == 0 && canvasW == DISPLAY_WIDTH)
    {
        // Copy avatar.bin straight into the left half of the canvas, stepping canvasW per page
        int srcBytesPerPage = avatarRes;
        for (int page = 0; page < DISPLAY_NUM_PAGES; page++)
        {
            memcpy(&canvas[page * canvasW], &contact.avatar[page * srcBytesPerPage],
                   srcBytesPerPage);
        }
        return;
    }

    // Per-pixel copy for other sizes or offsets.
    // Each source page has avatarRes columns of one byte; within a byte, sub-pixels 0..3
    // run from the MSB down.
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

// ── text (U8g2 rasterizes → canvas) ───────────────────────
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
                // Inverted highlight: text pixels become white, everything else keeps the
                // existing (black) background
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
static void drawQR(const char *text, uint8_t version = 8, uint8_t ecc = ECC_LOW)
{
    QRCode  qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(8)];
    qrcode_initText(&qrcode, qrcodeData, version, ecc, text);

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

    flushCanvas(0, DISPLAY_WIDTH);
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

// Filled rectangle in a gray level (ORs onto existing content, so draw the
// lighter track before the darker fill)
static void canvasFillRect(uint8_t *canvas, int canvasW, int x, int y, int w, int h, uint8_t gray)
{
    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++)
        for (int px = x; px < x + w && px < canvasW; px++)
            canvasSetPixel(canvas, canvasW, px, py, gray);
}

// LCD reset is on MCP23017 GPA3; drive it via the expander (RMW preserves other Port A bits).
static void displayReset()
{
    uint8_t iodir = ioexpReadReg(MCP_IODIRA);
    iodir &= ~(1 << PIN_MCP_LCD_RST);
    ioexpWriteReg(MCP_IODIRA, iodir);

    uint8_t olat = ioexpReadReg(MCP_OLATA);

    olat &= ~(1 << PIN_MCP_LCD_RST);
    ioexpWriteReg(MCP_OLATA, olat);
    delay(100);

    olat |= (1 << PIN_MCP_LCD_RST);
    ioexpWriteReg(MCP_OLATA, olat);
    delay(100);
}

// ST75256 VDD is on the always-on +3.3V_MCU rail, so it keeps drawing current
// while the ESP32 is held in reset. Sleep it, then hold RST low (MCP keeps the level)
// so the floating SPI lines (SPI_CS_LCD has no pull-up) can't disturb it.
// displayInit() resets it again on the next boot.
void displaySleep()
{
    sendCommand(0x30);  // EXT=0 command set
    sendCommand(0xAE);  // display off
    sendCommand(0x95);  // sleep in

    uint8_t olat = ioexpReadReg(MCP_OLATA);
    olat &= ~(1 << PIN_MCP_LCD_RST);
    ioexpWriteReg(MCP_OLATA, olat);
}

// ── public API ─────────────────────────────────────────────
void displayInit()
{
    u8g2.setDrawColor(1);

    pinMode(PIN_DISPLAY_CS, OUTPUT);
    pinMode(PIN_DISPLAY_DC, OUTPUT);

    digitalWrite(PIN_DISPLAY_CS, HIGH);

    displayReset();

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

    // DDRAM is undefined after reset: write the whole screen so the mirror matches it
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    forceFlush = true;
    flushCanvas(0, DISPLAY_WIDTH);
    forceFlush = false;
}

void displayClearAll()
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    flushCanvas(0, DISPLAY_WIDTH);
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

    flushCanvas(0, DISPLAY_WIDTH);
}

// STATE_PAIRING: waiting for physical ACOM wrist contact.
void drawWaitContact(const Contact &self)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, self);

    u8g2.setFont(u8g2_font_8x13B_tf);
    int w1 = u8g2.getUTF8Width("Touch devices");
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Touch devices", DISPLAY_WIDTH / 2,
                   DISPLAY_HEIGHT / 2, DISPLAY_UI_WIDTH, 18, u8g2_font_7x13B_tf, 0, 0, nullptr);

    flushCanvas(0, DISPLAY_WIDTH);
}

// STATE_BLE_CONNECTING: physical contact done, BLE link coming up; ripple animation.
void drawConnecting(const Contact &self)
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

    flushCanvas(0, DISPLAY_WIDTH);
}

// STATE_BLE_TRANSFER: profile data moving. percent is an honest running total
// (receive + send). indeterminate == true means no honest denominator (fallback
// only); show a bare "Receiving..." with no fake bar.
void drawTransfer(int percent, bool indeterminate)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    u8g2.setFont(u8g2_font_7x13B_tf);
    int wLabel = u8g2.getUTF8Width("Receiving");
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Receiving", (DISPLAY_WIDTH - wLabel) / 2, 26,
                   DISPLAY_WIDTH, 16, u8g2_font_7x13B_tf, 0, 0, nullptr);

    if (indeterminate)
    {
        u8g2.setFont(u8g2_font_8x13B_tf);
        int wDots = u8g2.getUTF8Width("...");
        canvasDrawText(fullCanvas, DISPLAY_WIDTH, "...", (DISPLAY_WIDTH - wDots) / 2, 54,
                       DISPLAY_WIDTH, 18, u8g2_font_8x13B_tf, 0, 0, nullptr);
        flushCanvas(0, DISPLAY_WIDTH);
        return;
    }

    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", percent);
    u8g2.setFont(u8g2_font_8x13B_tf);
    int wPct = u8g2.getUTF8Width(pctText);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, pctText, (DISPLAY_WIDTH - wPct) / 2, 48,
                   DISPLAY_WIDTH, 18, u8g2_font_8x13B_tf, 0, 0, nullptr);

    // progress bar: light-gray track, black fill proportional to percent
    const int barX = 28;
    const int barY = 78;
    const int barW = DISPLAY_WIDTH - 56;
    const int barH = 12;
    canvasFillRect(fullCanvas, DISPLAY_WIDTH, barX, barY, barW, barH, DISPLAY_LIGHT_GRAY);
    int fillW = barW * percent / 100;
    if (fillW > 0)
        canvasFillRect(fullCanvas, DISPLAY_WIDTH, barX, barY, fillW, barH, DISPLAY_BLACK);

    flushCanvas(0, DISPLAY_WIDTH);
}

void drawContactCard(const Contact &contact)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, contact);
    flushCanvas(0, DISPLAY_AVATAR_WIDTH);

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 20);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.name, DISPLAY_UI_X, 3, DISPLAY_UI_WIDTH, 20,
                   u8g2_font_8x13B_tf, 4, 2, &scrollName, INV);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.species, DISPLAY_UI_X, 26, DISPLAY_UI_WIDTH,
                   14, u8g2_font_7x13B_tf, 4, 2, &scrollLink);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.from, DISPLAY_UI_X, 42, DISPLAY_UI_WIDTH, 14,
                   u8g2_font_7x13B_tf, 4, 2, &scrollMisc);
    flushCanvas(DISPLAY_UI_X, DISPLAY_UI_WIDTH);
}

static void drawListMenu(const char *const *items, int count, int selection)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    for (int i = 0; i < count; i++)
    {
        int  y = 20 + i * 16;
        bool selected = (selection == i);

        if (selected)
            canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, y, DISPLAY_WIDTH, 14);

        canvasDrawText(fullCanvas, DISPLAY_WIDTH, items[i], 0, y, DISPLAY_WIDTH, 14,
                       u8g2_font_6x10_tf, 8, 2, nullptr, selected ? INV : NOR);
    }

    flushCanvas(0, DISPLAY_WIDTH);
}

void drawMenu(int menuSelection)
{
    static const char *const items[3] = {"My Profile", "Friends", "Settings"};
    drawListMenu(items, 3, menuSelection);
}

void drawSettingsMenu(int settingsSelection)
{
    static const char *const items[2] = {"Wi-Fi setup", "Clock"};
    drawListMenu(items, 2, settingsSelection);
}

// Text centered horizontally on the full-width canvas. Regions on this page start on
// 4-px page boundaries because non-INV canvasDrawText overwrites whole pages.
static void canvasDrawCentered(const char *text, int y, int h, const uint8_t *font, int textY)
{
    u8g2.setFont(font);
    int w = u8g2.getUTF8Width(text);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, text, 0, y, DISPLAY_WIDTH, h, font,
                   (DISPLAY_WIDTH - w) / 2, textY, nullptr);
}

void drawClock(const ClockView &clockView)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

    canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, 0, 0, DISPLAY_WIDTH, 16);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Clock", 0, 0, DISPLAY_WIDTH, 16, u8g2_font_6x10_tf,
                   8, 3, nullptr, INV);

    if (clockView.status == CLOCK_OK)
    {
        char timeText[12];
        char dateText[16];
        snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u", clockView.hours, clockView.minutes,
                 clockView.seconds);
        snprintf(dateText, sizeof(dateText), "%04u-%02u-%02u", clockView.year, clockView.month,
                 clockView.day);
        canvasDrawCentered(timeText, 32, 36, u8g2_font_logisoso28_tr, 0);
        canvasDrawCentered(dateText, 80, 16, u8g2_font_7x13B_tf, 0);
    }
    else
    {
        // Fail-closed: never show a time the RTC cannot vouch for.
        if (clockView.status == CLOCK_NOT_SET)
        {
            canvasDrawCentered("Time not set", 40, 16, u8g2_font_7x13B_tf, 0);
            canvasDrawCentered("Settings > Wi-Fi setup", 64, 12, u8g2_font_6x10_tf, 0);
            canvasDrawCentered("to sync time", 76, 12, u8g2_font_6x10_tf, 0);
        }
        else
        {
            canvasDrawCentered("RTC error", 40, 16, u8g2_font_7x13B_tf, 0);
            canvasDrawCentered("I2C read failed", 64, 12, u8g2_font_6x10_tf, 0);
        }
    }

    flushCanvas(0, DISPLAY_WIDTH);
}

void drawSettings()
{
    if (!wifiConfigClientConnected())
    {
        drawQR(wifiConfigWifiQr(), 3, ECC_LOW);
    }
    else if (!wifiConfigUploadDone())
    {
        drawQR(wifiConfigUrl(), 2, ECC_MEDIUM);
    }
    else
    {
        const char *sid = wifiConfigSelfId();
        const char *note = "ID (recovery coming soon)";
        const char *reb = "Reboot to apply";

        canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);

        u8g2.setFont(u8g2_font_7x13B_tf);
        int wSaved = u8g2.getUTF8Width("Saved");
        canvasDrawText(fullCanvas, DISPLAY_WIDTH, "Saved", (DISPLAY_WIDTH - wSaved) / 2, 8,
                       DISPLAY_WIDTH, 16, u8g2_font_7x13B_tf, 0, 0, nullptr);

        u8g2.setFont(u8g2_font_6x10_tf);
        int wId = u8g2.getUTF8Width(sid);
        canvasDrawText(fullCanvas, DISPLAY_WIDTH, sid, (DISPLAY_WIDTH - wId) / 2, 34, DISPLAY_WIDTH,
                       12, u8g2_font_6x10_tf, 0, 0, nullptr);

        int wNote = u8g2.getUTF8Width(note);
        canvasDrawText(fullCanvas, DISPLAY_WIDTH, note, (DISPLAY_WIDTH - wNote) / 2, 50,
                       DISPLAY_WIDTH, 12, u8g2_font_6x10_tf, 0, 0, nullptr);

        int wReb = u8g2.getUTF8Width(reb);
        canvasDrawText(fullCanvas, DISPLAY_WIDTH, reb, (DISPLAY_WIDTH - wReb) / 2, 72,
                       DISPLAY_WIDTH, 12, u8g2_font_6x10_tf, 0, 0, nullptr);

        flushCanvas(0, DISPLAY_WIDTH);
    }
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

    flushCanvas(0, DISPLAY_WIDTH);
}

void drawProfileAvatar(const Contact &contact)
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasBlitAvatar(fullCanvas, DISPLAY_WIDTH, 0, 0, contact);
    flushCanvas(0, DISPLAY_AVATAR_WIDTH);

    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    canvasDrawHighlight(fullCanvas, DISPLAY_WIDTH, DISPLAY_UI_X, 0, DISPLAY_UI_WIDTH, 20);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.name, DISPLAY_UI_X, 3, DISPLAY_UI_WIDTH, 20,
                   u8g2_font_8x13B_tf, 4, 2, &scrollName, INV);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.species, DISPLAY_UI_X, 26, DISPLAY_UI_WIDTH,
                   14, u8g2_font_7x13B_tf, 4, 2, &scrollLink);
    canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.from, DISPLAY_UI_X, 42, DISPLAY_UI_WIDTH, 14,
                   u8g2_font_7x13B_tf, 4, 2, &scrollMisc);
    flushCanvas(DISPLAY_UI_X, DISPLAY_UI_WIDTH);
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

        canvasDrawText(fullCanvas, DISPLAY_WIDTH, contact.links[i].platform, 0, y, DISPLAY_WIDTH,
                       lineH, u8g2_font_6x10_tf, 4, 2, nullptr, selected ? INV : NOR);
    }

    flushCanvas(0, DISPLAY_WIDTH);
}

void drawProfileQR(const Contact &contact, int linkIndex)
{
    drawQR(contact.links[linkIndex].url);
}

void drawStandby()
{
    canvasClear(fullCanvas, DISPLAY_WIDTH, DISPLAY_NUM_PAGES);
    flushCanvas(0, DISPLAY_WIDTH);
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

    flushCanvas(0, DISPLAY_WIDTH);
}

void displayRender(state_t state, const Contact &self, const Contact &currentContact,
                   const char contactNames[][NAME_LEN], int contactCount, int contactIndex,
                   int menuSelection, bool idleShowQR, const Contact &profileContact, int linkIndex,
                   int transferPercent, bool transferIndeterminate, int settingsSelection,
                   const ClockView &clockView)
{
    // Resend the whole screen on page changes so any DDRAM corruption gets repaired
    static bool    hasLastState = false;
    static state_t lastState;
    if (!hasLastState || state != lastState)
    {
        forceFlush = true;
        hasLastState = true;
        lastState = state;
    }

    switch (state)
    {
        case STATE_IDLE:
            if (idleShowQR)
                drawProfileQR(self, 0);
            else
                drawHomepage(self);
            break;
        case STATE_PAIRING:
            drawWaitContact(self);
            break;
        case STATE_BLE_CONNECTING:
            drawConnecting(self);
            break;
        case STATE_BLE_TRANSFER:
            drawTransfer(transferPercent, transferIndeterminate);
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
        case STATE_SETTINGS:
            break;  // drawn by settingsService() only when the SoftAP state changes
        case STATE_SETTINGS_MENU:
            drawSettingsMenu(settingsSelection);
            break;
        case STATE_SETTINGS_CLOCK:
            drawClock(clockView);
            break;
        case STATE_STANDBY:
            drawStandby();
            break;
        case STATE_LOW_BATTERY:
            drawLowBattery();
            break;
    }

    forceFlush = false;
}