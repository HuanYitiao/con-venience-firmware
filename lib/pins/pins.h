#pragma once

// ── SPI Bus ──────────────────────────────────────────────
#define PIN_SPI_SCK 23
#define PIN_SPI_MISO 21
#define PIN_SPI_MOSI 22

// ── Display ───────────────────────
#define PIN_DISPLAY_CS 19
#define PIN_DISPLAY_DC 4
#define PIN_DISPLAY_RST 5

// ── MicroSD ──────────────────────────────────────────────
#define PIN_SD_CS 20

// ── WS2812B LED ──────────────────────────────────────────
#define PIN_LED 8

// ── I2C ──────────────────────────────────────────────────
#define PIN_I2C_SDA 6
#define PIN_I2C_SCL 7

// ── I2S Audio ────────────────────────────────────────────
#define PIN_I2S_BCLK 10
#define PIN_I2S_LRC 11
#define PIN_I2S_DIN 18

// ── ADC ──────────────────────────────────────────────────
#define PIN_VBAT_ADC 3

// ── Buttons (via MCP23008) ───────────────────────────────
#define PIN_MCP_INT 2
#define MCP23008_ADDR 0x20

#define PIN_MCP_BTN_UP 0
#define PIN_MCP_BTN_DOWN 1
#define PIN_MCP_BTN_PAIR 2
#define PIN_MCP_BTN_LEFT 3
#define PIN_MCP_BTN_RIGHT 4
// #define PIN_MCP_LEDA 5
#define PIN_MCP_AUDIO_SD 6
#define PIN_MCP_PWR_EN 7