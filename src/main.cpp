#include <Arduino.h>

#include "button.h"
#include "fsm.h"

#define PIN_UP 32
#define PIN_DOWN 33
#define PIN_PAIR 5
#define PIN_LEFT 19
#define PIN_RIGHT 18

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

void setup()
{
    Serial.begin(115200);
    fsm_init();

    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_PAIR, INPUT_PULLUP);
    pinMode(PIN_LEFT, INPUT_PULLUP);
    pinMode(PIN_RIGHT, INPUT_PULLUP);

    Serial.println("con-venience FSM test ready");
    Serial.printf("Initial state: %s\n", stateName(fsm_get_state()));
}

void loop()
{
    btn_event_t upEvent = btn_read(PIN_UP, &btnUp);
    btn_event_t downEvent = btn_read(PIN_DOWN, &btnDown);
    btn_event_t pairEvent = btn_read(PIN_PAIR, &btnPair);
    btn_event_t leftEvent = btn_read(PIN_LEFT, &btnLeft);
    btn_event_t rightEvent = btn_read(PIN_RIGHT, &btnRight);

    event_t event;
    bool    hasEvent = false;

    if (upEvent == BTN_CLICK)
    {
        event = EVENT_UP_CLICK;
        hasEvent = true;
    }
    else if (upEvent == BTN_LONG_PRESS)
    {
        event = EVENT_UP_LONG_PRESS;
        hasEvent = true;
    }
    else if (downEvent == BTN_CLICK)
    {
        event = EVENT_DOWN_CLICK;
        hasEvent = true;
    }
    else if (downEvent == BTN_LONG_PRESS)
    {
        event = EVENT_DOWN_LONG_PRESS;
        hasEvent = true;
    }
    else if (pairEvent == BTN_CLICK)
    {
        event = EVENT_PAIRING_CLICK;
        hasEvent = true;
    }
    else if (pairEvent == BTN_LONG_PRESS)
    {
        event = EVENT_PAIRING_LONG_PRESS;
        hasEvent = true;
    }
    else if (leftEvent == BTN_CLICK)
    {
        event = EVENT_LEFT_CLICK;
        hasEvent = true;
    }
    else if (leftEvent == BTN_LONG_PRESS)
    {
        event = EVENT_LEFT_LONG_PRESS;
        hasEvent = true;
    }
    else if (rightEvent == BTN_CLICK)
    {
        event = EVENT_RIGHT_CLICK;
        hasEvent = true;
    }
    else if (rightEvent == BTN_LONG_PRESS)
    {
        event = EVENT_RIGHT_LONG_PRESS;
        hasEvent = true;
    }

    if (hasEvent)
    {
        state_t before = fsm_get_state();
        fsm_handle_event(event);
        state_t after = fsm_get_state();

        if (before != after)
        {
            Serial.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                          stateName(after));
        }
        else
        {
            Serial.printf("[EVENT] %-20s | %s (no change)\n", eventName(event), stateName(before));
        }

        if (after == STATE_MENU)
        {
            Serial.printf("[MENU]  selection: %s\n",
                          fsm_get_menu_selection() ? "Friends" : "My Profile");
        }
    }
}