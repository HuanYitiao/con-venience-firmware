#pragma once

typedef enum
{
    STATE_IDLE,
    STATE_PAIRING,
    STATE_CONTACT_CARD,
    STATE_MENU,
    STATE_CONTACT_LIST,
    STATE_PROFILE_AVATAR,
    STATE_PROFILE_LINKS,
    STATE_PROFILE_QR,
    STATE_STANDBY,
    STATE_LOW_BATTERY
} state_t;

typedef enum
{
    EVENT_PAIRING_CLICK,
    EVENT_PAIRING_LONG_PRESS,
    EVENT_PAIRING_OVERTIME,
    EVENT_CARD_OVERTIME,
    EVENT_LEFT_CLICK,
    EVENT_LEFT_LONG_PRESS,
    EVENT_RIGHT_CLICK,
    EVENT_RIGHT_LONG_PRESS,
    EVENT_UP_CLICK,
    EVENT_UP_LONG_PRESS,
    EVENT_DOWN_CLICK,
    EVENT_DOWN_LONG_PRESS,
    EVENT_ACOM_SUCCESS,
    EVENT_ACOM_FAILURE,
    EVENT_BATTERY_LOW,
    EVENT_OVERTIME_SHUTDOWN
} event_t;

const char *eventName(event_t e);
const char *stateName(state_t s);

void    fsmInit();
void    fsmHandleEvent(event_t event);
state_t fsmGetState();
int     fsmGetContactIndex();
bool    fsmGetMenuSelection();
int     fsmGetLinkIndex();
bool    fsmIsViewingSelf();