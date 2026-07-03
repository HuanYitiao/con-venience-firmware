#pragma once
#include <Arduino.h>

#include "pins.h"

/*
self_profile.json example:
{
  "uuid": "A3F9",
  "name": "Wolfram",
  "species": "Samoyed",
  "from": "Sichuan, China",
  "links": [
    { "tag": "telegram", "url": "t.me/WolframLiu" },
    { "tag": "github",   "url": "github.com/HuanYitiao" }
  ],
  "avatar_res": 64,
  "avatar_mode": 0
}
*/

#define SELF_JSON "/self_profile/profile.json"
#define SELF_BIN "/self_profile/avatar.bin"
#define FRIENDS_DIR "/friends_profiles"

#define PATH_LEN 96

#define UUID_LEN 11
#define NAME_LEN 32
#define SPECIES_LEN 64
#define FROM_LEN 64
#define TAG_LEN 16
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
    char tag[TAG_LEN];
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