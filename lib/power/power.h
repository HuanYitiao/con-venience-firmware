#pragma once
#include <stdint.h>

class Pcf85063a;

#define BOOT_SETTLE_MS 10000
#define BAT_MON_SETTLE_MS 450
#define BAT_PRINT_INTERVAL_MS 30000

#define BATTERY_CHECK_INTERVAL_MS 30000
#define BATTERY_LOW_MV 3000
#define BATTERY_LOW_CONFIRM_COUNT 3

// How the MCU is kept alive / switched off (DigiFur Rev1.0, sheet MCU):
//
//   MCU_EN = NAND(U1 RESET, /INT, MCU_SLEEP_REQ)   (U2 NL17SZ10 -> ESP32 EN)
//
//   - SLEEP_REQ (MCP GPA4) = 0  -> MCU runs.
//   - SLEEP_REQ = 1 and /INT high -> ESP32 is held in reset (off).
//   - /INT pulled low by MCP INTB (button) or PCF85063A INT (timer) -> MCU_EN
//     goes high and the ESP32 cold-boots. INT stays low until the source is
//     cleared, so clearing it before SLEEP_REQ is dropped kills the MCU again.
//   - U1 holds MCU_EN high for a few seconds after +3.3V_MCU comes up, so a
//     fresh battery always boots the MCU.
//
// The MCP23017 and RTC live on the always-on +3.3V_MCU rail and keep their
// state while the ESP32 is off.

typedef enum
{
    POWER_WAKE_COLD_BOOT,  // battery inserted / +3.3V_MCU came up
    POWER_WAKE_BUTTON,     // MCP23017 INTB
    POWER_WAKE_TIMER,      // PCF85063A countdown timer or alarm
    POWER_WAKE_OTHER,      // was asleep, but no wake flag found
} power_wake_t;

// Call right after Wire.begin() + ioexpInit(), and BEFORE btnInit() or
// rtc.begin(): it drops SLEEP_REQ first, then records and clears the wake
// flags. Also puts every power output (Port A) into its off state.
void         powerEarlyInit(Pcf85063a &rtc);
power_wake_t powerGetWakeReason();
const char  *powerWakeName(power_wake_t w);

// +3.3V_PERIPH (speaker amp, LCD backlight, and +3.3V_SD via the card-detect
// load switch). On: waits for the rails to settle. Off: speaker amp is shut
// down first. Callers must stop I2S and SD before switching off.
void powerPeriphOn();
void powerPeriphOff();
bool powerPeriphIsOn();

// Ignored while +3.3V_PERIPH is off (would back-power the amp via SD_MODE).
void powerSetSpeakerEnable(bool on);
void powerSetBatMonEnable(bool on);

// Switches the battery monitor on, samples VBAT, switches it off again.
uint16_t powerReadBatteryMv();
void     loopBatteryMonitorTask();  // debug: prints VBAT every BAT_PRINT_INTERVAL_MS

// Turns the MCU off. Wakes on a button press, and after wakeAfterSec seconds
// if non-zero (RTC countdown, periodic). Peripherals must already be shut
// down (see shutdownDevice() in main.cpp). Does not return: if a wake source
// is already pending the MCU stays powered and restarts instead, and the next
// boot reports that wake reason.
[[noreturn]] void powerSleep(uint32_t wakeAfterSec);
