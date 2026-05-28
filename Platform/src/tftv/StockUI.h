#ifndef STOCK_UI_H
#define STOCK_UI_H

#include <lvgl.h>
#include <vector>
#include <ArduinoJson.h>
#include <String>

class StockUI {
public:
    static StockUI& get();

    void init(lv_obj_t* parent);
    void show(); // <--- 新增
    void hide(); // <--- 新增
    void updateData(JsonObject* data);
    void updateWindTemp(const char* text);
    void clearContent();
    void updateNetStatus(bool wifiOk, bool mqttOk, const String& time);
    void showWaiting();
    void nextPage();
    void prevPage();
    void showTip(const char* tip);
    void clearTip();

private:
    StockUI();
    void ensureVisible();
    void createStyles();
    void initTicker(lv_obj_t*& ticker, int y_pos, lv_color_t bg_color, lv_color_t text_color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT, lv_label_long_mode_t mode = LV_LABEL_LONG_SCROLL_CIRCULAR);
    void updateStockPage();
    void showStockItem(int index, const char* code, const char* name, const char* price, 
                       const char* change, const char* volume, const char* time, const char* currency, bool isPositive);
    void hideAllItems();

    lv_obj_t* _parent;
    lv_obj_t* _topBar;
    lv_obj_t* _cityLabel;
    lv_obj_t* _weatherLabel;
    lv_obj_t* _timeLabel;
    lv_obj_t* _tipsLabel;
    lv_obj_t* _ticker1;
    lv_obj_t* _ticker2;
    lv_obj_t* _ticker3;
    lv_obj_t* _ticker4;
    lv_obj_t* _stockList;
    lv_obj_t* _pageIndicator;
    lv_obj_t* _wifiBars;
    lv_obj_t* _waitingLabel;
    
    static const int STOCK_PER_PAGE = 4;
    lv_obj_t* _stockItems[STOCK_PER_PAGE];
    lv_obj_t* _itemCodes[STOCK_PER_PAGE];
    lv_obj_t* _itemNames[STOCK_PER_PAGE];
    lv_obj_t* _itemPrices[STOCK_PER_PAGE];
    lv_obj_t* _itemChanges[STOCK_PER_PAGE];
    lv_obj_t* _itemArrows[STOCK_PER_PAGE];
    lv_obj_t* _itemVolumes[STOCK_PER_PAGE];
    lv_obj_t* _itemTriangles[STOCK_PER_PAGE];
    lv_obj_t* _itemCurrency[STOCK_PER_PAGE];

    static lv_style_t style_item_pos;
    static lv_style_t style_item_neg;
    static lv_style_t style_text_white;
    static lv_style_t style_text_yellow;
    static lv_style_t style_text_green;
    static lv_style_t style_text_red;
    static lv_style_t style_text_white_small;
    static lv_style_t style_text_yellow_small;
    static bool stylesInitialized;

    int _currentPage;
    int _totalPages;
    int _weatherTempToggle;
    JsonArray _cachedGpData;

    struct {
        const char* text;
        int scrollX;
        unsigned long lastTick;
    } _scrollState1, _scrollState2, _scrollState3;
};

#endif
