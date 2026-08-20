#include "pcf85063a.h"

// ---- register map ----
static const uint8_t REG_CONTROL_1 = 0x00;
static const uint8_t REG_CONTROL_2 = 0x01;
static const uint8_t REG_RAM_BYTE = 0x03;
static const uint8_t REG_SECONDS = 0x04;  // bit7 = OS (oscillator stop)
static const uint8_t REG_MINUTES = 0x05;
static const uint8_t REG_HOURS = 0x06;

static const uint8_t CTRL1_STOP = 0x20;     // bit5: 1 = RTC stopped
static const uint8_t CTRL2_COF_OFF = 0x07;  // COF[2:0]=111 -> CLKOUT disabled

Pcf85063a::Pcf85063a(TwoWire &bus, uint8_t addr) : _bus(&bus), _addr(addr)
{
}

uint8_t Pcf85063a::bcdToDec(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

uint8_t Pcf85063a::decToBcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

pcf_status_t Pcf85063a::writeReg(uint8_t reg, uint8_t value)
{
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    _bus->write(value);
    return (_bus->endTransmission() == 0) ? PCF_OK : PCF_ERR_I2C;
}

pcf_status_t Pcf85063a::readRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    // Two separate transactions (STOP after the register write) instead of
    // a repeated-start. ESP32-C6 I2C-NG rejects endTransmission(false) here
    // with ESP_ERR_INVALID_STATE under a busy bus; a plain write + read is
    // accepted and is fine for low-speed RTC register access.
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    if (_bus->endTransmission(true) != 0)
    {
        return PCF_ERR_I2C;
    }

    uint8_t got = _bus->requestFrom((int)_addr, (int)len);
    if (got != len)
    {
        return PCF_ERR_I2C;
    }

    for (uint8_t i = 0; i < len; i++)
    {
        buf[i] = _bus->read();
    }
    return PCF_OK;
}

pcf_status_t Pcf85063a::begin()
{
    // Presence check via a control-register read.
    uint8_t tmp = 0;
    if (readRegs(REG_CONTROL_1, &tmp, 1) != PCF_OK)
    {
        return PCF_ERR_ABSENT;
    }

    // Disable CLKOUT: no needless toggling pin, lower power.
    return writeReg(REG_CONTROL_2, CTRL2_COF_OFF);
}

pcf_status_t Pcf85063a::setTime(const pcf_time_t &t)
{
    // Stop clock before writing time registers, then restart.
    if (writeReg(REG_CONTROL_1, CTRL1_STOP) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    writeReg(REG_SECONDS, decToBcd(t.seconds));  // clears OS flag (bit7)
    writeReg(REG_MINUTES, decToBcd(t.minutes));
    writeReg(REG_HOURS, decToBcd(t.hours));

    return writeReg(REG_CONTROL_1, 0x00);  // clear STOP -> run
}

pcf_status_t Pcf85063a::getTime(pcf_time_t &t, bool &oscStopped)
{
    uint8_t s[3] = {0, 0, 0};
    if (readRegs(REG_SECONDS, s, 3) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    oscStopped = (s[0] & 0x80) != 0;
    t.seconds = bcdToDec(s[0] & 0x7F);
    t.minutes = bcdToDec(s[1] & 0x7F);
    t.hours = bcdToDec(s[2] & 0x3F);
    return PCF_OK;
}

pcf_status_t Pcf85063a::readRamByte(uint8_t &value)
{
    return readRegs(REG_RAM_BYTE, &value, 1);
}

pcf_status_t Pcf85063a::writeRamByte(uint8_t value)
{
    return writeReg(REG_RAM_BYTE, value);
}