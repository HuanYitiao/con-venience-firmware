#pragma once
#include <Arduino.h>

#include <SPI.h>
#include <U8g2lib.h>

#include "fsm.h"
#include "pins.h"
#include "storage.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

void displayInit();
void displayRender(state_t state, const Contact &self, const Contact &currentContact,
                   const char contactNames[][NAME_LEN], int contactCount, int contactIndex,
                   bool menuSelection, bool idleShowQR, const Contact &profileContact,
                   int linkIndex);
void drawHomepage(const Contact &self);
void drawPairing(const Contact &self);
void drawContactCard(const Contact &contact);
void drawMenu(bool menuSelection);
void drawContactList(const char names[][NAME_LEN], int count, int index);
void drawProfileAvatar(const Contact &contact);
void drawProfileLinks(const Contact &contact, int linkIndex);
void drawProfileQR(const Contact &contact, int linkIndex);
void drawStandby();
void drawLowBattery();
void displayResetScroll();