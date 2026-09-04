#ifdef SELFTEST

#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>

#include "audio.h"
#include "button.h"
#include "display.h"
#include "io_expander.h"
#include "led.h"
#include "pcf85063a.h"
#include "pins.h"
#include "power.h"
#include "storage.h"

enum result_t
{
    R_PASS,
    R_FAIL,
    R_INIT,
    R_SKIP
};

static int nPass = 0;
static int nFail = 0;
static int nInit = 0;
static int nSkip = 0;

static Pcf85063a rtc(Wire, 0x51);
static Contact   self = {};

static void report(const char *name, result_t r, const char *detail)
{
    const char *label = "SKIP   ";
    switch (r)
    {
        case R_PASS:
            label = "PASS   ";
            nPass++;
            break;
        case R_FAIL:
            label = "FAIL   ";
            nFail++;
            break;
        case R_INIT:
            label = "INIT-OK";
            nInit++;
            break;
        default:
            label = "SKIP   ";
            nSkip++;
            break;
    }
    Serial0.printf("  %-16s %s  %s\n", name, label, detail);
    delay(200);
}

static void checkI2c()
{
    bool foundMcp = false;
    bool foundRtc = false;
    int  count = 0;

    for (uint8_t a = 1; a < 127; a++)
    {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0)
        {
            count++;
            if (a == 0x20)
            {
                foundMcp = true;
            }
            if (a == 0x51)
            {
                foundRtc = true;
            }
        }
    }

    char detail[64];
    snprintf(detail, sizeof(detail), "%d dev  MCP(0x20):%s  RTC(0x51):%s", count,
             foundMcp ? "ok" : "MISS", foundRtc ? "ok" : "MISS");
    report("I2C bus", (foundMcp && foundRtc) ? R_PASS : R_FAIL, detail);
}

static void checkPortA()
{
    powerSetBatMonEnable(true);
    uint8_t hi = ioexpReadReg(0x0A);
    powerSetBatMonEnable(false);
    uint8_t lo = ioexpReadReg(0x0A);

    bool setOk = ((hi >> PIN_MCP_BAT_MON_EN) & 1) != 0;
    bool clrOk = ((lo >> PIN_MCP_BAT_MON_EN) & 1) == 0;

    char detail[64];
    snprintf(detail, sizeof(detail), "OLATA read-back set=%d clr=%d", setOk, clrOk);
    report("MCP Port A", (setOk && clrOk) ? R_PASS : R_FAIL, detail);
}

static void checkRtc()
{
    if (rtc.begin() != PCF_OK)
    {
        report("RTC", R_FAIL, "begin() failed");
        return;
    }

    pcf_time_t t;
    bool       trusted = false;
    if (rtc.readTrusted(t, trusted) != PCF_OK)
    {
        report("RTC", R_FAIL, "read failed");
        return;
    }

    char detail[80];
    snprintf(detail, sizeof(detail), "%04u-%02u-%02u %02u:%02u:%02u trusted=%s", t.year, t.month,
             t.day, t.hours, t.minutes, t.seconds, trusted ? "yes" : "no(ok)");
    report("RTC", R_PASS, detail);
}

static void checkSd()
{
    storageInit();

    bool ok = storageLoadSelf(self);
    int  n = storageCountContacts();

    char detail[96];
    if (ok)
    {
        snprintf(detail, sizeof(detail), "mounted, self='%s' contacts=%d", self.name, n);
        report("SD / storage", R_PASS, detail);
    }
    else
    {
        snprintf(detail, sizeof(detail), "no profile - mount UNCONFIRMED (contacts=%d)", n);
        report("SD / storage", R_INIT, detail);
    }
}

static void checkLcd()
{
    displayInit();
    drawLowBattery();
    report("LCD", R_INIT, "init ran - LOOK at screen to confirm");
}

static void checkAudio()
{
    audio_init();
    audio_setWaveform(AUDIO_WAVE_SQUARE);

    static const audio_note_t jingle[] = {{262, 140}, {294, 140}, {330, 400}};
    audio_playSequence(jingle, 3, 15);
    report("audio", R_INIT, "jingle played - LISTEN to confirm");
}

static void printSummary()
{
    Serial0.println("");
    Serial0.println("==== POST SUMMARY ====");
    Serial0.printf("  PASS    : %d\n", nPass);
    Serial0.printf("  INIT-OK : %d  (LCD/audio - verify by eye/ear)\n", nInit);
    Serial0.printf("  SKIP    : %d  (ACOM/buttons - manual)\n", nSkip);
    Serial0.printf("  FAIL    : %d\n", nFail);
    Serial0.println("");

    if (nFail == 0)
    {
        Serial0.println("VERDICT: all AUTO checks PASSED");
        ledSetColor(0, 255, 0, 50);
    }
    else
    {
        Serial0.printf("VERDICT: %d FAILURE(S) - see above\n", nFail);
        ledSetColor(255, 0, 0, 50);
    }
}

void setup()
{
    delay(200);
    Serial0.begin(115200);
    delay(300);

    ledInit();
    ledSetColor(0, 0, 255, 50);

    Serial0.println("");
    Serial0.println("==== con-venience POST (self-test) ====");
    Serial0.println("");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    checkI2c();

    ioexpInit();
    btnInit();
    powerInit();
    checkPortA();

    checkRtc();

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
    delay(10);
    checkSd();

    checkLcd();
    checkAudio();

    report("ACOM", R_SKIP, "needs peer board - test manually");
    report("buttons", R_SKIP, "needs key press - test manually");

    printSummary();
}

void loop()
{
    btn_events_t e = btnPoll();

    if (e.up)
    {
        Serial0.printf("[btn] GPB%d  UP     %s\n", PIN_MCP_BTN_UP,
                       e.up == BTN_LONG_PRESS ? "LONG" : "click");
    }
    if (e.down)
    {
        Serial0.printf("[btn] GPB%d  DOWN   %s\n", PIN_MCP_BTN_DOWN,
                       e.down == BTN_LONG_PRESS ? "LONG" : "click");
    }
    if (e.pair)
    {
        Serial0.printf("[btn] GPB%d  PAIR   %s\n", PIN_MCP_BTN_PAIR,
                       e.pair == BTN_LONG_PRESS ? "LONG" : "click");
    }
    if (e.left)
    {
        Serial0.printf("[btn] GPB%d  LEFT   %s\n", PIN_MCP_BTN_LEFT,
                       e.left == BTN_LONG_PRESS ? "LONG" : "click");
    }
    if (e.right)
    {
        Serial0.printf("[btn] GPB%d  RIGHT  %s\n", PIN_MCP_BTN_RIGHT,
                       e.right == BTN_LONG_PRESS ? "LONG" : "click");
    }
}

#endif