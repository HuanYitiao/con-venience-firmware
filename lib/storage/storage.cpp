#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#define SELF_JSON "/self.json"
#define SELF_BIN "/self.bin"
#define CONTACTS_DIR "/contacts"

bool storageInit()
{
    if (!LittleFS.begin(true))
    {
        Serial0.println("LittleFS mount failed");
        return false;
    }
    if (!LittleFS.exists(CONTACTS_DIR))
    {
        LittleFS.mkdir(CONTACTS_DIR);
    }
    return true;
}

bool storageSaveSelf(const Contact &contact)
{
    File f = LittleFS.open(SELF_JSON, "w");
    if (!f)
        return false;
    JsonDocument doc;
    doc["username"] = contact.username;
    doc["url"] = contact.url;
    serializeJson(doc, f);
    f.close();

    File fb = LittleFS.open(SELF_BIN, "w");
    if (!fb)
        return false;
    fb.write(contact.avatar, AVATAR_LEN);
    fb.close();
    return true;
}

bool storageLoadSelf(Contact &contact)
{
    File f = LittleFS.open(SELF_JSON, "r");
    if (!f)
        return false;
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();
    strlcpy(contact.username, doc["username"], USERNAME_LEN);
    strlcpy(contact.url, doc["url"] | "", URL_LEN);

    File fb = LittleFS.open(SELF_BIN, "r");
    if (!fb)
        return true;
    fb.read(contact.avatar, AVATAR_LEN);
    fb.close();
    return true;
}

bool storageSaveContact(const Contact &contact)
{
    char jsonPath[64];
    char binPath[64];
    snprintf(jsonPath, sizeof(jsonPath), "/contacts/%s.json", contact.username);
    snprintf(binPath, sizeof(binPath), "/contacts/%s.bin", contact.username);

    File f = LittleFS.open(jsonPath, "w");
    if (!f)
        return false;
    JsonDocument doc;
    doc["username"] = contact.username;
    doc["url"] = contact.url;
    serializeJson(doc, f);
    f.close();

    File fb = LittleFS.open(binPath, "w");
    if (!fb)
        return false;
    fb.write(contact.avatar, AVATAR_LEN);
    fb.close();
    return true;
}

int storageLoadContacts(Contact *contacts, int maxCount)
{
    File dir = LittleFS.open(CONTACTS_DIR);
    if (!dir)
        return 0;

    int  count = 0;
    File file = dir.openNextFile();
    while (file && count < maxCount)
    {
        String name = file.name();
        if (name.endsWith(".json"))
        {
            JsonDocument doc;
            deserializeJson(doc, file);
            strlcpy(contacts[count].username, doc["username"] | "", USERNAME_LEN);
            strlcpy(contacts[count].url, doc["url"] | "", URL_LEN);
            file.close();

            String binName = name.substring(0, name.length() - 5) + ".bin";
            String binPath = String(CONTACTS_DIR) + "/" + binName;
            File   fb = LittleFS.open(binPath, "r");
            if (fb)
            {
                fb.read(contacts[count].avatar, AVATAR_LEN);
                fb.close();
            }
            else
            {
                memset(contacts[count].avatar, 0, AVATAR_LEN);
            }
            count++;
        }
        else
        {
            file.close();
        }
        file = dir.openNextFile();
    }
    return count;
}

int storageCountContacts()
{
    File dir = LittleFS.open(CONTACTS_DIR);
    if (!dir)
        return 0;
    int  count = 0;
    File file = dir.openNextFile();
    while (file)
    {
        String name = file.name();
        if (name.endsWith(".json"))
            count++;
        file.close();
        file = dir.openNextFile();
    }
    return count;
}