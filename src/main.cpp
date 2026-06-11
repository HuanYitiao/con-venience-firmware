/*#include <Arduino.h>

#include "button.h"
#include "display.h"
#include "fsm.h"
#include "led.h"
#include "storage.h"

#define PIN_UP 2
#define PIN_DOWN 3
#define PIN_PAIR 21
#define PIN_LEFT 19
#define PIN_RIGHT 20

static btn_state_t btnUp = {};
static btn_state_t btnDown = {};
static btn_state_t btnPair = {};
static btn_state_t btnLeft = {};
static btn_state_t btnRight = {};

static Contact self = {};
static Contact currentContact = {};
static char    contactNames[16][USERNAME_LEN] = {};
static int     contactCount = 0;
static bool    idleShowQR = false;

void setup()
{
    ledInit();
    ledSetColor(0, 255, 0, 50);
    delay(500);
    ledOff();

    Serial0.begin(115200);
    fsm_init();
    storageInit();
    storageLoadSelf(self);
    contactCount = storageCountContacts();
    for (int i = 0; i < contactCount; i++)
    {
        storageLoadContactName(i, contactNames[i], USERNAME_LEN);
        Serial0.printf("loaded name %d: %s\n", i, contactNames[i]);
    }
    displayInit();

    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_PAIR, INPUT_PULLUP);
    pinMode(PIN_LEFT, INPUT_PULLUP);
    pinMode(PIN_RIGHT, INPUT_PULLUP);

    Serial0.println("con-venience ready");
    Serial0.printf("Initial state: %s\n", stateName(fsm_get_state()));
}

void loop()
{
    btn_event_t upEvent = btn_read(PIN_UP, &btnUp);
    btn_event_t downEvent = btn_read(PIN_DOWN, &btnDown);
    btn_event_t pairEvent = btn_read(PIN_PAIR, &btnPair);
    btn_event_t leftEvent = btn_read(PIN_LEFT, &btnLeft);
    btn_event_t rightEvent = btn_read(PIN_RIGHT, &btnRight);

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
        state_t before = fsm_get_state();
        fsm_handle_event(event);
        state_t after = fsm_get_state();

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
                           fsm_get_menu_selection() ? "Friends" : "My Profile");
        }

        if (event == EVENT_PAIRING_CLICK && before == STATE_IDLE)
        {
            idleShowQR = !idleShowQR;
        }
    }

    static int lastContactIndex = -1;
    int        currentIndex = fsm_get_contact_index();
    if (contactCount > 0 && currentIndex != lastContactIndex)
    {
        bool ok = storageLoadContact(currentIndex, currentContact);
        Serial0.printf("loadContact %d: %s\n", currentIndex, ok ? "OK" : "FAIL");
        lastContactIndex = currentIndex;
    }

    displayRender(fsm_get_state(), self, currentContact, contactNames, contactCount,
                  fsm_get_contact_index(), fsm_get_menu_selection(), idleShowQR);
}*/

/*
// main MAC address: 48:f6:ee:c7:15:0e
#include <Arduino.h>

#include <NimBLEDevice.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

void setup()
{
    Serial0.begin(115200);
    Serial0.println("code start...");
    NimBLEDevice::init("con-venience");

    NimBLEServer         *pServer = NimBLEDevice::createServer();
    NimBLEService        *pService = pServer->createService(SERVICE_UUID);
    NimBLECharacteristic *pCharacteristic =
        pService->createCharacteristic(CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ);
    pCharacteristic->setValue("hello from con-venience");
    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setMinInterval(100);
    pAdvertising->setMaxInterval(200);
    pAdvertising->start(0);

    Serial0.println("BLE advertising...");
    Serial0.printf("MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());
}

void loop()
{
}*/

// client mac address:
#include <Arduino.h>

#include <NimBLEDevice.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static NimBLEAddress targetAddress("48:f6:ee:c7:15:0e", BLE_ADDR_PUBLIC);

void setup()
{
    Serial0.begin(115200);
    NimBLEDevice::init("con-venience-client");

    Serial0.println("Scanning...");
    NimBLEScan       *pScan = NimBLEDevice::getScan();
    NimBLEScanResults results = pScan->getResults(5000);

    for (int i = 0; i < results.getCount(); i++)
    {
        const NimBLEAdvertisedDevice *device = results.getDevice(i);
        Serial0.printf("Found: %s\n", device->getAddress().toString().c_str());
        if (device->getAddress() == targetAddress)
        {
            Serial0.println("Found target device!");

            NimBLEClient *pClient = NimBLEDevice::createClient();
            if (pClient->connect(device))
            {
                Serial0.println("Connected!");

                NimBLERemoteService *pService = pClient->getService(SERVICE_UUID);
                if (pService)
                {
                    NimBLERemoteCharacteristic *pChar =
                        pService->getCharacteristic(CHARACTERISTIC_UUID);
                    if (pChar && pChar->canRead())
                    {
                        std::string value = pChar->readValue();
                        Serial0.printf("Value: %s\n", value.c_str());
                    }
                }
                pClient->disconnect();
            }
            break;
        }
    }
}

void loop()
{
}