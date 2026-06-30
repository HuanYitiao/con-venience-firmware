#include <Arduino.h>

#include <NimBLEDevice.h>

#include "G.h"
#include "ble.h"
#include "button.h"
#include "display_st75256.h"
#include "fsm.h"
#include "led.h"
#include "pins.h"
#include "storage.h"
#include "wolframe.h"

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

static Contact self = {};
static Contact currentContact = {};
static char    contactNames[16][NAME_LEN] = {};
static int     contactCount = 0;
static bool    idleShowQR = false;

#if 0
#define MY_MAC {0x48, 0xf6, 0xee, 0xc7, 0x15, 0x0e}    // 自己的MAC
#define PEER_MAC {0x48, 0xf6, 0xee, 0xc7, 0x1d, 0xf2}  // 对方的MAC
#else
#define PEER_MAC {0x48, 0xf6, 0xee, 0xc7, 0x15, 0x0e}  // 自己的MAC
#define MY_MAC {0x48, 0xf6, 0xee, 0xc7, 0x1d, 0xf2}    // 对方的MAC
#endif

void setup()
{
    initLCD();
    clean();
}

static ScrollTextState state1, state2, state3, state4;

void loop()
{
    draw(wolframe_data, 0, 0, 128, 128);
    drawText("Wolframe", 128, 0, 128, 20, font_variant[1], INV, 5, 5, 10, &state1);
    drawText("Option 1", 144, 42, 112, 16, font_variant[0], BG, 5, 5, 10, &state2);
    drawText("Option 2", 144, 66, 112, 16, font_variant[0], NOR, 5, 5, 10, &state3);
    drawText("Option 3", 144, 90, 112, 16, font_variant[0], NOR, 5, 5, 10, &state4);
    delay(1000);
    drawText("Option 1", 144, 42, 112, 16, font_variant[0], NOR, 5, 5, 10, &state2);
    drawText("Option 2", 144, 66, 112, 16, font_variant[0], BG, 5, 5, 10, &state3);
    drawText("Option 3", 144, 90, 112, 16, font_variant[0], NOR, 5, 5, 10, &state4);
    delay(1000);

    draw(G_data, 0, 0, 128, 128);
    drawText("Günther", 128, 0, 128, 20, font_variant[1], INV, 5, 5, 10, &state1);
    drawText("Option 1", 144, 42, 112, 16, font_variant[0], BG, 5, 5, 10, &state2);
    drawText("Option 2", 144, 66, 112, 16, font_variant[0], NOR, 5, 5, 10, &state3);
    drawText("Option 3", 144, 90, 112, 16, font_variant[0], NOR, 5, 5, 10, &state4);
    delay(1000);
    drawText("Option 1", 144, 42, 112, 16, font_variant[0], NOR, 5, 5, 10, &state2);
    drawText("Option 2", 144, 66, 112, 16, font_variant[0], BG, 5, 5, 10, &state3);
    drawText("Option 3", 144, 90, 112, 16, font_variant[0], NOR, 5, 5, 10, &state4);
    delay(1000);
    drawText("Option 1", 144, 42, 112, 16, font_variant[0], NOR, 5, 5, 10, &state2);
    drawText("Option 2", 144, 66, 112, 16, font_variant[0], NOR, 5, 5, 10, &state3);
    drawText("Option 3", 144, 90, 112, 16, font_variant[0], BG, 5, 5, 10, &state4);
    delay(1000);
}