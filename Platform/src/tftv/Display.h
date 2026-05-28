#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <LittleFS.h>
#ifdef USE_SD_CARD
#include "../utils/SDCardManager.h"
#endif
#include "CnPrint.h"
#include "ImageDisplay.h"

// 声明全局 TFT 实例
extern TFT_eSPI tft;
#ifdef USE_SD_CARD
extern SDCardManager sdCard;
#endif
extern uint16_t read16(fs::File &f);
extern uint32_t read32(fs::File &f);

// 新增：用于挂起/恢复 LVGL 任务的函数（如果仍需要，但 lv_img 方案原则上不需要，保留兼容）
extern void suspendLVGLTask();
extern void resumeLVGLTask();

class Display {
public:
    enum InitStatus {
        SUCCESS = 0,
        ERROR_TFT,
        ERROR_LVGL,
        ERROR_IMAGE_DISPLAY
    };

    static Display& getInstance();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    InitStatus begin(uint8_t rotation = 0);
    
    void setBacklight(bool state);
    void setBacklightBrightness(uint8_t brightness);
    
    lv_obj_t* getScreen() { return lv_scr_act(); }
    void taskHandler();
    
    void clearScreen(uint32_t color = TFT_BLACK);
    void showMessage(const char* message, uint32_t color = TFT_WHITE, uint8_t size = 2);
    
    uint16_t getWidth() const { return _width; }
    uint16_t getHeight() const { return _height; }
    bool isInitialized() const { return _initialized; }

    // 获取 TFT 实例
    TFT_eSPI& getTFT() { return tft; }

    // 添加网络图像显示方法
    bool displayNetworkJPG(const char* url, int16_t x = 0, int16_t y = 0) {
        return _imageDisplay.displayFromURL(url, x, y);
    }
    bool displayNetworkJPG(const String& url, int16_t x = 0, int16_t y = 0) {
        return _imageDisplay.displayFromURL(url, x, y);
    }
    
    // 本地图像显示方法（保留，但建议用 lv_img 方式）
    bool displayLocalJPG(const char* filename, int16_t x = 0, int16_t y = 0) {
        return _imageDisplay.displayFromFile(filename, x, y);
    }
    
    void testdisp() {
        return _imageDisplay.testD();
    }

    // 新增：获取 ImageDisplay 实例，供 Photos 调用 decodeToBuffer
    ImageDisplay& getImageDisplay() { return _imageDisplay; }

private:
    Display();
    ImageDisplay _imageDisplay;
    static void lvglFlushCallback(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p);
    static void lvglRounderCallback(lv_disp_drv_t* disp_drv, lv_area_t* area);
    
    bool initTFT();
    bool initLVGL();
    
    bool _initialized;
    uint16_t _width;
    uint16_t _height;
    uint8_t _backlightPin;
    
    lv_disp_draw_buf_t _drawBuf;
    lv_disp_drv_t _dispDrv;
    lv_color_t* _frameBuffer1;
    lv_color_t* _frameBuffer2;
};

void displayTask(void* parameter);
void showChineseDemo();
void drawBmp(const char *filename, int16_t x, int16_t y, bool fromSD );
bool processBmpFile(fs::File &bmpFile, int16_t x, int16_t y, const char* filename);

#endif // DISPLAY_H