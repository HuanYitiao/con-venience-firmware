#pragma once
#include <Arduino.h>

#include "pins.h"

/*
self_profile.json example:
{
  "name": "Wolfram",
  "species": "Samoyed",
  "from": "Sichuan, China",
  "links": [
    { "platform": "telegram", "url": "https://t.me/WolframLiu" },
    { "platform": "x",        "url": "https://x.com/WolframLiu" }
  ],
  "avatar_res": 64,
  "avatar_mode": 0
}
uuid is not authored here; the firmware stamps it as the device BLE MAC on load.
*/

#define SELF_JSON "/self_profile/profile.json"
#define SELF_BIN "/self_profile/avatar.bin"
#define FRIENDS_DIR "/friends_profiles"

#define PATH_LEN 96

#define UUID_LEN 13
#define NAME_LEN 32
#define SPECIES_LEN 64
#define FROM_LEN 64
#define PLATFORM_LEN 16
#define URL_LEN 128
#define LINKS_MAX 5
#define AVATAR_LEN_MAX 4096

typedef enum : uint8_t
{
    AVATAR_MODE_1BIT = 0,
    AVATAR_MODE_4GRAY = 1,
} AvatarMode;

typedef struct
{
    char platform[PLATFORM_LEN];
    char url[URL_LEN];
} Link;

typedef struct
{
    char       uuid[UUID_LEN];
    char       name[NAME_LEN];
    char       species[SPECIES_LEN];
    char       from[FROM_LEN];
    Link       links[LINKS_MAX];
    uint8_t    linkCount;
    uint8_t    avatarResolution;
    AvatarMode avatarMode;
    uint8_t    avatar[AVATAR_LEN_MAX];
} Contact;

static inline uint16_t avatarLen(uint8_t res, AvatarMode mode)
{
    (void)mode;
    return ((uint16_t)res * (uint16_t)res) / 4;
}

bool storageInit(uint8_t csPin = PIN_SD_CS);
bool storageLoadSelf(Contact &contact);
bool storageSaveContact(const Contact &contact);
bool storageLoadContact(int index, Contact &contact);
int  storageCountContacts();
bool storageLoadContactName(int index, char *username, int maxLen);

void storageMacToUuid(const uint8_t mac[6], char out[UUID_LEN]);
bool storageUuidValidate(const char *s, char out[UUID_LEN]);
bool storageEnsureSelfUuid(const uint8_t mac[6]);