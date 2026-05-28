#include <Arduino.h>
#include "utils/pins.h"
#include "utils/network_config.h"
#include "BLE.h"
#include "function.h"
#include "WiFiMQTT.h"
#if defined(USE_AI) || defined(USE_STOCK2)
#include "tftv/ScreenManager.h"
#include "tftv/StockUI.h"
#include "tftv/LVGLGlobal.h"
#endif
#ifdef USE_AI
#include "tftv/MicIndicator.h"
#endif
#ifdef USE_SMARTLOCK
#include "dev_lock/SmartLock.h"
#endif
#include <ArduinoJson.h>

StaticJsonDocument<JSON_DOC_SIZE> globalDoc;
JsonObject globalWebData = globalDoc.to<JsonObject>();
SemaphoreHandle_t globalDataMutex = xSemaphoreCreateMutex();

ApiParam apiParams[] = {
  { "type", "" }, { "pwd", "" }, { "way", "" }, { "", "" }
};

bool firmupdate = false;
unsigned long last_sync_WebUpdate = 0;
unsigned long last_saas_WebUpdate = 0;
unsigned long lastWindTempUpdate = 0;
unsigned long lastStatusCheck = 0;
unsigned long apModeStartTime = 0;
bool apRestartInitialized = false;
uint8_t lastStationCount = 0;
uint8_t staFailureCount = 0;
NetworkState currentNetworkState = NET_STATE_INIT;
QueueHandle_t syncQueue = NULL;
QueueHandle_t photoQueue = NULL;
String pendingPsv = "";

unsigned long lastConnectionCheck = 0;
unsigned long apModeLastRetryTime = 0;
int errlinkCount = 0;
const NetworkConfig netConfig = {
  "run.yodin.com", 
  1883, 
  "test111", 
  "123456",
  "http://mqtt.yodin.com/loT/Maccode/receivemac/",
  "http://mqtt.yodin.com/loT/Maccode/getmacdata/",
  DEV_MQTT_TOPIC,  // 自动切换
  DEV_AP_NAME,     // 自动切换
  DEV_VER,
  "1",
  DEV_FIREURL,
  DEV_BLUE_NAME    // 自动切换
};

#include "NetworkTask.h"

#ifdef USE_AUDIO
  #include "AudioPlayer.h"
#endif

#ifdef USE_AI
  #include "tftv/Display.h"
  #include "ai/SpeechManager.h"
  TaskHandle_t displayTaskHandle = NULL;
  Display& display = Display::getInstance();
#endif

#ifdef USE_SD_CARD
  #include "utils/SDCardManager.h"
  SDCardManager sdCard(SD_CS, SD_SCK, SD_MISO, SD_MOSI);
#endif

#ifdef USE_SMARTLOCK
  #include "dev_lock/SmartLock.h"
  extern PasswordManager passwordManager;
  String enteredPassword = "";
#endif

#ifdef USE_LIGHTS
  #include "dev_led/LightControl.h"
  #include "dev_led/LightTask.h" // 新建灯光任务文件
  LightControl lights;
#endif

#ifdef USE_STOCK
#include "tftd/Display.h"
#include "dev_stock/DispGP.h"
int currentPage = 0;                    //股票显示分页
const char* exchangerate;              //汇率
const char* scrolling2; const char* scrolling3; const char* scrolling4; 
ScrollState state1;
ScrollState state2;
ScrollState state3;
ScrollState state4;
#endif

#ifdef USE_BOOKSLOT
#include "dev_bookslot/switch2route.h"
#endif

#ifdef USE_CAMERA
#include "cam/camera.h"
#endif

