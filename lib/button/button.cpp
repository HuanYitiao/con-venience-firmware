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
    ioexpWriteReg(MCP_IODIRB, 0xFF);
    ioexpWriteReg(MCP_GPPUB, MCP_BTN_MASK);
    ioexpWriteReg(MCP_GPINTENB, MCP_BTN_MASK);
    ioexpWriteReg(MCP_INTCONB, 0x00);  // interrupt on change (powerSleep switches to DEFVAL)
    ioexpReadReg(MCP_GPIOB);

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

    uint8_t captured = ioexpReadReg(MCP_GPIOB);

    events.up = btnRead(!((captured >> PIN_MCP_BTN_UP) & 1), &btnUp);
    events.down = btnRead(!((captured >> PIN_MCP_BTN_DOWN) & 1), &btnDown);
    events.left = btnRead(!((captured >> PIN_MCP_BTN_LEFT) & 1), &btnLeft);
    events.right = btnRead(!((captured >> PIN_MCP_BTN_RIGHT) & 1), &btnRight);
    events.pair = btnRead(!((captured >> PIN_MCP_BTN_PAIR) & 1), &btnPair);

    return events;
}