#include "pcf85063a.h"

// ---- register map (verified against NXP PCF85063A datasheet Rev.7, Table 5) ----
// NOTE: on this chip Days is at 0x07 and Weekdays at 0x08 -- the reverse of the
// ordering used by many other RTCs (e.g. PCF8563). Do not "fix" this to match
// muscle memory.
static const uint8_t REG_CONTROL_1 = 0x00;
static const uint8_t REG_CONTROL_2 = 0x01;
static const uint8_t REG_RAM_BYTE = 0x03;
static const uint8_t REG_SECONDS = 0x04;   // bit7 = OS (oscillator stop); bits6:0 sec BCD
static const uint8_t REG_MINUTES = 0x05;   // bits6:0 min BCD
static const uint8_t REG_HOURS = 0x06;     // 24h mode: bits5:0 hours BCD
static const uint8_t REG_DAYS = 0x07;      // day of month, BCD, bits5:0 (1..31)
static const uint8_t REG_WEEKDAYS = 0x08;  // weekday 0..6, bits2:0 -- NOT written (see setTime)
static const uint8_t REG_MONTHS = 0x09;    // month, BCD, bits4:0 (1..12); no century bit
static const uint8_t REG_YEARS = 0x0A;     // year, BCD, 00..99

static const uint8_t CTRL1_STOP = 0x20;     // Control_1 bit5: 1 = RTC stopped
static const uint8_t CTRL2_COF_OFF = 0x07;  // COF[2:0]=111 -> CLKOUT disabled

// PCF85063A year register holds two digits only (00..99). We map it onto a
// full calendar year with this base. The device will not outlive 2099.
static const uint16_t YEAR_BASE = 2000;

// Validity signature stored in the battery-backed RAM byte. Written LAST by
// setTime() only after every time/date-register write has succeeded and the
// clock has been restarted. Any other value means the time is NOT trusted:
//   - fresh chip / VBAT lost -> RAM holds an arbitrary value (very unlikely 0xA5)
//   - setTime() died partway  -> signature was cleared at entry, never re-stamped
static const uint8_t RAM_TIME_VALID_MAGIC = 0xA5;

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
    // accepted and is fine for low-speed RTC register access. STOP-then-START
    // is an explicitly permitted read method per datasheet section 8.4.
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
    // Read Control_1 first so we can toggle ONLY the STOP bit and preserve the
    // oscillator load-cap (CAP_SEL, bit0) and 12/24h (12_24, bit1) settings that
    // are configured elsewhere. Blindly writing 0x00 here would silently force
    // 7 pF + 24-hour mode and could break timekeeping on a 12.5 pF crystal.
    uint8_t ctrl1 = 0;
    if (readRegs(REG_CONTROL_1, &ctrl1, 1) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Invalidate the validity signature FIRST, before touching anything.
    // For the entire write window below the device is now "untrusted": if we
    // die partway through, the next boot correctly refuses to trust the time.
    // This clear-first step is mandatory -- without it a stale 0xA5 left over
    // from a previous successful setTime() would make a half-written time look
    // valid.
    if (writeRamByte(0x00) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Stop the clock (set STOP, keep every other Control_1 bit). While stopped
    // the prescaler is held in reset and no time increment can occur between
    // the individual register writes below.
    if (writeReg(REG_CONTROL_1, (uint8_t)(ctrl1 | CTRL1_STOP)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Time. Writing the seconds register also clears the OS flag (bit7), because
    // decToBcd(seconds<=59) always has bit7 = 0.
    if (writeReg(REG_SECONDS, decToBcd(t.seconds)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }
    if (writeReg(REG_MINUTES, decToBcd(t.minutes)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }
    if (writeReg(REG_HOURS, decToBcd(t.hours)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Date. The weekday register (0x08) is intentionally NOT written: on this
    // chip it is a free-running manual counter, not derived from the date, and
    // nothing in this firmware reads it.
    if (writeReg(REG_DAYS, decToBcd(t.day)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }
    if (writeReg(REG_MONTHS, decToBcd(t.month)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }
    if (writeReg(REG_YEARS, decToBcd((uint8_t)(t.year % 100))) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Restart the clock (clear STOP, keep every other Control_1 bit).
    if (writeReg(REG_CONTROL_1, (uint8_t)(ctrl1 & ~CTRL1_STOP)) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Every prior step landed and the clock is running -> stamp the signature
    // LAST. This is the seal: "I confirm the time/date in this RTC is real."
    // Any earlier failure above returned already, leaving the signature cleared.
    return writeRamByte(RAM_TIME_VALID_MAGIC);
}

pcf_status_t Pcf85063a::getTime(pcf_time_t &t, bool &oscStopped)
{
    // One block read covers seconds..years (0x04..0x0A = 7 bytes). Reading the
    // whole span in a single transaction is the datasheet-recommended method
    // (section 8.4): the time counters are frozen for the duration, so no
    // carry/rollover can split the fields. Index 4 is the unused weekday byte.
    uint8_t b[7] = {0, 0, 0, 0, 0, 0, 0};
    if (readRegs(REG_SECONDS, b, 7) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    oscStopped = (b[0] & 0x80) != 0;
    t.seconds = bcdToDec(b[0] & 0x7F);
    t.minutes = bcdToDec(b[1] & 0x7F);
    t.hours = bcdToDec(b[2] & 0x3F);
    t.day = bcdToDec(b[3] & 0x3F);  // 0x07 Days
    // b[4] = weekday (0x08) -- ignored on purpose (see setTime).
    t.month = bcdToDec(b[5] & 0x1F);  // 0x09 Months
    t.year = (uint16_t)(YEAR_BASE + bcdToDec(b[6]));
    return PCF_OK;
}

pcf_status_t Pcf85063a::readTrusted(pcf_time_t &t, bool &trusted)
{
    // Single entry point for "read the clock AND decide whether to believe it".
    // Encapsulated here on purpose: callers must not be able to read the time
    // and forget to check its validity. `trusted` is fail-closed -- it is only
    // ever true when BOTH signals agree the time is real.
    trusted = false;

    bool oscStopped = false;
    if (getTime(t, oscStopped) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    uint8_t ram = 0;
    if (readRamByte(ram) != PCF_OK)
    {
        return PCF_ERR_I2C;
    }

    // Two independent proofs, both required:
    //   OS==0   -> oscillator has not stopped since the time was last set
    //   ram==A5 -> setTime() completed fully and the seal survived in VBAT RAM
    trusted = (!oscStopped) && (ram == RAM_TIME_VALID_MAGIC);
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