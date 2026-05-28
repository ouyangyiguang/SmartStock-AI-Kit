#include "StockUI.h"
#include "CnPrint.h"
#include "font/FontManager.h"
#include "LVGLGlobal.h"
#include <Arduino.h>

lv_style_t StockUI::style_item_pos;
lv_style_t StockUI::style_item_neg;
lv_style_t StockUI::style_text_white;
lv_style_t StockUI::style_text_yellow;
lv_style_t StockUI::style_text_yellow_small;
lv_style_t StockUI::style_text_white_small; 
lv_style_t StockUI::style_text_green;
lv_style_t StockUI::style_text_red;
bool StockUI::stylesInitialized = false;

StockUI& StockUI::get() {
    static StockUI instance;
    return instance;
}

StockUI::StockUI() 
    : _parent(nullptr), _stockList(nullptr), _pageIndicator(nullptr),
      _wifiBars(nullptr), _weatherTempToggle(0), _currentPage(0), _totalPages(0) {
    for (int i = 0; i < STOCK_PER_PAGE; i++) {
        _stockItems[i] = nullptr;
        _itemCodes[i] = nullptr;
        _itemNames[i] = nullptr;
        _itemPrices[i] = nullptr;
        _itemChanges[i] = nullptr;
        _itemArrows[i] = nullptr;
        _itemVolumes[i] = nullptr;
        _itemTriangles[i] = nullptr;
    }
}

void StockUI::createStyles() {
    if (stylesInitialized) return;
    stylesInitialized = true;

    // 股票条目背景 - 涨 (红色边框)
    lv_style_init(&style_item_pos);
    lv_style_set_bg_color(&style_item_pos, lv_color_hex(0x330000));
    lv_style_set_bg_opa(&style_item_pos, LV_OPA_COVER);
    lv_style_set_radius(&style_item_pos, 10);
    lv_style_set_border_width(&style_item_pos, 2);
    lv_style_set_border_color(&style_item_pos, lv_color_hex(0xFF0000));
    lv_style_set_border_opa(&style_item_pos, LV_OPA_COVER);
    lv_style_set_border_side(&style_item_pos, LV_BORDER_SIDE_FULL);

// 股票条目背景 - 跌 (绿色边框)
    lv_style_init(&style_item_neg);
    lv_style_set_bg_color(&style_item_neg, lv_color_hex(0x002200));
    lv_style_set_bg_opa(&style_item_neg, LV_OPA_COVER);
    lv_style_set_radius(&style_item_neg, 10);
    lv_style_set_border_width(&style_item_neg, 2);
    lv_style_set_border_color(&style_item_neg, lv_color_hex(0x00FF00));
    lv_style_set_border_opa(&style_item_neg, LV_OPA_COVER);
    lv_style_set_border_side(&style_item_neg, LV_BORDER_SIDE_FULL);

    // 20像素标准白字 (名称、价格、跑马灯)
    lv_style_init(&style_text_white);
    lv_style_set_text_color(&style_text_white, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_text_white, FontManager::getInstance().getChineseFont20());

    // 14像素辅助字
    lv_style_init(&style_text_white_small);
    lv_style_set_text_color(&style_text_white_small, lv_color_hex(0xCCCCCC));
    lv_style_set_text_font(&style_text_white_small, FontManager::getInstance().getChineseFont14());

    lv_style_init(&style_text_yellow_small);
    lv_style_set_text_color(&style_text_yellow_small, lv_color_hex(0xFFFF00));
    lv_style_set_text_font(&style_text_yellow_small, FontManager::getInstance().getChineseFont14());

    lv_style_init(&style_text_green);
    lv_style_set_text_color(&style_text_green, lv_color_hex(0x00FF00));
    lv_style_set_text_font(&style_text_green, FontManager::getInstance().getChineseFont20());

    lv_style_init(&style_text_red);
    lv_style_set_text_color(&style_text_red, lv_color_hex(0xFF0000));
    lv_style_set_text_font(&style_text_red, FontManager::getInstance().getChineseFont20());
}

