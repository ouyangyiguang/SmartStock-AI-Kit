#include "CnPrint.h"
#include "LVGLGlobal.h"
#include <Arduino.h>
#include <vector>

static unsigned long lastUpdate[20] = {0};
static const unsigned long UPDATE_INTERVAL_MS = 200;

CnPrint& CnPrint::get() {
    static CnPrint instance;
    return instance;
}

lv_align_t CnPrint::convertAlign(Alignment align) {
    switch(align) {
        case ALIGN_TL: return LV_ALIGN_TOP_LEFT;
        case ALIGN_TC: return LV_ALIGN_TOP_MID;
        case ALIGN_TR: return LV_ALIGN_TOP_RIGHT;
        case ALIGN_CL: return LV_ALIGN_LEFT_MID;
        case ALIGN_CENTER:  return LV_ALIGN_CENTER;
        case ALIGN_CR: return LV_ALIGN_RIGHT_MID;
        case ALIGN_BL: return LV_ALIGN_BOTTOM_LEFT;
        case ALIGN_BC: return LV_ALIGN_BOTTOM_MID;
        case ALIGN_BR: return LV_ALIGN_BOTTOM_RIGHT;
        default: return LV_ALIGN_CENTER;
    }
}

const lv_font_t* CnPrint::getFont(uint8_t size) {
    // 优先使用中文字体，如果不可用则使用 LVGL 内置字体
    const lv_font_t* chFont = FontManager::getInstance().getChineseFont20();
    if (chFont) {
        return chFont;
    }
    // 使用 LVGL 内置字体（支持 ASCII 和基本标点）
    return LV_FONT_DEFAULT;
}

