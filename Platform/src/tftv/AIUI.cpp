#include "AIUI.h"
#include <Arduino.h>
#include "font/FontManager.h"

static void convertChineseSymbols(const char* src, char* dst, size_t dstSize) {
    if (!src || !dst || dstSize == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstSize - 1; ) {
        unsigned char c = src[i];
        // 1. 跳过所有空格 (ASCII 32 或 UTF-8 全角 E3 80 80)
        if (c == ' ') { i++; continue; }
        if (c == 0xE3 && (unsigned char)src[i+1] == 0x80 && (unsigned char)src[i+2] == 0x80) { i += 3; continue; }

        // 2. 处理常用的 3 字节 UTF-8 符号
        if (c >= 0xE0 && src[i+1] && src[i+2]) {
            unsigned char c2 = src[i+1], c3 = src[i+2];
            char target = 0;
            if (c == 0xEF && c2 == 0xBC) { // 绝大多数全角符号区间
                const char* list = "\x8C\x81\x9F\x9A\x9B\x88\x89"; // ，！ ？ ： ； （ ）
                const char* map  = ",!?:;()";
                for (int k = 0; list[k]; k++) if (c3 == (unsigned char)list[k]) { target = map[k]; break; }
            } else if (c == 0xE3 && c2 == 0x80) { // 句号与中括号区间
                if (c3 == 0x82) target = '.'; // 。
                else if (c3 == 0x90) target = '['; // 【
                else if (c3 == 0x91) target = ']'; // 】
            }

            if (target) { dst[j++] = target; i += 3; continue; }
            
            // 非标点汉字，完整复制 3 字节
            if (j + 3 < dstSize) { dst[j++] = src[i++]; dst[j++] = src[i++]; dst[j++] = src[i++]; continue; }
        }
        // 3. 基础 ASCII 复制
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

AIUI& AIUI::get() {
    static AIUI instance;
    return instance;
}

void AIUI::init(lv_obj_t* parent) {
    _parent = parent;
    LVGL_LOCK();

    lv_obj_set_size(_parent, 240, 320);
    lv_obj_set_style_bg_color(_parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_parent, 0, 0);

    // 1. 状态栏
    lv_obj_t* statusBg = lv_obj_create(_parent);
    lv_obj_set_size(statusBg, 240, 28);
    lv_obj_set_pos(statusBg, 0, 6);  // 让出 MicIndicator 高度
    lv_obj_set_style_bg_color(statusBg, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(statusBg, 0, 0);
    lv_obj_set_style_radius(statusBg, 0, 0);
    lv_obj_set_style_pad_all(statusBg, 0, 0);  // 去掉默认 padding，消除间隔
    lv_obj_set_flex_flow(statusBg, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(statusBg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_align(statusBg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _statusLabel = lv_label_create(statusBg);
    lv_label_set_recolor(_statusLabel, true);
    lv_label_set_text(_statusLabel, "#888888 正在唤醒...");
    lv_obj_set_style_text_font(_statusLabel, FontManager::getInstance().getChineseFont20(), 0);
    
    _countdownLabel = lv_label_create(_parent);
    lv_obj_set_pos(_countdownLabel, 210, 12);
    lv_obj_set_style_text_font(_countdownLabel, FontManager::getInstance().getChineseFont14(), 0);
    lv_obj_set_style_text_color(_countdownLabel, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(_countdownLabel, "");

    // 2. 滚动视窗 (修改：pad_hor 设为 0，让头像能贴边)
    _page = lv_obj_create(_parent);
    lv_obj_set_size(_page, 240, 285);
    lv_obj_set_pos(_page, 0, 36);
    lv_obj_set_style_bg_color(_page, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(_page, 0, 0);
    lv_obj_set_style_pad_hor(_page, 0, 0); 
    lv_obj_set_scrollbar_mode(_page, LV_SCROLLBAR_MODE_OFF);

    // 3. 内容容器
    _content = lv_obj_create(_page);
    lv_obj_set_size(_content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(_content, 0, 0);
    lv_obj_set_style_border_width(_content, 0, 0);
    lv_obj_set_style_pad_hor(_content, 0, 0);
    lv_obj_set_style_pad_gap(_content, 6, 0);
    lv_obj_set_style_pad_top(_content, 0, 0);
    lv_obj_set_scrollbar_mode(_content, LV_SCROLLBAR_MODE_OFF);

    // 4. 对象池
    for (int i = 0; i < AI_UI_POOL_SIZE; i++) {
        _pool[i].container = lv_obj_create(_content);
        lv_obj_set_width(_pool[i].container, LV_PCT(100));
        lv_obj_set_height(_pool[i].container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(_pool[i].container, 0, 0);
        lv_obj_set_style_border_width(_pool[i].container, 0, 0);
        lv_obj_set_style_pad_all(_pool[i].container, 0, 0);
        lv_obj_set_scrollbar_mode(_pool[i].container, LV_SCROLLBAR_MODE_OFF);

        // 头像框
        _pool[i].avatar = lv_obj_create(_pool[i].container);
        lv_obj_set_size(_pool[i].avatar, 30, 30);
        lv_obj_set_style_radius(_pool[i].avatar, 4, 0);
        lv_obj_set_style_border_width(_pool[i].avatar, 0, 0);
        lv_obj_set_style_pad_all(_pool[i].avatar, 0, 0);
        lv_obj_clear_flag(_pool[i].avatar, LV_OBJ_FLAG_SCROLLABLE); // 禁用头像滑动条

        lv_obj_t* al = lv_label_create(_pool[i].avatar);
        lv_obj_set_style_text_font(al, FontManager::getInstance().getChineseFont14(), 0);
        lv_obj_set_align(al, LV_ALIGN_CENTER);

        // 气泡背景
        _pool[i].bg = lv_obj_create(_pool[i].container);
        lv_obj_set_width(_pool[i].bg, LV_PCT(84)); 
        lv_obj_set_height(_pool[i].bg, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(_pool[i].bg, 0, 0);
        lv_obj_set_style_pad_all(_pool[i].bg, 6, 0);
        lv_obj_set_style_radius(_pool[i].bg, 12, 0);
        lv_obj_set_scrollbar_mode(_pool[i].bg, LV_SCROLLBAR_MODE_OFF);

        _pool[i].label = lv_label_create(_pool[i].bg);
        lv_obj_set_style_text_font(_pool[i].label, FontManager::getInstance().getChineseFont14(), 0);
        lv_label_set_long_mode(_pool[i].label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_pool[i].label, LV_PCT(100));

        lv_obj_add_flag(_pool[i].container, LV_OBJ_FLAG_HIDDEN);
    }

    // 5. 底部垫片
    _spacer = lv_obj_create(_content);
    lv_obj_set_size(_spacer, LV_PCT(100), 30); 
    lv_obj_set_style_bg_opa(_spacer, 0, 0); 
    lv_obj_set_style_border_width(_spacer, 0, 0);
    lv_obj_clear_flag(_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void AIUI::addBubble(const char* text, bool isUser) {
    if (!text || !_content) return;

    LVGL_LOCK();
    lv_obj_clear_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    _msgCount++;
    
    char converted[256];
    convertChineseSymbols(text, converted, sizeof(converted));
    
    BubbleItem& item = _pool[_poolIndex];
    lv_obj_clear_flag(item.container, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(item.label, converted);

    lv_obj_t* al = lv_obj_get_child(item.avatar, 0);

    if (isUser) {
        // 用户：头像完全贴右
        lv_obj_set_align(item.avatar, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_style_bg_color(item.avatar, lv_color_hex(0x95EC69), 0);
        lv_label_set_text(al, "我");
        lv_obj_set_style_text_color(al, lv_color_hex(0x000000), 0);

        lv_obj_set_align(item.bg, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_pos(item.avatar, 0, 0);
        lv_obj_set_pos(item.bg, -36, 0); // 气泡偏移出头像位置
        lv_obj_set_style_bg_color(item.bg, lv_color_hex(0x95EC69), 0);
        lv_obj_set_style_text_color(item.label, lv_color_hex(0x000000), 0);
    } else {
        // AI：头像完全贴左
        lv_obj_set_align(item.avatar, LV_ALIGN_TOP_LEFT);
        lv_obj_set_style_bg_color(item.avatar, lv_color_hex(0x2C2C2C), 0);
        lv_label_set_text(al, "AI");
        lv_obj_set_style_text_color(al, lv_color_hex(0xFFFFFF), 0);

        lv_obj_set_align(item.bg, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(item.avatar, 0, 0);
        lv_obj_set_pos(item.bg, 36, 0); // 气泡偏移出头像位置
        lv_obj_set_style_bg_color(item.bg, lv_color_hex(0x2C2C2C), 0);
        lv_obj_set_style_text_color(item.label, lv_color_hex(0xFFFFFF), 0);
    }

    lv_obj_update_layout(item.label);
    lv_obj_update_layout(item.bg);
    lv_obj_update_layout(item.container);

    lv_obj_move_to_index(item.container, -2);
    _poolIndex = (_poolIndex + 1) % AI_UI_POOL_SIZE;

    lv_obj_update_layout(_content);

    if (_msgCount > 2) {
        scrollToBottom();
    }
    LVGL_UNLOCK();
}

void AIUI::updateLastBubbleText(const char* text) {
    if (!text || !_content || _msgCount == 0) return;

    LVGL_LOCK();
    int lastIdx = (_poolIndex - 1 + AI_UI_POOL_SIZE) % AI_UI_POOL_SIZE;
    BubbleItem& item = _pool[lastIdx];
    char converted[256];
    convertChineseSymbols(text, converted, sizeof(converted));
    lv_label_set_text(item.label, converted);
    lv_obj_update_layout(item.label);
    lv_obj_update_layout(item.bg);
    lv_obj_update_layout(item.container);
    scrollToBottom();
    LVGL_UNLOCK();
}

void AIUI::removeLastBubble() {
    if (_msgCount == 0) return;
    LVGL_LOCK();
    int lastIdx = (_poolIndex - 1 + AI_UI_POOL_SIZE) % AI_UI_POOL_SIZE;
    lv_obj_add_flag(_pool[lastIdx].container, LV_OBJ_FLAG_HIDDEN);
    _poolIndex = lastIdx;
    _msgCount--;
    LVGL_UNLOCK();
}

void AIUI::scrollToBottom() {
    if (!_page || !_content) return;
    LVGL_LOCK();
    lv_anim_del(_page, NULL);
    lv_obj_update_layout(_content);
    lv_coord_t content_h = lv_obj_get_height(_content);
    lv_coord_t page_h = lv_obj_get_height(_page);
    if (content_h > page_h) {
        lv_obj_scroll_to_y(_page, content_h - page_h + 5, LV_ANIM_ON);
    }
    LVGL_UNLOCK();
}

void AIUI::setStatus(const char* status) {
    LVGL_LOCK();
    if (_statusLabel) {
        lv_label_set_text(_statusLabel, status);
    }
    LVGL_UNLOCK();
}

void AIUI::clearDialog() {
    LVGL_LOCK();
    lv_anim_del(_page, NULL);
    for (int i = 0; i < AI_UI_POOL_SIZE; i++) {
        lv_obj_add_flag(_pool[i].container, LV_OBJ_FLAG_HIDDEN);
    }
    _poolIndex = 0;
    _msgCount = 0;
    lv_obj_scroll_to_y(_page, 0, LV_ANIM_OFF);
    LVGL_UNLOCK();
}

void AIUI::setCountdown(int seconds) {
    LVGL_LOCK();
    if (seconds > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", seconds);
        lv_label_set_text(_countdownLabel, buf);
    } else {
        lv_label_set_text(_countdownLabel, "");
    }
    LVGL_UNLOCK();
}