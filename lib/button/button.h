#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LONG_PRESS_MS 300

typedef struct
{
    bool     pressed;
    bool     prevPressed;
    uint32_t pressTime;
    uint32_t releaseTime;
    uint32_t debounceTime;
} btn_state_t;

typedef enum
{
    BTN_NONE,
    BTN_CLICK,
    BTN_LONG_PRESS
} btn_event_t;

btn_event_t btn_read(int buttonPin, btn_state_t *state);