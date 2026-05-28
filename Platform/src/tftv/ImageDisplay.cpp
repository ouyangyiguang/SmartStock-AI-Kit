#include "ImageDisplay.h"
#include <LittleFS.h>
#include <FS.h>

#ifdef ESP32
#include "esp_task_wdt.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp32s3/rom/cache.h"
#define PSRAM_CACHE_WRITEBACK(addr, size) Cache_WriteBack_Addr((uint32_t)(addr), (size))
#elif defined(CONFIG_IDF_TARGET_ESP32)
#include "esp32/rom/cache.h"
#define PSRAM_CACHE_WRITEBACK(addr, size) Cache_WriteBack_Addr((uint32_t)(addr), (size))
#else
#define PSRAM_CACHE_WRITEBACK(addr, size) do{}while(0)
#endif

extern TFT_eSPI tft;

static int _jpegX = 0;
static int _jpegY = 0;

// 静态缓冲区用于解码到内存（供 decodeToBuffer 使用）
static uint16_t* g_rgb565_buffer = nullptr;
static int g_buffer_width = 0;
static int g_buffer_height = 0;
static int g_current_y = 0;

// ------------------------------------------------------------
// 构造与基本设置
// ------------------------------------------------------------
ImageDisplay::ImageDisplay(TFT_eSPI* tft) : _tft(tft) {
}

void ImageDisplay::setTFT(TFT_eSPI* tft) {
    _tft = tft;
}

bool ImageDisplay::begin() {
    return _tft != nullptr;
}

// ------------------------------------------------------------
// 原有的直接绘制回调（用于直接显示）
// ------------------------------------------------------------
int ImageDisplay::jpegDrawCallback(JPEGDRAW *pDraw) {
    if (pDraw->iWidth == 0) return 1;
    
#ifdef ESP32
    esp_task_wdt_reset();
#endif

    bool oldSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.pushImage(_jpegX + pDraw->x, _jpegY + pDraw->y, 
                  pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    tft.setSwapBytes(oldSwap);
    return 1;
}

// ------------------------------------------------------------
// 新增：解码到内存的回调（收集像素数据到全局缓冲区）
// ------------------------------------------------------------
int ImageDisplay::jpegBufferCallback(JPEGDRAW *pDraw) {
    if (pDraw->iWidth == 0 || pDraw->iHeight == 0) return 1;
    if (!g_rgb565_buffer) return 0;
    
    uint16_t* dest = g_rgb565_buffer + (pDraw->y * g_buffer_width + pDraw->x);
    uint16_t* src = (uint16_t*)pDraw->pPixels;
    
    // 关键修复：JPEGDEC输出的像素格式需要字节交换
    // 逐像素复制并交换字节顺序
    for (int y = 0; y < pDraw->iHeight; y++) {
        for (int x = 0; x < pDraw->iWidth; x++) {
            uint16_t pixel = src[y * pDraw->iWidth + x];
            // 交换字节顺序：0xRRRRRGGG GGGBBBBB -> 0xGGGBBBBB RRRRRGGG
            dest[y * g_buffer_width + x] = (pixel << 8) | (pixel >> 8);
        }
    }
    return 1;
}
// ------------------------------------------------------------
// 解码JPEG到RGB565缓冲区（使用流式解码，内存高效）
// ------------------------------------------------------------
bool ImageDisplay::decodeToBuffer(const char* filename, uint16_t*& outBuffer, int16_t& width, int16_t& height) {
    File file = LittleFS.open(filename, "r");
    if (!file) return false;
    
    // 将整个文件读取到内存中获取尺寸
    file.seek(0);
    size_t fileSize = file.size();
    uint8_t* jpegData = (uint8_t*)malloc(fileSize);
    if (!jpegData) {
        file.close();
        return false;
    }
    
    size_t bytesRead = file.read(jpegData, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        free(jpegData);
        return false;
    }
    
    // 使用openRAM获取尺寸
    if (!_jpeg.openRAM(jpegData, fileSize, nullptr)) {
        free(jpegData);
        return false;
    }
    
    width = _jpeg.getWidth();
    height = _jpeg.getHeight();
    _jpeg.close();
    
    // 重置文件指针，准备后续解码
    free(jpegData);
    
    if (width <= 0 || height <= 0) return false;
    
    // 分配RGB565缓冲区
    size_t bufferPixels = width * height;
    uint16_t* buffer = nullptr;
    
    #ifdef BOARD_HAS_PSRAM
    buffer = (uint16_t*)ps_malloc(bufferPixels * 2);
    #else
    buffer = (uint16_t*)malloc(bufferPixels * 2);
    #endif
    
    if (!buffer) return false;
    memset(buffer, 0, bufferPixels * 2);
    
    // 重新打开文件进行解码
    file = LittleFS.open(filename, "r");
    if (!file) {
        free(buffer);
        return false;
    }
    
    file.seek(0);
    size_t decodeFileSize = file.size();
    uint8_t* decodeJpegData = nullptr;
    
    #ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        decodeJpegData = (uint8_t*)ps_malloc(decodeFileSize);
    } else {
        decodeJpegData = (uint8_t*)malloc(decodeFileSize);
    }
    #else
    decodeJpegData = (uint8_t*)malloc(decodeFileSize);
    #endif
    
    if (!decodeJpegData) {
        free(buffer);
        file.close();
        return false;
    }
    
    size_t decodeBytesRead = file.read(decodeJpegData, decodeFileSize);
    file.close();
    
    if (decodeBytesRead != decodeFileSize) {
        free(decodeJpegData);
        free(buffer);
        return false;
    }
    
    // 设置全局静态变量
    g_rgb565_buffer = buffer;
    g_buffer_width = width;
    g_buffer_height = height;
    g_current_y = 0;
    
    // 使用openRAM函数替代open(File&)
    if (_jpeg.openRAM(decodeJpegData, decodeFileSize, jpegBufferCallback)) {
        bool success = _jpeg.decode(0, 0, 0);
        _jpeg.close();
        free(decodeJpegData);
        
        // 解码完成后立刻清除全局静态指针，防止后续误触发回调写入脏数据
        g_rgb565_buffer = nullptr;
        
        if (success) {
            // 强制把 CPU Cache 中的像素数据回写到 PSRAM 物理内存
            // 避免 LVGL/DMA 读取时读到还在 Cache 中的脏数据 → 彩色器点/错位
            PSRAM_CACHE_WRITEBACK(buffer, bufferPixels * 2);
            outBuffer = buffer;
            return true;
        }
    } else {
        free(decodeJpegData);
    }
    
    g_rgb565_buffer = nullptr;
    free(buffer);
    return false;
}

