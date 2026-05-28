#include "Display.h"
#include "font/FontManager.h"
#include "LVGLGlobal.h"
#include <algorithm>

// 全局 TFT 实例
TFT_eSPI tft;

// 单例实例
Display& Display::getInstance() {
    static Display instance;
    return instance;
}

Display::Display() 
    : _initialized(false), 
      _width(0), 
      _height(0), 
      _backlightPin(14),
      _frameBuffer1(nullptr),
      _frameBuffer2(nullptr) {
}

Display::InitStatus Display::begin(uint8_t rotation) {
    if (_initialized) {
        return SUCCESS;
    }

    Serial.println(F("Initializing display..."));
    FontManager::getInstance().init();
    pinMode(_backlightPin, OUTPUT);
    setBacklight(true);

    // 初始化 TFT
    if (!initTFT()) {
        Serial.println(F("TFT initialization failed!"));
        return ERROR_TFT;
    }

    // 设置旋转
    tft.setRotation(rotation);
    _width = tft.width();
    _height = tft.height();
    Serial.printf("Display size: %dx%d\n", _width, _height);

    _imageDisplay.setTFT(&tft);
    if (!_imageDisplay.begin()) {
        Serial.println(F("ImageDisplay initialization failed!"));
    }
    
    // 初始化 LVGL
    if (!initLVGL()) {
        Serial.println(F("LVGL initialization failed!"));
        return ERROR_LVGL;
    }

    // 保险措施：LVGL初始化后再次清屏
    tft.fillScreen(TFT_BLACK);
    
    _initialized = true;
    Serial.println(F("Display initialization completed successfully!"));
    
    return SUCCESS;
}

bool Display::initTFT() {
    try {
        tft.init();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(0, 0);
        tft.println("TFT Init OK");        
        return true;
    } catch (...) {
        return false;
    }
}

bool Display::initLVGL() {
    // 初始化 LVGL
    lv_init();
    
    // 创建帧缓冲区 - 增大到屏幕高度的1/10 (240*32=7680像素)
    size_t bufferSize = _width * 32;
    
    // 尝试使用 PSRAM
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        Serial.println(F("Using PSRAM for frame buffers"));
        _frameBuffer1 = (lv_color_t*)ps_malloc(bufferSize * sizeof(lv_color_t));
        _frameBuffer2 = (lv_color_t*)ps_malloc(bufferSize * sizeof(lv_color_t));
    }
#endif
    
    // 如果 PSRAM 不可用或分配失败，使用普通内存
    if (_frameBuffer1 == nullptr || _frameBuffer2 == nullptr) {
        Serial.println(F("Using normal RAM for frame buffers"));
        if (_frameBuffer1) free(_frameBuffer1);
        if (_frameBuffer2) free(_frameBuffer2);
        
        bufferSize = _width * 16;
        _frameBuffer1 = (lv_color_t*)malloc(bufferSize * sizeof(lv_color_t));
        _frameBuffer2 = (lv_color_t*)malloc(bufferSize * sizeof(lv_color_t));
    }
    
    if (_frameBuffer1 == nullptr || _frameBuffer2 == nullptr) {
        Serial.println(F("Failed to allocate frame buffers!"));
        return false;
    }
    
    Serial.printf("Frame buffer size: %d pixels each\n", bufferSize);
    
    // 初始化显示缓冲区
    lv_disp_draw_buf_init(&_drawBuf, _frameBuffer1, _frameBuffer2, bufferSize);
    
    // 初始化显示驱动
    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res = _width;
    _dispDrv.ver_res = _height;
    _dispDrv.flush_cb = lvglFlushCallback;
    _dispDrv.rounder_cb = lvglRounderCallback;
    _dispDrv.draw_buf = &_drawBuf;
    lv_disp_drv_register(&_dispDrv);
    
    // 关键修复：LVGL初始化完成后，再次清屏并设置黑色背景
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clean(scr);
    
    // 强制刷新整个屏幕
    lv_obj_invalidate(scr);
    lv_refr_now(NULL);
    
    Serial.println(F("LVGL初始化完成，屏幕已清空"));
    
    return true;
}

