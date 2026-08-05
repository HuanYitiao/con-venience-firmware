// packing.cpp
#include "packing.h"

#include <Arduino.h>

#include <ArduinoJson.h>
#include <SD.h>

#define JSON_LEN_MAX 512

size_t packingPack(uint8_t *out, size_t outLen)
{
    File jsonFile = SD.open(SELF_JSON, FILE_READ);
    if (!jsonFile)
    {
        return 0;
    }

    uint8_t jsonBuf[JSON_LEN_MAX];
    size_t  jsonLen = jsonFile.read(jsonBuf, sizeof(jsonBuf));
    jsonFile.close();

    File binFile = SD.open(SELF_BIN, FILE_READ);
    if (!binFile)
    {
        return 0;
    }

    size_t avatarSize = binFile.size();
    size_t totalLen = 4 + jsonLen + avatarSize;

    if (totalLen > outLen)
    {
        binFile.close();
        return 0;
    }

    out[0] = (jsonLen >> 24) & 0xFF;
    out[1] = (jsonLen >> 16) & 0xFF;
    out[2] = (jsonLen >> 8) & 0xFF;
    out[3] = (jsonLen) & 0xFF;

    memcpy(out + 4, jsonBuf, jsonLen);
    binFile.read(out + 4 + jsonLen, avatarSize);
    binFile.close();

    return totalLen;
}

bool packingUnpack(const uint8_t *data, size_t len, const uint8_t acomMac[6])
{
    if (len < 4 || !acomMac)
    {
        return false;
    }

    uint32_t jsonLen = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                       | ((uint32_t)data[2] << 8) | (uint32_t)data[3];

    if (jsonLen + 4 > len)
    {
        return false;
    }

    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, data + 4, jsonLen);
    if (err)
        return false;

    // peer uuid is untrusted; require an exact 12-hex-char MAC before it touches a path
    char safeUuid[UUID_LEN];
    if (!storageUuidValidate(doc["uuid"], safeUuid))
    {
        return false;
    }

    // and it must be the MAC we just exchanged over ACOM, not an arbitrary spoofed identity
    char acomUuid[UUID_LEN];
    storageMacToUuid(acomMac, acomUuid);
    if (strcmp(safeUuid, acomUuid) != 0)
    {
        return false;
    }

    char dirPath[PATH_LEN];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", FRIENDS_DIR, safeUuid);
    SD.mkdir(dirPath);

    char jsonPath[PATH_LEN];
    snprintf(jsonPath, sizeof(jsonPath), "%s/profile.json", dirPath);
    File jsonFile = SD.open(jsonPath, FILE_WRITE);
    if (!jsonFile)
    {
        return false;
    }
    jsonFile.write(data + 4, jsonLen);
    jsonFile.close();

    size_t avatarSize = len - 4 - jsonLen;
    if (avatarSize > 0)
    {
        char binPath[PATH_LEN];
        snprintf(binPath, sizeof(binPath), "%s/avatar.bin", dirPath);
        File binFile = SD.open(binPath, FILE_WRITE);
        if (!binFile)
        {
            return false;
        }
        binFile.write(data + 4 + jsonLen, avatarSize);
        binFile.close();
    }

    return true;
}