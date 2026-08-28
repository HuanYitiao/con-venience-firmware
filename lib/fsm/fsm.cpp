#include "fsm.h"

#include <esp32-hal.h>

enum
{
    MENU_PROFILE = 0,
    MENU_FRIENDS,
    MENU_SETTINGS,
    MENU_COUNT
};

static state_t       currentState = STATE_IDLE;
static state_t       prePairingState = STATE_IDLE;
static state_t       preProfileState = STATE_MENU;
static int           contactIndex = 0;
static unsigned long stateEnterTime = 0;
static int           menuIndex = MENU_PROFILE;
static int           linkIndex = 0;

void fsmInit()
{
    currentState = STATE_IDLE;
    prePairingState = STATE_IDLE;
    preProfileState = STATE_MENU;
    contactIndex = 0;
    stateEnterTime = millis();
    menuIndex = MENU_PROFILE;
    linkIndex = 0;
}

state_t fsmGetState()
{
    return currentState;
}

int fsmGetContactIndex()
{
    return contactIndex;
}

int fsmGetMenuSelection()
{
    return menuIndex;
}

int fsmGetLinkIndex()
{
    return linkIndex;
}

bool fsmIsViewingSelf()
{
    return preProfileState == STATE_MENU;
}

const char *eventName(event_t e)
{
    switch (e)
    {
        case EVENT_PAIRING_CLICK:
            return "PAIRING_CLICK";
        case EVENT_PAIRING_LONG_PRESS:
            return "PAIRING_LONG_PRESS";
        case EVENT_PAIRING_OVERTIME:
            return "PAIRING_OVERTIME";
        case EVENT_CARD_OVERTIME:
            return "CARD_OVERTIME";
        case EVENT_LEFT_CLICK:
            return "LEFT_CLICK";
        case EVENT_LEFT_LONG_PRESS:
            return "LEFT_LONG_PRESS";
        case EVENT_RIGHT_CLICK:
            return "RIGHT_CLICK";
        case EVENT_RIGHT_LONG_PRESS:
            return "RIGHT_LONG_PRESS";
        case EVENT_UP_CLICK:
            return "UP_CLICK";
        case EVENT_UP_LONG_PRESS:
            return "UP_LONG_PRESS";
        case EVENT_DOWN_CLICK:
            return "DOWN_CLICK";
        case EVENT_DOWN_LONG_PRESS:
            return "DOWN_LONG_PRESS";
        case EVENT_ACOM_SUCCESS:
            return "ACOM_SUCCESS";
        case EVENT_ACOM_FAILURE:
            return "ACOM_FAILURE";
        case EVENT_BLE_TRANSFER_START:
            return "BLE_TRANSFER_START";
        case EVENT_BLE_SUCCESS:
            return "BLE_SUCCESS";
        case EVENT_BLE_FAILURE:
            return "BLE_FAILURE";
        case EVENT_ENTER_SETTINGS:
            return "ENTER_SETTINGS";
        case EVENT_BATTERY_LOW:
            return "BATTERY_LOW";
        case EVENT_OVERTIME_SHUTDOWN:
            return "OVERTIME_SHUTDOWN";
        default:
            return "UNKNOWN";
    }
}

const char *stateName(state_t s)
{
    switch (s)
    {
        case STATE_IDLE:
            return "IDLE";
        case STATE_PAIRING:
            return "PAIRING";
        case STATE_CONTACT_CARD:
            return "CONTACT_CARD";
        case STATE_MENU:
            return "MENU";
        case STATE_CONTACT_LIST:
            return "CONTACT_LIST";
        case STATE_PROFILE_AVATAR:
            return "PROFILE_AVATAR";
        case STATE_PROFILE_LINKS:
            return "PROFILE_LINKS";
        case STATE_PROFILE_QR:
            return "PROFILE_QR";
        case STATE_SETTINGS:
            return "SETTINGS";
        case STATE_STANDBY:
            return "STANDBY";
        case STATE_LOW_BATTERY:
            return "LOW_BATTERY";
        case STATE_BLE_CONNECTING:
            return "BLE_CONNECTING";
        case STATE_BLE_TRANSFER:
            return "BLE_TRANSFER";
        default:
            return "UNKNOWN";
    }
}

