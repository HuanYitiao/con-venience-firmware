#include "storage.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <esp_random.h>

bool storageUuidValidate(const char *s, char out[UUID_LEN])
{
    if (!s)
    {
        return false;
    }
    for (int i = 0; i < 16; i++)
    {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        if (!hex)
        {
            return false;
        }
        out[i] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
    }
    if (s[16] != '\0')
    {
        return false;
    }
    out[16] = '\0';
    return true;
}

bool storageReadSelfId(char out[UUID_LEN])
{
    if (out)
    {
        out[0] = '\0';
    }
    if (!SD.exists(SELF_JSON))
    {
        return false;
    }
    File selfJson = SD.open(SELF_JSON, FILE_READ);
    if (!selfJson)
    {
        return false;
    }
    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, selfJson);
    selfJson.close();
    if (err)
    {
        return false;
    }
    char id[UUID_LEN];
    if (!storageUuidValidate(doc["uuid"], id))
    {
        return false;
    }
    if (out)
    {
        strlcpy(out, id, UUID_LEN);
    }
    return true;
}

bool storageEnsureSelfId(char outId[UUID_LEN], const char *preferId)
{
    if (outId)
    {
        outId[0] = '\0';
    }
    if (!SD.exists(SELF_JSON))
    {
        return false;
    }

    JsonDocument doc;
    File         selfJson = SD.open(SELF_JSON, FILE_READ);
    if (!selfJson)
    {
        return false;
    }
    DeserializationError err = deserializeJson(doc, selfJson);
    selfJson.close();
    if (err)
    {
        return false;
    }

    char id[UUID_LEN];
    bool haveId = false;

    if (storageUuidValidate(preferId, id))
    {
        haveId = true;
    }
    if (!haveId && storageUuidValidate(doc["uuid"], id))
    {
        haveId = true;
    }
    if (!haveId)
    {
        uint8_t r[8];
        esp_fill_random(r, sizeof(r));
        snprintf(id, UUID_LEN, "%02X%02X%02X%02X%02X%02X%02X%02X", r[0], r[1], r[2], r[3], r[4],
                 r[5], r[6], r[7]);
    }

    const char *cur = doc["uuid"];
    if (!cur || strcmp(cur, id) != 0)
    {
        doc["uuid"] = id;
        SD.remove(SELF_JSON);
        File out = SD.open(SELF_JSON, FILE_WRITE);
        if (!out)
        {
            return false;
        }
        serializeJson(doc, out);
        out.close();
    }

    if (outId)
    {
        strlcpy(outId, id, UUID_LEN);
    }
    return true;
}

bool storageInit(uint8_t csPin)
{
    bool mounted = false;
    for (int i = 0; i < 3; i++)
    {
        if (SD.begin(csPin, SPI, 4000000))
        {
            mounted = true;
            break;
        }
        delay(500);
    }
    if (!mounted)
    {
        Serial0.println("SD init failed");
        return false;
    }

    SD.mkdir(SELF_DIR);
    SD.mkdir(FRIENDS_DIR);
    return true;
}

bool storageLoadSelf(Contact &contact)
{
    if (!SD.exists(SELF_JSON) || !SD.exists(SELF_BIN))
    {
        return false;
    }

    File selfJson = SD.open(SELF_JSON, FILE_READ);
    if (!selfJson)
    {
        return false;
    }
    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, selfJson);
    selfJson.close();
    if (err)
    {
        return false;
    }

    strlcpy(contact.uuid, doc["uuid"] | "", UUID_LEN);
    strlcpy(contact.name, doc["name"] | "", NAME_LEN);
    strlcpy(contact.species, doc["species"] | "", SPECIES_LEN);
    strlcpy(contact.from, doc["from"] | "", FROM_LEN);
    if (contact.name[0] == '\0')
    {
        return false;
    }

    contact.linkCount = 0;
    JsonArray links = doc["links"].as<JsonArray>();
    for (JsonObject link : links)
    {
        if (contact.linkCount >= LINKS_MAX)
        {
            break;
        }
        strlcpy(contact.links[contact.linkCount].platform, link["platform"] | (link["tag"] | ""),
                PLATFORM_LEN);
        strlcpy(contact.links[contact.linkCount].url, link["url"] | "", URL_LEN);
        contact.linkCount++;
    }
    contact.avatarResolution = doc["avatar_res"] | 0;
    contact.avatarMode = (AvatarMode)(doc["avatar_mode"] | 0);

    uint16_t expected = avatarLen(contact.avatarResolution, contact.avatarMode);
    if (expected == 0 || expected > AVATAR_LEN_MAX)
    {
        return false;
    }

    File selfBin = SD.open(SELF_BIN, FILE_READ);
    if (!selfBin)
    {
        return false;
    }
    if (selfBin.size() != expected)
    {
        selfBin.close();
        return false;
    }
    size_t got = selfBin.read(contact.avatar, expected);
    selfBin.close();
    if (got != expected)
    {
        return false;
    }
    return true;
}

