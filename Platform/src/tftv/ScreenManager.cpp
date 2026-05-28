#include "ScreenManager.h"
#include "AIUI.h"
#include "StockUI.h"
#include "Photos.h"
#include "LVGLGlobal.h"
#include <LittleFS.h>
#include "../ai/SpeechManager.h"

extern JsonObject globalWebData;

ScreenManager& ScreenManager::getInstance() {
    static ScreenManager instance;
    return instance;
}

ScreenManager::ScreenManager() 
    : _currentState(STATE_STOCK), _stateBeforeAI(STATE_STOCK), _aiContainer(nullptr), _stockContainer(nullptr),
      _photosContainer(nullptr), _lastAIInteractionTime(0), _initialized(false), _taskReady(false) {}

void ScreenManager::init() {
    _queue = xQueueCreate(15, sizeof(DisplayRequest));
    
    LVGL_LOCK();
    lv_obj_t* scr = lv_scr_act();
    
    _stockContainer = lv_obj_create(scr);
    lv_obj_set_size(_stockContainer, 240, 320);
    lv_obj_set_style_bg_color(_stockContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_stockContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_stockContainer, 0, 0);
    lv_obj_set_style_radius(_stockContainer, 0, 0);
    StockUI::get().init(_stockContainer);

    _aiContainer = lv_obj_create(scr);
    lv_obj_set_size(_aiContainer, 240, 320);
    lv_obj_set_style_bg_color(_aiContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_aiContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_aiContainer, 0, 0);
    lv_obj_set_style_radius(_aiContainer, 0, 0);
    lv_obj_set_scrollbar_mode(_aiContainer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(_aiContainer, LV_OBJ_FLAG_HIDDEN);
    AIUI::get().init(_aiContainer);
    
    _photosContainer = lv_obj_create(scr);
    lv_obj_set_size(_photosContainer, 240, 320);
    lv_obj_set_style_bg_color(_photosContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_photosContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_photosContainer, 0, 0);
    lv_obj_set_style_radius(_photosContainer, 0, 0);
    lv_obj_add_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
    Photos::getInstance().init(_photosContainer);
    
    _initialized = true;
    LVGL_UNLOCK();
}

void ScreenManager::sendRequest(DisplayRequest req) {
    if (_queue) xQueueSend(_queue, &req, 0);
}

// 适配 main.cpp 的调用
void ScreenManager::updateStockData(JsonObject data) {
    if (!_initialized) return;
    LVGL_LOCK();
    StockUI::get().updateData(&data); // 内部传地址给 StockUI
    LVGL_UNLOCK();
}

void ScreenManager::processRequest(const DisplayRequest& req) {
    LVGL_LOCK();

    switch (req.type) {
        case DisplayRequest::SWITCH_AI:
            // 仅在首次进入 AI 时保存之前的界面状态（防止重复 SWITCH_AI 覆盖为 STATE_AI_CHAT）
            if (_currentState != STATE_AI_CHAT) {
                _stateBeforeAI = _currentState;
            }
            lv_anim_del(_aiContainer, NULL); 
            lv_obj_add_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_aiContainer, LV_OBJ_FLAG_HIDDEN);
            _currentState = STATE_AI_CHAT;
            _lastAIInteractionTime = millis();
            break;

        case DisplayRequest::SWITCH_STOCK:
            lv_anim_del(_stockContainer, NULL); 
            lv_obj_add_flag(_aiContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
            _currentState = STATE_STOCK;
            Photos::getInstance().hide();
            break;

        case DisplayRequest::SWITCH_PHOTOS:
            lv_obj_add_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_aiContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
            _currentState = STATE_PHOTOS;
            Photos::getInstance().show();
            break;

        case DisplayRequest::SET_AI_STATUS:
            AIUI::get().setStatus(req.text);
            break;

        case DisplayRequest::SHOW_USER_TEXT:
            if (_currentState != STATE_AI_CHAT) {
                lv_obj_add_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(_aiContainer, LV_OBJ_FLAG_HIDDEN);
                _currentState = STATE_AI_CHAT;
            }
            AIUI::get().addBubble(req.text, true);
            _lastAIInteractionTime = millis();
            break;

        case DisplayRequest::SHOW_AI_TEXT:
            AIUI::get().addBubble(req.text, false);
            _lastAIInteractionTime = millis();
            break;

        case DisplayRequest::CLEAR_DIALOG:
            AIUI::get().clearDialog();
            break;
    }
    LVGL_UNLOCK();
}

bool ScreenManager::isPhotosDefault() {
    if (globalWebData.isNull() || !globalWebData.containsKey("set") || !globalWebData["set"].containsKey("dshow")) {
        return false;
    }
    String val = globalWebData["set"]["dshow"].as<String>();
    return val == "1";
}

bool ScreenManager::isStockDataStale() {
    if (globalWebData.isNull()) {
        return true;
    }
    if (!globalWebData.containsKey("set")) {
        return true;
    }
    if (!globalWebData["set"].containsKey("gp")) {
        return true;
    }
    
    JsonArray gp = globalWebData["set"]["gp"].as<JsonArray>();
    if (gp.isNull() || gp.size() == 0) return true;
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        return true;
    }
    int curHour = timeinfo.tm_hour;
    int curMin = timeinfo.tm_min;
    int curMins = curHour * 60 + curMin;
    
    // 找出最新（最大）的股票时间
    int maxStMins = 0;
    for (JsonObject stock : gp) {
        if (stock.containsKey("st")) {
            String st = stock["st"].as<String>();
            int colonIdx = st.indexOf(':');
            if (colonIdx > 0) {
                int stHour = st.substring(0, colonIdx).toInt();
                int stMin = st.substring(colonIdx + 1).toInt();
                int stMins = stHour * 60 + stMin;
                if (stMins > maxStMins) maxStMins = stMins;
            }
        }
    }
    
    int diff = curMins - maxStMins;
    bool isStale = diff < 0 || abs(diff) > 60;

    return isStale;
}

