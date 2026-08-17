// timebase.cpp
//
// See timebase.h for the design notes. Implementation highlights:
//   - Cold boot: deep verify (esp_clk_tree + retry) once + seed wall-clock
//     base via settimeofday.
//   - Wake: mux register fast-check (rtc_clk_slow_src_get) on the hot path.
//   - Wall clock via gettimeofday (maintained by the RTC domain / crystal),
//     continuous across deep sleep; RTC_NOINIT holds only the magic.
//
// Platform: pioarduino, platform-espressif32 55.3.39 / IDF 5.5, ESP32-C6
// Hardware: passive crystal on XTAL_32K_P/N (GPIO0/GPIO1), 2-pin mode

#include "timebase.h"

#include <Arduino.h>

#include <sys/time.h>


extern "C"
{
#include "esp_clk_tree.h"
#include "esp_sleep.h"
#include "soc/clk_tree_defs.h"
#include "soc/rtc.h"

}

// Key fix (after the step2 pitfall): time continuous across deep sleep MUST
// go through gettimeofday. esp_timer_get_time() restarts from ~0 after a
// deep-sleep wake (per the docs: deep sleep does not add back the sleep
// duration); using it as the base makes the wall clock reset -- or even go
// backwards -- every cycle. System time (gettimeofday) is maintained by the
// RTC domain -- now the external crystal -- keeps counting during deep sleep,
// and resumes automatically on wake. So seed the base once via settimeofday
// at cold boot, then always read gettimeofday.

// ---- Parameters ----
static const uint32_t XTAL_SETTLE_TIMEOUT_MS = 2000;
static const uint32_t XTAL_POLL_INTERVAL_MS = 100;
static const uint32_t XTAL_FREQ_NOMINAL = 32768;
static const uint32_t XTAL_FREQ_TOL = 500;

// Retry count for cold-boot deep verify, to absorb esp_clk_tree's occasional
// return of 0 (measured ~25%).
static const uint32_t DEEP_VERIFY_RETRY = 8;

// ---- State preserved across deep sleep ----
// Only the magic is needed to distinguish cold boot from wake. The wall clock
// itself is carried across sleep by system time (RTC domain / crystal), so no
// manual epoch base is stored/replayed in RTC_NOINIT.
#define TIMEBASE_MAGIC 0x54424153u  // 'TBAS'

RTC_NOINIT_ATTR static uint32_t sMagic;

static timebase_status_t sStatus = TIMEBASE_INVALID;

// ---- Internal helpers ----

// Single measurement of the slow clock frequency. May occasionally return 0
// (esp_clk_tree false positive).
static uint32_t measureSlowFreqHzOnce(void)
{
    uint32_t hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_XTAL32K, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &hz);
    return hz;
}

// Deep measurement (with retry): absorbs esp_clk_tree's occasional 0. Used
// only at cold boot. Returns an in-window frequency; 0 if no attempt yields
// a valid value.
static uint32_t measureSlowFreqHzDeep(void)
{
    for (uint32_t i = 0; i < DEEP_VERIFY_RETRY; i++)
    {
        uint32_t hz = measureSlowFreqHzOnce();
        if (hz > XTAL_FREQ_NOMINAL - XTAL_FREQ_TOL && hz < XTAL_FREQ_NOMINAL + XTAL_FREQ_TOL)
        {
            return hz;
        }
        delay(20);
    }
    return 0;
}

// mux register fast-check: zero cost, zero false positive. Answers "is the
// mux on XTAL32K".
static bool muxIsXtal32k(void)
{
    return rtc_clk_slow_src_get() == SOC_RTC_SLOW_CLK_SRC_XTAL32K;
}

// Re-switch crystal + wait for the mux to select it. Does not do a deep
// frequency verify (that is the cold-boot layer's job).
static bool reswitchXtal32k(void)
{
    rtc_clk_32k_enable(true);                            // 2-pin passive crystal, CRYSTAL mode
    rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);  // switch mux + keep domain on in sleep

    uint32_t waited = 0;
    while (waited < XTAL_SETTLE_TIMEOUT_MS)
    {
        delay(XTAL_POLL_INTERVAL_MS);
        waited += XTAL_POLL_INTERVAL_MS;
        if (muxIsXtal32k())
        {
            return true;
        }
    }
    return false;
}

// ---- Public API ----

timebase_status_t timebaseInit(const timebase_config_t *cfg)
{
    bool coldBoot = (sMagic != TIMEBASE_MAGIC);

    if (!reswitchXtal32k())
    {
        // mux failed to select XTAL32K: silent fallback risk, wall clock
        // untrustworthy.
        sStatus = TIMEBASE_FAIL_SRC_LOST;
        Serial0.println("[timebase] FAIL: mux not XTAL32K after re-switch");
        return sStatus;
    }

    if (coldBoot)
    {
        // Cold boot: deep verify physical oscillation (with retry). Whether
        // this board's crystal is good is decided once, here.
        uint32_t hz = measureSlowFreqHzDeep();
        if (hz == 0)
        {
            sStatus = TIMEBASE_FAIL_NO_XTAL;
            Serial0.println("[timebase] FAIL: crystal not oscillating (deep verify)");
            return sStatus;
        }
        Serial0.printf("[timebase] cold boot: crystal verified ~%luHz\n", (unsigned long)hz);

        // Seed the wall-clock base via settimeofday. After this, gettimeofday
        // is maintained by the RTC domain (crystal) and continuous across
        // deep sleep. cfg->epoch0_us==0 sets 0 (start from 0; upper layer may
        // set time later).
        int64_t        epoch0 = (cfg != nullptr) ? cfg->epoch0_us : 0;
        struct timeval tv;
        tv.tv_sec = (time_t)(epoch0 / 1000000LL);
        tv.tv_usec = (suseconds_t)(epoch0 % 1000000LL);
        settimeofday(&tv, nullptr);

        sMagic = TIMEBASE_MAGIC;
    }
    else
    {
        // Wake: mux fast-check already passed above. No wall-clock replay --
        // system time (gettimeofday) keeps counting across deep sleep via the
        // RTC domain and resumes automatically on wake.
        Serial0.println("[timebase] wake: mux ok, system time carried by RTC domain");
    }

    sStatus = TIMEBASE_OK;
    return sStatus;
}

timebase_status_t timebaseStatus(void)
{
    return sStatus;
}

bool timebaseIsValid(void)
{
    return sStatus == TIMEBASE_OK;
}

int64_t timebaseWallNowUs(void)
{
    // Read system time: maintained by the RTC domain (crystal) during deep
    // sleep, continuous across it.
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

int64_t timebaseWallNowS(void)
{
    return timebaseWallNowUs() / 1000000LL;
}

void timebaseDeepSleepUs(uint64_t sleepUs)
{
    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
}