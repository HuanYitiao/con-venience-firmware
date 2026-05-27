#include <Arduino.h>

#include "button.h"
#include "display.h"
#include "fsm.h"
#include "storage.h"

#define PIN_UP 2
#define PIN_DOWN 3
#define PIN_PAIR 21
#define PIN_LEFT 19
#define PIN_RIGHT 20

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

static Contact self = {};
static Contact contacts[16] = {};
static int     contactCount = 0;
static bool    idleShowQR = false;

void setup()
{
    Serial0.begin(115200);
    fsm_init();
    storageInit();
    storageLoadSelf(self);
    contactCount = storageLoadContacts(contacts, 16);
    displayInit();

    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_PAIR, INPUT_PULLUP);
    pinMode(PIN_LEFT, INPUT_PULLUP);
    pinMode(PIN_RIGHT, INPUT_PULLUP);

    Serial0.println("con-venience ready");
    Serial0.printf("Initial state: %s\n", stateName(fsm_get_state()));
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
            Serial0.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                           stateName(after));
        else
            Serial0.printf("[EVENT] %-20s | %s (no change)\n", eventName(event), stateName(before));

        if (after == STATE_MENU)
            Serial0.printf("[MENU]  selection: %s\n",
                           fsm_get_menu_selection() ? "Friends" : "My Profile");

        if (event == EVENT_PAIRING_CLICK && before == STATE_IDLE)
            idleShowQR = !idleShowQR;
    }

    displayRender(fsm_get_state(), self, contacts, contactCount, fsm_get_contact_index(),
                  fsm_get_menu_selection(), idleShowQR);
}