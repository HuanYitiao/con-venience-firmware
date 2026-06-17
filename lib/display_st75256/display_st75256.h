#pragma once

#include <Arduino.h>

#include <SPI.h>

#define SCLK_PIN 10  // 串行时钟 (对应屏幕 D0)
#define SID_PIN 11   // 串行数据 (对应屏幕 D1~D3 短接)
#define CS_PIN 7     // 片选引脚 (对应屏幕 CS)
#define RS_PIN 6     // 寄存器/数据选择引脚 (对应屏幕 A0/RS)
#define RES_PIN 9    // 屏幕复位引脚 (对应屏幕 RES)

void initLCD();
void testGrayScale();
void clean();
void drawBlock();
void drawGrayChessboard(uint8_t bias = 0);
void drawImage(const uint8_t *data);  // data: 256*32 bytes in ST75256 display format