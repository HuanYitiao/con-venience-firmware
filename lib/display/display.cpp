#include "display.h"

#include "qrcode.h"

static U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R2,
                                                    /* cs  */ PIN_CS,
                                                    /* dc  */ PIN_DC,
                                                    /* rst */ U8X8_PIN_NONE);

static uint8_t       pairingFrame = 0;
static unsigned long lastFrameTime = 0;
#define PAIRING_FRAME_INTERVAL 200  // ms

static void drawAvatar(int drawX, int drawY, const uint8_t *avatar)
{
#if DISPLAY_WIDTH == 128
    for (int y = 0; y < 32; y++)
    {
        for (int x = 0; x < 32; x++)
        {
            int     srcX = x * 2;
            int     srcY = y * 2;
            int     bitIndex = srcY * 64 + srcX;
            uint8_t byte = avatar[bitIndex / 8];
            uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
            if (bit)
                u8g2.drawPixel(drawX + x, drawY + y);
        }
    }
#else
    // 1:1，64x64
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int     bitIndex = y * 64 + x;
            uint8_t byte = avatar[bitIndex / 8];
            uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
            if (bit)
                u8g2.drawPixel(drawX + x, drawY + y);
        }
    }
#endif
}

void drawQR(const char *text)
{
    QRCode  qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(1)];
    qrcode_initText(&qrcode, qrcodeData, 1, ECC_LOW, text);

    int moduleSize = 3;
    int qrSize = qrcode.size * moduleSize;  // 21*3=63
    int offsetX = (DISPLAY_WIDTH - qrSize) / 2;
    int offsetY = (DISPLAY_HEIGHT - qrSize) / 2;

    for (int y = 0; y < qrcode.size; y++)
    {
        for (int x = 0; x < qrcode.size; x++)
        {
            if (qrcode_getModule(&qrcode, x, y))
            {
                u8g2.drawBox(offsetX + x * moduleSize, offsetY + y * moduleSize, moduleSize,
                             moduleSize);
            }
        }
    }
    u8g2.sendBuffer();
}

#define DITHER_WIDTH (DISPLAY_WIDTH / 8)

