#pragma once

#include <Arduino.h>

#include <SPI.h>
#include <U8g2lib.h>

#define SCLK_PIN 10  // 串行时钟 (对应屏幕 D0)
#define SID_PIN 11   // 串行数据 (对应屏幕 D1~D3 短接)
#define CS_PIN 7     // 片选引脚 (对应屏幕 CS)
#define RS_PIN 6     // 寄存器/数据选择引脚 (对应屏幕 A0/RS)
#define RES_PIN 9    // 屏幕复位引脚 (对应屏幕 RES)
#define black 0xFF
#define white 0x00
#define lightGray 0x55
#define darkGray 0xAA

void initLCD();
void testGrayScale();
void clean();
void drawBlock();
void drawGrayChessboard(uint8_t bias = 0);
// 局部刷新原语：将 data 绘制到以 (x, y) 为左上角、w×h 像素的区域。
// data 布局：page 主序，w * ((h+3)/4) 字节，每字节编码 4 个垂直像素（2bpp）。
// y 应为 4 的倍数（页对齐）以获得正确定位。
// bgColor：衬底颜色，通过 OR 运算填充背景（0x00 = 透明直传，0xFF = 黑色衬底，默认）。
// 当且仅当字体渲染时将字形 buffer（背景=0x00）与 bgColor 进行 OR，背景即呈现衬底颜色；
// 对原始图像数据请传入 bgColor=0x00 以保留原始像素值。
void draw(const uint8_t *data, int x, int y, int w, int h, uint8_t bgColor = 0xFF);

// u8g2灰度渲染功能
extern U8G2_ST75256_JLX256128_F_4W_SW_SPI u8g2;
void                                      initU8g2();

// 局部刷新：只在指定矩形区域内渲染文字
// rectX, rectY: 矩形左上角坐标(像素)
// rectW, rectH: 矩形宽高(像素)
// fgGray: 文字灰度 (0x00=白, 0x55=浅灰, 0xAA=深灰, 0xFF=黑)
// bgGray: 背景灰度
void drawTextInRect(const char *text, int textX, int textY, int rectX, int rectY, int rectW,
                    int rectH, uint8_t fgGray = 0xFF, uint8_t bgGray = white);

// 全屏渲染文字（带灰度背景）
void drawTextWithGrayscale(const char *text, int x, int y, uint8_t fgGray = black,
                           uint8_t bgGray = white);