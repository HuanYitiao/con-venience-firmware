#pragma once

// ── SPI Bus ──────────────────────────────────────────────
#define PIN_SPI_SCK 11
#define PIN_SPI_MISO 10
#define PIN_SPI_MOSI 2

// ── Display (ST75256) ────────────────────────────────────
#define PIN_DISPLAY_CS 23
#define PIN_DISPLAY_DC 15
#define PIN_DISPLAY_BL 4

// ── MicroSD ──────────────────────────────────────────────
#define PIN_SD_CS 3

// ── WS2812B LED ──────────────────────────────────────────
#define PIN_LED 8

// ── I2C ──────────────────────────────────────────────────
#define PIN_I2C_SDA 6
#define PIN_I2C_SCL 7

// ── I2S Audio ────────────────────────────────────────────
#define PIN_I2S_BCLK 21
#define PIN_I2S_LRC 22
#define PIN_I2S_DIN 20

// ── ACOM ─────────────────────────────────────────────────
#define PIN_ACOM_OD 19
#define PIN_ACOM_PU 18

// ── ADC ──────────────────────────────────────────────────
#define PIN_VBAT_ADC 1

// ── MCP23017 ─────────────────────────────────────────────
#define PIN_MCP_INT 5
#define MCP23017_ADDR 0x20

// Buttons on Port B. SW1..SW5 = GPB1..GPB5.
// GPB0 is the SD-rail sense input, not a button.
#define PIN_MCP_BTN_UP 1
#define PIN_MCP_BTN_DOWN 2
#define PIN_MCP_BTN_PAIR 3
#define PIN_MCP_BTN_LEFT 4
#define PIN_MCP_BTN_RIGHT 5

// Outputs on Port A.
#define PIN_MCP_BAT_MON_EN 0
#define PIN_MCP_PWR_PERIPH_EN 1
#define PIN_MCP_SPK_EN 2
#define PIN_MCP_LCD_RST 3
#define PIN_MCP_SLEEP_REQ 4