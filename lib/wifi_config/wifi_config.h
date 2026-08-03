#pragma once

#include <Arduino.h>

void wifiConfigStart();
void wifiConfigStop();
void wifiConfigLoop();

bool     wifiConfigClientConnected();
bool     wifiConfigUploadDone();
uint32_t wifiConfigIdleMs();

const char *wifiConfigWifiQr();
const char *wifiConfigUrl();
const char *wifiConfigSsid();