void StockUI::initTicker(lv_obj_t*& ticker, int y_pos, lv_color_t bg_color, lv_color_t text_color, lv_text_align_t align, lv_label_long_mode_t scroll_mode) {
    ticker = lv_label_create(_parent);
    lv_obj_set_size(ticker, 240, 21);
    lv_obj_set_pos(ticker, 0, y_pos);
    
    lv_obj_set_style_bg_color(ticker, bg_color, 0);
    lv_obj_set_style_bg_opa(ticker, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ticker, text_color, 0);
    lv_obj_set_style_text_font(ticker, FontManager::getInstance().getChineseFont20(), 0);
    
    lv_obj_set_style_pad_top(ticker, 0, 0); 
    
    lv_obj_set_scrollbar_mode(ticker, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_align(ticker, align, 0);
    lv_label_set_long_mode(ticker, scroll_mode);
    lv_label_set_text(ticker, "");
}

void StockUI::init(lv_obj_t* parent) {
    createStyles();
    _parent = parent;
    
    LVGL_LOCK();
    lv_obj_set_size(_parent, 240, 320);
    lv_obj_set_pos(_parent, 0, 0);
    lv_obj_set_style_bg_color(_parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_parent, 0, 0);
    lv_obj_set_style_radius(_parent, 0, 0);
    lv_obj_set_style_pad_all(_parent, 0, 0);
    lv_obj_set_scrollbar_mode(_parent, LV_SCROLLBAR_MODE_OFF);

    // 1. 顶栏
    _topBar = lv_obj_create(_parent);
    lv_obj_set_size(_topBar, 240, 30);
    lv_obj_set_pos(_topBar, 0, 4);//WIFI与时间坐标
    lv_obj_set_style_bg_color(_topBar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_topBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_topBar, 0, 0);
    lv_obj_set_style_pad_all(_topBar, 0, 0);
    lv_obj_set_scrollbar_mode(_topBar, LV_SCROLLBAR_MODE_OFF);

    _cityLabel = lv_label_create(_topBar);
    lv_obj_set_pos(_cityLabel, 0, 0);
    lv_obj_set_style_pad_all(_cityLabel, 0, 0);
    lv_obj_set_style_text_font(_cityLabel, FontManager::getInstance().getChineseFont20(), 0);
    lv_obj_add_style(_cityLabel, &style_text_white, 0);
    lv_label_set_text(_cityLabel, "");

    _weatherLabel = lv_label_create(_topBar);
    lv_obj_set_pos(_weatherLabel, 50, 3);
    lv_obj_set_width(_weatherLabel, 110);
    lv_obj_add_style(_weatherLabel, &style_text_white_small, 0); 
    lv_label_set_long_mode(_weatherLabel, LV_LABEL_LONG_DOT);
    lv_label_set_text(_weatherLabel, "");

    _timeLabel = lv_label_create(_topBar);
    lv_obj_align(_timeLabel, LV_ALIGN_TOP_RIGHT, -22, 5);
    lv_obj_add_style(_timeLabel, &style_text_white_small, 0);
    lv_label_set_text(_timeLabel, "");

    _tipsLabel = lv_label_create(_topBar);
    lv_obj_align(_tipsLabel, LV_ALIGN_TOP_RIGHT, 0, 8);
    lv_obj_set_style_text_align(_tipsLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_style(_tipsLabel, &style_text_white_small, 0);
    lv_obj_set_style_text_color(_tipsLabel, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(_tipsLabel, "");

    _wifiBars = nullptr;

    // 2. 跑马灯与汇率

initTicker(_ticker1, 28, lv_color_hex(0x00FFFF), lv_color_hex(0x0000FF), LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_SCROLL_CIRCULAR);
initTicker(_ticker2, 49, lv_color_hex(0xFF00FF), lv_color_hex(0xFFFF00), LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);
initTicker(_ticker3, 70, lv_color_hex(0x000011), lv_color_hex(0xFF00FF), LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);
initTicker(_ticker4, 91, lv_color_hex(0xFF0000), lv_color_hex(0xFFFFFF), LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_CLIP);

    // 3. 股票列表
    _stockList = lv_obj_create(_parent);
    lv_obj_set_size(_stockList, 240, 214);
    lv_obj_set_pos(_stockList, 0, 114);
    lv_obj_set_style_bg_color(_stockList, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_stockList, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_stockList, 0, 0);
    lv_obj_set_style_radius(_stockList, 0, 0);
    lv_obj_set_style_pad_all(_stockList, 0, 0); 
    lv_obj_set_style_pad_row(_stockList, 2, 0); 
    lv_obj_set_scrollbar_mode(_stockList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(_stockList, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < STOCK_PER_PAGE; i++) {
        _stockItems[i] = lv_obj_create(_stockList);
        lv_obj_set_size(_stockItems[i], lv_pct(100), 50); 
        lv_obj_set_style_pad_all(_stockItems[i], 0, 0);
        lv_obj_set_scrollbar_mode(_stockItems[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(_stockItems[i], LV_OBJ_FLAG_HIDDEN);

        // 股票代码：14px
        _itemCodes[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemCodes[i], 5, 5);
        lv_obj_add_style(_itemCodes[i], &style_text_yellow_small, 0);
        
        // 股票名：20px
        _itemNames[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemNames[i], 80, 2);
        lv_obj_add_style(_itemNames[i], &style_text_white, 0);

        // 价格：恢复 20px
        _itemPrices[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemPrices[i], 5, 22);
        lv_obj_add_style(_itemPrices[i], &style_text_white, 0);

        // 涨跌：20px左边
        _itemChanges[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemChanges[i], 80, 24);
        lv_obj_set_style_text_font(_itemChanges[i], FontManager::getInstance().getChineseFont20(), 0);

        // 箭头：右上角
        _itemArrows[i] = lv_label_create(_stockItems[i]);
        lv_obj_align(_itemArrows[i], LV_ALIGN_TOP_RIGHT, -5, 5);
        lv_obj_set_style_text_font(_itemArrows[i], FontManager::getInstance().getChineseFont20(), 0);

        // 货币单位：用lv_font_montserrat_8内置小号英文像素字体
        _itemCurrency[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemCurrency[i], 54, 24);
        lv_obj_set_style_text_font(_itemCurrency[i], &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(_itemCurrency[i], lv_color_hex(0x0088FF), 0);

        // 成交量：14px 黄色 (左移)
        _itemVolumes[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemVolumes[i], 145, 28);
        lv_obj_add_style(_itemVolumes[i], &style_text_yellow_small, 0);

        // 时间：14px 白色 (成交量后面)
        _itemTriangles[i] = lv_label_create(_stockItems[i]);
        lv_obj_set_pos(_itemTriangles[i], 195, 28);
        lv_obj_add_style(_itemTriangles[i], &style_text_white_small, 0);
    }

    _pageIndicator = lv_label_create(_parent);
    lv_obj_set_pos(_pageIndicator, 215, 28);
    lv_obj_add_style(_pageIndicator, &style_text_white_small, 0);
    lv_label_set_text(_pageIndicator, "1/1");

    // 等待提示：初始居中显示
    _waitingLabel = lv_label_create(_parent);
    lv_obj_set_size(_waitingLabel, 240, 320);
    lv_obj_set_pos(_waitingLabel, 0, 0);
    lv_obj_set_style_bg_color(_waitingLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_waitingLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(_waitingLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_waitingLabel, FontManager::getInstance().getChineseFont20(), 0);
    lv_obj_set_style_text_align(_waitingLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_waitingLabel, "等待数据更新...");
    lv_obj_align(_waitingLabel, LV_ALIGN_CENTER, 0, 0);

    // 初始隐藏ticker和股票列表
    lv_obj_add_flag(_ticker1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_ticker2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_ticker3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_ticker4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_stockList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_pageIndicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void StockUI::ensureVisible() {
    if (_parent) {
        lv_obj_clear_flag(_parent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_parent);
    }
}

void StockUI::show() {
    LVGL_LOCK();
    ensureVisible();
    LVGL_UNLOCK();
}

void StockUI::hide() {
    LVGL_LOCK();
    if (_parent) lv_obj_add_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void StockUI::hideAllItems() {
    for (int i = 0; i < STOCK_PER_PAGE; i++) {
        if (_stockItems[i]) lv_obj_add_flag(_stockItems[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void StockUI::showStockItem(int index, const char* code, const char* name, 
                            const char* price, const char* change, const char* volume, const char* time, const char* currency, bool isPositive) {
    if (index >= STOCK_PER_PAGE || !_stockItems[index]) return;

    lv_obj_t* item = _stockItems[index];
    lv_obj_clear_flag(item, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_style(item, &style_item_pos, LV_PART_MAIN);
    lv_obj_remove_style(item, &style_item_neg, LV_PART_MAIN);
    
    if (isPositive) {
        lv_obj_add_style(item, &style_item_pos, LV_PART_MAIN);
    } else {
        lv_obj_add_style(item, &style_item_neg, LV_PART_MAIN);
    }

    lv_obj_invalidate(item);

    lv_label_set_text(_itemCodes[index], code ? code : "");
    lv_label_set_text(_itemNames[index], name ? name : "");
    lv_label_set_text(_itemPrices[index], price ? price : "");
    lv_label_set_text(_itemCurrency[index], currency ? currency : "");  // 货币单位蓝色
    lv_obj_set_style_text_color(_itemCurrency[index], lv_color_hex(0x0088FF), 0);
    
    // 涨跌：百分比显示在左边
    lv_label_set_text(_itemChanges[index], change ? change : "");
    lv_obj_set_style_text_color(_itemChanges[index], 
        isPositive ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00), 0);
    
    // 箭头：右上角
    const char* arrow = isPositive ? LV_SYMBOL_UP : LV_SYMBOL_DOWN;
    lv_label_set_text(_itemArrows[index], arrow);
    lv_obj_set_style_text_color(_itemArrows[index], 
        isPositive ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00), 0);
    
    // 成交量（黄色）
    lv_label_set_text(_itemVolumes[index], volume ? volume : "");
    
    // 时间（白色，成交量后面）
    lv_label_set_text(_itemTriangles[index], time ? time : "");
}

void StockUI::updateStockPage() {
    if (_cachedGpData.isNull()) return;

    int displayCount = 0;
    int startIdx = _currentPage * STOCK_PER_PAGE;
    int currentMatch = 0;
    
    for (int i = 0; i < _cachedGpData.size() && displayCount < STOCK_PER_PAGE; i++) {
        JsonObject stock = _cachedGpData[i];
        int zd = stock["zd"] | 0;
        
        if (zd == 3) continue;

        if (currentMatch >= startIdx) {
            const char* name = stock["name"] | "";
            const char* price = stock["price"] | "";
            const char* change = stock["ch"] | "";
            const char* volume = stock["vo"] | "";
            const char* timeStr = stock["st"] | "";
            const char* currency = stock["cu"] | "";
            bool isPositive = (zd == 1); 

            char codeBuf[32] = {0};
            const char* ex = stock["ex"] | "";
            const char* co = stock["co"] | "";
            if (ex && strlen(ex) > 0) snprintf(codeBuf, sizeof(codeBuf), "[%s.%s]", ex, co);
            
            showStockItem(displayCount, codeBuf, name, price, change, volume, timeStr, currency, isPositive);
            displayCount++;
        }
        currentMatch++;
    }

    for (int i = displayCount; i < STOCK_PER_PAGE; i++) {
        lv_obj_add_flag(_stockItems[i], LV_OBJ_FLAG_HIDDEN);
    }

    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", _currentPage + 1, _totalPages);
    lv_label_set_text(_pageIndicator, pageStr);
}

void StockUI::nextPage() {
    if (_totalPages <= 1) return;
    _currentPage = (_currentPage + 1) % _totalPages;
    updateStockPage();
}

void StockUI::prevPage() {
    if (_totalPages <= 1) return;
    _currentPage = (_currentPage - 1 + _totalPages) % _totalPages;
    updateStockPage();
}

void StockUI::showWaiting() {
    LVGL_LOCK();
    ensureVisible();
    hideAllItems();
    // 显示居中的等待提示
    lv_obj_clear_flag(_waitingLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_stockList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_pageIndicator, LV_OBJ_FLAG_HIDDEN);
    LVGL_UNLOCK();
}

void StockUI::updateWindTemp(const char* text) {
    LVGL_LOCK();
    if (_weatherLabel) {
        lv_label_set_text(_weatherLabel, text);
    }
    LVGL_UNLOCK();
}

void StockUI::showTip(const char* tip) {
    LVGL_LOCK();
    if (_tipsLabel) {
        lv_label_set_text(_tipsLabel, tip);
    }
    LVGL_UNLOCK();
}

void StockUI::clearTip() {
    LVGL_LOCK();
    if (_tipsLabel) {
        lv_label_set_text(_tipsLabel, "");
    }
    LVGL_UNLOCK();
}

void StockUI::updateData(JsonObject* data) {
    LVGL_LOCK();
    if (!data || data->isNull()) {
        LVGL_UNLOCK();
        showWaiting();
        return;
    }

    lv_label_set_text(_cityLabel, (*data)["city"] | "");
    
    JsonObject now = (*data)["now"];
    if (!now.isNull()) {
        lv_label_set_text(_weatherLabel, now["temp"] | "");
    }

    // 隐藏等待提示，显示ticker和股票列表
    lv_obj_add_flag(_waitingLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_parent, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_topBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_ticker1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_ticker2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_ticker3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_ticker4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_stockList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_pageIndicator, LV_OBJ_FLAG_HIDDEN);
    
    lv_label_set_text(_ticker1, (*data)["yulu1"] | "");
    lv_label_set_text(_ticker2, (*data)["y2"] | "");
    lv_label_set_text(_ticker3, (*data)["y3"] | "");
    lv_label_set_text(_ticker4, (*data)["set"]["gp"][0]["title"] | "");
    
    lv_obj_set_style_bg_opa(_ticker1, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(_ticker2, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(_ticker3, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(_ticker4, LV_OPA_COVER, 0);

    JsonArray gpData = (*data)["set"]["gp"];
    if (!gpData.isNull()) {
        _cachedGpData = gpData;
        
        int validCount = 0;
        for (JsonVariant v : _cachedGpData) {
            if ((v["zd"] | 0) != 3) validCount++;
        }

        _totalPages = (validCount + STOCK_PER_PAGE - 1) / STOCK_PER_PAGE;
        if (_totalPages < 1) _totalPages = 1;
        _currentPage = 0;
        updateStockPage();
    }
    LVGL_UNLOCK();
}


void StockUI::clearContent() {
    LVGL_LOCK();
    hideAllItems();
    _cachedGpData = JsonArray();
    LVGL_UNLOCK();
}

void StockUI::updateNetStatus(bool wifiOk, bool mqttOk, const String& time) {
    LVGL_LOCK();
    
    if (!_topBar) {
        LVGL_UNLOCK();
        return;
    }
    
    if (_timeLabel) {
        lv_label_set_text(_timeLabel, time.c_str());
    }
    
    lv_color_t barColor;
    if (!wifiOk) {
        barColor = lv_color_hex(0xFF0000); // 红色：无网络
    } else if (!mqttOk) {
        barColor = lv_color_hex(0xFFFF00); // 黄色：WiFi OK 但 MQTT 连不上
    } else {
        barColor = lv_color_hex(0x00FF00); // 绿色：全部正常
    }
    
    // 优化方案：重用现有对象，避免频繁删除创建
    if (!_wifiBars) {
        // 首次创建
        _wifiBars = lv_obj_create(_topBar);
        if (!_wifiBars) {
            LVGL_UNLOCK();
            return;
        }
        
        lv_obj_set_size(_wifiBars, 20, 15); 
        lv_obj_align(_wifiBars, LV_ALIGN_TOP_RIGHT, 0, 5);
        lv_obj_set_style_bg_opa(_wifiBars, 0, 0);
        lv_obj_set_style_border_width(_wifiBars, 0, 0);
        lv_obj_set_style_pad_all(_wifiBars, 0, 0);
        lv_obj_set_scrollbar_mode(_wifiBars, LV_SCROLLBAR_MODE_OFF);
        
        // 创建4个信号条
        for (int i = 0; i < 4; i++) {
            lv_obj_t* bar = lv_obj_create(_wifiBars);
            if (!bar) continue;
            
            int h = 3 + (i * 3); 
            lv_obj_set_size(bar, 3, h);
            lv_obj_set_pos(bar, i * 5, 12 - h); 
            lv_obj_set_style_bg_color(bar, barColor, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_radius(bar, 1, 0);
        }
    } else {
        // 重用现有对象，只更新颜色
        int child_cnt = lv_obj_get_child_cnt(_wifiBars);
        for(int i = 0; i < child_cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(_wifiBars, i);
            if(child) {
                lv_obj_set_style_bg_color(child, barColor, 0);
            }
        }
    }
    
    LVGL_UNLOCK();
}