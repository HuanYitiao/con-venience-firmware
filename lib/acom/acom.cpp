#include "acom.h"

#include <NimBLEDevice.h>
#include <soc/gpio_struct.h>

#include "esp_rom_crc.h"


static bool          active = false;
static unsigned long nextProbeTime = 0;
static unsigned long startTime = 0;
static bool          macReceived = false;
static bool          peerSeen = false;
static unsigned long peerSeenTime = 0;
static uint8_t       peerMac[6] = {0};
static uint8_t       peerType = 0;
static uint8_t       ownMac[6] = {0};
static uint8_t       rxBuf[ACOM_FRAME_LEN];
static int           rxLen = 0;
static bool          failed = false;

static void scheduleNextProbe();

static const int MAC_OFS = ACOM_HEADER_LEN;
static const int TYPE_OFS = ACOM_HEADER_LEN + 6;
static const int CRC_OFS = ACOM_FRAME_LEN - 1;

void acomGetOwnMac(uint8_t mac[6])
{
    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
    {
        mac[i] = addr->val[5 - i];
    }
}

void acomInit()
{
    // Pull-up (PIN_ACOM_PU) off by default: avoids back-powering an unpowered peer on contact.
    pinMode(PIN_ACOM_PU, OUTPUT);
    digitalWrite(PIN_ACOM_PU, LOW);

    Serial1.begin(ACOM_BAUD, SERIAL_8N1, PIN_ACOM_OD, PIN_ACOM_OD);
    // Open-drain on the UART pad WITHOUT detaching the peripheral: pinMode() would
    // re-route the pad to a plain GPIO output (latch=0) and hold the line low.
    GPIO.pin[PIN_ACOM_OD].pad_driver = 1;

    acomGetOwnMac(ownMac);
}

void acomStart()
{
    digitalWrite(PIN_ACOM_PU, HIGH);
    active = true;
    macReceived = false;
    peerSeen = false;
    failed = false;
    rxLen = 0;
    startTime = millis();
    // First probe after a random delay also lets the line settle after the pull-up turns on.
    scheduleNextProbe();
    while (Serial1.available())
    {
        Serial1.read();
    }
}

void acomStop()
{
    active = false;
    digitalWrite(PIN_ACOM_PU, LOW);
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

static void sendOwnMac()
{
    uint8_t frame[ACOM_FRAME_LEN];
    frame[0] = ACOM_HEADER_0;
    frame[1] = ACOM_HEADER_1;
    memcpy(frame + MAC_OFS, ownMac, 6);
    frame[TYPE_OFS] = NimBLEDevice::getAddress().getType() | (peerSeen ? ACOM_FLAG_ACK : 0);
    frame[CRC_OFS] = esp_rom_crc8_le(0, frame + MAC_OFS, ACOM_PAYLOAD_LEN);
    Serial1.write(frame, ACOM_FRAME_LEN);
    // No RX drain here: our own echo is dropped by the parser (own-MAC filter),
    // while draining would also throw away a peer frame that just arrived.
    Serial1.flush();
}

// Random 0.5x..1.5x interval: with a fixed period, two devices whose probes
// happen to overlap keep colliding on the shared wire for the whole attempt.
static void scheduleNextProbe()
{
    nextProbeTime = millis() + ACOM_PROBE_MS / 2 + esp_random() % ACOM_PROBE_MS;
}

// Feeds one byte into the frame parser; returns true once a valid peer frame is in rxBuf.
static bool parseByte(uint8_t b)
{
    rxBuf[rxLen++] = b;
    if (rxLen < ACOM_FRAME_LEN)
    {
        return false;
    }

    bool valid =
        (rxBuf[0] == ACOM_HEADER_0) && (rxBuf[1] == ACOM_HEADER_1)
        && (esp_rom_crc8_le(0, rxBuf + MAC_OFS, ACOM_PAYLOAD_LEN) == rxBuf[CRC_OFS]);

    if (!valid)
    {
        memmove(rxBuf, rxBuf + 1, ACOM_FRAME_LEN - 1);
        rxLen = ACOM_FRAME_LEN - 1;
        return false;
    }

    rxLen = 0;
    // Own echo on the single wire.
    return memcmp(rxBuf + MAC_OFS, ownMac, 6) != 0;
}

static void finish(bool acked)
{
    macReceived = true;
    active = false;

    // Peer may still be waiting for our ACK; keep sending ACK frames for longer
    // than its max probe interval before we drop the pull-up.
    for (int i = 0; i < 5; i++)
    {
        delay(50 + esp_random() % 30);
        sendOwnMac();
    }

    Serial0.printf("[ACOM] peer MAC: %02x:%02x:%02x:%02x:%02x:%02x type=%d %s\n", peerMac[0],
                   peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], peerType,
                   acked ? "(acked)" : "(no ack, proceeding)");
}

void acomTick()
{
    if (!active)
    {
        return;
    }

    // Parse before probing: the main loop (full LCD redraw) is slow enough that a
    // complete peer frame is often already waiting in the RX buffer.
    while (Serial1.available())
    {
        if (!parseByte(Serial1.read()))
        {
            continue;
        }

        // Lock onto the first peer; ignore anyone else on the wire.
        if (peerSeen && memcmp(peerMac, rxBuf + MAC_OFS, 6) != 0)
        {
            continue;
        }

        if (!peerSeen)
        {
            memcpy(peerMac, rxBuf + MAC_OFS, 6);
            peerType = rxBuf[TYPE_OFS] & ~ACOM_FLAG_ACK;
            peerSeen = true;
            peerSeenTime = millis();
            Serial0.print("[ACOM] raw rx:");
            for (int i = 0; i < ACOM_FRAME_LEN; i++)
            {
                Serial0.printf(" %02x", rxBuf[i]);
            }
            Serial0.println();
            // Tell the peer right away that we have its MAC.
            nextProbeTime = millis();
        }

        // Peer has our MAC too: both sides are done.
        if (rxBuf[TYPE_OFS] & ACOM_FLAG_ACK)
        {
            finish(true);
            return;
        }
    }

    // We have the peer's MAC but never heard its ACK: proceed rather than fail.
    if (peerSeen && millis() - peerSeenTime > ACOM_ACK_WAIT_MS)
    {
        finish(false);
        return;
    }

    if (!peerSeen && millis() - startTime > ACOM_TIMEOUT_MS)
    {
        active = false;
        failed = true;
        return;
    }

    if ((long)(millis() - nextProbeTime) >= 0)
    {
        sendOwnMac();
        scheduleNextProbe();
    }
}

bool acomFailed()
{
    return failed;
}