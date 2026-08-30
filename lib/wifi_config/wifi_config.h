#pragma once

#include <stdint.h>

#include "pcf85063a.h"  // 让 Pcf85063a 类型可见

void wifiConfigStart(Pcf85063a &rtc);  // <- 带参
void wifiConfigStop(void);
void wifiConfigLoop(void);

bool     wifiConfigClientConnected(void);
bool     wifiConfigUploadDone(void);
uint32_t wifiConfigIdleMs(void);

const char *wifiConfigWifiQr(void);
const char *wifiConfigUrl(void);
const char *wifiConfigSsid(void);
const char *wifiConfigSelfId(void);