void setup() {
    // 统一初始化继电器（最优先，防止启动瞬间闪烁）
    #if defined(USE_BOOKSLOT) || defined(USE_LIGHTS)
    pinMode(RELAY1_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);
    digitalWrite(RELAY1_PIN, LOW);
    digitalWrite(RELAY2_PIN, LOW);
    delay(100); // 等待继电器稳定
    Serial.println("[RELAY] 已初始化为安全状态");
    #endif
    // 初始化Serial（所有情况都需要）
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial.begin(115200, SERIAL_8N1, 44, 43);
    #elif defined(CONFIG_IDF_TARGET_ESP32)
    Serial.begin(115200);
    #endif
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("上次重启原因: %d\n", reason);
    Serial.println(F("=== SETUP START ==="));
    delay(30);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(0, INPUT_PULLUP);     // GPIO0上拉(确保启动模式) INPUT_PULLUP  INPUT_PULLDOWN
    pinMode(PCF_INT_PIN, OUTPUT);
    #ifdef USE_SMARTLOCK
    pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
    pinMode(DOOR_OPEN_PIN, INPUT_PULLUP);
    #endif
    #ifdef USE_STOCK
    digitalWrite(PCF_INT_PIN, HIGH);
    #endif
    #ifdef USE_BOOKSLOT
    // 先设置所有敏感引脚为安全状态
    pinMode(13, INPUT);           // GPIO13高阻态
    #endif
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();    
    Serial.printf("[SYSTEM] 唤醒原因: %d\n", wakeup_reason);
    
    // 设置时区为中国标准时间 (UTC+8)
    const char* timezoneStr = "CST-8";
    setenv("TZ", timezoneStr, 1);
    tzset();

    // 从 RTC 恢复时间锚点（Deep Sleep 唤醒后立即恢复）
    restoreTimeFromRTC();

    Serial.printf("[TIME] 时区设置: %s\n", getenv("TZ"));
    
    // 注意：不要在此处调用 updateRTCSystemTime()！
    // 它会用 restoreTimeFromRTC 恢复的旧时间覆盖 sleepTimestamp，
    // 导致 compensateSystemTime() 时间补偿计算错误。
    // updateRTCSystemTime 应该在正常启动并 NTP 同步后再调用。

    // 检查是否需要继续深度睡眠
    // 【关键】checkDeepSleepFromRTC() 返回 true 表示仍在睡眠时段，
    //        必须回去睡觉，绝不允许落空到后续的联网/继电器逻辑
    if (checkDeepSleepFromRTC()) {
        unsigned long secondsToSleep = 300; // 默认安全值：5分钟后再醒来检查
        
        // 直接用 time() + localtime_r()，不依赖 getLocalTime(timeout=0) 的年份校验
        // checkDeepSleepFromRTC() 已验证过时间有效性并做了补偿
        time_t nowSec;
        time(&nowSec);
        struct tm sleepTm;
        localtime_r(&nowSec, &sleepTm);
        
        if (nowSec >= 1704067200) {  // 2024-01-01，时间有效
            TimeHM endTime = {(uint8_t)sleepEndHour, (uint8_t)sleepEndMinute};
            secondsToSleep = getRemainingSeconds(sleepTm, endTime);
            Serial.printf("[SLEEP] 当前 %02d:%02d:%02d，计算剩余睡眠: %lu秒\n",
                sleepTm.tm_hour, sleepTm.tm_min, sleepTm.tm_sec, secondsToSleep);
        } else {
            Serial.println(F("[SLEEP] ⚠️ 系统时间无效，使用安全睡眠时间300秒"));
        }
        
        if (secondsToSleep > 60) {
            Serial.println(F("[SLEEP] 继续深度睡眠"));
            enterDeepSleepSegmented(secondsToSleep);
            // enterDeepSleepSegmented 不会返回，以下是保险
        } else if (secondsToSleep > 0) {
            // 剩余不足1分钟但仍在睡眠区间，睡最后这段
            Serial.printf("[SLEEP] 剩余%lu秒，最后一段睡眠\n", secondsToSleep);
            enterDeepSleepSegmented(secondsToSleep);
        } else {
            Serial.println(F("[SLEEP] 睡眠时间已结束，正常唤醒"));
            sleepTimestamp = 0;
            sleepDurationSeconds = 0;
        }
    }
    // 先初始化基本系统功能
    initSystem();
    initSpiffs();
#ifdef USE_STOCK
    pinMode(TFT_BL, OUTPUT);
    initDisplay(); // 立即初始化屏幕
    displayChineseString("元气满满 好运加载中...", 25, 90, TFT_RED, NO_BACKGROUND, 1, 240);
#endif
    for (int i = 400; i <= 1000; i += 120) {
        buzzz(i, 80);
    }
    delay(200);
    buzzz(1200, 300);buzzz(1800, 100);buzzz(2200, 50);
    Serial.println(F(">>> 0. 调用 initNetwork 前"));
    initNetwork(netConfig);
    Serial.println(F(">>> 0b. initNetwork 已完成"));
    getMacAddress();digitalWrite(PCF_INT_PIN, LOW);delay(100);
    
    // 创建同步队列用于网络任务和主循环的通信
    syncQueue = xQueueCreate(10, sizeof(QueuedCmd));
    if (syncQueue == NULL) {
        Serial.println(F("❌ 同步队列创建失败"));
    }

    // 创建照片下载队列
    photoQueue = xQueueCreate(20, sizeof(char*));
    if (photoQueue == NULL) {
        Serial.println(F("❌ 照片下载队列创建失败"));
    }

#ifdef USE_STOCK
  delay(100);
  tft.fillRect(0, 35, 240, 78, TFT_BLACK);
  tft.setCursor(88, 78);
  tft.setTextFont(1); 
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("Loading...");
#endif

    Serial.println(F(">>> 1. 调用 initBluetooth 前"));
    initBluetooth(macAddress, netConfig.bluename);    
    Serial.println(F(">>> 2. initBluetooth 已完成"));
    // 内存监控
    // Serial.printf("[MEM] 初始化后可用内存: %lu bytes\n", ESP.getFreeHeap());
    // Serial.printf("[MEM] 最大可分配块: %lu bytes\n", ESP.getMaxAllocHeap());
    
    xTaskCreatePinnedToCore(NetworkMaintenanceTask, "NetTask", NET_STACK_SIZE, NULL, 1, NULL, 0);

#ifdef USE_SD_CARD
    sdCard.begin(); // 1. 启动初始化任务（内部会开启 spi 并尝试连接卡）
    sdCard.startHotplugDetection(); // 2. 开启热插拔检测 (如果在构造函数里传了检测引脚，建议开启)
    sdCard.startHeartbeatCheck(30000); // 30秒检查一次 // 3. 开启心跳检测 (防止卡死或非正常的连接丢失)
    sdCard.requestInit();// 4. 发送立即初始化请求 (触发一次扫描)
#endif


#ifdef USE_LIGHTS
    lights.begin();
    lights.clearAll(); // 立即清除所有像素，防止启动微亮
    xTaskCreatePinnedToCore(LightAnimationTask, "LightTask", 4096, NULL, 1, NULL, 1);
#endif

#ifdef USE_AUDIO
    pinMode(AMP_SD, OUTPUT);
    //digitalWrite(AMP_SD, HIGH);     
    AudioPlayer::getInstance().begin(I2S_BCK, I2S_WS, I2S_DOUT); 
    xTaskCreatePinnedToCore(musicTask, "MusicTask", 6144, NULL, 2, NULL, 1);
    //AudioPlayer::getInstance().play("/music.mp3", SOURCE_SD_CARD);=> //PLAY_LOCAL("/music.mp3");
    PLAY_LOCAL("/start.wav");
#endif

#ifdef USE_AI
    Display::InitStatus status = display.begin(0);
    if (status == Display::SUCCESS) {
        LVGL_LOCK();
        // 终极测试：绕过LVGL，直接使用TFT库显示JPEG
        Serial.println("🎯 终极测试：直接显示JPEG图片");
        // if (display.displayLocalJPG("/test.jpg", 0, 0)) {
        //     Serial.println("✅ JPEG显示成功");
        // } else {
        //     Serial.println("❌ JPEG显示失败");
        //     // 备用方案：显示BMP
        //     drawBmp("/a.bmp",0,0,false);
        // }
        // drawBmp("/a.bmp",0,0,false);
        display.displayLocalJPG("/a.jpg", 0, 0);
        LVGL_UNLOCK();
    }
    // 启动浮动麦克风收音指示器（屏幕顶部 240x8 横条）
    MicIndicator::get().init();

    SpeechManager::getInstance().init();
    delay(1000);// 关键：启动后延迟一秒（等待电源和传感器稳定），然后清空音频缓冲
    MicModule::getInstance().stopListeningImmediately(); 
    Serial.println(F("🚀 系统就绪，已重置音频采样，等待唤醒..."));
    SpeechManager::getInstance().start();
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 8192, NULL, 1, &displayTaskHandle, 1);
    delay(200);
    xTaskCreatePinnedToCore(
    ScreenManager::screenTask, 
    "ScreenMgr", 
    8192, 
    NULL, 
    1, 
    NULL, 
    0
);
    delay(500);
