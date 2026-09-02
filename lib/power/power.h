#pragma once
#include <stdint.h>

void powerInit();
void powerSetBatMonEnable(bool on);
void powerSetPeriphEnable(bool on);
void powerSetSpeakerEnable(bool on);
void powerSetSleepReq(bool on);