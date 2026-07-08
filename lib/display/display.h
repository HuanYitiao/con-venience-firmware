#pragma once
#include <Arduino.h>

#include <SPI.h>
#include <U8g2lib.h>

#include "fsm.h"
#include "pins.h"
#include "storage.h"

#define DISPLAY_WIDTH 256
#define DISPLAY_HEIGHT 128

#define DISPLAY_PAGE_HEIGHT 4
#define DISPLAY_NUM_PAGES (DISPLAY_HEIGHT / DISPLAY_PAGE_HEIGHT)

#define DISPLAY_DITHER_WIDTH (DISPLAY_WIDTH / 8)
#define DISPLAY_AVATAR_WIDTH 128
#define DISPLAY_AVATAR_HEIGHT 128
#define DISPLAY_UI_X 128
#define DISPLAY_UI_WIDTH 128
#define DISPLAY_UI_HEIGHT 128
#define DISPLAY_QR_SIZE 128

enum DrawMode
{
    NOR,
    INV,
    BG
};

#define DISPLAY_WHITE 0x00
#define DISPLAY_LIGHT_GRAY 0x01
#define DISPLAY_DARK_GRAY 0x02
#define DISPLAY_BLACK 0x03

struct ScrollTextState
{
    int           offset;
    bool          paused;
    unsigned long pauseStart;
    unsigned long lastTime;
    char          lastText[64];
    uint8_t       lastMaxChars;
};

#define SCROLL_TEXT_INTERVAL 100
#define SCROLL_TEXT_PAUSE 1000

void displayInit();
void displayClearAll();
void displayResetScroll();
void displayRender(state_t state, const Contact &self, const Contact &currentContact,
                   const char contactNames[][NAME_LEN], int contactCount, int contactIndex,
                   int menuSelection, bool idleShowQR, const Contact &profileContact,
                   int linkIndex);
void drawHomepage(const Contact &self);
void drawPairing(const Contact &self);
void drawContactCard(const Contact &contact);
void drawMenu();
void drawSettings();
void drawContactList(const char names[][NAME_LEN], int count, int index);
void drawProfileAvatar(const Contact &contact);
void drawProfileLinks(const Contact &contact, int linkIndex);
void drawProfileQR(const Contact &contact, int linkIndex);
void drawStandby();
void drawLowBattery();