#include "acom.h"

#include <NimBLEDevice.h>

static bool          active = false;
static unsigned long lastProbeTime = 0;
static unsigned long startTime = 0;
static bool          macReceived = false;
static uint8_t       peerMac[6] = {0};
static uint8_t       peerType = 0;
static uint8_t       ownMac[6] = {0};
static uint8_t       rxBuf[16];
static int           rxLen = 0;

static void get_own_mac(uint8_t mac[6])
{
    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
    {
        mac[i] = addr->val[5 - i];
    }
}

void acom_init()
{
    pinMode(PIN_ACOM, OUTPUT_OPEN_DRAIN);
    Serial1.begin(ACOM_BAUD, SERIAL_8N1, PIN_ACOM, PIN_ACOM);
    get_own_mac(ownMac);
}

void acom_start()
{
    active = true;
    macReceived = false;
    rxLen = 0;
    startTime = millis();
    lastProbeTime = 0;
    while (Serial1.available())
    {
        Serial1.read();
    }
}

void acom_stop()
{
    active = false;
}

bool acom_has_mac(uint8_t mac_out[6], uint8_t *type_out)
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

static void send_own_mac()
{
    uint8_t frame[8];
    frame[0] = 0xA5;
    memcpy(frame + 1, ownMac, 6);
    frame[7] = NimBLEDevice::getAddress().getType();
    Serial1.write(frame, 8);
    Serial1.flush();
    delay(10);
    while (Serial1.available())
    {
        Serial1.read();
    }
}

void acom_tick()
{
    if (!active)
    {
        return;
    }

    if (millis() - startTime > ACOM_TIMEOUT_MS)
    {
        active = false;
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
        send_own_mac();
    }

    while (Serial1.available())
    {
        uint8_t b = Serial1.read();
        if (rxLen == 0)
        {
            if (b == 0xA5)
                rxBuf[rxLen++] = b;
        }
        else if (rxLen < 8)
        {
            rxBuf[rxLen++] = b;
        }
    }

    if (rxLen >= 8)
    {
        if (memcmp(rxBuf + 1, ownMac, 6) != 0)
        {
            memcpy(peerMac, rxBuf + 1, 6);
            peerType = rxBuf[7];
            macReceived = true;
            active = false;

            for (int i = 0; i < 5; i++)
            {
                while (Serial1.available())
                    Serial1.read();
                send_own_mac();
                delay(50);
            }

            Serial0.printf("[ACOM] raw rx: %02x %02x %02x %02x %02x %02x %02x %02x\n", rxBuf[0],
                           rxBuf[1], rxBuf[2], rxBuf[3], rxBuf[4], rxBuf[5], rxBuf[6], rxBuf[7]);
            Serial0.println("[ACOM] mac received");
            Serial0.printf("[ACOM] peer MAC: %02x:%02x:%02x:%02x:%02x:%02x type=%d\n", peerMac[0],
                           peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], peerType);
        }
        else
        {
            rxLen = 0;
        }
    }
}