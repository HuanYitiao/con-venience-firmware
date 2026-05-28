#pragma once
#include <Arduino.h>

#include <SPI.h>
#include <U8g2lib.h>

#include "fsm.h"
#include "storage.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define PIN_SCK 10
#define PIN_MOSI 11
#define PIN_MISO -1
#define PIN_CS 18
#define PIN_DC 23

void displayInit();

void displayRender(state_t state, const Contact &self, const Contact *contacts, int contactCount,
                   int contactIndex, bool menuSelection, bool idleShowQR);

void drawHomepage(const Contact &self);
void drawPairing(const Contact &self);
void drawContactCard(const Contact &contact);
void drawMenu(bool menuSelection);
void drawContactList(const Contact *contacts, int count, int index);
void drawContactDetail(const Contact &contact);
void drawMyProfile(const Contact &self);
void drawStandby();
void drawLowBattery();
void displayResetScroll();