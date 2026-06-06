#include "storage.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

#define SELF_JSON "/self_profile/profile.json"
#define SELF_BIN "/self_profile/avatar.bin"
#define FRIENDS_DIR "/friends_profiles"

#define PIN_SCK 10
#define PIN_MISO 15
#define PIN_MOSI 11

#define PATH_LEN 96

bool storageInit(uint8_t csPin)
{
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, csPin);
    if (!SD.begin(csPin))
    {
        Serial0.println("SD init failed");
        return false;
    }
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
    JsonDocument doc;
    deserializeJson(doc, selfJson);
    selfJson.close();
    strlcpy(contact.username, doc["username"], USERNAME_LEN);
    strlcpy(contact.url, doc["url"] | "", URL_LEN);

    File selfBin = SD.open(SELF_BIN, FILE_READ);
    if (!selfBin)
    {
        return false;
    }
    selfBin.read(contact.avatar, AVATAR_LEN);
    selfBin.close();
    return true;
}

bool storageSaveContact(const Contact &contact)
{
    char jsonPath[PATH_LEN];
    char binPath[PATH_LEN];
    char dirPath[PATH_LEN];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", FRIENDS_DIR, contact.username);
    snprintf(jsonPath, sizeof(jsonPath), "%s/%s/profile.json", FRIENDS_DIR, contact.username);
    snprintf(binPath, sizeof(binPath), "%s/%s/avatar.bin", FRIENDS_DIR, contact.username);
    SD.mkdir(dirPath);
    SD.remove(jsonPath);
    SD.remove(binPath);

    File friendJson = SD.open(jsonPath, FILE_WRITE);
    if (!friendJson)
    {
        return false;
    }
    JsonDocument doc;
    doc["username"] = contact.username;
    doc["url"] = contact.url;
    serializeJson(doc, friendJson);
    friendJson.close();

    File friendBin = SD.open(binPath, FILE_WRITE);
    if (!friendBin)
    {
        return false;
    }
    friendBin.write(contact.avatar, AVATAR_LEN);
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
    strlcpy(contact.username, doc["username"], USERNAME_LEN);
    strlcpy(contact.url, doc["url"] | "", URL_LEN);

    File friendBin = SD.open(binPath, FILE_READ);
    if (!friendBin)
    {
        return false;
    }
    friendBin.read(contact.avatar, AVATAR_LEN);
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
                strlcpy(username, doc["username"] | "", maxLen);
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