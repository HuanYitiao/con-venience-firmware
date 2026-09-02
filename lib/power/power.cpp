#include "power.h"

#include "io_expander.h"
#include "pins.h"

#define REG_IODIRA 0x00
#define REG_OLATA 0x0A

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