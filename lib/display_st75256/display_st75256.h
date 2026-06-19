/**
 * @file display_st75256.h
 * @brief ST75256 Display Driver Header File
 * @details Supports 256x128 resolution with 4-level grayscale display
 */

#pragma once

#include <Arduino.h>

#include <SPI.h>
#include <U8g2lib.h>

/**
 * @defgroup DISPLAY_PINS Display Hardware Interface Pins
 * @{
 */
#define SCLK_PIN 10  ///< Serial Clock (Screen D0)
#define SID_PIN 11   ///< Serial Data (Screen D1~D3 connected)
#define CS_PIN 7     ///< Chip Select (Screen CS)
#define RS_PIN 6     ///< Register/Data Select (Screen A0/RS)
#define RES_PIN 9    ///< Screen Reset Pin (Screen RES)
/** @} */

/**
 * @defgroup DISPLAY_GRAY_LEVELS Grayscale Level Definitions (4-level)
 * @{
 */
#define DISPLAY_BLACK 0xFF       ///< Full black (0b11111111)
#define DISPLAY_WHITE 0x00       ///< Full white (0b00000000)
#define DISPLAY_LIGHT_GRAY 0x55  ///< Light gray (0b01010101)
#define DISPLAY_DARK_GRAY 0xAA   ///< Dark gray (0b10101010)

/**
 * @defgroup DISPLAY_EXTERN External Global Object Declarations
 * @{
 */
extern U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2;            ///< U8g2 display object
extern const uint8_t                     *font_variant[];  ///< Font variant array
/** @} */

/**
 * @defgroup DISPLAY_FUNCTIONS Display Interface Functions
 * @{
 */

/// @brief Initialize U8g2 library
void initU8g2();

/// @brief Initialize display hardware interface
void initLCD();

/// @brief Test grayscale display effects
void testGrayScale();

/// @brief Clear display content
void clean();

/// @brief Draw test block
void drawBlock();

/// @brief Draw grayscale chessboard pattern
/// @param bias Offset parameter to generate different chessboard patterns
void drawGrayChessboard(uint8_t bias = 0);

/// @brief Draw bitmap data to specified position
/// @param canvas Bitmap data buffer
/// @param x Starting X coordinate
/// @param y Starting Y coordinate
/// @param w Width in pixels
/// @param h Height in pixels
/// @param bg Background color (grayscale value)
void draw(const uint8_t *canvas, int x, int y, int w, int h, uint8_t bg = DISPLAY_WHITE);

/// @brief Draw text to specified rectangle area
/// @param text Text content
/// @param textX Text relative X coordinate
/// @param textY Text relative Y coordinate
/// @param rectX Rectangle area starting X coordinate
/// @param rectY Rectangle area starting Y coordinate
/// @param rectW Rectangle area width
/// @param rectH Rectangle area height
/// @param fgGray Text foreground grayscale value
/// @param bgGray Text background grayscale value
/// @param font Font pointer
void drawText(const char *text, int textX, int textY, int rectX, int rectY, int rectW, int rectH,
              uint8_t fgGray = DISPLAY_BLACK, uint8_t bgGray = DISPLAY_LIGHT_GRAY,
              const uint8_t *font = font_variant[0]);

/** @} */
