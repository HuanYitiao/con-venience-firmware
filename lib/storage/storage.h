#pragma once
#include <Arduino.h>

#define PIN_SD_CS 22

#define UUID_LEN 11
#define USERNAME_LEN 32
#define AVATAR_LEN 512
#define URL_LEN 128

typedef struct
{
    char    uuid[UUID_LEN];
    char    username[USERNAME_LEN];
    char    url[URL_LEN];
    uint8_t avatar[AVATAR_LEN];
} Contact;

bool storageInit(uint8_t csPin = PIN_SD_CS);

// bool storageSaveSelf(const Contact &contact);
bool storageLoadSelf(Contact &contact);

bool storageSaveContact(const Contact &contact);
bool storageLoadContact(int index, Contact &contact);
int  storageCountContacts();
bool storageLoadContactName(int index, char *username, int maxLen);