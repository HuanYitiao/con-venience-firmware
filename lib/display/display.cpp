#include "display.h"

#include "qrcode.h"
// change

#define PAIRING_FRAME_INTERVAL 200  // ms
#define SCROLL_INTERVAL 100
#define SCROLL_PAUSE 1000

static U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R2,
                                                    /* cs  */ PIN_CS,
                                                    /* dc  */ PIN_DC,
                                                    /* rst */ U8X8_PIN_NONE);

static uint8_t       pairingFrame = 0;
static unsigned long lastFrameTime = 0;

static int           scrollOffset = 0;
static unsigned long lastScrollTime = 0;
static bool          scrollPaused = false;
static unsigned long scrollPauseStart = 0;

static void utf8ToLatin1(const char *src, char *dst, size_t dstLen)
{
    size_t i = 0;
    while (*src && i < dstLen - 1)
    {
        uint8_t c = (uint8_t)*src;
        if (c < 0x80)
        {
            dst[i++] = c;
            src++;
        }
        else if (c == 0xC3 && (uint8_t)*(src + 1) >= 0x80)
        {
            dst[i++] = (uint8_t)*(src + 1) + 0x40;
            src += 2;
        }
        else
        {
            dst[i++] = '?';
            src++;
        }
    }
    dst[i] = 0;
}

static void drawScrollingStr(int x, int y, int maxWidth, const char *str)
{
    char converted[128];
    utf8ToLatin1(str, converted, sizeof(converted));

    int strW = u8g2.getStrWidth(converted);
    if (strW <= maxWidth)
    {
        u8g2.drawStr(x, y, converted);
        return;
    }
    unsigned long now = millis();
    if (scrollPaused)
    {
        if (now - scrollPauseStart > SCROLL_PAUSE)
        {
            scrollPaused = false;
            scrollOffset = 0;
            lastScrollTime = now;
        }
    }
    else
    {
        if (now - lastScrollTime > SCROLL_INTERVAL)
        {
            scrollOffset++;
            lastScrollTime = now;
            if (scrollOffset >= strW - maxWidth)
            {
                scrollPaused = true;
                scrollPauseStart = now;
            }
        }
    }
    u8g2.setClipWindow(x, y - 12, x + maxWidth, y + 2);
    u8g2.drawStr(x - scrollOffset, y, converted);
    u8g2.setMaxClipWindow();
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
    u8g2.begin();
}

