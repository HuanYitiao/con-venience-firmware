#include <Arduino.h>

#include <NimBLEDevice.h>
#include <SPI.h>
#include <Wire.h>

#include "ble.h"
#include "button.h"
#include "display.h"
#include "fsm.h"
#include "led.h"
#include "pins.h"
#include "storage.h"

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
    displayInit();

    Serial0.printf("BLE MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    btn_init();

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
    btn_events_t btnEvents = btn_poll();

    event_t event;
    bool    hasEvent = false;

    if (btnEvents.up == BTN_CLICK)
    {
        event = EVENT_UP_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.up == BTN_LONG_PRESS)
    {
        event = EVENT_UP_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.down == BTN_CLICK)
    {
        event = EVENT_DOWN_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.down == BTN_LONG_PRESS)
    {
        event = EVENT_DOWN_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.pair == BTN_CLICK)
    {
        event = EVENT_PAIRING_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.pair == BTN_LONG_PRESS)
    {
        event = EVENT_PAIRING_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.left == BTN_CLICK)
    {
        event = EVENT_LEFT_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.left == BTN_LONG_PRESS)
    {
        event = EVENT_LEFT_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.right == BTN_CLICK)
    {
        event = EVENT_RIGHT_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.right == BTN_LONG_PRESS)
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
            displayResetScroll();
            Serial0.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                           stateName(after));
        }
        else
        {
            Serial0.printf("[EVENT] %-20s | %s (no change)\n", eventName(event), stateName(before));
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

    static int lastContactIndex = -1;
    int        currentIndex = fsmGetContactIndex();
    if (contactCount > 0 && currentIndex != lastContactIndex)
    {
        bool ok = storageLoadContact(currentIndex, currentContact);
        Serial0.printf("loadContact %d: %s\n", currentIndex, ok ? "OK" : "FAIL");
        lastContactIndex = currentIndex;
    }

    const Contact &profileContact = fsmIsViewingSelf() ? self : currentContact;
    displayRender(fsmGetState(), self, currentContact, contactNames, contactCount,
                  fsmGetContactIndex(), fsmGetMenuSelection(), idleShowQR, profileContact,
                  fsmGetLinkIndex());
}