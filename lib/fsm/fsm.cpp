#include "fsm.h"

#include <esp32-hal.h>

static state_t       currentState = STATE_IDLE;
static state_t       prePairingState = STATE_IDLE;
static int           contactIndex = 0;
static unsigned long stateEnterTime = 0;
static bool          menuOnFriends = true;

void fsm_init()
{
    currentState = STATE_IDLE;
    prePairingState = STATE_IDLE;
    contactIndex = 0;
    stateEnterTime = millis();
}

state_t fsm_get_state()
{
    return currentState;
}

int fsm_get_contact_index()
{
    return contactIndex;
}

bool fsm_get_menu_selection()
{
    return menuOnFriends;
}

void fsm_handle_event(event_t event)
{
    switch (currentState)
    {
        case STATE_IDLE:
            switch (event)
            {
                case EVENT_PAIRING_CLICK:
                    currentState = STATE_IDLE;  // Switch between QR code/avatar handled by display
                    break;
                case EVENT_PAIRING_LONG_PRESS:
                    prePairingState = currentState;
                    currentState = STATE_PAIRING;
                    stateEnterTime = millis();
                    break;
                case EVENT_DOWN_CLICK:
                    currentState = STATE_MENU;
                    stateEnterTime = millis();
                    break;
                case EVENT_OVERTIME_SHUTDOWN:
                    currentState = STATE_STANDBY;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_PAIRING:
            switch (event)
            {
                case EVENT_PAIRING_LONG_PRESS:
                    currentState = prePairingState;
                    stateEnterTime = millis();
                    break;
                case EVENT_PAIRING_OVERTIME:
                    currentState = prePairingState;
                    stateEnterTime = millis();
                    break;
                case EVENT_ACOM_SUCCESS:
                    currentState = STATE_CONTACT_CARD;
                    stateEnterTime = millis();
                    break;
                case EVENT_ACOM_FAILURE:
                    currentState = prePairingState;
                    stateEnterTime = millis();
                    break;
                default:
                    break;
            }
            break;

        case STATE_CONTACT_CARD:
            switch (event)
            {
                case EVENT_CARD_OVERTIME:
                    currentState = STATE_IDLE;
                    stateEnterTime = millis();
                    break;
                default:
                    break;
            }
            break;

        case STATE_MENU:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                case EVENT_UP_CLICK:
                    currentState = STATE_IDLE;
                    stateEnterTime = millis();
                    break;
                case EVENT_DOWN_CLICK:
                    menuOnFriends = !menuOnFriends;
                    break;
                case EVENT_RIGHT_CLICK:
                    if (menuOnFriends)
                    {
                        currentState = STATE_CONTACT_LIST;
                    }
                    else
                    {
                        currentState = STATE_MY_PROFILE;
                    }
                    stateEnterTime = millis();
                    break;
                case EVENT_OVERTIME_SHUTDOWN:
                    currentState = STATE_STANDBY;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_CONTACT_LIST:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_MENU;
                    stateEnterTime = millis();
                    break;
                case EVENT_UP_CLICK:
                    if (contactIndex > 0)
                        contactIndex--;
                    break;
                case EVENT_DOWN_CLICK:
                    contactIndex++;
                    break;
                case EVENT_RIGHT_CLICK:
                    currentState = STATE_CONTACT_DETAIL;
                    stateEnterTime = millis();
                    break;
                case EVENT_OVERTIME_SHUTDOWN:
                    currentState = STATE_STANDBY;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_CONTACT_DETAIL:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_CONTACT_LIST;
                    stateEnterTime = millis();
                    break;
                case EVENT_OVERTIME_SHUTDOWN:
                    currentState = STATE_STANDBY;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_MY_PROFILE:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_MENU;
                    stateEnterTime = millis();
                    break;
                case EVENT_OVERTIME_SHUTDOWN:
                    currentState = STATE_STANDBY;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_STANDBY:
            switch (event)
            {
                case EVENT_PAIRING_LONG_PRESS:
                    currentState = STATE_IDLE;
                    stateEnterTime = millis();
                    break;
                default:
                    break;
            }
            break;

        case STATE_LOW_BATTERY:
            // Completely locked, does not respond to any events
            break;
    }
}