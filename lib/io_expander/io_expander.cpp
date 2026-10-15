#include "io_expander.h"

#include <Arduino.h>

#include <Wire.h>

void ioexpWriteReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t ioexpReadReg(uint8_t reg)
{
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MCP23017_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// The MCP23017 sits on the always-on +3.3V_MCU rail, so after a sleep/wake
// cycle it is still in BANK=1 while after a battery insert it is in BANK=0.
// 0x05 is IOCON in BANK=1 (-> drop to BANK=0) and GPINTENB in BANK=0; 0x0A is
// IOCON in BANK=0, so the second write lands on IOCON either way.
// ODR stays set in the intermediate write: INT is wired-OR with the RTC's
// open-drain INT, and a push-pull INTB would fight it.
// This does not touch OLATA, so MCU_SLEEP_REQ survives (see powerEarlyInit).
void ioexpInit()
{
    ioexpWriteReg(0x05, 0x04);
    ioexpWriteReg(0x0A, 0x84);  // BANK=1, MIRROR=0, ODR=1
}