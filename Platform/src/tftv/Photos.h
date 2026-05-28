#ifndef PHOTOS_H
#define PHOTOS_H

#include <lvgl.h>
#include <string>
#include <vector>

class Photos {
public:
    static Photos& getInstance();
    void init(lv_obj_t* parent);
    void show();
    void hide();
    bool hasPhotos() const { return !_photoFiles.empty(); }
    void loadPhotoList();
    void nextPhoto();

private:
    Photos();
    void displayCurrentPhoto();
    bool isValidJpeg(const char* filename);

    lv_obj_t* _parent = nullptr;
    lv_obj_t* _hintLabel = nullptr;      // 无照片提示
    lv_obj_t* _errorLabel = nullptr;     // 错误提示
    lv_obj_t* _imageWidget = nullptr;    // LVGL 图像控件

    std::vector<std::string> _photoFiles;
    int _currentIndex = 0;
    bool _initialized = false;

    uint16_t* _currentImageBuffer = nullptr;  // 当前图像的RGB565数据
    lv_img_dsc_t* _currentImgDsc = nullptr;    // 当前图像的LVGL描述符
    int16_t _currentImgWidth = 0;
    int16_t _currentImgHeight = 0;
};

#endif // PHOTOS_H