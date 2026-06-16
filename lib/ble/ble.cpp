#include "ble.h"

#include <Arduino.h>

#include <NimBLEDevice.h>
#include <string.h>


#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TX_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RX_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"

#define PROFILE_LEN_MAX 4608

static ble_callback_t        s_callback = nullptr;
static bool                  s_busy = false;
static uint8_t               s_peer_mac[6];
static uint8_t               s_rx_buf[PROFILE_LEN_MAX];
static size_t                s_rx_len = 0;
static NimBLECharacteristic *s_rxChar = nullptr;

static void bleFinish(bool success)
{
    NimBLEDevice::stopAdvertising();
    s_busy = false;
    if (s_callback)
        s_callback(success, s_rx_buf, s_rx_len);
}

class RxCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar, NimBLEConnInfo &connInfo) override
    {
        std::string val = pChar->getValue();
        s_rx_len = val.size();
        memcpy(s_rx_buf, val.data(), s_rx_len);
        Serial0.printf("Server: received %d bytes\n", s_rx_len);
    }
};

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        NimBLEDevice::stopAdvertising();
        Serial0.println("Server: client connected");
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        Serial0.println("Server: client disconnected");
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
        Serial0.println("Client: target not found");
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    NimBLEClient *pClient = NimBLEDevice::createClient();
    if (!pClient->connect(target))
    {
        Serial0.println("Client: connect failed");
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    Serial0.println("Client: connected");

    NimBLERemoteService *pSvc = pClient->getService(SERVICE_UUID);
    if (!pSvc)
    {
        Serial0.println("Client: service not found");
        pClient->disconnect();
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    NimBLERemoteCharacteristic *pTx = pSvc->getCharacteristic(TX_CHAR_UUID);
    NimBLERemoteCharacteristic *pRx = pSvc->getCharacteristic(RX_CHAR_UUID);

    if (pTx && pTx->canRead())
    {
        std::string val = pTx->readValue();
        s_rx_len = val.size();
        memcpy(s_rx_buf, val.data(), s_rx_len);
        Serial0.printf("Client: read %d bytes\n", s_rx_len);
    }

    if (pRx && pRx->canWrite())
    {
        // 从s_rxChar拿自己的profile写过去
        // 这里的my_profile已经在bleStart里设好了
        NimBLECharacteristic *myChar = NimBLEDevice::getServer()
                                           ->getServiceByUUID(SERVICE_UUID)
                                           ->getCharacteristic(TX_CHAR_UUID);
        if (myChar)
        {
            std::string myVal = myChar->getValue();
            pRx->writeValue((uint8_t *)myVal.data(), myVal.size(), true);
            Serial0.printf("Client: wrote %d bytes\n", myVal.size());
        }
    }

    pClient->disconnect();
    bleFinish(s_rx_len > 0);
    vTaskDelete(nullptr);
}

void bleStart(const uint8_t peer_mac[6], ble_role_t role, const uint8_t *my_profile,
              size_t my_profile_len, ble_callback_t callback)
{
    if (s_busy)
        return;

    s_busy = true;
    s_callback = callback;
    s_rx_len = 0;
    memcpy(s_peer_mac, peer_mac, 6);

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService *pSvc = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic *pTx = pSvc->createCharacteristic(TX_CHAR_UUID, NIMBLE_PROPERTY::READ);
    pTx->setValue(my_profile, my_profile_len);

    s_rxChar = pSvc->createCharacteristic(RX_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    s_rxChar->setCallbacks(new RxCallbacks());

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
    s_busy = false;
}

bool bleIsBusy()
{
    return s_busy;
}