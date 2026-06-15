#include "ble.h"

#include <NimBLEDevice.h>
#include <string.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define PROFILE_LEN_MAX 4608

static ble_callback_t s_callback = nullptr;
static ble_role_t     s_role;
static bool           s_busy = false;
static uint8_t        s_peer_mac[6];
static uint8_t        s_rx_buf[PROFILE_LEN_MAX];
static size_t         s_rx_len = 0;

static void bleFinish(bool success)
{
    NimBLEDevice::stopAdvertising();
    s_busy = false;
    if (s_callback)
    {
        s_callback(success, s_rx_buf, s_rx_len);
    }
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        NimBLEDevice::stopAdvertising();
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        bleFinish(s_rx_len > 0);
    }
};

static void clientTask(void *)
{
    NimBLEAddress addr(s_peer_mac, BLE_ADDR_PUBLIC);
    NimBLEScan   *pScan = NimBLEDevice::getScan();
    auto          results = pScan->getResults(10000);

    const NimBLEAdvertisedDevice *target = nullptr;
    for (int i = 0; i < results.getCount(); i++)
    {
        const NimBLEAdvertisedDevice *dev = results.getDevice(i);
        if (dev->getAddress() == addr)
        {
            target = dev;
            break;
        }
    }

    if (!target)
    {
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    NimBLEClient *pClient = NimBLEDevice::createClient();
    if (!pClient->connect(target))
    {
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    NimBLERemoteService        *pSvc = pClient->getService(SERVICE_UUID);
    NimBLERemoteCharacteristic *pChar =
        pSvc ? pSvc->getCharacteristic(CHARACTERISTIC_UUID) : nullptr;

    if (pChar && pChar->canRead())
    {
        std::string val = pChar->readValue();
        s_rx_len = val.size();
        memcpy(s_rx_buf, val.data(), s_rx_len);
    }

    pClient->disconnect();
    bleFinish(s_rx_len > 0);
    vTaskDelete(nullptr);
}

void bleStart(const uint8_t peer_mac[6], ble_role_t role, const uint8_t *my_profile,
              size_t my_profile_len, ble_callback_t callback)
{
    if (s_busy)
    {
        return;
    }

    s_busy = true;
    s_role = role;
    s_callback = callback;
    s_rx_len = 0;
    memcpy(s_peer_mac, peer_mac, 6);

    NimBLEDevice::init("con-venience");

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService        *pSvc = pServer->createService(SERVICE_UUID);
    NimBLECharacteristic *pChar =
        pSvc->createCharacteristic(CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ);
    pChar->setValue(my_profile, my_profile_len);
    pSvc->start();

    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->start(0);

    if (role == BLE_ROLE_CLIENT)
    {
        xTaskCreate(clientTask, "ble_client", 4096, nullptr, 1, nullptr);
    }
}

void bleStop()
{
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);
    s_busy = false;
}

bool bleIsBusy()
{
    return s_busy;
}