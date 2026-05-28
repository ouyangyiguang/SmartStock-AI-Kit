#include "Photos.h"
#include <FS.h>
#include <LittleFS.h>
#include "font/FontManager.h"
#include "LVGLGlobal.h"
#include "Display.h"
#include <TFT_eSPI.h>

#ifdef ESP32
#include "esp_task_wdt.h"
#define FEED_WATCHDOG() esp_task_wdt_reset()
#else
#define FEED_WATCHDOG() do {} while(0)
#endif

extern TFT_eSPI tft;

Photos& Photos::getInstance() {
    static Photos instance;
    return instance;
}

Photos::Photos() 
    : _initialized(false), _currentIndex(0), _parent(nullptr),
      _hintLabel(nullptr), _errorLabel(nullptr),
      _imageWidget(nullptr), _currentImageBuffer(nullptr),
      _currentImgWidth(0), _currentImgHeight(0) {
}

void Photos::init(lv_obj_t* parent) {
    if (_initialized) return;
    _parent = parent;
    LVGL_LOCK();

    lv_obj_set_size(_parent, 240, 320);
    lv_obj_set_style_bg_color(_parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_parent, 0, 0);
    lv_obj_set_scrollbar_mode(_parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(_parent, LV_OBJ_FLAG_HIDDEN);

    _hintLabel = lv_label_create(_parent);
    lv_obj_align(_hintLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(_hintLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_hintLabel, FontManager::getInstance().getChineseFont20(), 0);
    lv_label_set_text(_hintLabel, "还未添加照片\n请在小程序添加");
    lv_obj_add_flag(_hintLabel, LV_OBJ_FLAG_HIDDEN);



    _errorLabel = lv_label_create(_parent);
    lv_obj_align(_errorLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(_errorLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(_errorLabel, FontManager::getInstance().getChineseFont20(), 0);
    lv_label_set_text(_errorLabel, "图片解码失败");
    lv_obj_add_flag(_errorLabel, LV_OBJ_FLAG_HIDDEN);

    _imageWidget = lv_img_create(_parent);
    // 关键修复：设置图片控件为屏幕大小
    lv_obj_set_size(_imageWidget, 240, 320);
    lv_obj_center(_imageWidget);
    lv_obj_set_style_bg_color(_imageWidget, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_imageWidget, LV_OPA_COVER, 0);
    // 关键修复：设置图片填充模式
    lv_obj_set_style_img_recolor_opa(_imageWidget, LV_OPA_TRANSP, 0);
    lv_img_set_zoom(_imageWidget, 256); // 100%缩放
    lv_img_set_angle(_imageWidget, 0); // 无旋转
    lv_obj_add_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);

    LVGL_UNLOCK();

    loadPhotoList();
    _initialized = true;
}

void Photos::loadPhotoList() {
    _photoFiles.clear();
    if (!LittleFS.exists("/photo")) {
        LittleFS.mkdir("/photo");
        return;
    }
    fs::File dir = LittleFS.open("/photo");
    if (!dir) return;
    fs::File file = dir.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
            _photoFiles.push_back(std::string("/photo/") + name.c_str());
        }
        file = dir.openNextFile();
    }
    dir.close();

    if (_photoFiles.empty()) {
        _currentIndex = 0;
    } else if (_currentIndex >= _photoFiles.size()) {
        _currentIndex = 0;
    }
}

bool Photos::isValidJpeg(const char* filename) {
    fs::File file = LittleFS.open(filename, "r");
    if (!file) return false;
    
    uint8_t header[3];
    size_t n = file.readBytes((char*)header, 3);
    file.close();
    
    if (n < 3) return false;
    bool valid = (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF);
    return valid;
}

void Photos::displayCurrentPhoto() {
    if (_photoFiles.empty()) {
        LVGL_LOCK();
        lv_obj_add_flag(_errorLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_hintLabel, LV_OBJ_FLAG_HIDDEN);
        LVGL_UNLOCK();
        return;
    }

    size_t attemptCount = 0;
    size_t maxAttempts = _photoFiles.size();

    while (attemptCount < maxAttempts) {
        const char* currentPhoto = _photoFiles[_currentIndex].c_str();

        LVGL_LOCK();
        if (_imageWidget) {
            lv_obj_add_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(_imageWidget, NULL);
            lv_obj_invalidate(_imageWidget);
        }
        // 显式清除 LVGL 内部图片缓存，防止旧地址/旧描述符被复用
        lv_img_cache_invalidate_src(NULL);
        LVGL_UNLOCK();

        // 等待足够长时间，让 LVGL 渲染 + TFT_eSPI DMA 传输全部完成
        // 120ms 足以覆盖 60Hz 下多帧的任意阶段，避免 freeBuffer 与 DMA 读冲突
        vTaskDelay(pdMS_TO_TICKS(120));

        if (_currentImgDsc) {
            free(_currentImgDsc);
            _currentImgDsc = nullptr;
        }
        if (_currentImageBuffer) {
            Display::getInstance().getImageDisplay().freeBuffer(_currentImageBuffer);
            _currentImageBuffer = nullptr;
        }

        LVGL_LOCK();
        lv_obj_add_flag(_hintLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_errorLabel, LV_OBJ_FLAG_HIDDEN);
        LVGL_UNLOCK();

        FEED_WATCHDOG();

        if (isValidJpeg(currentPhoto)) {
            int16_t w = 0, h = 0;
            uint16_t* rgb565 = nullptr;
            bool success = Display::getInstance().getImageDisplay().decodeToBuffer(currentPhoto, rgb565, w, h);

            FEED_WATCHDOG();

            if (success && rgb565 && w > 0 && h > 0) {
                LVGL_LOCK();
                _currentImgDsc = (lv_img_dsc_t*)malloc(sizeof(lv_img_dsc_t));
                if (_currentImgDsc) {
                    _currentImgDsc->header.cf = LV_IMG_CF_TRUE_COLOR;
                    _currentImgDsc->header.w = w;
                    _currentImgDsc->header.h = h;
                    _currentImgDsc->header.always_zero = 0;
                    _currentImgDsc->data_size = w * h * 2;
                    _currentImgDsc->data = (const uint8_t*)rgb565;

                    lv_img_set_src(_imageWidget, _currentImgDsc);
                    lv_obj_set_size(_imageWidget, w, h);
                    lv_img_set_zoom(_imageWidget, 256);
                    lv_obj_set_style_img_recolor_opa(_imageWidget, LV_OPA_TRANSP, 0);
                    lv_obj_center(_imageWidget);
                    lv_obj_clear_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);

                    _currentImageBuffer = rgb565;
                    _currentImgWidth = w;
                    _currentImgHeight = h;
                    LVGL_UNLOCK();
                    return;
                }
                LVGL_UNLOCK();
                Display::getInstance().getImageDisplay().freeBuffer(rgb565);
            }
        }

        _currentIndex = (_currentIndex + 1) % _photoFiles.size();
        attemptCount++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    LVGL_LOCK();
    lv_obj_clear_flag(_errorLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_errorLabel, "所有图片均损坏\n或内存不足");
    LVGL_UNLOCK();
}

void Photos::show() {
    if (!_parent || !_initialized) return;

    loadPhotoList();

    LVGL_LOCK();
    lv_obj_clear_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_parent);
    LVGL_UNLOCK();

    if (_photoFiles.empty()) {
        LVGL_LOCK();
        lv_obj_clear_flag(_hintLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_errorLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);
        tft.fillScreen(TFT_BLACK);
        LVGL_UNLOCK();
    } else {
        displayCurrentPhoto();
    }
}

void Photos::nextPhoto() {
    if (_photoFiles.empty()) return;
    _currentIndex = (_currentIndex + 1) % _photoFiles.size();
    displayCurrentPhoto();
}

void Photos::hide() {
    if (_parent) {
        LVGL_LOCK();
        lv_obj_add_flag(_parent, LV_OBJ_FLAG_HIDDEN);
        if (_imageWidget) {
            lv_obj_add_flag(_imageWidget, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(_imageWidget, NULL);
            lv_obj_invalidate(_imageWidget);
        }
        lv_img_cache_invalidate_src(NULL);
        LVGL_UNLOCK();
        // 等待 LVGL 渲染 + DMA 完成后再释放 buffer
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    if (_currentImgDsc) {
        free(_currentImgDsc);
        _currentImgDsc = nullptr;
    }
    if (_currentImageBuffer) {
        Display::getInstance().getImageDisplay().freeBuffer(_currentImageBuffer);
        _currentImageBuffer = nullptr;
    }
}