void ImageDisplay::freeBuffer(uint16_t* buffer) {
    if (buffer) free(buffer);
}

// ------------------------------------------------------------
// 原有方法：从内存解码（网络图片）
// ------------------------------------------------------------
bool ImageDisplay::downloadImageToMemory(const char* url, uint8_t** buffer, size_t* bufferSize) {
    HTTPClient http;
    http.setTimeout(30000);
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        http.end();
        return false;
    }

    uint8_t* jpegData = nullptr;
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        jpegData = (uint8_t*)ps_malloc(contentLength);
    } else {
        jpegData = (uint8_t*)malloc(contentLength);
    }
#else
    jpegData = (uint8_t*)malloc(contentLength);
#endif

    if (!jpegData) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = 0;
    uint32_t startTime = millis();

    while (bytesRead < contentLength) {
        int chunkSize = stream->readBytes(jpegData + bytesRead, contentLength - bytesRead);
        if (chunkSize > 0) {
            bytesRead += chunkSize;
        } else {
            delay(10);
        }
        if (millis() - startTime > 30000) break;
    }

    http.end();

    if (bytesRead != contentLength) {
        free(jpegData);
        return false;
    }

    *buffer = jpegData;
    *bufferSize = contentLength;
    return true;
}

bool ImageDisplay::decodeJpegFromMemory(uint8_t* jpegData, size_t dataSize, int16_t x, int16_t y) {
    _jpegX = x;
    _jpegY = y;
    
    if (_jpeg.openRAM(jpegData, dataSize, jpegDrawCallback)) {
        int w = _jpeg.getWidth();
        int h = _jpeg.getHeight();
        
        // 如果发现识别出来的宽高不对（比如变成了 160x120），直接报错，别强行解码
        if (w < 200) { 
            _jpeg.close();
            return false;
        }
        
        // 强制使用 FULL 分辨率
        _jpeg.decode(0, 0, 0); 
        return true;
    }
    return false;
}

bool ImageDisplay::decodeJpegFromFile(const char* filename, int16_t x, int16_t y) {
    _jpegX = x;
    _jpegY = y;
    
    File file = LittleFS.open(filename, "r");
    if (!file) return false;
    
    // 将文件读取到内存中
    file.seek(0);
    size_t fileSize = file.size();
    uint8_t* jpegData = (uint8_t*)malloc(fileSize);
    if (!jpegData) {
        file.close();
        return false;
    }
    
    size_t bytesRead = file.read(jpegData, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        free(jpegData);
        return false;
    }
    
    // 使用openRAM替代open(File&)
    if (_jpeg.openRAM(jpegData, fileSize, jpegDrawCallback)) {
        bool success = _jpeg.decode(x, y, 0);
        _jpeg.close();
        free(jpegData);
        return success;
    } else {
        free(jpegData);
        return false;
    }
}

// ------------------------------------------------------------
// 对外接口
// ------------------------------------------------------------
bool ImageDisplay::displayFromFile(const char* filename, int16_t x, int16_t y) {
    if (!LittleFS.exists(filename)) return false;
    return decodeJpegFromFile(filename, x, y);
}

bool ImageDisplay::displayFromURL(const char* url, int16_t x, int16_t y) {
    if (!_tft || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    uint8_t* jpegBuffer = nullptr;
    size_t bufferSize = 0;

    if (!downloadImageToMemory(url, &jpegBuffer, &bufferSize)) {
        return false;
    }

    bool result = decodeJpegFromMemory(jpegBuffer, bufferSize, x, y);
    
    if (jpegBuffer) {
        free(jpegBuffer);
    }
    
    return result;
}

bool ImageDisplay::displayFromURL(const String& url, int16_t x, int16_t y) {
    return displayFromURL(url.c_str(), x, y);
}

void ImageDisplay::testD() {
    // 空实现
}