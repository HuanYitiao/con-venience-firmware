#pragma once
#include <Arduino.h>

#define UUID_LEN 11
#define USERNAME_LEN 32
#define AVATAR_LEN 512  // 64x64 1bit
#define URL_LEN 128

typedef struct
{
    char    uuid[UUID_LEN];
    char    username[USERNAME_LEN];
    char    url[URL_LEN];
    uint8_t avatar[AVATAR_LEN];
} Contact;

bool storageInit();

bool storageSaveSelf(const Contact &contact);

bool storageLoadSelf(Contact &contact);

bool storageSaveContact(const Contact &contact);

int storageLoadContacts(Contact *contacts, int maxCount);

int storageCountContacts();