static void drawDitherLeft(int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
    {
        for (int px = x; px < x + w; px++)
        {
            int col = px - x;                // 0..w-1
            int threshold = (col * 16) / w;  // 0..15
            // Bayer 4x4 ordered dither matrix
            static const uint8_t bayer4[4][4] = {
                {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
            if (bayer4[py % 4][px % 4] >= threshold)
            {
                u8g2.drawPixel(px, py);
            }
        }
    }
}

static void drawDitherRight(int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
    {
        for (int px = x; px < x + w; px++)
        {
            int                  col = (x + w - 1) - px;  // 0..w-1，从右到左
            int                  threshold = (col * 16) / w;
            static const uint8_t bayer4[4][4] = {
                {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
            if (bayer4[py % 4][px % 4] >= threshold)
            {
                u8g2.drawPixel(px, py);
            }
        }
    }
}

void displayInit()
{
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    u8g2.begin();
}

void drawHomepage(const Contact &self, bool showQR)
{
    u8g2.clearBuffer();
    drawDitherLeft(0, 0, DITHER_WIDTH, DISPLAY_HEIGHT);
    drawDitherRight(DISPLAY_WIDTH - DITHER_WIDTH, 0, DITHER_WIDTH, DISPLAY_HEIGHT);
    if (showQR)
    {
        drawQR("t.me/WolframLiu");
    }
    else
    {
        int avatarX = (DISPLAY_WIDTH - 64) / 2;
        for (int y = 0; y < 64; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                int     bitIndex = y * 64 + x;
                uint8_t byte = self.avatar[bitIndex / 8];
                uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
                if (bit)
                    u8g2.drawPixel(avatarX + x, y);
            }
        }
    }
    u8g2.sendBuffer();
}

// ── pairing ───────────────────────────────────────────────
void drawPairing(const Contact &self)
{
    unsigned long now = millis();
    if (now - lastFrameTime > PAIRING_FRAME_INTERVAL)
    {
        pairingFrame = (pairingFrame + 1) % 4;
        lastFrameTime = now;
    }

    u8g2.clearBuffer();

    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;

    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int     bitIndex = y * 64 + x;
            uint8_t byte = self.avatar[bitIndex / 8];
            uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
            if (bit)
                u8g2.drawPixel(x, y);
        }
    }

    for (int i = 0; i < 3; i++)
    {
        int frameOffset = (pairingFrame + i) % 4;
        int r = 20 + frameOffset * 6;
        if (r > 0 && r < 40)
        {
            u8g2.drawCircle(cx, cy, r, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
        }
    }

    u8g2.sendBuffer();
}

// ── contact card ─────────────────────
void drawContactCard(const Contact &contact)
{
    u8g2.clearBuffer();

    drawAvatar(2, (DISPLAY_HEIGHT - 32) / 2, contact.avatar);

    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr(38, 14, contact.username);

    u8g2.setFont(u8g2_font_6x10_tr);
    char urlShort[18];
    strlcpy(urlShort, contact.url, sizeof(urlShort));
    u8g2.drawStr(38, 28, urlShort);

    u8g2.sendBuffer();
}

// ── menu ──────────────────────────────────────────────────
void drawMenu(bool menuSelection)
{
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tr);

    if (menuSelection)
    {
        u8g2.drawBox(0, 20, DISPLAY_WIDTH, 14);
        u8g2.setDrawColor(0);
    }
    u8g2.drawStr(8, 31, "Friends");
    u8g2.setDrawColor(1);

    if (!menuSelection)
    {
        u8g2.drawBox(0, 36, DISPLAY_WIDTH, 14);
        u8g2.setDrawColor(0);
    }
    u8g2.drawStr(8, 47, "My Profile");
    u8g2.setDrawColor(1);

    u8g2.sendBuffer();
}

// ── contact list ──────────────────────────────────────────
void drawContactList(const Contact *contacts, int count, int index)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    const int lineH = 12;
    const int visibleLines = DISPLAY_HEIGHT / lineH;  // 5行
    int       scrollOffset = 0;
    if (index >= visibleLines)
        scrollOffset = index - visibleLines + 1;

    for (int i = 0; i < visibleLines && (i + scrollOffset) < count; i++)
    {
        int ci = i + scrollOffset;
        int y = (i + 1) * lineH;
        if (ci == index)
        {
            u8g2.drawBox(0, y - 10, DISPLAY_WIDTH, lineH);
            u8g2.setDrawColor(0);
        }
        u8g2.drawStr(4, y, contacts[ci].username);
        u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}

// ── contact detail ────────────────────────────────────────
void drawContactDetail(const Contact &contact)
{
    drawContactCard(contact);  // 布局相同
}

// ── my profile ────────────────────────────────────────────
void drawMyProfile(const Contact &self)
{
    u8g2.clearBuffer();

    drawAvatar(2, (DISPLAY_HEIGHT - 32) / 2, self.avatar);

    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr(38, 14, self.username);

    u8g2.setFont(u8g2_font_6x10_tr);
    char urlShort[18];
    strlcpy(urlShort, self.url, sizeof(urlShort));
    u8g2.drawStr(38, 28, urlShort);

    // Edit提示
    u8g2.drawStr(38, 52, "[Edit via BLE]");

    u8g2.sendBuffer();
}

// ── standby ───────────────────────────────────────────────
void drawStandby()
{
    u8g2.clearBuffer();
    u8g2.sendBuffer();
}

// ── low battery ───────────────────────────────────────────
void drawLowBattery()
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x13B_tr);
    int w = u8g2.getStrWidth("Low Battery");
    u8g2.drawStr((DISPLAY_WIDTH - w) / 2, 28, "Low Battery");
    u8g2.setFont(u8g2_font_6x10_tr);
    w = u8g2.getStrWidth("Please charge");
    u8g2.drawStr((DISPLAY_WIDTH - w) / 2, 44, "Please charge");
    u8g2.sendBuffer();
}

void displayRender(state_t state, const Contact &self, const Contact *contacts, int contactCount,
                   int contactIndex, bool menuSelection, bool idleShowQR)
{
    switch (state)
    {
        case STATE_IDLE:
            drawHomepage(self, idleShowQR);
            break;
        case STATE_PAIRING:
            drawPairing(self);
            break;
        case STATE_CONTACT_CARD:
            drawContactCard(contacts[contactIndex]);
            break;
        case STATE_MENU:
            drawMenu(menuSelection);
            break;
        case STATE_CONTACT_LIST:
            drawContactList(contacts, contactCount, contactIndex);
            break;
        case STATE_CONTACT_DETAIL:
            drawContactDetail(contacts[contactIndex]);
            break;
        case STATE_MY_PROFILE:
            drawMyProfile(self);
            break;
        case STATE_STANDBY:
            drawStandby();
            break;
        case STATE_LOW_BATTERY:
            drawLowBattery();
            break;
    }
}