#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_MEM_SIZE (128 * 1024)
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 1
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// 大字体支持
#define LV_FONT_FMT_TXT_LARGE 1

// 启用需要的字体大小
#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1


// 图片解码器配置
#define LV_IMG_CF_INDEXED 1
#define LV_IMG_CF_ALPHA 1
#define LV_IMG_CACHE_DEF_SIZE 4

// JPG 支持
#define LV_USE_SJPG 1
#define LV_USE_TJPGD 1

// BMP 支持  
#define LV_USE_BMP 1

// 文件系统配置
#define LV_USE_FILESYSTEM 1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH ""

// 字体加载器支持TTF
#define LV_USE_FONT_LOADER 1

// PSRAM配置 - 使用标准 malloc（ESP32 Arduino 开启 PSRAM 后 malloc 会自动使用 PSRAM）
#ifdef BOARD_HAS_PSRAM
    #define LV_MEM_CUSTOM 1
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC malloc
    #define LV_MEM_CUSTOM_FREE free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#endif