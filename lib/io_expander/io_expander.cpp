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

void ioexpInit()
{
    ioexpWriteReg(0x05, 0x00);
    ioexpWriteReg(0x0A, 0x84);
}