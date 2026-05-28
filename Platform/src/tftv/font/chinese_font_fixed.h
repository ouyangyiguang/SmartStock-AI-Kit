#ifndef CHINESE_FONT_FIXED_H
#define CHINESE_FONT_FIXED_H

#include "lvgl.h"

// 只使用一个字体定义，避免冲突
extern "C" {
    LV_FONT_DECLARE(ui_font_AlimamaShuHeiFont16Bpp4);
}

#endif // CHINESE_FONT_FIXED_H