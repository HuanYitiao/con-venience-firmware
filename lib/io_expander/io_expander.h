#pragma once
#include <stdint.h>

#define MCP23017_ADDR 0x20

void    ioexpInit();
void    ioexpWriteReg(uint8_t reg, uint8_t val);
uint8_t ioexpReadReg(uint8_t reg);