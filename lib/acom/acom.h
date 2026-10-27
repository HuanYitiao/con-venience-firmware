#pragma once

#include <Arduino.h>

#include "pins.h"

#define ACOM_BAUD 4800
#define ACOM_PROBE_MS 200
#define ACOM_TIMEOUT_MS 8000
// After seeing the peer, how long to wait for its ACK before proceeding anyway.
#define ACOM_ACK_WAIT_MS 1500

// Two-byte header: a lone header byte matches random MAC data far too often.
#define ACOM_HEADER_0 0xA5
#define ACOM_HEADER_1 0x5A
#define ACOM_HEADER_LEN 2
// Payload: MAC[6] + addr type[1], followed by CRC-8 over the payload.
#define ACOM_PAYLOAD_LEN 7
#define ACOM_FRAME_LEN (ACOM_HEADER_LEN + ACOM_PAYLOAD_LEN + 1)
// Set in the addr-type byte once we hold the peer's MAC; the low bits stay the BLE addr type.
#define ACOM_FLAG_ACK 0x80

void acomGetOwnMac(uint8_t mac[6]);

void acomInit();
void acomStart();
void acomStop();
void acomTick();
bool acomFailed();
bool acomHasMac(uint8_t mac_out[6], uint8_t *type_out);