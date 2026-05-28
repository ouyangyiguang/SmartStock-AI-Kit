#include "FontManager.h"
#include <Arduino.h>
#include <LittleFS.h>

// ====================================================
// LVGL FS Driver — 让 lv_font_load() 可以从 LittleFS 读取
// ====================================================

static void* littlefs_open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    // mode: LV_FS_MODE_RD = 0x02
    const char* flags = "r";
    if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = "r+";
    else if (mode == LV_FS_MODE_WR) flags = "w";

    File* file = new File();
    *file = LittleFS.open(path, flags);
    if (!*file) {
        delete file;
        Serial.printf("[Font] LittleFS打开失败: %s\n", path);
        return NULL;
    }
    return (void*)file;
}

static lv_fs_res_t littlefs_close_cb(lv_fs_drv_t* drv, void* file_p) {
    File* file = (File*)file_p;
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    File* file = (File*)file_p;
    size_t bytesRead = file->read((uint8_t*)buf, btr);
    if (br) *br = bytesRead;
    return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    File* file = (File*)file_p;
    SeekMode mode;
    switch (whence) {
        case LV_FS_SEEK_SET: mode = SeekSet; break;
        case LV_FS_SEEK_CUR: mode = SeekCur; break;
        case LV_FS_SEEK_END: mode = SeekEnd; break;
        default: return LV_FS_RES_INV_PARAM;
    }
    if (!file->seek(pos, mode)) return LV_FS_RES_UNKNOWN;
    return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    File* file = (File*)file_p;
    *pos_p = file->position();
    return LV_FS_RES_OK;
}

void FontManager::registerLittleFSDriver() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    fs_drv.letter = 'S';
    fs_drv.open_cb = littlefs_open_cb;
    fs_drv.close_cb = littlefs_close_cb;
    fs_drv.read_cb = littlefs_read_cb;
    fs_drv.seek_cb = littlefs_seek_cb;
    fs_drv.tell_cb = littlefs_tell_cb;
    // write_cb, dir_open_cb 等不需要，字体只读

    lv_fs_drv_register(&fs_drv);
    Serial.println(F("[Font] LVGL FS driver 'S:' registered -> LittleFS"));
}

// ====================================================
// FontManager
// ====================================================

FontManager& FontManager::getInstance() {
    static FontManager instance;
    return instance;
}

void FontManager::init() {
    if (_initialized) return;

    // 确保 LittleFS 已挂载
    if (!LittleFS.begin(false)) {
        Serial.println(F("[Font] LittleFS挂载失败，尝试格式化..."));
        if (!LittleFS.begin(true)) {
            Serial.println(F("[Font] LittleFS格式化失败！"));
            return;
        }
    }

    // 注册 LVGL FS 驱动
    registerLittleFSDriver();

    // 诊断：加载前检查堆内存和文件完整性
    auto diagFont = [](const char* label, const char* lfsPath, const char* lvglPath) -> lv_font_t* {
        if (!LittleFS.exists(lfsPath)) {
            Serial.printf("[Font] %s 文件不存在，请上传 LittleFS 数据\n", label);
            return NULL;
        }
        File f = LittleFS.open(lfsPath, "r");
        if (!f) {
            Serial.printf("[Font] %s 无法打开\n", label);
            return NULL;
        }
        size_t fileSize = f.size();
        // 读取文件头前8字节验证格式
        uint8_t header[8] = {0};
        f.read(header, 8);
        f.close();
        Serial.printf("[Font] %s: 大小=%u, 头=[%02X %02X %02X %02X %c%c%c%c]\n",
            label, fileSize, header[0], header[1], header[2], header[3],
            header[4], header[5], header[6], header[7]);
        
        Serial.printf("[Font] 加载前 FreeHeap=%u, FreePSRAM=%u\n",
            ESP.getFreeHeap(), ESP.getFreePsram());
        
        lv_font_t* font = lv_font_load(lvglPath);
        
        Serial.printf("[Font] 加载后 FreeHeap=%u, FreePSRAM=%u, 结果=%s\n",
            ESP.getFreeHeap(), ESP.getFreePsram(), font ? "成功" : "失败");
        return font;
    };

    _chineseFont20 = diagFont("20px", "/font/lv_font_chinese_20.bin", "S:/font/lv_font_chinese_20.bin");
    _chineseFont14 = diagFont("14px", "/font/lv_font_chinese_14.bin", "S:/font/lv_font_chinese_14.bin");

    // 打印 LittleFS 状态
    Serial.printf("[Font] LittleFS总空间: %u  已用: %u\n",
        LittleFS.totalBytes(), LittleFS.usedBytes());

    _initialized = true;
}

FontManager::~FontManager() {
    if (_chineseFont20) lv_font_free(_chineseFont20);
    if (_chineseFont14) lv_font_free(_chineseFont14);
}

const lv_font_t* FontManager::getChineseFont20() {
    if (!_initialized || !_chineseFont20) {
        init();
    }
    return _chineseFont20 ? _chineseFont20 : LV_FONT_DEFAULT;
}

const lv_font_t* FontManager::getChineseFont14() {
    if (!_initialized || !_chineseFont14) {
        init();
    }
    return _chineseFont14 ? _chineseFont14 : LV_FONT_DEFAULT;
}
