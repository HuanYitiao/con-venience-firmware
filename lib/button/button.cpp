#include "button.h"

#include <Arduino.h>

#include "io_expander.h"
#include "pins.h"

static volatile bool mcpIntFlag = false;

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

btn_event_t btnRead(bool pressed, btn_state_t *state)
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

void btnInit()
{
    ioexpWriteReg(0x10, 0xFF);
    ioexpWriteReg(0x16, 0x3E);
    ioexpWriteReg(0x12, 0x3E);
    ioexpWriteReg(0x14, 0x00);
    ioexpReadReg(0x19);

    pinMode(PIN_MCP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MCP_INT), [] { mcpIntFlag = true; }, FALLING);
}

btn_events_t btnPoll()
{
    btn_events_t events = {BTN_NONE, BTN_NONE, BTN_NONE, BTN_NONE, BTN_NONE};

    if (!mcpIntFlag)
    {
        return events;
    }
    mcpIntFlag = false;

    uint8_t captured = ioexpReadReg(0x19);

    events.up = btnRead(!((captured >> PIN_MCP_BTN_UP) & 1), &btnUp);
    events.down = btnRead(!((captured >> PIN_MCP_BTN_DOWN) & 1), &btnDown);
    events.left = btnRead(!((captured >> PIN_MCP_BTN_LEFT) & 1), &btnLeft);
    events.right = btnRead(!((captured >> PIN_MCP_BTN_RIGHT) & 1), &btnRight);
    events.pair = btnRead(!((captured >> PIN_MCP_BTN_PAIR) & 1), &btnPair);

    return events;
}