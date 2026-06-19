#include "ble.h"

#include <Arduino.h>

#include <NimBLEDevice.h>
#include <string.h>

#include "packing.h"

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
static uint8_t               s_tx_buf[PACKING_BUF_MAX];
static size_t                s_tx_len = 0;
static uint8_t               s_indicate_buf[PACKING_BUF_MAX];
static size_t                s_indicate_len = 0;
static bool                  s_indicate_done = false;

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
        auto   raw = pChar->getValue();
        size_t rawLen = pChar->getLength();

        if (rawLen == 1 && raw[0] == 0xAA)
        {
            Serial0.println("Server: got ready signal, starting indicate...");
            NimBLECharacteristic *pTx = NimBLEDevice::getServer()
                                            ->getServiceByUUID(SERVICE_UUID)
                                            ->getCharacteristic(TX_CHAR_UUID);
            if (!pTx)
                return;

            const size_t chunkSize = 508;
            uint16_t     totalChunks = (s_tx_len + chunkSize - 1) / chunkSize;

            for (uint16_t i = 0; i < totalChunks; i++)
            {
                size_t  offset = i * chunkSize;
                size_t  len = min(chunkSize, s_tx_len - offset);
                uint8_t pkt[512];

                pkt[0] = (i >> 8) & 0xFF;
                pkt[1] = i & 0xFF;
                pkt[2] = (totalChunks >> 8) & 0xFF;
                pkt[3] = totalChunks & 0xFF;
                memcpy(pkt + 4, s_tx_buf + offset, len);

                bool ok = pTx->indicate(pkt, len + 4, connInfo.getConnHandle());
                Serial0.printf("Server: indicate chunk %d/%d %s\n", i + 1, totalChunks,
                               ok ? "OK" : "FAIL");
            }
            return;
        }

        if (rawLen < 4)
            return;

        uint16_t chunkIndex = ((uint16_t)raw[0] << 8) | raw[1];
        uint16_t totalChunks = ((uint16_t)raw[2] << 8) | raw[3];
        size_t   dataLen = rawLen - 4;

        memcpy(s_rx_buf + s_rx_len, raw.data() + 4, dataLen);
        s_rx_len += dataLen;

        Serial0.printf("Server: chunk %d/%d received, total so far=%d\n", chunkIndex + 1,
                       totalChunks, s_rx_len);
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
    pClient->setDataLen(251);
    bool mtuOk = pClient->exchangeMTU();
    Serial0.printf("MTU: %d\n", pClient->getMTU());
    pClient->exchangeMTU();
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

    if (pTx && pTx->canIndicate())
    {
        s_indicate_len = 0;
        s_indicate_done = false;

        pTx->subscribe(
            false,
            [](NimBLERemoteCharacteristic *pChar, uint8_t *data, size_t len, bool isNotify)
            {
                if (len < 4)
                    return;

                uint16_t chunkIndex = ((uint16_t)data[0] << 8) | data[1];
                uint16_t totalChunks = ((uint16_t)data[2] << 8) | data[3];
                size_t   dataLen = len - 4;

                memcpy(s_indicate_buf + s_indicate_len, data + 4, dataLen);
                s_indicate_len += dataLen;

                Serial0.printf("Client: indicate chunk %d/%d total=%d\n", chunkIndex + 1,
                               totalChunks, s_indicate_len);

                if (chunkIndex + 1 == totalChunks)
                    s_indicate_done = true;
            });

        uint8_t ready = 0xAA;
        pRx->writeValue(&ready, 1, false);
        Serial0.println("Client: sent ready signal");

        uint32_t timeout = millis() + 10000;
        while (!s_indicate_done && millis() < timeout)
            delay(10);

        if (s_indicate_done)
        {
            s_rx_len = s_indicate_len;
            memcpy(s_rx_buf, s_indicate_buf, s_rx_len);
            Serial0.printf("Client: indicate done, %d bytes\n", s_rx_len);
        }
    }

    if (pRx && pRx->canWrite())
    {
        const size_t chunkSize = 508;
        uint16_t     totalChunks = (s_tx_len + chunkSize - 1) / chunkSize;
        bool         writeOk = true;

        for (uint16_t i = 0; i < totalChunks && writeOk; i++)
        {
            size_t  offset = i * chunkSize;
            size_t  len = min(chunkSize, s_tx_len - offset);
            uint8_t pkt[512];

            pkt[0] = (i >> 8) & 0xFF;
            pkt[1] = i & 0xFF;
            pkt[2] = (totalChunks >> 8) & 0xFF;
            pkt[3] = totalChunks & 0xFF;
            memcpy(pkt + 4, s_tx_buf + offset, len);

            writeOk = pRx->writeValue(pkt, len + 4, false);
            Serial0.printf("Client: chunk %d/%d %s\n", i + 1, totalChunks, writeOk ? "OK" : "FAIL");
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
    {
        return;
    }
    Serial0.printf("bleStart: my_profile_len=%d\n", my_profile_len);
    s_busy = true;
    s_callback = callback;
    s_rx_len = 0;
    memcpy(s_peer_mac, peer_mac, 6);
    memcpy(s_tx_buf, my_profile, my_profile_len);
    s_tx_len = my_profile_len;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService *pSvc = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic *pTx =
        pSvc->createCharacteristic(TX_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::INDICATE);
    pTx->setValue(s_tx_buf, s_tx_len);

    s_rxChar = pSvc->createCharacteristic(RX_CHAR_UUID, NIMBLE_PROPERTY::WRITE, PACKING_BUF_MAX);
    s_rxChar->setCallbacks(new RxCallbacks());

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