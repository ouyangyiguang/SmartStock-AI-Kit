#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <lvgl.h>

class FontManager {
public:
    static FontManager& getInstance();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    // 初始化字体管理器 (注册LittleFS到LVGL FS，加载字体)
    void init();

    // 获取字体
    const lv_font_t* getChineseFont20();
    const lv_font_t* getChineseFont14();
    const lv_font_t* getDefaultFont() { return getChineseFont20(); }

private:
    FontManager() = default;
    ~FontManager();

    bool _initialized = false;
    lv_font_t* _chineseFont20 = nullptr;
    lv_font_t* _chineseFont14 = nullptr;

    // 注册LVGL文件系统驱动，使lv_font_load能从LittleFS读取
    static void registerLittleFSDriver();
};

#endif
