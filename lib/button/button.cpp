#include "button.h"

#include <Arduino.h>

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