bool ScreenManager::hasPhotos() {
    #ifdef USE_LITTLEFS
    File root = LittleFS.open("/photo");
    if (!root || !root.isDirectory()) return false;
    int count = 0;
    while (File file = root.openNextFile()) {
        String name = file.name();
        if (name.endsWith(".jpg") || name.endsWith(".JPG")) {
            count++;
            break;
        }
    }
    return count > 0;
    #else
    return false;
    #endif
}

void ScreenManager::screenTask(void* p) {
    ScreenManager& sm = ScreenManager::getInstance();
    sm.init();
    
    delay(3000);
    
    // 初始化 _stateBeforeAI 为默认显示模式
    sm._stateBeforeAI = sm.isPhotosDefault() ? STATE_PHOTOS : STATE_STOCK;
    
    bool isPhotosMode = sm.isPhotosDefault();
    if (!isPhotosMode && globalWebData.containsKey("set")) {
        String dshow = globalWebData["set"]["dshow"].as<String>();
        if (dshow == "3" && sm.isStockDataStale() && sm.hasPhotos()) {
            isPhotosMode = true;
            sm._stateBeforeAI = STATE_PHOTOS;
        }
    }
    
    if (isPhotosMode) {
        LVGL_LOCK();
        lv_obj_add_flag(sm._stockContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sm._photosContainer, LV_OBJ_FLAG_HIDDEN);
        sm._currentState = STATE_PHOTOS;
        Photos::getInstance().show();
        LVGL_UNLOCK();
    }
    
    // 标记 screenTask 初始化完成
    sm._taskReady = true;

    static unsigned long lastPhotoSwitch = 0;
    
    while (true) {
        DisplayRequest req;
        if (xQueueReceive(sm._queue, &req, pdMS_TO_TICKS(5)) == pdPASS) {
            sm.processRequest(req);
        }
        
        if (sm._currentState == STATE_PHOTOS) {
            if (millis() - lastPhotoSwitch > 5000) {
                lastPhotoSwitch = millis();
                Photos::getInstance().nextPhoto();
            }
        }
        
        if (sm._currentState == STATE_AI_CHAT) {
            int remaining = 30 - (millis() - sm._lastAIInteractionTime) / 1000;
            if (remaining > 0) {
                AIUI::get().setCountdown(remaining);
            }
            if (remaining <= 0) {
                AIUI::get().setCountdown(0);
                // 恢复 SpeechManager 状态，确保可以重新唤醒
                SpeechManager::getInstance().resetToIdle();
                // 恢复到进入 AI 前的界面
                if (sm._stateBeforeAI == STATE_PHOTOS) {
                    sm.switchToPhotos();
                } else {
                    sm.switchToStock();
                }
                // 清除倒计时，防止重复触发
                sm._lastAIInteractionTime = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ScreenManager::checkAndSwitchScreen() {
    // 只有在 screenTask 初始化完成后才执行
    if (!_taskReady) {
        return;
    }
    if (!_initialized) return;
    
    // 只在非 AI 对话状态下检查
    if (_currentState == STATE_AI_CHAT) return;
    
    // 检查 dshow 配置
    String dshowVal = "none";
    if (globalWebData.containsKey("set") && globalWebData["set"].containsKey("dshow")) {
        dshowVal = globalWebData["set"]["dshow"].as<String>();
    }
    
    bool shouldShowPhotos = isPhotosDefault();
    bool stockStale = false;
    bool hasPhoto = false;
    
    if (!shouldShowPhotos && globalWebData.containsKey("set")) {
        if (dshowVal == "3") {
            stockStale = isStockDataStale();
            hasPhoto = hasPhotos();

            if (stockStale && hasPhoto) {
                shouldShowPhotos = true;
            }
        }
    }
    

    
    // 无论屏幕是否切换，都需要更新 _stateBeforeAI 为当前配置对应的正确状态
    DisplayState targetState = shouldShowPhotos ? STATE_PHOTOS : STATE_STOCK;
    if (_stateBeforeAI != targetState) {
        _stateBeforeAI = targetState;
    }
    
    // 当前不是相册模式但配置要求显示相册
    if (shouldShowPhotos && _currentState != STATE_PHOTOS) {
        LVGL_LOCK();
        lv_obj_add_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
        _currentState = STATE_PHOTOS;
        Photos::getInstance().show();
        LVGL_UNLOCK();
    }
    // 当前是相册模式但配置不再要求显示相册
    else if (!shouldShowPhotos && _currentState == STATE_PHOTOS) {
        LVGL_LOCK();
        lv_obj_add_flag(_photosContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_stockContainer, LV_OBJ_FLAG_HIDDEN);
        _currentState = STATE_STOCK;
        Photos::getInstance().hide();
        LVGL_UNLOCK();
    }
}