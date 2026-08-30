#pragma once

#include <stddef.h>

#include "pcf85063a.h"

typedef enum
{
    SETTIME_OK = 0,
    SETTIME_BAD_JSON,
    SETTIME_BAD_FIELD,
    SETTIME_RTC_FAIL
} settime_result_t;

// Returns the embedded auto-posting time-sync page (served on GET /settime).
const char *settimePage(void);

// Parses a {year,month,day,hour,minute,second} JSON body, validates every
// field, and on success writes the full datetime to the RTC (which stamps the
// validity signature). Fail-closed: any malformed or out-of-range field rejects
// the whole request; a partial time is never written. The caller owns the HTTP
// response and idle-timer handling.
settime_result_t applySetTime(Pcf85063a &rtc, const char *body, size_t len);