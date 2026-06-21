#include "storage.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

bool storageInit(uint8_t csPin)
{
    for (int i = 0; i < 3; i++)
    {
        if (SD.begin(csPin, SPI, 4000000))
            return true;
        delay(500);
    }
    Serial0.println("SD init failed");
    return false;
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
    JsonDocument doc;
    deserializeJson(doc, selfJson);
    selfJson.close();
    strlcpy(contact.uuid, doc["uuid"], UUID_LEN);
    strlcpy(contact.name, doc["name"], NAME_LEN);
    strlcpy(contact.species, doc["species"], SPECIES_LEN);
    strlcpy(contact.from, doc["from"], FROM_LEN);
    contact.linkCount = 0;
    JsonArray links = doc["links"].as<JsonArray>();
    for (JsonObject link : links)
    {
        if (contact.linkCount >= LINKS_MAX)
        {
            break;
        }
        strlcpy(contact.links[contact.linkCount].tag, link["tag"] | "", TAG_LEN);
        strlcpy(contact.links[contact.linkCount].url, link["url"] | "", URL_LEN);
        contact.linkCount++;
    }
    contact.avatarResolution = doc["avatar_res"] | 0;
    contact.avatarMode = (AvatarMode)(doc["avatar_mode"] | 0);

    File selfBin = SD.open(SELF_BIN, FILE_READ);
    if (!selfBin)
    {
        return false;
    }
    selfBin.read(contact.avatar, avatarLen(contact.avatarResolution, contact.avatarMode));
    selfBin.close();
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
        link["tag"] = contact.links[i].tag;
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
        return false;
    }

    char jsonPath[PATH_LEN];
    char binPath[PATH_LEN];
    snprintf(jsonPath, sizeof(jsonPath), "%s/%s/profile.json", FRIENDS_DIR, file.name());
    snprintf(binPath, sizeof(binPath), "%s/%s/avatar.bin", FRIENDS_DIR, file.name());

    JsonDocument doc;
    File         jsonFile = SD.open(jsonPath, FILE_READ);
    deserializeJson(doc, jsonFile);
    jsonFile.close();
    strlcpy(contact.name, doc["name"], NAME_LEN);
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
        strlcpy(contact.links[contact.linkCount].tag, link["tag"] | "", TAG_LEN);
        strlcpy(contact.links[contact.linkCount].url, link["url"] | "", URL_LEN);
        contact.linkCount++;
    }
    contact.avatarResolution = doc["avatar_res"] | 0;
    contact.avatarMode = (AvatarMode)(doc["avatar_mode"] | 0);

    File friendBin = SD.open(binPath, FILE_READ);
    if (!friendBin)
    {
        return false;
    }
    friendBin.read(contact.avatar, avatarLen(contact.avatarResolution, contact.avatarMode));
    friendBin.close();

    file.close();
    dir.close();

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