#endif

#ifdef USE_CAMERA
    Serial.print(F("等待 Wi-Fi 连接获取 IP..."));
    int retry = 0;
    // 等待最多 10 秒
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500);
        Serial.print(F("."));
        retry++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("\n✅ Wi-Fi 已就绪"));
        setup_camera(); 
    } else {
        Serial.println(F("\n❌ Wi-Fi 连接超时，请检查配置"));
    }
#endif

#ifdef USE_SMARTLOCK
  keyboardManager.setup();
  passwordManager.begin();
  lockActuator.init();
  smartLock.begin(passwordManager, lockActuator, lockManager);
#endif

delay(200);

    // 最终安全检查：确保继电器在进入 loop 前关闭
    #if defined(USE_BOOKSLOT) || defined(USE_LIGHTS)
    digitalWrite(RELAY1_PIN, LOW);
    digitalWrite(RELAY2_PIN, LOW);
    Serial.println("[RELAY] 最终安全检查完成");
    #endif
}


void loop() {
    unsigned long current = millis();
    static unsigned long lastPoll = 0;
    static unsigned long lastNtpSync = 0;

    // 0. 每小时检查一次 NTP 同步（后台运行，不阻塞）
    if (lastNtpSync == 0 || current - lastNtpSync > 3600000) {
        if (WiFi.status() == WL_CONNECTED) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 1000)) {
                saveTimeToRTC(mktime(&timeinfo));
                Serial.println("[TIME] NTP同步成功，已更新锚点");
            }
        }
        lastNtpSync = current;
    }

    // 1. AI 语音状态检查（需高频轮询：TTS debounce 800ms，间隔过长会漏检短语音播完）
    #ifdef USE_AI
    if (current - lastPoll >= 100) {
        lastPoll = current;
        SpeechManager::getInstance().poll();
    }
    #endif

    // 2. 异步命令处理 (使用队列是正确的做法)
    QueuedCmd cmd;
    if (syncQueue != NULL && xQueueReceive(syncQueue, &cmd, 0) == pdTRUE) {
        processQueuedCmd(cmd);
    }

    // 3. 门锁逻辑：需要实时性，放在靠前位置
    #ifdef USE_SMARTLOCK
    lockManager.check();
    handleDoorLockLogic();
    keyboardManager.scan(enteredPassword);
    if (enteredPassword.length() >= 6) {
        CLEAR_AUDIO_QUEUE();
        STOP_AUDIO();
        smartLock.processPasswordInput(enteredPassword);
    }
    #endif

    // 4. 数据同步：定时检查
    unsigned long syncInterval = 180000; // 默认3分钟
    
    #ifdef USE_SMARTLOCK
    // 智能锁状态下每8小时同步一次数据
    syncInterval = 28800000;
    #endif
    // 可以在这里添加其他设备类型的判断
    // #ifdef DEVICE_TYPE_B
    // syncInterval = 3600000; // 1小时
    // #endif
    
    if (last_sync_WebUpdate == 0 || (current - last_sync_WebUpdate >= syncInterval && !firmupdate)) {
        Serial.println(F(">>> CALLING sync_WebData FROM LOOP"));
        
        // 使用全局数据锁保护 JSON 数据的读取和更新
        if (xSemaphoreTake(globalDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            sync_WebData();
            
            #ifdef USE_AI
            // 股票模块：WiFi连接时才更新
            if (WiFi.status() == WL_CONNECTED) {
                if (!globalWebData.isNull() && globalWebData.containsKey("timestamp")) {
                    // 在锁内调用 UI 更新，确保原子性
                    ScreenManager::getInstance().updateStockData(globalWebData);
                    // 数据同步后检查是否需要切换屏幕
                    ScreenManager::getInstance().checkAndSwitchScreen();
                }
            }
            #endif
            
            xSemaphoreGive(globalDataMutex);
        }
        
        last_sync_WebUpdate = current;
        
        // 更新时间（仅在 NTP 同步后才覆盖，避免无效值触发误报警）
        static char hourmin[6] = "00:00";
        if (getLocalTime(&timeinfo, 0)) {
            snprintf(hourmin, sizeof(hourmin), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        }
        
    }

    // 5. 屏幕状态/页面切换更新 (15秒一次)
    if ((lastStatusCheck == 0 || current - lastStatusCheck > 28000) && !firmupdate) {

        //Serial.printf("可用堆内存: %d\n", ESP.getFreeHeap());
        lastStatusCheck = current;

        // 安全读取全局数据
        if (xSemaphoreTake(globalDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            bool dataValid = !globalWebData.isNull() && globalWebData.containsKey("timestamp");
                char hourmin[6] = "00:00";
                bool timeOk = getLocalTime(&timeinfo, 0);
            if (dataValid) {
                if (timeOk) {
                    snprintf(hourmin, sizeof(hourmin), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                }
                JsonArray alarm_time = globalWebData["set"]["alarm_time"].as<JsonArray>();
                alarm_buzz(alarm_time, hourmin);

                // 更新股票界面 - 数据已在上面的sync_WebData中更新,这里只更新时间
                #ifdef USE_AI
                    if (ScreenManager::getInstance().hasStockUI()) {
                        Serial.print(F(">"));
                        #ifdef USE_AUDIO
                        if (!AudioPlayer::getInstance().isPlaying()) {
                            StockUI::get().updateNetStatus(WiFi.status() == WL_CONNECTED, client.connected(),hourmin);
                        }
                        #else
                        StockUI::get().updateNetStatus(WiFi.status() == WL_CONNECTED, client.connected(),hourmin);
                        #endif
                        StockUI::get().clearTip();
                    }
                #endif

                #ifdef USE_STOCK
                    if (lastStatusCheck != 0) currentPage++;             
                    dGPscreen2(globalWebData, currentPage, WiFi.status() == WL_CONNECTED, client.connected(), hourmin, macAddress);
                #endif

                #ifdef USE_BOOKSLOT
                    checkRelay(getUTCTimestamp());           // 立即执行的继电器
                    checkRelayWithDelay(getUTCTimestamp());  // 延迟执行的继电器
                #endif
            }
            xSemaphoreGive(globalDataMutex);            
        }
    }


                // 6. 高频视觉刷新：滚动字幕 (需要加锁保护数据读取)
                #ifdef USE_STOCK
                if (!firmupdate && xSemaphoreTake(globalDataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    if (!globalWebData.isNull() && globalWebData.containsKey("timestamp")) {
                        const char* exRate = globalWebData["set"]["gp"][0]["title"] | "";
                        const char* sc2 = globalWebData["yulu1"] | "";
                        const char* sc3 = globalWebData["y2"] | "";
                        const char* sc4 = globalWebData["y3"] | "";

                        // 注意：滚动函数内部不应包含阻塞性 delay
                        scrollingChineseTicker(exRate, 36, TFT_WHITE, NO_BACKGROUND, 1, state1);
                        scrollingChineseTicker(sc2, 55, 0x0000, 0x07FF, 1, state2);
                        scrollingChineseTicker(sc3, 74, TFT_WHITE, 0x041F, 1, state3);
                        scrollingChineseTicker(sc4, 93, 0xFFE0, 0xBC40, 1, state4);
                    }
                    xSemaphoreGive(globalDataMutex);
                }
                #endif


    #ifdef USE_AI
    if (ScreenManager::getInstance().hasStockUI() && current - lastWindTempUpdate >= 3000) {
            lastWindTempUpdate = current;
            if (!globalWebData.isNull()) {
                JsonObject now = globalWebData["now"];
                if (!now.isNull()) {
                    static int toggle = 0;
                    toggle = (toggle + 1) % 2;
                    const char* wt = toggle == 0 ? now["temp"] | "" : now["wind"] | "";
                    StockUI::get().updateWindTemp(wt);
                }
            }
    }
    #endif

    // 稍微释放 CPU 给其他任务 (如果是多任务环境，yield 或小延时是必要的)
    yield(); 
    delay(50);
}