// timebase.h
//
// Prototype library: drive the ESP32-C6 internal RTC from an external
// 32.768kHz passive crystal, providing a stable wall clock to upper layers.
//
// Two things validated on hardware (step1 / step2):
//   - The RC source locked into the precompiled IDF libs can be switched to
//     the external crystal at runtime via rtc_clk_slow_src_set.
//   - After deep-sleep wake the runtime clock state falls back to RC, so the
//     crystal must be re-switched on every wake; re-switching is stably OK.
//
// Design boundaries (intentionally out of scope at prototype stage):
//   - Does not own wake sources: GPIO / RTC INT wakeup is left to the caller
//     via esp_sleep_enable_*.
//   - Does not integrate countdown / timetable: only exposes a wall-clock us
//     interface; upper layers build on top.
//   - Does not decide the failure response: on failure it sets invalid,
//     returns a fail status, and logs; the caller decides halt/LED/degrade.
//
// Oscillation check uses a two-layer structure (forced by step2 data):
//   - Cold-boot layer: esp_clk_tree deep measurement (with retry, to absorb
//     its ~25% occasional false-zero). Answers "is the crystal on this board
//     physically oscillating". Slowness is fine; done once.
//   - Per-wake layer: rtc_clk_slow_src_get reads the mux register bit
//     (zero cost, zero false positive). Answers "did this re-switch succeed,
//     any silent fallback to RC". Safe to put on the hot path.

#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C"
{
#endif

    // Wall-clock base (epoch microseconds). Provided once at cold boot (seeded
    // via settimeofday). No replay needed on wake -- system time is carried
    // across deep sleep by the RTC domain (crystal).
    // Pass 0 to mean "wall clock starts from 0; upper layer may set time later".
    typedef struct
    {
        int64_t epoch0_us;  // Wall clock (us) at cold boot. 0 = start from 0.
    } timebase_config_t;

    // Init result / health status.
    typedef enum
    {
        TIMEBASE_OK = 0,         // Crystal running, wall clock trustworthy
        TIMEBASE_FAIL_NO_XTAL,   // Cold-boot deep verify failed: crystal not
                                 // oscillating (suspect cold/missing solder/caps)
        TIMEBASE_FAIL_SRC_LOST,  // mux not on XTAL32K after re-switch: silent
                                 // fallback risk
        TIMEBASE_INVALID,        // Not yet init, or judged untrustworthy
    } timebase_status_t;

    // Call on both cold boot and wake. Distinguishes internally (via RTC_NOINIT
    // magic):
    //   - Cold boot: switch crystal + deep verify (with retry) + seed wall-clock
    //     base from cfg via settimeofday.
    //   - Wake: re-switch crystal + mux fast-check; system time carries the wall
    //     clock across sleep, no replay needed.
    // Returns TIMEBASE_OK if the wall clock is trustworthy now; otherwise the
    // caller decides how to fail-closed based on status.
    timebase_status_t timebaseInit(const timebase_config_t *cfg);

    // Current health status (does not re-check; returns the last init verdict).
    timebase_status_t timebaseStatus(void);

    // Convenience query for "wall clock trustworthy" == TIMEBASE_OK.
    bool timebaseIsValid(void);

    // Current wall clock (epoch microseconds). Read from system time, which the
    // RTC domain (crystal) maintains continuously across deep sleep.
    // Note: when status is not OK the value is not trustworthy; the caller should
    // check timebaseIsValid() first.
    int64_t timebaseWallNowUs(void);

    // Current wall clock (epoch seconds). Convenience wrapper.
    int64_t timebaseWallNowS(void);

    // Optional helper: enter deep sleep for the given microseconds (timer wake).
    // At prototype stage this only wraps the timer wake; for GPIO / RTC INT wake,
    // call esp_sleep_enable_* yourself before calling this -- it does not clear
    // any wake sources you have already registered.
    void timebaseDeepSleepUs(uint64_t sleepUs);

#ifdef __cplusplus
}
#endif

#endif  // TIMEBASE_H