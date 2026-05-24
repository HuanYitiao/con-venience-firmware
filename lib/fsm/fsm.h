#pragma once

typedef enum
{
    STATE_IDLE,
    STATE_PAIRING,
    STATE_CONTACT_CARD,
    STATE_MENU,
    STATE_CONTACT_LIST,
    STATE_CONTACT_DETAIL,
    STATE_MY_PROFILE,
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

void    fsm_init();
void    fsm_handle_event(event_t event);
state_t fsm_get_state();
int     fsm_get_contact_index();
bool    fsm_get_menu_selection();  // true = Friends, false = MyProfile