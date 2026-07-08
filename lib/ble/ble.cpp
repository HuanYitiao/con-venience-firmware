#include "ble.h"

#include <Arduino.h>

#include <NimBLEDevice.h>
#include <string.h>

#include "packing.h"

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TX_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RX_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"

#define PROFILE_LEN_MAX 4608
#define CHUNK_SIZE 240
#define REQ_MAGIC 0xBB

static ble_callback_t        s_callback = nullptr;
static bool                  s_busy = false;
static uint8_t               s_peer_mac[6];
static uint8_t               s_peer_type = 0;
static uint8_t               s_rx_buf[PROFILE_LEN_MAX];
static size_t                s_rx_len = 0;
static uint8_t               s_tx_buf[PACKING_BUF_MAX];
static size_t                s_tx_len = 0;
static NimBLECharacteristic *s_rxChar = nullptr;
static volatile uint16_t     s_req_chunk = 0;

static void bleFinish(bool success)
{
    NimBLEDevice::stopAdvertising();
    s_busy = false;
    if (s_callback)
        s_callback(success, s_rx_buf, s_rx_len);
}

// Callbacks for both characteristics.
// TX_CHAR (READ): onRead serves the chunk the client requested.
// RX_CHAR (WRITE): onWrite handles chunk requests and incoming profile data.
class CharCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar, NimBLEConnInfo &connInfo) override
    {
        auto   raw = pChar->getValue();
        size_t rawLen = pChar->getLength();

        // Chunk request from client: [REQ_MAGIC][index_hi][index_lo]
        if (rawLen == 3 && raw[0] == REQ_MAGIC)
        {
            s_req_chunk = ((uint16_t)raw[1] << 8) | raw[2];
            return;
        }

        // Otherwise: a profile data chunk [index_hi][index_lo][total_hi][total_lo][data...]
        if (rawLen < 4)
        {
            return;
        }

        uint16_t chunkIndex = ((uint16_t)raw[0] << 8) | raw[1];
        uint16_t totalChunks = ((uint16_t)raw[2] << 8) | raw[3];
        size_t   dataLen = rawLen - 4;

        memcpy(s_rx_buf + s_rx_len, raw.data() + 4, dataLen);
        s_rx_len += dataLen;

        Serial0.printf("Server: chunk %d/%d received, total so far=%d\n", chunkIndex + 1,
                       totalChunks, s_rx_len);
    }

    void onRead(NimBLECharacteristic *pChar, NimBLEConnInfo &connInfo) override
    {
        uint16_t total = (s_tx_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
        uint16_t i = s_req_chunk;

        if (i >= total)
        {
            pChar->setValue((uint8_t *)"", 0);
            return;
        }

        size_t  offset = (size_t)i * CHUNK_SIZE;
        size_t  len = min((size_t)CHUNK_SIZE, s_tx_len - offset);
        uint8_t pkt[CHUNK_SIZE + 4];

        pkt[0] = (i >> 8) & 0xFF;
        pkt[1] = i & 0xFF;
        pkt[2] = (total >> 8) & 0xFF;
        pkt[3] = total & 0xFF;
        memcpy(pkt + 4, s_tx_buf + offset, len);

        pChar->setValue(pkt, len + 4);
        Serial0.printf("Server: serving chunk %d/%d on read\n", i + 1, total);
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
    NimBLEAddress addr(s_peer_mac, s_peer_type);
    Serial0.printf("Client: direct connect to %s type=%d\n", addr.toString().c_str(), s_peer_type);

    NimBLEClient *pClient = NimBLEDevice::createClient();
    if (!pClient->connect(addr))
    {
        Serial0.println("Client: connect failed");
        NimBLEDevice::deleteClient(pClient);
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    Serial0.println("Client: connected");
    pClient->setDataLen(251);
    pClient->exchangeMTU();
    Serial0.printf("MTU: %d\n", pClient->getMTU());

    NimBLERemoteService *pSvc = pClient->getService(SERVICE_UUID);
    if (!pSvc)
    {
        Serial0.println("Client: service not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    NimBLERemoteCharacteristic *pTx = pSvc->getCharacteristic(TX_CHAR_UUID);
    NimBLERemoteCharacteristic *pRx = pSvc->getCharacteristic(RX_CHAR_UUID);

    if (!pTx || !pRx || !pTx->canRead() || !pRx->canWrite())
    {
        Serial0.println("Client: characteristics not usable");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        bleFinish(false);
        vTaskDelete(nullptr);
        return;
    }

    // --- Receive server's profile: client-driven request/read loop ---
    s_rx_len = 0;
    uint16_t i = 0;
    uint16_t total = 1;
    int      retries = 0;
    bool     recvOk = true;

    while (i < total)
    {
        uint8_t req[3] = {REQ_MAGIC, (uint8_t)(i >> 8), (uint8_t)(i & 0xFF)};
        if (!pRx->writeValue(req, 3, true))
        {
            if (++retries > 5)
            {
                recvOk = false;
                break;
            }
            delay(20);
            continue;
        }

        NimBLEAttValue val = pTx->readValue();
        if (val.length() < 4)
        {
            if (++retries > 5)
            {
                recvOk = false;
                break;
            }
            delay(20);
            continue;
        }

        const uint8_t *d = val.data();
        uint16_t       idx = ((uint16_t)d[0] << 8) | d[1];
        total = ((uint16_t)d[2] << 8) | d[3];
        size_t dataLen = val.length() - 4;

        if (idx != i)
        {
            if (++retries > 5)
            {
                recvOk = false;
                break;
            }
            delay(20);
            continue;
        }

        retries = 0;
        memcpy(s_rx_buf + s_rx_len, d + 4, dataLen);
        s_rx_len += dataLen;
        Serial0.printf("Client: read chunk %d/%d total=%d\n", idx + 1, total, s_rx_len);
        i++;
    }

    if (recvOk)
    {
        Serial0.printf("Client: read done, %d bytes\n", s_rx_len);
    }
    else
    {
        Serial0.println("Client: read failed");
    }

    // --- Send own profile to server via write ---
    uint16_t txTotal = (s_tx_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    for (uint16_t j = 0; j < txTotal; j++)
    {
        size_t  offset = (size_t)j * CHUNK_SIZE;
        size_t  len = min((size_t)CHUNK_SIZE, s_tx_len - offset);
        uint8_t pkt[CHUNK_SIZE + 4];

        pkt[0] = (j >> 8) & 0xFF;
        pkt[1] = j & 0xFF;
        pkt[2] = (txTotal >> 8) & 0xFF;
        pkt[3] = txTotal & 0xFF;
        memcpy(pkt + 4, s_tx_buf + offset, len);

        pRx->writeValue(pkt, len + 4, true);
        Serial0.printf("Client: chunk %d/%d sent\n", j + 1, txTotal);
    }

    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    bleFinish(recvOk && s_rx_len > 0);
    vTaskDelete(nullptr);
}

void bleStart(const uint8_t peer_mac[6], uint8_t peer_type, ble_role_t role,
              const uint8_t *my_profile, size_t my_profile_len, ble_callback_t callback)
{
    Serial0.println(">>> BLE BUILD MARKER v5 (read model) <<<");

    if (s_busy)
    {
        return;
    }
    Serial0.printf("bleStart: my_profile_len=%d\n", my_profile_len);
    s_busy = true;
    s_callback = callback;

    s_rx_len = 0;
    s_req_chunk = 0;

    memcpy(s_peer_mac, peer_mac, 6);
    s_peer_type = peer_type;
    memcpy(s_tx_buf, my_profile, my_profile_len);
    s_tx_len = my_profile_len;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService *pSvc = pServer->createService(SERVICE_UUID);

    static CharCallbacks charCb;

    NimBLECharacteristic *pTx = pSvc->createCharacteristic(TX_CHAR_UUID, NIMBLE_PROPERTY::READ);
    pTx->setCallbacks(&charCb);

    s_rxChar = pSvc->createCharacteristic(RX_CHAR_UUID, NIMBLE_PROPERTY::WRITE, PACKING_BUF_MAX);
    s_rxChar->setCallbacks(&charCb);

    pSvc->start();

    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->start(0);

    if (role == BLE_ROLE_CLIENT)
    {
        xTaskCreate(clientTask, "ble_client", 4096, nullptr, 1, nullptr);
    }
    // Server role needs no task: it responds entirely through callbacks.
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