#pragma once

#include <stddef.h>
#include <stdint.h>

#include "storage.h"

#define PACKING_BUF_MAX 5000

size_t packingPack(uint8_t *out, size_t outLen);
// On success, outUuid (if given) receives the peer's validated uuid.
bool   packingUnpack(const uint8_t *data, size_t len, char *outUuid = nullptr);