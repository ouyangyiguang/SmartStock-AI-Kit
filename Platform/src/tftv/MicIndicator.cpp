#include "MicIndicator.h"
#include "LVGLGlobal.h"
#include "../ai/MicModule.h"
#include <Arduino.h>

// 高度 8 像素，全宽 240
static const int IND_HEIGHT = 6;
static const int IND_WIDTH  = 240;

MicIndicator& MicIndicator::get() {
    static MicIndicator inst;
    return inst;
}

void MicIndicator::init() {
    if (_initialized) return;

    LVGL_LOCK();
    // 使用顶层（LV_LAYER_TOP）让它浮于所有界面之上，且不被 lv_obj_clean(scr) 影响
    lv_obj_t* top = lv_layer_top();

    _container = lv_obj_create(top);
    lv_obj_remove_style_all(_container);
    lv_obj_set_size(_container, IND_WIDTH, IND_HEIGHT);
    lv_obj_set_pos(_container, 0, 0);
    // 更深、更透的底色，让亮色 bar 更突出
    lv_obj_set_style_bg_color(_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_container, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_container, 0, 0);
    lv_obj_set_style_radius(_container, 0, 0);
    lv_obj_set_style_pad_all(_container, 0, 0);
    lv_obj_clear_flag(_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_container, LV_OBJ_FLAG_IGNORE_LAYOUT);

    _bar = lv_obj_create(_container);
    lv_obj_remove_style_all(_bar);
    lv_obj_set_pos(_bar, 0, 0);
    lv_obj_set_size(_bar, 0, IND_HEIGHT);
    lv_obj_set_style_bg_color(_bar, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_grad_color(_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, IND_HEIGHT / 2, 0);  // 全圆角胶囊形
    // 发光阴影提升质感
    lv_obj_set_style_shadow_width(_bar, 8, 0);
    lv_obj_set_style_shadow_spread(_bar, 1, 0);
    lv_obj_set_style_shadow_opa(_bar, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_bar, LV_OBJ_FLAG_CLICKABLE);

    // 80ms 刷新一次足够顺滑且省 CPU
    _timer = lv_timer_create(MicIndicator::timerCb, 80, this);

    _initialized = true;
    LVGL_UNLOCK();

    Serial.println("✅ MicIndicator 浮动麦克风指示器已启用");
}

void MicIndicator::show() {
    if (!_initialized) return;
    LVGL_LOCK();
    lv_obj_clear_flag(_container, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void MicIndicator::hide() {
    if (!_initialized) return;
    LVGL_LOCK();
    lv_obj_add_flag(_container, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void MicIndicator::timerCb(lv_timer_t* t) {
    static_cast<MicIndicator*>(t->user_data)->update();
}

void MicIndicator::update() {
    if (!_bar) return;

    // 读状态（这些 getter 都是非阻塞 / 原子读取）
    int  energy   = MicModule::getCurrentEnergy();
    int  envBase  = MicModule::getEnvBaseEnergy();
    bool i2sOk    = MicModule::isI2sActive();
    bool listening = MicModule::getInstance().isListening();
    if (MicModule::consumeWakeword()) {
        _wakewordUntil = millis() + 500; // 闪 0.5 秒
    }
    bool wakeFlash = (millis() < _wakewordUntil);

    // 计算条宽：用 envBase 做底（避免底噪占满），高于底噪映射到 0..240
    // 【优化】满刻度从 200 降到 80，让小声音也有大跳动，更灵敏
    int delta = energy - envBase;
    if (delta < 0) delta = 0;
    int level = (delta * IND_WIDTH) / 80;
    if (level > IND_WIDTH) level = IND_WIDTH;

    // 颜色规则：渐变起点色 + 高光终点色 + 发光色
    uint32_t colorBase, colorBright, colorGlow;
    if (!i2sOk) {
        colorBase = 0x1E40FF; colorBright = 0x60A5FF; colorGlow = 0x60A5FF;  // 亮蓝：I2S 暂停
        level = IND_WIDTH;
    } else if (wakeFlash) {
        colorBase = 0xFFFFFF; colorBright = 0xFFFFFF; colorGlow = 0xFFFFFF;  // 丝滑白闪
        level = IND_WIDTH;
    } else if (listening) {
        if (delta > 70) {
            colorBase = 0xFF1744; colorBright = 0xFF8A95; colorGlow = 0xFF4569; // 玫红起、粉高光
        } else {
            colorBase = 0xFF8C00; colorBright = 0xFFD86B; colorGlow = 0xFFB347; // 橙起、金高光
        }
    } else if (delta > 10) {
        colorBase = 0x00C853; colorBright = 0x76FF98; colorGlow = 0x4ADE80;    // 荈荈鲜绿：一发声就变绿
    } else {
        // 【优化】待机静音：清凉青白色，明亮明显且有科技感
        colorBase = 0x06B6D4; colorBright = 0xE0F7FF; colorGlow = 0x67E8F9;

        // 【呼吸动画】3 秒一个周期，三角波从 2px 渐变到 24px 再回落
        unsigned long t = millis() % 3000;
        int phase = (t < 1500) ? (int)t : (int)(3000 - t);  // 0..1500..0
        int breath = 2 + (phase * 22) / 1500;               // 2..24..2

        if (delta <= 0) {
            // 完全没声响：纯粹呼吸
            level = breath;
        } else {
            // 有微小波动：呼吸基线 + 实时抖动放大
            level = breath + level * 4;
            if (level > 80) level = 80;
        }
    }

    // 仅在确实变化时调用，省 LVGL 重绘
    static int lastLevel = -1;
    static uint32_t lastColor = 0xFFFFFFFF;
    if (level != lastLevel) {
        lv_obj_set_width(_bar, level);
        lastLevel = level;
    }
    if (colorBase != lastColor) {
        lv_obj_set_style_bg_color(_bar, lv_color_hex(colorBase), 0);
        lv_obj_set_style_bg_grad_color(_bar, lv_color_hex(colorBright), 0);
        lv_obj_set_style_shadow_color(_bar, lv_color_hex(colorGlow), 0);
        lastColor = colorBase;
    }
}
