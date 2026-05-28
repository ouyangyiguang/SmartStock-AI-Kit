#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <ArduinoJson.h>
#include "Photos.h"
#include "LVGLGlobal.h"

enum DisplayState { STATE_STOCK, STATE_AI_CHAT, STATE_PHOTOS };

struct DisplayRequest {
    enum Type { 
        SWITCH_AI, 
        SWITCH_STOCK, 
        SHOW_USER_TEXT, 
        SHOW_AI_TEXT, 
        CLEAR_DIALOG,
        SET_AI_STATUS,
        SWITCH_PHOTOS
    } type;
    char text[256]; 
};

class ScreenManager {
public:
    static ScreenManager& getInstance();
    void init();
    
    // 发送 UI 请求至队列
    void sendRequest(DisplayRequest req);
    
    // --- 兼容 SpeechManager.cpp 的旧调用接口 ---
    void switchToAI() { sendRequest({DisplayRequest::SWITCH_AI}); }
    void switchToStock() { sendRequest({DisplayRequest::SWITCH_STOCK}); }
    void switchToPhotos() { sendRequest({DisplayRequest::SWITCH_PHOTOS}); }
    bool isPhotosDefault();
    bool hasStockUI() { return _stockContainer != nullptr && _initialized; }
    bool isStockDataStale();
    bool hasPhotos();
    
    void showAIUserText(const char* t) { 
        DisplayRequest r = {DisplayRequest::SHOW_USER_TEXT};
        strncpy(r.text, t, 255);
        sendRequest(r);
    }
    
    void showAIResponse(const char* t) {
        DisplayRequest r = {DisplayRequest::SHOW_AI_TEXT};
        strncpy(r.text, t, 255);
        sendRequest(r);
    }
    
    // 适配 showAIState(status, text, color1, color2)
    void showAIState(const char* status, const char* unused = "", uint32_t c1 = 0, uint32_t c2 = 0) {
        DisplayRequest r = {DisplayRequest::SET_AI_STATUS};
        strncpy(r.text, status, 255);
        sendRequest(r);
    }
    
    void updateAIInteraction() { _lastAIInteractionTime = millis(); }

    // 清除 AI 倒计时（切换屏幕时调用，防止 AI 超时返回）
    void clearAICountdown() { _lastAIInteractionTime = millis() - 30000; }

    // 设置 AI 倒计时时间（用于延迟切换）
    void setAICountdown(unsigned long delayMs) { _lastAIInteractionTime = millis() - 30000 + delayMs; }

    // 数据同步后检查是否需要切换屏幕
    void checkAndSwitchScreen();

    // --- 修复 main.cpp:347 的报错：改为接收 JsonObject 引用 ---
    void updateStockData(JsonObject data); 

    DisplayState getState() { return _currentState; }
    uint32_t getLastTime() { return _lastAIInteractionTime; }
    void setStateBeforeAI(DisplayState state) { _stateBeforeAI = state; }

    // 供 main.cpp 创建任务使用
    static void screenTask(void* p);

private:
    ScreenManager();
    void processRequest(const DisplayRequest& req);

    DisplayState _currentState;
    DisplayState _stateBeforeAI;  // 进入 AI 前的界面状态
    lv_obj_t* _aiContainer;
    lv_obj_t* _stockContainer;
    lv_obj_t* _photosContainer;
    QueueHandle_t _queue;
    uint32_t _lastAIInteractionTime;
    bool _initialized;
    bool _taskReady = false;
};

#endif