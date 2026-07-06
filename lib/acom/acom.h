#pragma once

#include <Arduino.h>

#include "pins.h"

#define ACOM_BAUD 4800
#define ACOM_PROBE_BYTE 0xAC
#define ACOM_PROBE_MS 200
#define ACOM_TIMEOUT_MS 30000

void acom_init();
void acom_start();
void acom_stop();
void acom_tick();

bool acom_has_mac(uint8_t mac_out[6], uint8_t *type_out);