bool storageSaveContact(const Contact &contact)
{
    char jsonPath[PATH_LEN];
    char binPath[PATH_LEN];
    char dirPath[PATH_LEN];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", FRIENDS_DIR, contact.uuid);
    snprintf(jsonPath, sizeof(jsonPath), "%s/%s/profile.json", FRIENDS_DIR, contact.uuid);
    snprintf(binPath, sizeof(binPath), "%s/%s/avatar.bin", FRIENDS_DIR, contact.uuid);
    SD.mkdir(dirPath);
    SD.remove(jsonPath);
    SD.remove(binPath);

    File friendJson = SD.open(jsonPath, FILE_WRITE);
    if (!friendJson)
    {
        return false;
    }
    JsonDocument doc;
    doc["uuid"] = contact.uuid;
    doc["species"] = contact.species;
    doc["name"] = contact.name;
    doc["from"] = contact.from;
    doc["avatar_res"] = contact.avatarResolution;
    doc["avatar_mode"] = contact.avatarMode;
    JsonArray links = doc["links"].to<JsonArray>();
    for (int i = 0; i < contact.linkCount; i++)
    {
        JsonObject link = links.add<JsonObject>();
        link["platform"] = contact.links[i].platform;
        link["url"] = contact.links[i].url;
    }
    serializeJson(doc, friendJson);
    friendJson.close();

    File friendBin = SD.open(binPath, FILE_WRITE);
    if (!friendBin)
    {
        return false;
    }
    friendBin.write(contact.avatar, avatarLen(contact.avatarResolution, contact.avatarMode));
    friendBin.close();
    return true;
}

bool storageLoadContact(int index, Contact &contact)
{
    File dir = SD.open(FRIENDS_DIR, FILE_READ);
    if (!dir)
    {
        return false;
    }

    int  count = 0;
    File file = dir.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            if (count == index)
                break;
            count++;
        }
        file.close();
        file = dir.openNextFile();
    }
    if (!file || !file.isDirectory())
    {
        if (file)
        {
            file.close();
        }
        dir.close();
        return false;
    }

    char jsonPath[PATH_LEN];
    char binPath[PATH_LEN];
    snprintf(jsonPath, sizeof(jsonPath), "%s/%s/profile.json", FRIENDS_DIR, file.name());
    snprintf(binPath, sizeof(binPath), "%s/%s/avatar.bin", FRIENDS_DIR, file.name());
    file.close();
    dir.close();

    File jsonFile = SD.open(jsonPath, FILE_READ);
    if (!jsonFile)
    {
        return false;
    }
    JsonDocument         doc;
    DeserializationError err = deserializeJson(doc, jsonFile);
    jsonFile.close();
    if (err)
    {
        return false;
    }
    strlcpy(contact.name, doc["name"] | "", NAME_LEN);
    strlcpy(contact.from, doc["from"] | "", FROM_LEN);
    strlcpy(contact.uuid, doc["uuid"] | "", UUID_LEN);
    strlcpy(contact.species, doc["species"] | "", SPECIES_LEN);
    contact.linkCount = 0;
    JsonArray links = doc["links"].as<JsonArray>();
    for (JsonObject link : links)
    {
        if (contact.linkCount >= LINKS_MAX)
        {
            break;
        }
        strlcpy(contact.links[contact.linkCount].platform, link["platform"] | (link["tag"] | ""),
                PLATFORM_LEN);
        strlcpy(contact.links[contact.linkCount].url, link["url"] | "", URL_LEN);
        contact.linkCount++;
    }
    contact.avatarResolution = doc["avatar_res"] | 0;
    contact.avatarMode = (AvatarMode)(doc["avatar_mode"] | 0);

    uint16_t expected = avatarLen(contact.avatarResolution, contact.avatarMode);
    if (expected == 0 || expected > AVATAR_LEN_MAX)
    {
        return false;
    }

    File friendBin = SD.open(binPath, FILE_READ);
    if (!friendBin)
    {
        return false;
    }
    if (friendBin.size() != expected)
    {
        friendBin.close();
        return false;
    }
    friendBin.read(contact.avatar, expected);
    friendBin.close();

    return true;
}

int storageCountContacts()
{
    File dir = SD.open(FRIENDS_DIR);
    if (!dir)
    {
        return 0;
    }
    int  count = 0;
    File entry = dir.openNextFile();
    while (entry)
    {
        Serial0.printf("entry: %s isDir=%d\n", entry.name(), entry.isDirectory());
        if (entry.isDirectory())
        {
            count++;
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    return count;
}

bool storageLoadContactName(int index, char *username, int maxLen)
{
    File dir = SD.open(FRIENDS_DIR, FILE_READ);
    if (!dir)
    {
        return false;
    }

    int  count = 0;
    File file = dir.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            if (count == index)
            {
                char jsonPath[PATH_LEN];
                snprintf(jsonPath, sizeof(jsonPath), "%s/%s/profile.json", FRIENDS_DIR,
                         file.name());
                file.close();
                dir.close();

                File jsonFile = SD.open(jsonPath, FILE_READ);
                if (!jsonFile)
                {
                    return false;
                }
                JsonDocument doc;
                deserializeJson(doc, jsonFile);
                jsonFile.close();
                strlcpy(username, doc["name"] | "", maxLen);
                return true;
            }
            count++;
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    return false;
}

void storageEnsureParentDir(const char *path)
{
    char dir[PATH_LEN];
    if (strlcpy(dir, path, sizeof(dir)) >= sizeof(dir))
    {
        return;
    }
    for (char *p = dir + 1; *p; ++p)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (!SD.exists(dir))
            {
                SD.mkdir(dir);
            }
            *p = '/';
        }
    }
}