void Display::setBacklight(bool state) {
    digitalWrite(_backlightPin, state ? HIGH : LOW);
}

void Display::setBacklightBrightness(uint8_t brightness) {
    analogWrite(_backlightPin, brightness);
}

void Display::taskHandler() {
    if (_initialized) {
        LVGL_LOCK();
        lv_timer_handler();
    }
}

void Display::clearScreen(uint32_t color) {
    tft.fillScreen(color);
}

void Display::showMessage(const char* message, uint32_t color, uint8_t size) {
    if (!_initialized) return;
    
    // 清除 LVGL 显示
    lv_obj_clean(lv_scr_act());
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(size);
    
    int16_t textWidth = strlen(message) * 6 * size;
    int16_t textHeight = 8 * size;
    int16_t x = (_width - textWidth) / 2;
    int16_t y = (_height - textHeight) / 2;
    
    int16_t cursorX = (x < 0) ? 0 : x;
    int16_t cursorY = (y < 0) ? 0 : y;
    tft.setCursor(cursorX, cursorY);
    tft.print(message);
}

void Display::lvglFlushCallback(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
    Display& display = Display::getInstance();
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t len = w * h;
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t*)color_p, len);
    tft.endWrite();
    
    lv_disp_flush_ready(disp_drv);
}

void Display::lvglRounderCallback(lv_disp_drv_t* disp_drv, lv_area_t* area) {
    if (area->x1 < 0) area->x1 = 0;
    if (area->y1 < 0) area->y1 = 0;
    if (area->x2 >= disp_drv->hor_res) area->x2 = disp_drv->hor_res - 1;
    if (area->y2 >= disp_drv->ver_res) area->y2 = disp_drv->ver_res - 1;
}

