#ifndef IMAGE_DISPLAY_H
#define IMAGE_DISPLAY_H

#include <FS.h>
using fs::File;
#include <JPEGDEC.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>

class ImageDisplay {
public:
    ImageDisplay(TFT_eSPI* tft = nullptr);
    void setTFT(TFT_eSPI* tft);
    bool begin();
    bool displayFromURL(const char* url, int16_t x, int16_t y);
    bool displayFromURL(const String& url, int16_t x, int16_t y);
    bool displayFromFile(const char* filename, int16_t x, int16_t y);
    void testD();

    // 新增：解码JPEG到RGB565缓冲区（用于LVGL图像控件）
    bool decodeToBuffer(const char* filename, uint16_t*& outBuffer, int16_t& width, int16_t& height);
    void freeBuffer(uint16_t* buffer);   // 释放缓冲区

private:
    TFT_eSPI* _tft;
    JPEGDEC _jpeg;

    static int jpegDrawCallback(JPEGDRAW *pDraw);
    static int jpegBufferCallback(JPEGDRAW *pDraw);
    bool downloadImageToMemory(const char* url, uint8_t** buffer, size_t* bufferSize);
    bool decodeJpegFromMemory(uint8_t* jpegData, size_t dataSize, int16_t x, int16_t y);
    bool decodeJpegFromFile(const char* filename, int16_t x, int16_t y);
};

#endif // IMAGE_DISPLAY_H