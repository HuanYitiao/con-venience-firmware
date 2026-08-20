#pragma once
#include <Arduino.h>

#include <Wire.h>

typedef enum
{
    PCF_OK = 0,
    PCF_ERR_I2C,
    PCF_ERR_ABSENT
} pcf_status_t;

typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
} pcf_time_t;

class Pcf85063a
{
   public:
    Pcf85063a(TwoWire &bus = Wire, uint8_t addr = 0x51);

    pcf_status_t begin();
    pcf_status_t setTime(const pcf_time_t &t);
    pcf_status_t getTime(pcf_time_t &t, bool &oscStopped);
    pcf_status_t readRamByte(uint8_t &value);
    pcf_status_t writeRamByte(uint8_t value);

   private:
    TwoWire *_bus;
    uint8_t  _addr;

    pcf_status_t writeReg(uint8_t reg, uint8_t value);
    pcf_status_t readRegs(uint8_t reg, uint8_t *buf, uint8_t len);

    static uint8_t bcdToDec(uint8_t v);
    static uint8_t decToBcd(uint8_t v);
};