void fsmHandleEvent(event_t event)
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
                case EVENT_RIGHT_CLICK:
                    currentState = STATE_MENU;
                    stateEnterTime = millis();
                    break;
                case EVENT_ENTER_SETTINGS:
                    currentState = STATE_SETTINGS;
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
                case EVENT_PAIRING_OVERTIME:
                case EVENT_ACOM_FAILURE:
                    currentState = prePairingState;
                    stateEnterTime = millis();
                    break;
                case EVENT_ACOM_SUCCESS:
                    currentState = STATE_BLE_CONNECTING;
                    stateEnterTime = millis();
                    break;
                default:
                    break;
            }
            break;

        case STATE_CONTACT_CARD:
            switch (event)
            {
                case EVENT_PAIRING_LONG_PRESS:
                case EVENT_CARD_OVERTIME:
                    currentState = STATE_IDLE;
                    stateEnterTime = millis();
                    break;
                case EVENT_BATTERY_LOW:
                    currentState = STATE_LOW_BATTERY;
                    break;
                default:
                    break;
            }
            break;

        case STATE_MENU:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_IDLE;
                    stateEnterTime = millis();
                    break;
                case EVENT_UP_CLICK:
                    menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
                    break;
                case EVENT_DOWN_CLICK:
                    menuIndex = (menuIndex + 1) % MENU_COUNT;
                    break;
                case EVENT_RIGHT_CLICK:
                    switch (menuIndex)
                    {
                        case MENU_PROFILE:
                            preProfileState = currentState;
                            currentState = STATE_PROFILE_AVATAR;
                            break;
                        case MENU_FRIENDS:
                            currentState = STATE_CONTACT_LIST;
                            break;
                        case MENU_SETTINGS:
                            currentState = STATE_SETTINGS;
                            break;
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
                    preProfileState = currentState;
                    currentState = STATE_PROFILE_AVATAR;
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

        case STATE_PROFILE_AVATAR:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = preProfileState;
                    stateEnterTime = millis();
                    break;
                case EVENT_RIGHT_CLICK:
                    currentState = STATE_PROFILE_LINKS;
                    linkIndex = 0;
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

        case STATE_PROFILE_LINKS:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_PROFILE_AVATAR;
                    stateEnterTime = millis();
                    break;
                case EVENT_UP_CLICK:
                    if (linkIndex > 0)
                        linkIndex--;
                    break;
                case EVENT_DOWN_CLICK:
                    linkIndex++;
                    break;
                case EVENT_RIGHT_CLICK:
                    currentState = STATE_PROFILE_QR;
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

        case STATE_PROFILE_QR:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_PROFILE_LINKS;
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

        case STATE_SETTINGS:
            switch (event)
            {
                case EVENT_LEFT_CLICK:
                    currentState = STATE_MENU;  // side effects (AP down / BLE up) in dispatchEvent
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

        case STATE_BLE_CONNECTING:
            switch (event)
            {
                case EVENT_BLE_TRANSFER_START:
                    currentState = STATE_BLE_TRANSFER;
                    stateEnterTime = millis();
                    break;
                // A fast/tiny exchange may complete before the main loop ever
                // observes the transfer sub-state -- accept success here too.
                case EVENT_BLE_SUCCESS:
                    currentState = STATE_CONTACT_CARD;
                    stateEnterTime = millis();
                    break;
                case EVENT_PAIRING_LONG_PRESS:
                case EVENT_BLE_FAILURE:
                    currentState = prePairingState;
                    stateEnterTime = millis();
                    break;
                default:
                    break;
            }
            break;

        case STATE_BLE_TRANSFER:
            switch (event)
            {
                case EVENT_BLE_SUCCESS:
                    currentState = STATE_CONTACT_CARD;
                    stateEnterTime = millis();
                    break;
                case EVENT_PAIRING_LONG_PRESS:
                case EVENT_BLE_FAILURE:
                    currentState = prePairingState;
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

unsigned long fsmGetStateEnterTime()
{
    return stateEnterTime;
}