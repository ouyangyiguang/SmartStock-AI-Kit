#ifndef MIC_INDICATOR_H
#define MIC_INDICATOR_H

#include <lvgl.h>

// 浮动麦克风收音指示器：贴在屏幕顶部 240×8 的细条
// 通过 LVGL 顶层 (lv_layer_top) 渲染，自动浮在所有界面之上
class MicIndicator {
public:
    static MicIndicator& get();

    // 初始化（必须在 Display::begin 之后调用）
    void init();

    // 启停（如调试时可隐藏）
    void show();
    void hide();

private:
    MicIndicator() = default;
    static void timerCb(lv_timer_t* t);
    void update();

    lv_obj_t* _container = nullptr; // 240×8 背景
    lv_obj_t* _bar       = nullptr; // 内部能量条
    lv_timer_t* _timer   = nullptr;
    bool _initialized = false;
    uint32_t _wakewordUntil = 0; // 唤醒闪烁结束时间(ms)
};

#endif
