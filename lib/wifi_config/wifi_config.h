#pragma once

#include <Arduino.h>

void wifi_config_start();
void wifi_config_stop();
void wifi_config_loop();

bool     wifi_config_client_connected();
bool     wifi_config_upload_done();
uint32_t wifi_config_idle_ms();

const char *wifi_config_wifi_qr();
const char *wifi_config_url();
const char *wifi_config_ssid();