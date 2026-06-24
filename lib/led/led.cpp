#include "led.h"

#include <Adafruit_NeoPixel.h>

#define PIN_LED 8
#define NUM_LEDS 1

Adafruit_NeoPixel rgbLed(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);

void ledInit()
{
    rgbLed.begin();
    rgbLed.show();
}

void ledSetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    uint32_t color = rgbLed.Color((uint16_t)r * brightness / 100, (uint16_t)g * brightness / 100,
                                  (uint16_t)b * brightness / 100);
    rgbLed.setPixelColor(0, color);
    rgbLed.show();
}

void ledOff()
{
    rgbLed.setPixelColor(0, 0);
    rgbLed.show();
}