#pragma once
#include <stdint.h>

#define REG_IODIRA 0x00
#define REG_OLATA 0x0A

#define BOOT_SETTLE_MS 10000
#define BAT_MON_SETTLE_MS 450
#define BAT_PRINT_INTERVAL_MS 30000

#define BATTERY_CHECK_INTERVAL_MS 30000
#define BATTERY_LOW_MV 3000
#define BATTERY_LOW_CONFIRM_COUNT 3

void powerInit();
void powerSetBatMonEnable(bool on);
void powerSetPeriphEnable(bool on);
void powerSetSpeakerEnable(bool on);
void powerSetSleepReq(bool on);

void     loopBatteryMonitorTask();
uint16_t powerReadBatteryMv();