#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BLE_ROLE_SERVER,
    BLE_ROLE_CLIENT,
} ble_role_t;

typedef void (*ble_callback_t)(bool success, const uint8_t *profile_data, size_t len);

void bleStart(const uint8_t peer_mac[6], uint8_t peer_type, ble_role_t role,
              const uint8_t *my_profile, size_t my_profile_len, ble_callback_t callback);
void bleStop();
bool bleIsBusy();