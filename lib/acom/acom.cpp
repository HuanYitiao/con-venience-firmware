#include "acom.h"

#include <NimBLEDevice.h>

#include "esp_rom_crc.h"

static bool          active = false;
static unsigned long lastProbeTime = 0;
static unsigned long startTime = 0;
static bool          macReceived = false;
static uint8_t       peerMac[6] = {0};
static uint8_t       peerType = 0;
static uint8_t       ownMac[6] = {0};
static uint8_t       rxBuf[16];
static int           rxLen = 0;
static bool          failed = false;

static void getOwnMac(uint8_t mac[6])
{
    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
    {
        mac[i] = addr->val[5 - i];
    }
}

void acomInit()
{
    pinMode(PIN_ACOM, OUTPUT_OPEN_DRAIN);
    Serial1.begin(ACOM_BAUD, SERIAL_8N1, PIN_ACOM, PIN_ACOM);
    getOwnMac(ownMac);
}

void acomStart()
{
    active = true;
    macReceived = false;
    failed = false;
    rxLen = 0;
    startTime = millis();
    lastProbeTime = 0;
    while (Serial1.available())
    {
        Serial1.read();
    }
}

void acomStop()
{
    active = false;
}

bool acomHasMac(uint8_t mac_out[6], uint8_t *type_out)
{
    if (!macReceived)
    {
        return false;
    }
    memcpy(mac_out, peerMac, 6);
    if (type_out)
    {
        *type_out = peerType;
    }
    return true;
}

static void sendWwnMac()
{
    uint8_t frame[ACOM_FRAME_LEN];
    frame[0] = ACOM_HEADER;
    memcpy(frame + 1, ownMac, 6);
    frame[7] = NimBLEDevice::getAddress().getType();
    frame[8] = esp_rom_crc8_le(0, frame + 1, ACOM_PAYLOAD_LEN);
    Serial1.write(frame, ACOM_FRAME_LEN);
    Serial1.flush();
    delay(10);
    while (Serial1.available())
    {
        Serial1.read();
    }
}

void acomTick()
{
    if (!active)
    {
        return;
    }

    if (millis() - startTime > ACOM_TIMEOUT_MS)
    {
        active = false;
        failed = true;
        return;
    }

    if (millis() - lastProbeTime > ACOM_PROBE_MS)
    {
        lastProbeTime = millis();
        rxLen = 0;
        while (Serial1.available())
        {
            Serial1.read();
        }
        sendWwnMac();
    }

    while (Serial1.available())
    {
        uint8_t b = Serial1.read();

        if (rxLen < ACOM_FRAME_LEN)
        {
            rxBuf[rxLen++] = b;
        }

        if (rxLen < ACOM_FRAME_LEN)
        {
            continue;
        }

        bool valid =
            (rxBuf[0] == ACOM_HEADER)
            && (esp_rom_crc8_le(0, rxBuf + 1, ACOM_PAYLOAD_LEN) == rxBuf[ACOM_FRAME_LEN - 1]);

        if (valid && memcmp(rxBuf + 1, ownMac, 6) == 0)
        {
            rxLen = 0;
            continue;
        }

        if (!valid)
        {
            memmove(rxBuf, rxBuf + 1, ACOM_FRAME_LEN - 1);
            rxLen = ACOM_FRAME_LEN - 1;
            continue;
        }

        memcpy(peerMac, rxBuf + 1, 6);
        peerType = rxBuf[7];
        macReceived = true;
        active = false;

        for (int i = 0; i < 5; i++)
        {
            while (Serial1.available())
            {
                Serial1.read();
            }
            sendWwnMac();
            delay(50);
        }

        Serial0.printf("[ACOM] raw rx: %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", rxBuf[0],
                       rxBuf[1], rxBuf[2], rxBuf[3], rxBuf[4], rxBuf[5], rxBuf[6], rxBuf[7],
                       rxBuf[8]);
        Serial0.println("[ACOM] mac received");
        Serial0.printf("[ACOM] peer MAC: %02x:%02x:%02x:%02x:%02x:%02x type=%d\n", peerMac[0],
                       peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], peerType);
        break;
    }
}

bool acomFailed()
{
    return failed;
}