void showChineseDemo() {
    Serial.println(F("开始中文显示演示..."));
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    
    lv_obj_t* label3 = lv_label_create(scr);
    lv_obj_set_style_text_font(label3, FontManager::getInstance().getChineseFont20(), 0);
    lv_obj_set_style_text_color(label3, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(label3, "ESP32-S3开发板");
    lv_obj_align(label3, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    Serial.println(F("中文标签创建完成"));
}

// 通用的 BMP 显示函数
void drawBmp(const char *filename, int16_t x, int16_t y, bool fromSD = false) {
    // 基本参数检查
    if (x >= tft.width() || y >= tft.height()) {
        Serial.printf("位置超出屏幕: (%d, %d)\n", x, y);
        return;
    }

    // SD 卡使用安全操作
    if (fromSD) {
        #ifdef USE_SD_CARD
        if (!sdCard.isInitialized()) {
            Serial.println(F("SD卡未初始化"));
            return;
        }

        bool success = sdCard.performSafeOperation([&]() -> bool {
            File bmpFile = SD.open(filename, "r");
            if (!bmpFile) {
                Serial.printf("无法打开SD卡文件: %s\n", filename);
                return false;
            }
            
            bool result = processBmpFile(bmpFile, x, y, filename);
            bmpFile.close();
            return result;
        });
        
        if (!success) {
            Serial.printf("显示SD图片失败: %s\n", filename);
        }
        #endif
        return;
    }

    // LittleFS 直接操作
    fs::File bmpFile = LittleFS.open(filename, "r");
    if (!bmpFile) {
        Serial.printf("无法打开LittleFS文件: %s\n", filename);
        return;
    }

    processBmpFile(bmpFile, x, y, filename);
    bmpFile.close();
}

// 通用的 BMP 文件处理函数
bool processBmpFile(fs::File &bmpFile, int16_t x, int16_t y, const char* filename) {
    // 读取 BMP 文件头
    if (read16(bmpFile) != 0x4D42) {
        Serial.printf("无效的BMP文件: %s\n", filename);
        return false;
    }

    // 解析文件头
    read32(bmpFile); // 文件大小
    read32(bmpFile); // 保留字段
    uint32_t dataOffset = read32(bmpFile);
    read32(bmpFile); // 头大小
    int32_t width = read32(bmpFile);
    int32_t height = read32(bmpFile);
    
    // 基本格式检查
    if (read16(bmpFile) != 1 || read16(bmpFile) != 24 || read32(bmpFile) != 0) {
        Serial.println(F("不支持的BMP格式"));
        return false;
    }

    // 计算显示区域
    int32_t absHeight = abs(height);
    bool isBottomUp = (height > 0);
    int16_t displayWidth = min((int32_t)tft.width() - x, width);
    int16_t displayHeight = min((int32_t)tft.height() - y, absHeight);
    
    if (displayWidth <= 0 || displayHeight <= 0) {
        Serial.println(F("显示区域超出屏幕"));
        return false;
    }

    // 计算行填充
    uint16_t padding = (4 - ((width * 3) % 4)) % 4;
    uint16_t lineBufferSize = width * 3 + padding;
    uint8_t* lineBuffer = (uint8_t*)malloc(lineBufferSize);
    
    if (!lineBuffer) {
        Serial.println(F("内存分配失败"));
        return false;
    }

    // 设置显示
    bool oldSwapBytes = tft.getSwapBytes();
    tft.setSwapBytes(true);
    bmpFile.seek(dataOffset);

    uint32_t startTime = millis();

    // 逐行处理
    for (int32_t row = 0; row < absHeight; row++) {
        if (bmpFile.read(lineBuffer, lineBufferSize) != lineBufferSize) {
            break;
        }

        // 计算屏幕位置
        int32_t sourceRow = isBottomUp ? (absHeight - 1 - row) : row;
        int16_t screenY = y + sourceRow;
        
        // 跳过屏幕外的行
        if (screenY < 0 || screenY >= tft.height()) {
            continue;
        }

        // 转换颜色格式
        uint16_t pixelBuffer[displayWidth];
        uint8_t* srcPtr = lineBuffer;
        
        for (int16_t col = 0; col < displayWidth; col++) {
            uint8_t b = *srcPtr++;
            uint8_t g = *srcPtr++;
            uint8_t r = *srcPtr++;
            pixelBuffer[col] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // 显示行
        tft.pushImage(x, screenY, displayWidth, 1, pixelBuffer);
    }

    // 清理资源
    free(lineBuffer);
    tft.setSwapBytes(oldSwapBytes);

    uint32_t loadTime = millis() - startTime;
    Serial.printf("BMP显示完成: %s, 耗时 %lums\n", filename, loadTime);
    
    return true;
}



uint16_t read16(fs::File &f) {
    uint16_t result;
    ((uint8_t *)&result)[0] = f.read();  // LSB
    ((uint8_t *)&result)[1] = f.read();  // MSB
    return result;
}

uint32_t read32(fs::File &f) {
    uint32_t result;
    ((uint8_t *)&result)[0] = f.read();  // LSB
    ((uint8_t *)&result)[1] = f.read();
    ((uint8_t *)&result)[2] = f.read();
    ((uint8_t *)&result)[3] = f.read();  // MSB
    return result;
}

// 在 Display.cpp 最末尾（所有原有函数之后）添加：

TaskHandle_t lvglTaskHandle = NULL;

void displayTask(void* parameter) {
    Serial.println(F("🖥️ 显示任务启动"));
    lvglTaskHandle = xTaskGetCurrentTaskHandle();
    
    Display& display = Display::getInstance();
    vTaskDelay(pdMS_TO_TICKS(500));
    while (true) {
        LVGL_LOCK() {
            display.taskHandler(); 
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}

// 挂起LVGL任务（如果将来需要）
void suspendLVGLTask() {
    if (lvglTaskHandle != NULL) {
        vTaskSuspend(lvglTaskHandle);
        Serial.println("[LVGL] 任务已挂起");
    }
}

void resumeLVGLTask() {
    if (lvglTaskHandle != NULL) {
        vTaskResume(lvglTaskHandle);
        Serial.println("[LVGL] 任务已恢复");
    }
}
