#pragma once
#ifndef DISPGP_H
#define DISPGP_H
#include "../tftd/Display.h"
#include <ArduinoJson.h>


// 颜色方案枚举
enum ColorScheme {
  SCHEME_BLUE,      // 蓝色系
  SCHEME_GREEN,     // 绿色系
  SCHEME_RED,       // 红色系
  SCHEME_PURPLE,    // 紫色系
  SCHEME_GOLD,      // 金色系
  SCHEME_MONOCHROME // 单色系
};

// 显示模式枚举
enum DisplayMode {
  MODE_NORMAL,       // 普通模式
  MODE_GLOSSY,       // 光面效果
  MODE_NEON,         // 霓虹灯效果
  MODE_MINIMAL       // 极简模式
};
struct ScrollState {
  int scrollX = 0;
  uint32_t lastTick = 0;
  String lastContent = "";   
  int textPixelWidth = 0;
};
void dGPscreen2(JsonObject data, int pageIndex,bool ztw,bool ztm,const String& hourmin,const String& mac);
void drawStockBlock(const char* symbol, const char* price, const char* change, 
                   int x, int y, int width, int height, 
                   ColorScheme scheme, 
                   DisplayMode mode, const char* name, const char* exchange, const char* status, const char* currency, const char* volume,
                    const char* amount,
                    const char* high,
                    const char* low,
                    const char* rate);
void selectColors(ColorScheme scheme, 
                 uint16_t &primary, uint16_t &secondary, 
                 uint16_t &text, uint16_t &accent);
void applyDisplayMode(DisplayMode mode, 
                     uint16_t &primary, uint16_t &secondary, 
                     uint16_t &text, uint16_t &accent);
uint16_t blendColors(uint16_t color1, uint16_t color2, uint8_t ratio);
void drawIndexBlock(const String& name, float value, float percent,
                    int x, int y, int width, int height);
void scrollingChineseTicker(const String& content, int y,
                             uint32_t textColor, uint32_t bgColor,
                             int textSize,ScrollState &state);
int getTextPixelWidth(const String &content, int textSize);

#endif