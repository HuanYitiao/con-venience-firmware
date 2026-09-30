#include "power.h"

#include <Arduino.h>

#include "io_expander.h"
#include "pins.h"

static uint32_t lastBatPrintMs = 0;
static bool     batMonStarted = false;

// GPA3 (LCD_RST) is intentionally excluded: it is owned by the display layer.
#define POWER_PORTA_MASK                                                              \
    ((1 << PIN_MCP_BAT_MON_EN) | (1 << PIN_MCP_PWR_PERIPH_EN) | (1 << PIN_MCP_SPK_EN) \
     | (1 << PIN_MCP_SLEEP_REQ))

static void powerSetBit(uint8_t bit, bool on)
{
    uint8_t olat = ioexpReadReg(REG_OLATA);
    if (on)
    {
        olat |= (1 << bit);
    }
    else
    {
        olat &= ~(1 << bit);
    }
    ioexpWriteReg(REG_OLATA, olat);
}

void powerInit()
{
    uint8_t olat = ioexpReadReg(REG_OLATA);
    olat &= ~POWER_PORTA_MASK;
    ioexpWriteReg(REG_OLATA, olat);

    uint8_t iodir = ioexpReadReg(REG_IODIRA);
    iodir &= ~POWER_PORTA_MASK;
    ioexpWriteReg(REG_IODIRA, iodir);

    powerSetPeriphEnable(true);
    powerSetSpeakerEnable(true);
    powerSetSleepReq(false);
}

void powerSetBatMonEnable(bool on)
{
    powerSetBit(PIN_MCP_BAT_MON_EN, on);
}

void powerSetPeriphEnable(bool on)
{
    powerSetBit(PIN_MCP_PWR_PERIPH_EN, on);
}

void powerSetSpeakerEnable(bool on)
{
    powerSetBit(PIN_MCP_SPK_EN, on);
}

void powerSetSleepReq(bool on)
{
    powerSetBit(PIN_MCP_SLEEP_REQ, on);
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

uint16_t powerReadBatteryMv()
{
    powerSetBatMonEnable(true);
    delay(BAT_MON_SETTLE_MS);

    uint32_t adcMv = analogReadMilliVolts(PIN_VBAT_ADC);

    powerSetBatMonEnable(false);

    const float DIVIDER_RATIO = 2.0f;

    return (uint16_t)(adcMv * DIVIDER_RATIO);
}