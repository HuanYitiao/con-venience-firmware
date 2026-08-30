#include "set_time.h"

#include <Arduino.h>

#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Time sync over SoftAP. The page POSTs a full local wall-clock datetime:
//   { "year":2026, "month":8, "day":30, "hour":14, "minute":9, "second":3 }
// All six fields are always sent and always validated. The full datetime
// (incl. year/month/day) is written to the RTC. Fail-closed: any malformed or
// out-of-range field rejects the whole request; a partial time is never
// written. On success setTime() stamps the RTC validity signature, so the next
// boot's readTrusted() reports trusted = true.
//
// Route binding + HTTP responses live in the SoftAP module (wifi_config), which
// owns the shared WebServer and the idle timer. This file stays server-agnostic.
// ---------------------------------------------------------------------------

static const char SETTIME_PAGE[] = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>con-venience &middot; set time</title>
<style>:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;
display:flex;align-items:center;justify-content:center;font-family:system-ui,-apple-system,
"Segoe UI",Roboto,sans-serif;background:#0f1115;color:#e8eaed}main{width:100%;max-width:22rem;
padding:2rem 1.5rem;text-align:center}.brand{font-size:.8rem;letter-spacing:.18em;
text-transform:uppercase;color:#6b7280;margin-bottom:1.5rem}.status{font-size:1.5rem;
font-weight:600;margin:0 0 .5rem}.clock{font-size:3rem;font-weight:700;
font-variant-numeric:tabular-nums;letter-spacing:.02em;margin:.5rem 0}.detail{font-size:.95rem;
color:#9aa0a6;min-height:1.4em;margin:0}.ok .status{color:#34d399}.fail .status{color:#f87171}
button{margin-top:2rem;width:100%;padding:1rem;font-size:1.1rem;font-weight:600;color:#0f1115;
background:#e8eaed;border:0;border-radius:.75rem}button:active{background:#b9bdc2}
a.back{display:inline-block;margin-top:1.5rem;color:#6b7280;text-decoration:none;font-size:.9rem}
[hidden]{display:none!important}</style></head>
<body><main id="root" class="pending"><div class="brand">con-venience</div>
<h1 class="status" id="status">Syncing time&hellip;</h1><div class="clock" id="clock">--:--:--</div>
<p class="detail" id="detail"></p><button id="retry" hidden>Sync again</button>
<a class="back" href="/">&lsaquo; Settings</a></main>
<script>var root=document.getElementById("root"),status=document.getElementById("status"),
clock=document.getElementById("clock"),detail=document.getElementById("detail"),
retry=document.getElementById("retry");function two(n){return(n<10?"0":"")+n}
async function syncTime(){root.className="pending";status.textContent="Syncing time\u2026";
detail.textContent="";retry.hidden=true;var now=new Date();var payload={year:now.getFullYear(),
month:now.getMonth()+1,day:now.getDate(),hour:now.getHours(),minute:now.getMinutes(),
second:now.getSeconds()};clock.textContent=two(payload.hour)+":"+two(payload.minute)+":"
+two(payload.second);try{var res=await fetch("/settime",{method:"POST",
headers:{"Content-Type":"application/json"},body:JSON.stringify(payload)});
if(!res.ok){throw new Error("HTTP "+res.status)}root.className="ok";status.textContent="Time set";
detail.textContent=payload.year+"-"+two(payload.month)+"-"+two(payload.day)}catch(e){
root.className="fail";status.textContent="Sync failed";detail.textContent=String(e.message||e);
retry.hidden=false}}retry.addEventListener("click",syncTime);
window.addEventListener("load",syncTime);</script></body></html>)HTML";

const char *settimePage(void)
{
    return SETTIME_PAGE;
}

// Days in a given month, leap-year aware. Guards against an impossible date
// (e.g. Feb 30) being persisted from a hand-crafted POST -- a real browser
// never sends one, but fail-closed does not rely on that.
static int daysInMonth(int year, int month)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)
    {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[month - 1];
}

// Extract an integer field, requiring it to be present, an int, and in range.
static bool fieldInRange(JsonDocument &doc, const char *key, int lo, int hi, int &out)
{
    if (!doc[key].is<int>())
    {
        return false;
    }
    int v = doc[key].as<int>();
    if (v < lo || v > hi)
    {
        return false;
    }
    out = v;
    return true;
}

settime_result_t applySetTime(Pcf85063a &rtc, const char *body, size_t len)
{
    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, body, len);
    if (err)
    {
        return SETTIME_BAD_JSON;
    }

    int year, month, day, hour, minute, second;

    // year and month first: the valid range for day depends on them.
    if (!fieldInRange(doc, "year", 2000, 2099, year))
    {
        return SETTIME_BAD_FIELD;
    }
    if (!fieldInRange(doc, "month", 1, 12, month))
    {
        return SETTIME_BAD_FIELD;
    }
    if (!fieldInRange(doc, "day", 1, daysInMonth(year, month), day))
    {
        return SETTIME_BAD_FIELD;
    }
    if (!fieldInRange(doc, "hour", 0, 23, hour))
    {
        return SETTIME_BAD_FIELD;
    }
    if (!fieldInRange(doc, "minute", 0, 59, minute))
    {
        return SETTIME_BAD_FIELD;
    }
    if (!fieldInRange(doc, "second", 0, 59, second))
    {
        return SETTIME_BAD_FIELD;
    }

    pcf_time_t t;
    t.year = (uint16_t)year;
    t.month = (uint8_t)month;
    t.day = (uint8_t)day;
    t.hours = (uint8_t)hour;
    t.minutes = (uint8_t)minute;
    t.seconds = (uint8_t)second;

    if (rtc.setTime(t) != PCF_OK)
    {
        return SETTIME_RTC_FAIL;
    }

    Serial0.printf("[settime] set %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute,
                   second);
    return SETTIME_OK;
}