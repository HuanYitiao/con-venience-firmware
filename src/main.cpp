#include <Arduino.h>

#include <NimBLEDevice.h>
#include <SPI.h>
#include <Wire.h>

#include "acom.h"
#include "audio.h"
#include "ble.h"
#include "button.h"
#include "display.h"
#include "fsm.h"
#include "io_expander.h"
#include "led.h"
#include "packing.h"
#include "pins.h"
#include "storage.h"
#include "wifi_config.h"

static Contact self = {};
static Contact currentContact = {};
static char    contactNames[16][NAME_LEN] = {};
static int     contactCount = 0;
static bool    idleShowQR = false;

static bool    bleResultReady = false;
static bool    bleResultSuccess = false;
static uint8_t bleRxBuf[PACKING_BUF_MAX];
static size_t  bleRxLen = 0;

static void onBleComplete(bool success, const uint8_t *data, size_t len)
{
    bleResultSuccess = success;
    bleResultReady = true;
    if (success && data && len > 0)
    {
        memcpy(bleRxBuf, data, len);
        bleRxLen = len;
    }
}

static void startBle()
{
    Serial0.println("startBle called");
    uint8_t ownMac[6];
    uint8_t peerMac[6];
    uint8_t peerType;

    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
        ownMac[i] = addr->val[5 - i];

    if (!acom_has_mac(peerMac, &peerType))
    {
        Serial0.println("startBle: no peer mac");
        fsmHandleEvent(EVENT_BLE_FAILURE);
        return;
    }
    Serial0.printf("startBle: peer MAC %02x:%02x:%02x:%02x:%02x:%02x type=%d\n", peerMac[0],
                   peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], peerType);

    ble_role_t role = (memcmp(ownMac, peerMac, 6) < 0) ? BLE_ROLE_SERVER : BLE_ROLE_CLIENT;
    Serial0.printf("BLE role: %s\n", role == BLE_ROLE_SERVER ? "SERVER" : "CLIENT");

    static uint8_t packBuf[PACKING_BUF_MAX];
    size_t         packLen = packingPack(packBuf, sizeof(packBuf));
    Serial0.printf("packLen: %d\n", packLen);
    if (packLen == 0)
    {
        Serial0.println("startBle: pack failed");
        fsmHandleEvent(EVENT_BLE_FAILURE);
        return;
    }

    bleStart(peerMac, peerType, role, packBuf, packLen, onBleComplete);
    Serial0.println("startBle: bleStart called");
}

static void dispatchEvent(event_t event)
{
    state_t before = fsmGetState();
    fsmHandleEvent(event);
    state_t after = fsmGetState();

    if (before != after)
    {
        if (after == STATE_PAIRING)
        {
            acom_start();
        }

        if (after == STATE_BLE_EXCHANGE)
        {
            startBle();
        }

        if (before == STATE_BLE_EXCHANGE && after != STATE_BLE_EXCHANGE)
        {
            if (!bleResultReady)
            {
                bleStop();
            }
        }

        if (before == STATE_PAIRING && after != STATE_PAIRING)
        {
            acom_stop();
        }

        if (after == STATE_SETTINGS)
        {
            wifi_config_start();
        }
        if (before == STATE_SETTINGS && after != STATE_SETTINGS)
        {
            wifi_config_stop();
        }

        displayResetScroll();
        Serial0.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                       stateName(after));
    }
    else
    {
        Serial0.printf("[EVENT] %-20s | %s (no change)\n", eventName(event), stateName(before));
    }
}

void settingsService()
{
    static bool wasActive = false;

    if (fsmGetState() != STATE_SETTINGS)
    {
        wasActive = false;
        return;
    }

    wifi_config_loop();

    static bool lastConn = false;
    static bool lastDone = false;
    if (!wasActive || wifi_config_client_connected() != lastConn
        || wifi_config_upload_done() != lastDone)
    {
        wasActive = true;
        lastConn = wifi_config_client_connected();
        lastDone = wifi_config_upload_done();
        drawSettings();
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
    NimBLEDevice::setMTU(517);
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
    ioexpInit();
    btnInit();

    audio_init();
    audio_setWaveform(AUDIO_WAVE_SQUARE);

    static const audio_note_t startupJingle[] = {
        {262, 140},
        {294, 140},
        {330, 400},
    };

    audio_playSequence(startupJingle, sizeof(startupJingle) / sizeof(startupJingle[0]), 15);

    Serial0.println("audio ready");
    acom_init();

    Serial0.println("con-venience ready");
    Serial0.printf("Initial state: %s\n", stateName(fsmGetState()));
}

void loop()
{
    btn_events_t btnEvents = btnPoll();

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
        dispatchEvent(event);

        if (fsmGetState() == STATE_MENU)
        {
            int         sel = fsmGetMenuSelection();
            const char *name = sel == 0 ? "My Profile" : sel == 1 ? "Friends" : "Settings";
            Serial0.printf("[MENU]  selection: %s\n", name);
        }

        if (event == EVENT_PAIRING_CLICK && before == STATE_IDLE)
        {
            idleShowQR = !idleShowQR;
        }
    }

    if (bleResultReady)
    {
        bleResultReady = false;
        if (bleResultSuccess)
        {
            bool ok = packingUnpack(bleRxBuf, bleRxLen);
            Serial0.printf("unpack: %s\n", ok ? "OK" : "FAIL");
            if (ok)
            {
                contactCount = storageCountContacts();
                storageLoadContactName(contactCount - 1, contactNames[contactCount - 1], NAME_LEN);
            }
            fsmHandleEvent(EVENT_BLE_SUCCESS);
        }
        else
        {
            fsmHandleEvent(EVENT_BLE_FAILURE);
        }
    }

    if (fsmGetState() == STATE_PAIRING)
    {
        acom_tick();

        uint8_t peerMac[6];
        if (acom_has_mac(peerMac, nullptr))
        {
            dispatchEvent(EVENT_ACOM_SUCCESS);
        }
        else if (acom_failed())
        {
            acom_start();
        }
        else if (millis() - fsmGetStateEnterTime() > PAIRING_TIMEOUT_MS)
        {
            dispatchEvent(EVENT_PAIRING_OVERTIME);
        }
    }

    if (fsmGetState() == STATE_CONTACT_CARD)
    {
        if (millis() - fsmGetStateEnterTime() > CARD_DISPLAY_MS)
        {
            dispatchEvent(EVENT_CARD_OVERTIME);
        }
    }

    settingsService();

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