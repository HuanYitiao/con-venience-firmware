#include <Arduino.h>

#include <NimBLEDevice.h>
#include <SPI.h>
#include <Wire.h>

#include "ble.h"
#include "button.h"
// #include "display.h"
#include "fsm.h"
#include "led.h"
#include "pins.h"
#include "storage.h"

static void mcpWriteReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(MCP23008_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t mcpReadGPIO()
{
    Wire.beginTransmission(MCP23008_ADDR);
    Wire.write(0x09);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MCP23008_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

volatile bool mcpIntFlag = false;

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

static Contact self = {};
static Contact currentContact = {};
static char    contactNames[16][NAME_LEN] = {};
static int     contactCount = 0;
static bool    idleShowQR = false;

#if 0
#define MY_MAC {0x48, 0xf6, 0xee, 0xc7, 0x15, 0x0e}
#define PEER_MAC {0x48, 0xf6, 0xee, 0xc7, 0x1d, 0xf2}
#else
#define PEER_MAC {0x48, 0xf6, 0xee, 0xc7, 0x15, 0x0e}
#define MY_MAC {0x48, 0xf6, 0xee, 0xc7, 0x1d, 0xf2}
#endif

static bool bleResultReady = false;
static bool bleResultSuccess = false;

static void onBleComplete(bool success, const uint8_t *data, size_t len)
{
    bleResultSuccess = success;
    bleResultReady = true;
    Serial0.printf("BLE done: %s len=%d\n", success ? "OK" : "FAIL", len);
    if (success && data != nullptr)
    {
        Serial0.printf("Data: %.*s\n", (int)len, (char *)data);
    }
}

void setup()
{
    ledInit();
    ledSetColor(0, 255, 0, 50);
    delay(500);
    ledOff();

    Serial0.begin(115200);
    NimBLEDevice::init("con-venience");
    fsmInit();
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
    delay(200);
    storageInit();
    storageLoadSelf(self);
    Serial0.printf("self: name=%s res=%d links=%d\n", self.name, self.avatarResolution,
                   self.linkCount);
    contactCount = storageCountContacts();
    Serial0.printf("contactCount: %d\n", contactCount);
    for (int i = 0; i < contactCount; i++)
    {
        storageLoadContactName(i, contactNames[i], NAME_LEN);
        Serial0.printf("loaded name %d: %s\n", i, contactNames[i]);
    }
    // displayInit();

    Serial0.printf("BLE MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    Serial0.println("I2C scan:");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
        {
            Serial0.printf("  found: 0x%02X\n", addr);
        }
    }

    mcpWriteReg(0x00, 0x1F);
    mcpWriteReg(0x06, 0xFF);
    mcpWriteReg(0x02, 0x1F);
    mcpWriteReg(0x03, 0x1F);
    mcpWriteReg(0x04, 0x00);
    mcpWriteReg(0x05, 0x04);
    mcpReadGPIO();

    pinMode(PIN_MCP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MCP_INT), [] { mcpIntFlag = true; }, FALLING);

    Serial0.println("con-venience ready");
    Serial0.printf("Initial state: %s\n", stateName(fsmGetState()));

    uint8_t myMac[] = MY_MAC;
    uint8_t peerMac[] = PEER_MAC;

    ble_role_t role = (memcmp(myMac, peerMac, 6) < 0) ? BLE_ROLE_SERVER : BLE_ROLE_CLIENT;
    Serial0.printf("BLE role: %s\n", role == BLE_ROLE_SERVER ? "SERVER" : "CLIENT");

    const char *testProfile = "test_profile_data";
    bleStart(peerMac, role, (const uint8_t *)testProfile, strlen(testProfile), onBleComplete);
}

void loop()
{
    static bool lastFlag = false;
    if (mcpIntFlag != lastFlag)
    {
        lastFlag = mcpIntFlag;
        Serial0.printf("mcpIntFlag = %d\n", mcpIntFlag);
    }

    if (bleResultReady)
    {
        bleResultReady = false;
        Serial0.printf("BLE result: %s\n", bleResultSuccess ? "success" : "fail");
    }

    if (mcpIntFlag)
    {
        mcpIntFlag = false;
        uint8_t captured = mcpReadGPIO();

        btn_event_t upEvent = btn_read((captured >> MCP_PIN_BTN_UP) & 1 ? false : true, &btnUp);
        btn_event_t downEvent =
            btn_read((captured >> MCP_PIN_BTN_DOWN) & 1 ? false : true, &btnDown);
        btn_event_t pairEvent =
            btn_read((captured >> MCP_PIN_BTN_PAIR) & 1 ? false : true, &btnPair);
        btn_event_t leftEvent =
            btn_read((captured >> MCP_PIN_BTN_LEFT) & 1 ? false : true, &btnLeft);
        btn_event_t rightEvent =
            btn_read((captured >> MCP_PIN_BTN_RIGHT) & 1 ? false : true, &btnRight);

        event_t event;
        bool    hasEvent = false;

        if (upEvent == BTN_CLICK)
        {
            event = EVENT_UP_CLICK;
            hasEvent = true;
        }
        else if (upEvent == BTN_LONG_PRESS)
        {
            event = EVENT_UP_LONG_PRESS;
            hasEvent = true;
        }
        else if (downEvent == BTN_CLICK)
        {
            event = EVENT_DOWN_CLICK;
            hasEvent = true;
        }
        else if (downEvent == BTN_LONG_PRESS)
        {
            event = EVENT_DOWN_LONG_PRESS;
            hasEvent = true;
        }
        else if (pairEvent == BTN_CLICK)
        {
            event = EVENT_PAIRING_CLICK;
            hasEvent = true;
        }
        else if (pairEvent == BTN_LONG_PRESS)
        {
            event = EVENT_PAIRING_LONG_PRESS;
            hasEvent = true;
        }
        else if (leftEvent == BTN_CLICK)
        {
            event = EVENT_LEFT_CLICK;
            hasEvent = true;
        }
        else if (leftEvent == BTN_LONG_PRESS)
        {
            event = EVENT_LEFT_LONG_PRESS;
            hasEvent = true;
        }
        else if (rightEvent == BTN_CLICK)
        {
            event = EVENT_RIGHT_CLICK;
            hasEvent = true;
        }
        else if (rightEvent == BTN_LONG_PRESS)
        {
            event = EVENT_RIGHT_LONG_PRESS;
            hasEvent = true;
        }

        if (hasEvent)
        {
            state_t before = fsmGetState();
            fsmHandleEvent(event);
            state_t after = fsmGetState();

            if (before != after)
            {
                // displayResetScroll();
                Serial0.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                               stateName(after));
            }
            else
            {
                Serial0.printf("[EVENT] %-20s | %s (no change)\n", eventName(event),
                               stateName(before));
            }

            if (after == STATE_MENU)
            {
                Serial0.printf("[MENU]  selection: %s\n",
                               fsmGetMenuSelection() ? "Friends" : "My Profile");
            }

            if (event == EVENT_PAIRING_CLICK && before == STATE_IDLE)
            {
                idleShowQR = !idleShowQR;
            }
        }
    }

    static int lastContactIndex = -1;
    int        currentIndex = fsmGetContactIndex();
    if (contactCount > 0 && currentIndex != lastContactIndex)
    {
        bool ok = storageLoadContact(currentIndex, currentContact);
        Serial0.printf("loadContact %d: %s\n", currentIndex, ok ? "OK" : "FAIL");
        lastContactIndex = currentIndex;
    }

    const Contact &profileContact = fsmIsViewingSelf() ? self : currentContact;
    // displayRender(fsmGetState(), self, currentContact, contactNames, contactCount,
    //               fsmGetContactIndex(), fsmGetMenuSelection(), idleShowQR, profileContact,
    //               fsmGetLinkIndex());
}