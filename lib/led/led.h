#pragma once
#include <stdint.h>

void ledInit();
void ledSetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
void ledOff();
