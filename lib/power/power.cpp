#include "power.h"

#include <Arduino.h>
#include <Preferences.h>

#include "io_expander.h"
#include "pcf85063a.h"
#include "pins.h"

// GPA3 (LCD_RST) is intentionally excluded: it is owned by the display layer.
#define POWER_PORTA_MASK                                                              \
    ((1 << PIN_MCP_BAT_MON_EN) | (1 << PIN_MCP_PWR_PERIPH_EN) | (1 << PIN_MCP_SPK_EN) \
     | (1 << PIN_MCP_SLEEP_REQ))

// TPS63900 soft start + TPS22917 (SD rail, CT=470p) rise, with margin.
#define POWER_PERIPH_SETTLE_MS 10
#define POWER_BAT_SAMPLES 8
// VBAT -> R14/R15 (10k/10k) -> ADC.
#define POWER_BAT_DIVIDER 2
// Longest a button may still be held when powerSleep() is called.
#define POWER_BTN_RELEASE_TIMEOUT_MS 2000
// U1 (TLV840, CT=10u) holds MCU_EN high for ~6 s after +3.3V_MCU comes up.
// Wait a bit longer than that before giving up on the MCU switching off.
#define POWER_U1_HOLD_MAX_MS 8000

static Pcf85063a   *rtcDev = nullptr;
static power_wake_t wakeReason = POWER_WAKE_COLD_BOOT;
static bool         periphOn = false;

static uint32_t lastBatPrintMs = 0;
static bool     batMonStarted = false;

// Lowest VBAT (mV) for each level 1..BATTERY_LEVELS (index 0 unused), roughly
// 1.0..1.47 V per alkaline cell under light load. Tune against real discharge data.
static const uint16_t kBatLevelMv[BATTERY_LEVELS + 1] = {0,    3000, 3200, 3400, 3600,
                                                         3800, 4000, 4200, 4400};
#define BAT_LEVEL_UNKNOWN 0xFF
#define BAT_NVS_NAMESPACE "power"
#define BAT_NVS_KEY "batLevel"

static uint8_t batLevel = BAT_LEVEL_UNKNOWN;

static void powerSetBit(uint8_t bit, bool on)
{
    uint8_t olat = ioexpReadReg(MCP_OLATA);
    if (on)
    {
        olat |= (1 << bit);
    }
    else
    {
        olat &= ~(1 << bit);
    }
    ioexpWriteReg(MCP_OLATA, olat);
}

void powerEarlyInit(Pcf85063a &rtc)
{
    rtcDev = &rtc;

    // 1. Keep-alive first. On a wake from sleep SLEEP_REQ is still 1 and only
    //    /INT is holding MCU_EN high; clearing a wake flag before this write
    //    would reset the MCU straight away. After a battery insert the MCP is
    //    fresh (OLATA = 0, all inputs): SLEEP_REQ floats (no pull-down, R6 is
    //    on LCD_RST) and only U1 keeps the MCU up, for ~6 s.
    uint8_t olat = ioexpReadReg(MCP_OLATA);
    bool    wasAsleep = (olat >> PIN_MCP_SLEEP_REQ) & 1;
    ioexpWriteReg(MCP_OLATA, olat & ~POWER_PORTA_MASK);

    uint8_t iodir = ioexpReadReg(MCP_IODIRA);
    ioexpWriteReg(MCP_IODIRA, iodir & ~POWER_PORTA_MASK);

    // 2. Now it is safe to look at (and release) the wake sources.
    uint8_t btnFlags = ioexpReadReg(MCP_INTFB) & MCP_BTN_MASK;

    uint8_t rtcFlags = 0;
    if (rtc.readFlags(rtcFlags) != PCF_OK)
    {
        rtcFlags = 0;
    }
    // The timer is only armed while asleep. Left running, it would hold the
    // shared INT line low and swallow every later button edge.
    rtc.stopTimer();
    rtc.clearFlags();

    if (!wasAsleep)
    {
        wakeReason = POWER_WAKE_COLD_BOOT;
    }
    else if (btnFlags)
    {
        wakeReason = POWER_WAKE_BUTTON;
    }
    else if (rtcFlags)
    {
        wakeReason = POWER_WAKE_TIMER;
    }
    else
    {
        wakeReason = POWER_WAKE_OTHER;
    }
    periphOn = false;
}

power_wake_t powerGetWakeReason()
{
    return wakeReason;
}

const char *powerWakeName(power_wake_t w)
{
    switch (w)
    {
        case POWER_WAKE_COLD_BOOT:
            return "COLD_BOOT";
        case POWER_WAKE_BUTTON:
            return "BUTTON";
        case POWER_WAKE_TIMER:
            return "TIMER";
        case POWER_WAKE_OTHER:
        default:
            return "OTHER";
    }
}

void powerPeriphOn()
{
    if (periphOn)
    {
        return;
    }
    powerSetBit(PIN_MCP_PWR_PERIPH_EN, true);
    delay(POWER_PERIPH_SETTLE_MS);
    periphOn = true;
}

void powerPeriphOff()
{
    // Amp into shutdown before its supply goes away (no pop).
    powerSetBit(PIN_MCP_SPK_EN, false);
    powerSetBit(PIN_MCP_PWR_PERIPH_EN, false);
    periphOn = false;
}

bool powerPeriphIsOn()
{
    return periphOn;
}

bool powerUsbPresent()
{
    return (ioexpReadReg(MCP_GPIOB) >> PIN_MCP_USB_PWR) & 1;
}

void powerSetSpeakerEnable(bool on)
{
    if (on && !periphOn)
    {
        return;
    }
    powerSetBit(PIN_MCP_SPK_EN, on);
}

