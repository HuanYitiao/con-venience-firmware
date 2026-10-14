#pragma once
#include <stdint.h>

#define MCP23017_ADDR 0x20

// Register map with IOCON.BANK = 1 (set by ioexpInit). Port A at 0x00..0x0A,
// port B at 0x10..0x1A.
#define MCP_IODIRA 0x00
#define MCP_GPINTENA 0x02
#define MCP_INTFA 0x07
#define MCP_GPIOA 0x09
#define MCP_OLATA 0x0A

#define MCP_IODIRB 0x10
#define MCP_GPINTENB 0x12
#define MCP_DEFVALB 0x13
#define MCP_INTCONB 0x14
#define MCP_GPPUB 0x16
#define MCP_INTFB 0x17
#define MCP_INTCAPB 0x18
#define MCP_GPIOB 0x19

void    ioexpInit();
void    ioexpWriteReg(uint8_t reg, uint8_t val);
uint8_t ioexpReadReg(uint8_t reg);
