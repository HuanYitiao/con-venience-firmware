#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BLE_ROLE_SERVER,
    BLE_ROLE_CLIENT,
} ble_role_t;

typedef void (*ble_callback_t)(bool success, const uint8_t *profile_data, size_t len);

// Transfer-progress reporting. The active role publishes a monotonic,
// never-overshooting chunk count into a shared snapshot; the caller polls it.
//   BLE_PROG_CONNECTING    -- not connected yet, or denominator not yet known
//   BLE_PROG_ACTIVE        -- honest done/total available (draw a real bar)
//   BLE_PROG_INDETERMINATE -- transferring but no honest denominator (fallback)
typedef enum
{
    BLE_PROG_CONNECTING,
    BLE_PROG_ACTIVE,
    BLE_PROG_INDETERMINATE
} ble_prog_t;

void bleStart(const uint8_t peer_mac[6], uint8_t peer_type, ble_role_t role,
              const uint8_t *my_profile, size_t my_profile_len, ble_callback_t callback);
void bleStop();
bool bleIsBusy();

// Fill done/total with completed/total chunk counts (either may be null) and
// return the current progress state. done/total are meaningful only when the
// return value is BLE_PROG_ACTIVE.
ble_prog_t bleGetProgress(uint16_t *done_chunks, uint16_t *total_chunks);