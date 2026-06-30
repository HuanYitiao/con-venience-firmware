#include "button.h"

#include <Arduino.h>

#include <Wire.h>

#include "pins.h"

static volatile bool mcpIntFlag = false;

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

static void mcp_write_reg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(MCP23008_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t mcp_read_gpio()
{
    Wire.beginTransmission(MCP23008_ADDR);
    Wire.write(0x09);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MCP23008_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

btn_event_t btn_read(bool pressed, btn_state_t *state)
{
    bool current = pressed;

    if (current != state->pressed)
    {
        if (millis() - state->debounceTime < 20)
        {
            return BTN_NONE;
        }
        state->debounceTime = millis();
    }

    state->pressed = current;
    if (!state->prevPressed && state->pressed)
    {
        state->prevPressed = true;
        state->pressTime = millis();
        return BTN_NONE;
    }

    if (state->prevPressed && !state->pressed)
    {
        state->prevPressed = false;
        state->releaseTime = millis();
        if (LONG_PRESS_MS < state->releaseTime - state->pressTime)
        {
            return BTN_LONG_PRESS;
        }
        else
        {
            return BTN_CLICK;
        }
    }
    return BTN_NONE;
}

void btn_init()
{
    mcp_write_reg(0x00, 0x1F);
    mcp_write_reg(0x06, 0xFF);
    mcp_write_reg(0x02, 0x1F);
    mcp_write_reg(0x03, 0x1F);
    mcp_write_reg(0x04, 0x00);
    mcp_write_reg(0x05, 0x04);
    mcp_read_gpio();

    pinMode(PIN_MCP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MCP_INT), [] { mcpIntFlag = true; }, FALLING);
}

btn_events_t btn_poll()
{
    btn_events_t events = {BTN_NONE, BTN_NONE, BTN_NONE, BTN_NONE, BTN_NONE};

    if (!mcpIntFlag)
    {
        return events;
    }
    mcpIntFlag = false;

    uint8_t captured = mcp_read_gpio();

    events.up = btn_read(!((captured >> MCP_PIN_BTN_UP) & 1), &btnUp);
    events.down = btn_read(!((captured >> MCP_PIN_BTN_DOWN) & 1), &btnDown);
    events.left = btn_read(!((captured >> MCP_PIN_BTN_LEFT) & 1), &btnLeft);
    events.right = btn_read(!((captured >> MCP_PIN_BTN_RIGHT) & 1), &btnRight);
    events.pair = btn_read(!((captured >> MCP_PIN_BTN_PAIR) & 1), &btnPair);

    return events;
}