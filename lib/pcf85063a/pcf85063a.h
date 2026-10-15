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
    uint16_t year;     // full year, e.g. 2026 (RTC stores year % 100)
    uint8_t  month;    // 1..12
    uint8_t  day;      // 1..31
    uint8_t  hours;    // 0..23
    uint8_t  minutes;  // 0..59
    uint8_t  seconds;  // 0..59
} pcf_time_t;

// Control_2 interrupt flags, as returned by readFlags().
#define PCF_FLAG_AF 0x40  // alarm
#define PCF_FLAG_TF 0x08  // countdown timer

class Pcf85063a
{
   public:
    Pcf85063a(TwoWire &bus = Wire, uint8_t addr = 0x51);

    pcf_status_t begin();
    pcf_status_t setTime(const pcf_time_t &t);
    pcf_status_t getTime(pcf_time_t &t, bool &oscStopped);
    pcf_status_t readRamByte(uint8_t &value);
    pcf_status_t writeRamByte(uint8_t value);
    pcf_status_t readTrusted(pcf_time_t &t, bool &trusted);

    // Countdown timer driving the open-drain INT pin (level mode: INT stays
    // low until clearFlags()). Periodic: reloads and keeps firing. Up to 255 s
    // at 1 Hz, beyond that rounded up to whole minutes (max 255 min).
    pcf_status_t startTimer(uint32_t seconds);
    pcf_status_t stopTimer();
    pcf_status_t readFlags(uint8_t &flags);
    pcf_status_t clearFlags();

   private:
    TwoWire *_bus;
    uint8_t  _addr;

    pcf_status_t writeReg(uint8_t reg, uint8_t value);
    pcf_status_t readRegs(uint8_t reg, uint8_t *buf, uint8_t len);

    static uint8_t bcdToDec(uint8_t v);
    static uint8_t decToBcd(uint8_t v);
};