void powerSetBatMonEnable(bool on)
{
    powerSetBit(PIN_MCP_BAT_MON_EN, on);
}

uint16_t powerReadBatteryMv()
{
    powerSetBatMonEnable(true);
    delay(BAT_MON_SETTLE_MS);

    uint32_t sum = 0;
    for (int i = 0; i < POWER_BAT_SAMPLES; i++)
    {
        sum += analogReadMilliVolts(PIN_VBAT_ADC);
    }

    powerSetBatMonEnable(false);
    return (uint16_t)(sum / POWER_BAT_SAMPLES * POWER_BAT_DIVIDER);
}

void loopBatteryMonitorTask()
{
    uint32_t now = millis();

    if (!batMonStarted)
    {
        if (now < BOOT_SETTLE_MS)
        {
            return;
        }
        batMonStarted = true;
        lastBatPrintMs = now - BAT_PRINT_INTERVAL_MS;
    }

    if (now - lastBatPrintMs >= BAT_PRINT_INTERVAL_MS)
    {
        lastBatPrintMs = now;
        uint16_t vbatMv = powerReadBatteryMv();
        Serial0.print("VBAT: ");
        Serial0.print(vbatMv);
        Serial0.println(" mV");
    }
}

static uint8_t batLevelFromMv(uint16_t vbatMv)
{
    uint8_t level = 0;
    while (level < BATTERY_LEVELS && vbatMv >= kBatLevelMv[level + 1])
    {
        level++;
    }
    return level;
}

static void batLevelStore(uint8_t level)
{
    Preferences prefs;
    if (prefs.begin(BAT_NVS_NAMESPACE, false))
    {
        prefs.putUChar(BAT_NVS_KEY, level);
        prefs.end();
    }
}

void powerBatteryLevelInit(bool coldBoot)
{
    batLevel = BAT_LEVEL_UNKNOWN;
    if (coldBoot)
    {
        batLevelStore(BAT_LEVEL_UNKNOWN);
        return;
    }
    Preferences prefs;
    if (prefs.begin(BAT_NVS_NAMESPACE, true))
    {
        batLevel = prefs.getUChar(BAT_NVS_KEY, BAT_LEVEL_UNKNOWN);
        prefs.end();
    }
    if (batLevel > BATTERY_LEVELS)
    {
        batLevel = BAT_LEVEL_UNKNOWN;
    }
}

void powerBatteryLevelUpdate(uint16_t vbatMv)
{
    uint8_t level;
    if (batLevel == BAT_LEVEL_UNKNOWN)
    {
        level = batLevelFromMv(vbatMv);
    }
    else
    {
        uint8_t measured = batLevelFromMv(vbatMv + BATTERY_LEVEL_HYST_MV);
        if (measured >= batLevel)
        {
            return;
        }
        level = batLevel - 1;
    }
    Serial0.printf("[power] battery level %u -> %u (%u mV)\n",
                   batLevel == BAT_LEVEL_UNKNOWN ? BATTERY_LEVELS : batLevel, level, vbatMv);
    batLevel = level;
    batLevelStore(level);
}

uint8_t powerBatteryLevel()
{
    // Nothing measured yet (e.g. on USB without a battery): show full.
    return batLevel == BAT_LEVEL_UNKNOWN ? BATTERY_LEVELS : batLevel;
}

void powerSleep(uint32_t wakeAfterSec)
{
    // 1. Let go of the button first, so the release can't count as a wake.
    uint32_t t0 = millis();
    while ((ioexpReadReg(MCP_GPIOB) & MCP_BTN_MASK) != MCP_BTN_MASK
           && millis() - t0 < POWER_BTN_RELEASE_TIMEOUT_MS)
    {
        delay(10);
    }

    // 2. Buttons: interrupt only while a pin differs from DEFVAL (= pressed).
    //    btnInit() switches back to interrupt-on-change on the next boot.
    ioexpWriteReg(MCP_DEFVALB, MCP_BTN_MASK);
    ioexpWriteReg(MCP_INTCONB, MCP_BTN_MASK);
    ioexpWriteReg(MCP_GPINTENB, MCP_BTN_MASK);
    ioexpReadReg(MCP_GPIOB);

    // 3. RTC.
    if (rtcDev)
    {
        if (wakeAfterSec > 0)
        {
            rtcDev->startTimer(wakeAfterSec);
        }
        else
        {
            rtcDev->stopTimer();
            rtcDev->clearFlags();
        }
    }

    // 4. Everything off, then pull the plug in a separate write so the rails
    //    are already down when MCU_EN drops.
    uint8_t olat = ioexpReadReg(MCP_OLATA);
    olat &= ~((1 << PIN_MCP_BAT_MON_EN) | (1 << PIN_MCP_PWR_PERIPH_EN) | (1 << PIN_MCP_SPK_EN));
    ioexpWriteReg(MCP_OLATA, olat);
    periphOn = false;

    Serial0.printf("[power] sleep, timer=%lus\n", (unsigned long)wakeAfterSec);
    Serial0.flush();

    ioexpWriteReg(MCP_OLATA, olat | (1 << PIN_MCP_SLEEP_REQ));

    // 5. Normally the MCU is gone by now. Still running means either
    //    - /INT is low: a wake source is pending -> restart with SLEEP_REQ
    //      still set, so the next powerEarlyInit() reports that wake reason;
    //    - /INT is high: U1 is still in its power-up window -> wait for it to
    //      release MCU_EN, which switches the MCU off.
    uint32_t t1 = millis();
    while (millis() - t1 < POWER_U1_HOLD_MAX_MS)
    {
        if (digitalRead(PIN_MCP_INT) == LOW)
        {
            break;
        }
        delay(10);
    }
    esp_restart();
}