static void convertPunctuation(const char* src, char* dst, size_t dstSize) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstSize - 1; i++) {
        bool replaced = false;
        // 中文标点转英文 (双字节转单字节)
        if (i + 1 < dstSize) {
            if (src[i] == '\xE2' && src[i+1] == '\x80') {
                if (src[i+2] == '\xA2') { dst[j++] = '.'; i += 2; replaced = true; }
                else if (src[i+2] == '\x80') { dst[j++] = '-'; i += 2; replaced = true; }
                else if (src[i+2] == '\xA6') { dst[j++] = ' '; dst[j++] = ' '; i += 2; replaced = true; }
            }
            else if (src[i] == '\xE2' && src[i+1] == '\x81') {
                if (src[i+2] == '\xA6') { dst[j++] = ' '; i += 2; replaced = true; }
            }
        }
        if (!replaced) {
            // 单字节中文标点
            const char* pairs[] = {
                "\xEF\xBC\x8C", ",",  // ，
                "\xE3\x80\x82", ".",  // 。
                "\xE3\x80\x81", "!",  // ！
                "\xEF\xBC\x9F", "?",  // ？
                "\xEF\xBC\x9B", ";",  // ；
                "\xEF\xBC\x9A", ":",  // ：
                "\xEF\xBC\x88", "(",  // （
                "\xEF\xBC\x89", ")",  // ）
                "\xEF\xBC\xBB", "[",  // 【
                "\xEF\xBC\xBD", "]",  // 】
                "\xE3\x80\x8A", "<",  // 《
                "\xE3\x80\x8B", ">",  // 》
            };
            bool found = false;
            for (size_t p = 0; p < sizeof(pairs)/sizeof(pairs[0]); p += 2) {
                size_t len = strlen(pairs[p]);
                if (strncmp(src + i, pairs[p], len) == 0) {
                    strcpy(dst + j, pairs[p + 1]);
                    j += strlen(pairs[p + 1]);
                    i += len - 1;
                    found = true;
                    break;
                }
            }
            if (!found) dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

lv_obj_t* CnPrint::createText(const char* text, int x, int y, uint32_t color, uint8_t size, Alignment align, int width) {
    const lv_font_t* font = getFont(size);
    if (!font) {
        Serial.println("字体为空!");
        return nullptr;
    }
    lv_obj_t* parent = _currentParent ? _currentParent : lv_scr_act();
    if (!parent) {
        Serial.println("parent为空!");
        return nullptr;
    }
    lv_obj_t* label = lv_label_create(parent);
    
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    
    char converted[512];
    convertPunctuation(text, converted, sizeof(converted));
    lv_label_set_text(label, converted);
    
    // 改进的自动换行逻辑
    if (width > 0) {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    } else {
        // 如果没有指定宽度，使用屏幕宽度
        lv_coord_t screen_width = lv_disp_get_hor_res(nullptr);
        if (screen_width > 0) {
            lv_obj_set_width(label, screen_width);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        }
    }
    
    lv_obj_align(label, convertAlign(align), x, y);
    return label;
}

bool CnPrint::create(uint32_t id, const char* text, int x, int y,
                    uint32_t color, uint8_t size, Alignment align, int width) {
    // 节流：200ms内不重复更新同一个ID
    int slot = id % 20;
    unsigned long now = millis();
    if (now - lastUpdate[slot] < UPDATE_INTERVAL_MS) {
        return false;
    }
    lastUpdate[slot] = now;
    
    LVGL_LOCK();
    
    if (_labels.find(id) != _labels.end()) {
        auto it = _labels.find(id);
        lv_obj_t* oldLabel = it->second;
        if (oldLabel && lv_obj_is_valid(oldLabel)) {
            const char* oldText = lv_label_get_text(oldLabel);
            if (oldText && strcmp(oldText, text) == 0) {
                return true;
            }
            lv_obj_del(oldLabel);
        }
        _labels.erase(it);
    }

    lv_obj_t* label = createText(text, x, y, color, size, align, width);
    if (label) {
        _labels[id] = label;
        obj_map[id] = label;
        return true;
    }
    return false;
}

bool CnPrint::createBg(uint32_t id, const char* text, int x, int y,
                      uint32_t color, uint8_t size, Alignment align,
                      uint8_t bgA, uint32_t bgC, int radius, int width) {
    if (_labels.find(id) != _labels.end()) {
        remove(id);
    }

    lv_obj_t* label = createText(text, x, y, color, size, align, width);
    if (label) {
        lv_obj_set_style_bg_opa(label, bgA, 0);
        lv_obj_set_style_bg_color(label, lv_color_hex(bgC), 0);
        lv_obj_set_style_radius(label, radius, 0);
        lv_obj_set_style_pad_all(label, 5, 0);
        
        _labels[id] = label;
        return true;
    }
    return false;
}

bool CnPrint::update(uint32_t id, const char* text) {
    auto it = _labels.find(id);
    if (it != _labels.end()) {
        lv_obj_t* label = it->second;
        if (label && lv_obj_is_valid(label)) {
            lv_label_set_text(label, text);
            return true;
        }
    }
    // 不存在则不创建
    return false;
}

// 在 CnPrint.cpp 中
// src/tftv/CnPrint.cpp
void CnPrint::remove(unsigned int id) {
    LVGL_LOCK();
    
    lv_obj_t* obj = nullptr;
    
    if (obj_map.count(id)) obj = obj_map[id];
    else if (_labels.count(id)) obj = _labels[id];

    if (obj != nullptr) {
        if (lv_obj_is_valid(obj)) {
            lv_obj_del(obj);
        }
        obj_map.erase(id);
        _labels.erase(id);
    }
}

bool CnPrint::has(uint32_t id) {
    return _labels.find(id) != _labels.end();
}

void CnPrint::show(const char* text, uint32_t color, uint8_t size, Alignment align, uint32_t time) {
    LVGL_LOCK();
    
    // 清除之前的消息
    for (lv_obj_t* label : _msgs) {
        if (label && lv_obj_is_valid(label)) {
            lv_obj_del(label);
        }
    }
    _msgs.clear();
    
    // 创建新消息 - 使用屏幕宽度自动换行
    lv_coord_t screen_width = lv_disp_get_hor_res(nullptr);
    int msg_width = (screen_width > 0) ? screen_width : 240;
    
    lv_obj_t* label = createText(text, 0, 0, color, size, align, msg_width);
    if (label) {
        _msgs.push_back(label);
    }
    
    // 设置自动移除定时器
    if (time > 0) {
        if (_timer) {
            lv_timer_del(_timer);
        }
        _timer = lv_timer_create([](lv_timer_t* timer) {
            CnPrint::get().clear();
        }, time, nullptr);
    }
}

void CnPrint::clear() {
    LVGL_LOCK();
    
    // 清除所有临时消息
    for (lv_obj_t* label : _msgs) {
        if (label && lv_obj_is_valid(label)) {
            lv_obj_del(label);
        }
    }
    _msgs.clear();
    
    // 清除定时器
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
}