void drawHomepage(const Contact &self, bool showQR)
{
    u8g2.clearBuffer();
    drawDitherLeft(0, 0, DITHER_WIDTH, DISPLAY_HEIGHT);
    drawDitherRight(DISPLAY_WIDTH - DITHER_WIDTH, 0, DITHER_WIDTH, DISPLAY_HEIGHT);
    if (showQR)
    {
        drawQR(self.links[0].url);
    }
    else
    {
        int avatarX = (DISPLAY_WIDTH - self.avatarResolution) / 2;
        for (int y = 0; y < self.avatarResolution; y++)
        {
            for (int x = 0; x < self.avatarResolution; x++)
            {
                int     bitIndex = y * self.avatarResolution + x;
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

    for (int y = 0; y < self.avatarResolution; y++)
    {
        for (int x = 0; x < self.avatarResolution; x++)
        {
            int     bitIndex = y * self.avatarResolution + x;
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

    int avatarX = 0;
    for (int y = 0; y < contact.avatarResolution; y++)
    {
        for (int x = 0; x < contact.avatarResolution; x++)
        {
            int     bitIndex = y * contact.avatarResolution + x;
            uint8_t byte = contact.avatar[bitIndex / 8];
            uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
            if (bit)
                u8g2.drawPixel(avatarX + x, y);
        }
    }

    u8g2.setFont(u8g2_font_7x13B_tf);
    drawScrollingStr(contact.avatarResolution, 14, contact.avatarResolution, contact.name);

    u8g2.setFont(u8g2_font_6x10_tf);
    drawScrollingStr(contact.avatarResolution, 28, contact.avatarResolution, contact.links[0].url);

    u8g2.sendBuffer();
}

// ── menu ──────────────────────────────────────────────────
void drawMenu(bool menuSelection)
{
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tf);

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
void drawContactList(const char names[][NAME_LEN], int count, int index)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);

    const int lineH = 12;
    const int visibleLines = DISPLAY_HEIGHT / lineH;
    int       listOffset = 0;
    if (index >= visibleLines)
        listOffset = index - visibleLines + 1;

    for (int i = 0; i < visibleLines && (i + listOffset) < count; i++)
    {
        int  ci = i + listOffset;
        int  y = (i + 1) * lineH;
        bool selected = (ci == index);

        if (selected)
        {
            u8g2.drawBox(0, y - 10, DISPLAY_WIDTH, lineH);
            u8g2.setDrawColor(0);
        }

        char converted[NAME_LEN];
        utf8ToLatin1(names[ci], converted, sizeof(converted));

        if (selected)
            u8g2.drawStr(4, y, converted);
        else
            drawScrollingStr(4, y, DISPLAY_WIDTH - 8, converted);

        u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}

// ── contact detail ────────────────────────────────────────
void drawContactDetail(const Contact &contact)
{
    drawContactCard(contact);
}

// ── profile avatar ────────────────────────────────────────
void drawProfileAvatar(const Contact &contact)
{
    u8g2.clearBuffer();

    for (int y = 0; y < contact.avatarResolution; y++)
    {
        for (int x = 0; x < contact.avatarResolution; x++)
        {
            int     bitIndex = y * contact.avatarResolution + x;
            uint8_t byte = contact.avatar[bitIndex / 8];
            uint8_t bit = (byte >> (7 - (bitIndex % 8))) & 1;
            if (bit)
                u8g2.drawPixel(x, y);
        }
    }

    u8g2.setFont(u8g2_font_7x13B_tf);
    drawScrollingStr(contact.avatarResolution + 2, 14, DISPLAY_WIDTH - contact.avatarResolution - 2,
                     contact.name);

    u8g2.setFont(u8g2_font_6x10_tf);
    drawScrollingStr(contact.avatarResolution + 2, 26, DISPLAY_WIDTH - contact.avatarResolution - 2,
                     contact.species);

    u8g2.setFont(u8g2_font_6x10_tf);
    drawScrollingStr(contact.avatarResolution + 2, 38, DISPLAY_WIDTH - contact.avatarResolution - 2,
                     contact.from);

    u8g2.sendBuffer();
}

void drawProfileLinks(const Contact &contact, int linkIndex)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);

    const int lineH = 12;

    for (int i = 0; i < contact.linkCount; i++)
    {
        int  y = (i + 1) * lineH;
        int  clampedIndex = min(linkIndex, (int)contact.linkCount - 1);
        bool selected = (i == clampedIndex);

        if (selected)
        {
            u8g2.drawBox(0, y - 10, DISPLAY_WIDTH, lineH);
            u8g2.setDrawColor(0);
        }

        char converted[32];
        utf8ToLatin1(contact.links[i].tag, converted, sizeof(converted));
        u8g2.drawStr(4, y, converted);

        u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}

void drawProfileQR(const Contact &contact, int linkIndex)
{
    u8g2.clearBuffer();
    drawQR(contact.links[linkIndex].url);
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
    u8g2.setFont(u8g2_font_7x13B_tf);
    int w = u8g2.getStrWidth("Low Battery");
    u8g2.drawStr((DISPLAY_WIDTH - w) / 2, 28, "Low Battery");
    u8g2.setFont(u8g2_font_6x10_tf);
    w = u8g2.getStrWidth("Please charge");
    u8g2.drawStr((DISPLAY_WIDTH - w) / 2, 44, "Please charge");
    u8g2.sendBuffer();
}

void displayRender(state_t state, const Contact &self, const Contact &currentContact,
                   const char contactNames[][NAME_LEN], int contactCount, int contactIndex,
                   bool menuSelection, bool idleShowQR, const Contact &profileContact,
                   int linkIndex)
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

void displayResetScroll()
{
    scrollPaused = false;
    scrollOffset = 0;
    lastScrollTime = 0;
}