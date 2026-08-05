#pragma once

#include <Arduino.h>

#include "pins.h"

#define ACOM_BAUD 4800
#define ACOM_PROBE_MS 200
#define ACOM_TIMEOUT_MS 8000

#define ACOM_HEADER 0xA5
#define ACOM_PAYLOAD_LEN 7
#define ACOM_FRAME_LEN 9

void acomGetOwnMac(uint8_t mac[6]);

void acomInit();
void acomStart();
void acomStop();
void acomTick();
bool acomFailed();
bool acomHasMac(uint8_t mac_out[6], uint8_t *type_out);