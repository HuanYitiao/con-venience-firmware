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
#define DISPLAY_BLACK 0b11       ///< Full black
#define DISPLAY_WHITE 0b00       ///< Full white
#define DISPLAY_LIGHT_GRAY 0b01  ///< Light gray
#define DISPLAY_DARK_GRAY 0b10   ///< Dark gray

/**
 * @defgroup DISPLAY_SCROLLING_TEXT Scrolling Text Configuration
 * @details Parameters for controlling scrolling text behavior
 * @{
 */
#define SCROLL_TEXT_INTERVAL 40  ///< ms per scroll pixel step
#define SCROLL_TEXT_PAUSE 500    ///< ms pause before/after scrolling

/// @brief Drawing mode for the draw() function
enum DrawMode
{
    NOR,  ///< Normal display (as-is)
    BG,   ///< Substrate: replace white pixels with light gray
    INV   ///< Invert: bitwise negative image
};

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
/// @param canvas Bitmap data buffer (page-major, column-minor, 2bpp packed)
/// @param x Starting X coordinate
/// @param y Starting Y coordinate
/// @param w Width in pixels
/// @param h Height in pixels
/// @param mode Drawing mode: NOR (as-is), BG (white→light-gray), INV (negative)
void draw(const uint8_t *canvas, int x, int y, int w, int h, DrawMode mode = NOR);

/// @brief Per-instance state for scrolling text (one per line)
struct ScrollTextState
{
    int           offset = 0;
    unsigned long lastTime = 0;
    bool          paused = true;
    unsigned long pauseStart = 0;
    char          lastText[128] = "";
    uint8_t       lastMaxChars = 0;
};

/// @brief Initialize a ScrollTextState (call once per line before first use)
void scrollTextInit(ScrollTextState &s);

/// @brief Draw single-line text within a fixed canvas area, with optional scroll
/// @param text Text content (single line)
/// @param canvasX Canvas absolute X position on screen, 0 corresponds to the left
/// @param canvasY Canvas absolute Y position on screen, 0 corresponds to the top
/// @param canvasW Canvas width in pixels
/// @param canvasH Canvas height in pixels
/// @param font Font pointer
/// @param mode Drawing mode: NOR, BG, INV
/// @param textX Text X offset within canvas (default 5)
/// @param textY Text Y offset within canvas (default 3)
/// @param maxChars Max chars before scrolling (0=never scroll, default 0)
/// @param scrollState Per-line state for multi-line independent scroll (nullptr=use internal
/// singleton)
void drawText(const char *text, int canvasX, int canvasY, int canvasW, int canvasH,
              const uint8_t *font, DrawMode mode = NOR, uint8_t textX = 5, uint8_t textY = 3,
              uint8_t maxChars = 0, ScrollTextState *scrollState = nullptr);

/** @} */

void     test_GrayScale();
uint8_t *gen_GrayScale();