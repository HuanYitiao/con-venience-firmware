#include "acom.h"

#include <NimBLEDevice.h>

#include "fsm.h"

static bool          active = false;
static unsigned long lastProbeTime = 0;
static unsigned long startTime = 0;
static bool          macReceived = false;
static uint8_t       peerMac[6] = {0};
static uint8_t       ownMac[6] = {0};
static uint8_t       rxBuf[16];
static int           rxLen = 0;

static void get_own_mac(uint8_t mac[6])
{
    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
        mac[i] = addr->val[5 - i];
}

void acom_init()
{
    pinMode(ACOM_PIN, OUTPUT_OPEN_DRAIN);
    Serial1.begin(ACOM_BAUD, SERIAL_8N1, ACOM_PIN, ACOM_PIN);
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
        Serial1.read();
}

void acom_stop()
{
    active = false;
}

bool acom_has_mac(uint8_t mac_out[6])
{
    if (!macReceived)
        return false;
    memcpy(mac_out, peerMac, 6);
    return true;
}

static void send_own_mac()
{
    Serial1.write(ownMac, 6);
    Serial1.flush();
    delay(10);
    while (Serial1.available())
        Serial1.read();
}

void acom_tick()
{
    if (!active)
        return;

    if (millis() - startTime > ACOM_TIMEOUT_MS)
    {
        active = false;
        fsmHandleEvent(EVENT_ACOM_FAILURE);
        return;
    }

    if (millis() - lastProbeTime > ACOM_PROBE_MS)
    {
        lastProbeTime = millis() + random(0, 50);
        rxLen = 0;
        while (Serial1.available())
            Serial1.read();
        send_own_mac();
    }

    while (Serial1.available() && rxLen < 6)
        rxBuf[rxLen++] = Serial1.read();

    if (rxLen >= 6)
    {
        if (memcmp(rxBuf, ownMac, 6) != 0)
        {
            memcpy(peerMac, rxBuf, 6);
            macReceived = true;
            active = false;

            for (int i = 0; i < 5; i++)
            {
                while (Serial1.available())
                    Serial1.read();
                send_own_mac();
                delay(50);
            }

            Serial0.println("[ACOM] mac received");
            Serial0.printf("[ACOM] peer MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", peerMac[0],
                           peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
            fsmHandleEvent(EVENT_ACOM_SUCCESS);
        }
        else
        {
            rxLen = 0;
        }
    }
}