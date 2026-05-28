#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "function.h"
#include "utils/network_config.h"
#include "AudioPlayer.h"
#include <LittleFS.h>
#include <HTTPClient.h>
extern void displayAPMode(const String& apIP, const String& apName, const String& macAddress);
extern void displayChineseString(const char* text, int x, int y, uint32_t textcolor, uint32_t textbgcolor, int textSize, int hlineSize);
// 告诉编译器，这些变量在 main.cpp 里定义了，这里只是拿来用
extern StaticJsonDocument<JSON_DOC_SIZE> globalDoc;
extern JsonObject globalWebData;
extern SemaphoreHandle_t globalDataMutex;
extern bool firmupdate;
extern unsigned long last_sync_WebUpdate;
extern unsigned long lastStatusCheck;
extern QueueHandle_t syncQueue;
extern ApiParam apiParams[];
extern const NetworkConfig netConfig;
extern unsigned long apModeStartTime; 
extern bool apRestartInitialized;
extern uint8_t lastStationCount;
extern NetworkState currentNetworkState;
extern uint8_t staFailureCount;
extern int errlinkCount;
extern String macAddress;
extern unsigned long lastConnectionCheck;
extern unsigned long apModeLastRetryTime;
extern QueueHandle_t photoQueue;
extern String pendingPsv;
struct QueuedCmd {
    int cmd;
    char filename[32];
};

void processQueuedCmd(const QueuedCmd& cmd);

#ifdef USE_SMARTLOCK
  #include "dev_lock/SmartLock.h"
  extern PasswordManager passwordManager;
#endif


// ========================================================
// 辅助函数：统一处理 Serial 打印和 TFT 显示
// ========================================================
void displayNetworkStatus(const char* serialMsg) {
    if (serialMsg) {
      Serial.println(serialMsg);
    QueuedCmd startSync = {5, ""};
    if (serialMsg) {
        strncpy(startSync.filename, serialMsg, 31);
        startSync.filename[31] = '\0';
    }
    if (syncQueue != NULL) {
        if (xQueueSend(syncQueue, &startSync, 0) == pdTRUE) {
        }
    }

    }
}

// 辅助函数：下载图片到 /photo/ 目录
//宝塔 中 # 增加例外：如果是 f.tzzs.com，强制设置 $isRedcert 为 1 (即不跳转)
    // if ($host = "f.tzzs.com") {
    //     set $isRedcert 1;
    // }
    //防盗链设置中，f.tzzs.com 已经被设置为不跳转，所以这里直接下载即可，不需要特殊处理
inline bool downloadPhoto(const char* filename) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    
    String url = "http://f.tzzs.com/storage/lot/" + globalWebData["set"]["mci"].as<String>() + "/" + String(filename);
    
    if (!http.begin(url)) return false;
    http.addHeader("Referer", "http://f.tzzs.com/");
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    
    File file = LittleFS.open("/photo/" + String(filename), FILE_WRITE);
    if (!file) {
        http.end();
        return false;
    }
    
    bool ok = http.writeToStream(&file) >= 0;
    file.close();
    http.end();
    
    return ok;
}

// ========================================================
// 2. 后台下载任务
// ========================================================
inline void PhotoDownloadTask(void *pvParameters) {
    char* fileNamePtr;
    bool batchSuccess = true; // 新增：标记整批是否成功

    while (true) {
        if (xQueueReceive(photoQueue, &fileNamePtr, portMAX_DELAY) == pdTRUE) {
            String fName = String(fileNamePtr);
            
            // 只有下载成功才继续
            if (!downloadPhoto(fName.c_str())) {
                batchSuccess = false; // 标记失败
                Serial.printf("[PhotoTask] 警告：下载失败 %s\n", fName.c_str());
            }
            
            free(fileNamePtr);
            
            // 队列空了才尝试更新
            if (uxQueueMessagesWaiting(photoQueue) == 0 && pendingPsv.length() > 0) {
                if (batchSuccess) {
                     Serial.println("[PhotoTask] 所有任务成功，更新版本号...");
                     File vFile = LittleFS.open("/psv.txt", "w");
                     if (vFile) {
                         vFile.print(pendingPsv);
                         vFile.close();
                     }
                } else {
                     Serial.println("[PhotoTask] 部分文件下载失败，版本号未更新，等待下轮重试...");
                }
                pendingPsv = "";
                batchSuccess = true; // 重置
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 辅助函数：针对 /photo/ 目录的同步逻辑
inline void syncPhotos(JsonArray psl, String newPsv) {
    File dir = LittleFS.open("/photo");
    if (dir) {
        File file = dir.openNextFile();
        while (file) {
            String name = String("/photo/") + file.name();
            file.close();
            LittleFS.remove(name);
            dir = LittleFS.open("/photo");
            file = dir.openNextFile();
        }
        dir.close();
    }

    for (JsonVariant v : psl) {
        String fileName = v.as<String>();
        unsigned long downloadStart = millis();
        while (millis() - downloadStart < 60000) {
            if (downloadPhoto(fileName.c_str())) break;
            delay(1000);
        }
        File vf = LittleFS.open("/photo/" + fileName, "r");
        if (vf) {
            Serial.printf("[PhotoDL] %s: %d bytes\n", fileName.c_str(), vf.size());
            vf.close();
        }
    }

    File vFile = LittleFS.open("/psv.txt", "w");
    if (vFile) {
        vFile.print(newPsv);
        vFile.close();
    }
}

void sync_WebData() {
    
    if (WiFi.status() == WL_CONNECTED){
        String response = "";
        strncpy(apiParams[0].value, "", sizeof(apiParams[0].value) - 1);
        apiParams[0].value[sizeof(apiParams[0].value) - 1] = '\0';
        response = getJsonData(macAddress, apiParams);
          if (response.length() > 0 && isValidJSON(response)) {
            globalWebData = parseJSON(response);
            loadSchedule("office_hours", schedule);
            
            
            if (globalWebData.containsKey("timestamp")) {
                unsigned long timestamp = globalWebData["timestamp"];
                if (timestamp > 1000000000) {
                    setSystemTimeFromTimestamp(timestamp);
                    struct tm localTime;
                    getCurrentTime(&localTime);
                    if (globalWebData.containsKey("set")) {
                      JsonObject setObj = globalWebData["set"];
                      if (setObj.containsKey("bj")) {
                        int bj = setObj["bj"] | 0;
                        if (bj >= 1) {
                            Serial.println(F("[Alarm] 收到服务端报警指令，触发鸣叫"));
                            QueuedCmd alarmCmd = {6, ""};
                            snprintf(alarmCmd.filename, sizeof(alarmCmd.filename), "%d", bj);
                            if (syncQueue != NULL) {
                                xQueueSend(syncQueue, &alarmCmd, 0);
                            }
                        }
                      }
                      #ifdef USE_SMARTLOCK
                        if (setObj.containsKey("rtime")) {
                            const char* keys[] = {"rtime", "dtime", "stime", "ctime"};                            
                            for (const char* key : keys) {
                                // 直接获取原始指针，避免创建临时 String 对象分配内存
                                const char* newVal = setObj[key] | "";                                 
                                if (newVal[0] != '\0') { // 检查字符串不为空
                                    // 只有当新值与本地读取的值不同时才执行保存
                                    if (String(newVal) != passwordManager.readConfig(key)) {
                                        passwordManager.saveConfig(key, newVal);
                                    }
                                }
                            }
                        }
                        #endif
                        String thriftScreenStr = globalWebData["thrift_screen"].as<String>();
                        int thrift_screen = thriftScreenStr.toInt();

                        JsonVariant t_s = globalWebData["set"]["t_s"];          
                        if (thrift_screen == 1) {
                            Serial.println(F("[SLEEP] thrift_screen=1，清除睡眠配置并跳过"));
                            sleepConfigValid = 0;
                            sleepStartHour = 0;
                            sleepStartMinute = 0;
                            sleepEndHour = 0;
                            sleepEndMinute = 0;
                        } else if (!t_s.isNull() && t_s.is<JsonArray>()) {
                            JsonArray dev_deepsleep = t_s.as<JsonArray>();
                            if (dev_deepsleep.size() >= 2) {
                                String startTimeStr = dev_deepsleep[0].as<String>();
                                String endTimeStr = dev_deepsleep[1].as<String>();              
                                TimeHM startTime = parseTime(startTimeStr);
                                TimeHM endTime = parseTime(endTimeStr);              
                                
// 保存睡眠配置到 RTC（第一次获取时保存）
                                if (isValidTime(startTime) && isValidTime(endTime)) {
                                    saveSleepConfigToRTC(startTime.hour, startTime.minute, endTime.hour, endTime.minute);
                                }
                                
                                // thrift_screen==1时跳过睡眠
                                if (thrift_screen == 1) {
                                    Serial.println(F("[SLEEP] thrift_screen=1，跳过睡眠判断"));
                                } else if (isValidTime(startTime) && isValidTime(endTime) && 
                                    isInTimeRange(localTime, startTime, endTime)) {
                                     unsigned long secondsToSleep = getRemainingSeconds(localTime, endTime);
                                     
                                     // 在进入深度睡眠前，需要先释放互斥锁
                                     // 需要特殊处理这个情况
                                     Serial.println(F("[SLEEP] 检测到睡眠时间，准备进入深度睡眠"));
                                     Serial.printf("[SLEEP] 睡眠秒数: %lu\n", secondsToSleep);
                                     
                                     // 设置标志，让外层函数知道需要进入睡眠
                                     // 这里我们暂时不进入睡眠，避免锁问题
                                     enterDeepSleepSegmented(secondsToSleep);
                                     return;
                                     
                                     // 暂时注释掉深度睡眠，避免锁问题
                                     //Serial.println(F("[SLEEP] 深度睡眠功能暂时禁用"));
                                 }
                            }
                       }


        // === 新增：相册同步逻辑 ===
        #ifdef USE_AI
            if (globalWebData.containsKey("set") && globalWebData["set"].containsKey("psl") && globalWebData["set"].containsKey("psv")) {
                JsonArray psl = globalWebData["set"]["psl"].as<JsonArray>();
                String serverPsv = globalWebData["set"]["psv"].as<String>();
                String localPsv = "";

                // 跳过无数据情况：psv 为空或 psl 为空时清空目录
                if (serverPsv.length() == 0 || psl.size() == 0) {
                    File dir = LittleFS.open("/photo");
                    if (dir) {
                        File file = dir.openNextFile();
                        while (file) {
                            String name = String("/photo/") + file.name();
                            file.close();
                            LittleFS.remove(name);
                            dir = LittleFS.open("/photo");
                            file = dir.openNextFile();
                        }
                        dir.close();
                    }
                } else {
                    if (LittleFS.exists("/psv.txt")) {
                        File f = LittleFS.open("/psv.txt", "r");
                        localPsv = f.readString();
                        f.close();
                    }
                    
                    if (serverPsv != localPsv) {
                        Serial.println("[Photo] 版本不一致，开始同步...");
                        syncPhotos(psl, serverPsv);
                    }
                }
            } else {
                Serial.println("[Error] JSON 格式异常，无法找到 psl 或 psv");
            }
        #endif
        // === 新增结束 ===



                    }
            if (globalWebData.containsKey("pwdlist")) {

            int pwdCount = 1;
            JsonArray pwd_list = globalWebData["pwdlist"].as<JsonArray>();
            if (!pwd_list.isNull()) {
                Serial.println(F("开始写入密码列表"));
              #ifdef USE_SMARTLOCK
                for (JsonVariant value : pwd_list) {
                    if (pwdCount >= MAX_PASSWORDS) {break;}
                    String password = value.as<String>();
                    if (password.length() > 0) {
                        passwordManager.write(pwdCount, password);
                        Serial.println("写入密码槽位 " + String(pwdCount) + ": " + password);
                        pwdCount++;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                }
                #endif
            }
          }
          if (globalWebData.containsKey("period")) {
            //在function中parseJSON
          }



                }
            }
         } else if (response.length() == 0) {
            // displayNetworkStatus("HTTP请求失败", "HTTP请求失败", TFT_YELLOW);
         } else {
            // displayNetworkStatus("JSON数据无效", "JSON数据无效", TFT_YELLOW);
         }
     }
 }


// ========================================================
// 2. 这里是您的 setMQTTCallback (原封不动)
// ========================================================
void setMQTTCallback(char* topic, ::byte* payload, unsigned int length) {
    String message = String((char*)payload).substring(0, length);
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    String mqtttopic = "dev/ol/" + macAddress;
    String jsonPayload = "{\"zt\":\"1\",\"vr\":\"" + String(netConfig.version) + "\"}";
    
    if (error) {
        Serial.print(F("JSON 解析失败: "));
        Serial.println(error.f_str());
        return;
    }

    String command = doc["command"].as<String>();
    unsigned long current = millis();

    auto isRateLimited = [&](unsigned long minInterval) -> bool {
        if (current - last_saas_WebUpdate < minInterval) {
            Serial.printf("%lu秒内不saas操作\n", minInterval/1000);
            return true;
        }
        last_saas_WebUpdate = current;
        return false;
    };

    bool needUpdate = doc["mustupdate"].as<bool>();
    if (needUpdate) {
        sync_WebData();
        buzzz(1000, 150);
        lastStatusCheck =0; // 强制刷新状态显示        
        return;
    }
    
    Serial.printf("command= %s\n", command);

    if (command == "ota_update") {
        Serial.println(F("performOTAUpdate "));
        if (isRateLimited(5 * 1000)) return;
        firmupdate = true;
        performOTAUpdate_32(netConfig.firmware_url);
    } else if (command == "unlock") {
        if (isRateLimited(5 * 1000)) return;
        #ifdef USE_SMARTLOCK
        lockActuator.unlock("usercenter", "");
        #endif
    } else if (command == "sees") {
        setstatusMqtt(mqtttopic, jsonPayload);
    } else if (command == "lockpwd") {
      #ifdef USE_SMARTLOCK
        String pwd = doc["var"].as<String>();
        passwordManager.write(0, pwd);  // 设置常规密码
      #endif
    } else if (command == "testAlarm") {
        if (isRateLimited(5 * 1000)) return;
        buzzz(1800, 800);
        setstatusMqtt(mqtttopic, jsonPayload);
        #ifdef USE_AUDIO
        PLAY_SD("/a.wav");
        #endif
                            QueuedCmd alarmCmd = {6, "4"};
                            if (syncQueue != NULL) {
                                xQueueSend(syncQueue, &alarmCmd, 0);
                            }


    } else if (command == "setdev") {
        if (isRateLimited(5 * 1000)) return;
        buzzz(1800, 800);
        sync_WebData();
    }
}

void handleNetworkStatus(unsigned long currentTime, time_t CKcurrentTime) {
  int wifiMode = WiFi.getMode();
  uint32_t freeHeap = ESP.getFreeHeap();
  static unsigned long lastMemWarning = 0;

   // ==================== 内存监控和保护 ====================
   if (freeHeap < CRITICAL_HEAP_THRESHOLD) {
     char serialMsg[80];
     snprintf(serialMsg, sizeof(serialMsg), "[CRITICAL] 内存严重不足: %lu bytes，立即重启", freeHeap);
      Serial.println(F(serialMsg));
      vTaskDelay(pdMS_TO_TICKS(100));
      ESP.restart();
   }
  if (freeHeap < WARNING_HEAP_THRESHOLD && currentTime - lastMemWarning > HEAP_WARNING_INTERVAL) {
    Serial.printf("[WARN] 内存低: %lu bytes\n", freeHeap);
    lastMemWarning = currentTime;
  }

  // ==================== 状态机主逻辑 ====================
  
   // [1] AP模式转STA模式条件：STA连接已建立
   if ((wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA) && WiFi.status() == WL_CONNECTED) {
     Serial.println(F("\n[STATE] AP->STA: STA连接已恢复，退出AP模式"));
     WiFi.softAPdisconnect(true);
     dnsServer.stop();
     WiFi.mode(WIFI_STA);
     apModeActive = false;
     apRestartInitialized = false;
     currentNetworkState = NET_STATE_STA_CONNECTED;
     staFailureCount = 0;
     wifiRetryCount = 0;
     errlinkCount = 0;
     buzzz(100, 100);buzzz(800, 200);buzzz(1600, 50);buzzz(400, 100);
     return;
   }

  // ==================== AP模式处理 ====================
  if (wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA) {
    
    // AP模式初始化
    if (!apRestartInitialized) {
      WiFi.persistent(false);
      WiFi.disconnect(true, true);
      dnsServer.processNextRequest();
      apModeActive = true;
      currentNetworkState = NET_STATE_AP_SETUP;
      apModeStartTime = currentTime;
      apRestartInitialized = true;
      apModeLastRetryTime = currentTime;
      lastStationCount = 0;
      
      Serial.println(F("\n========== AP模式已启动 =========="));
      #ifdef USE_TFTD
      Serial.println(F("\n========== USE_TFTD =========="));
        displayAPMode(WiFi.softAPIP().toString().c_str(), netConfig.APNamePrefix, macAddress.substring(macAddress.length() - 4).c_str());
      #endif
  // QueuedCmd startSync = {5, "AP模式已启动"};
  // if (syncQueue != NULL) {
  //     if (xQueueSend(syncQueue, &startSync, 0) == pdTRUE) {
  //     }
  // }
       buzzz(150, 100);
       buzzz(800, 400);
       buzzz(2500, 800);
    }

    // AP模式：定期尝试STA恢复连接
    if (currentTime - apModeLastRetryTime >= AP_STA_RECOVERY_INTERVAL) {
      apModeLastRetryTime = currentTime;
      if (!ssid.isEmpty() && !password.isEmpty()) {
        Serial.printf("[AP-STA] 尝试恢复STA连接 (SSID: %s)...\n", ssid.c_str());
        // 必须切到 AP_STA 模式才能在保持 AP 的同时尝试 STA 连接
        if (WiFi.getMode() == WIFI_AP) {
          WiFi.mode(WIFI_AP_STA);
          delay(100);
        }
        WiFi.begin(ssid.c_str(), password.c_str());
        
        // 等待最多5秒的连接结果
        unsigned long waitStart = millis();
        while (millis() - waitStart < 5000) {
          if (WiFi.status() == WL_CONNECTED) {
            Serial.println(F("[AP-STA] STA连接成功！"));
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (WiFi.status() != WL_CONNECTED) {
          Serial.println(F("[AP-STA] STA连接失败，继续AP模式"));
        }
      }
    }

     // AP模式：定期检查客户端活动
     static unsigned long lastAPActivityCheck = 0;
     if (currentTime - lastAPActivityCheck >= AP_ACTIVITY_CHECK_INTERVAL) {
       lastAPActivityCheck = currentTime;
       uint8_t currentStations = WiFi.softAPgetStationNum();
       
       if (currentStations != lastStationCount) {
         apModeStartTime = currentTime;  // 重置超时计时器
         Serial.printf("[AP] 客户端活动检测: %d -> %d，重置超时\n", lastStationCount, currentStations);
         
         // 在TFT上显示客户端连接状态
         #ifdef USE_TFTD
         if (currentStations > 0) {
           displayChineseString("设置密码后重启", 0, 300, TFT_GREEN, TFT_BLACK, 1, 240);
         } else {
           displayChineseString("等待设备连接", 0, 300, TFT_YELLOW, TFT_BLACK, 1, 240);
         }
         #endif
         
         lastStationCount = currentStations;
       }
      
        // 检查AP模式超时
        // 特定设备类型在AP模式下不重启，允许正常运作
        #if !defined(USE_SMARTLOCK) && !defined(USE_LIGHT) && !defined(USE_BOOKSLOT)
        if (currentTime - apModeStartTime >= AP_IDLE_TIMEOUT) {
          displayNetworkStatus("[AP]超时，即将重启..");
          delay(1000);
          ESP.restart();  // ✅ 股票显示器时会启用自动重启
        }
        #endif
    }
  }
  // ==================== STA模式处理 ====================
  else {
    apRestartInitialized = false;
    apModeActive = false;

    // STA状态检查周期
    if (currentTime - lastConnectionCheck >= 15000) {
      lastConnectionCheck = currentTime;
      uint32_t heap = ESP.getFreeHeap();
      
      // 检查WiFi状态
      wl_status_t wifiStatus = WiFi.status();
      bool wifiConnected = (wifiStatus == WL_CONNECTED);
      
      // IDLE 状态：给一次宽限期（15秒），之后视为断连
      // 修复：原来直接 return 会导致永远卡在 IDLE，不递增 staFailureCount
      static uint8_t idleCount = 0;
      if (wifiStatus == WL_IDLE_STATUS) {
        idleCount++;
        if (idleCount <= 2) {
          Serial.printf("WiFi IDLE，等待...(%d/2)\n", idleCount);
          return;
        }
        // 超过2次（30秒）仍为IDLE，视为断连
        Serial.println(F("WiFi IDLE 超时，视为断连"));
      }
      if (wifiConnected) idleCount = 0;
      
      // ===== WiFi连接状态 =====
      if (!wifiConnected) {
        currentNetworkState = NET_STATE_STA_RECONNECTING;
        staFailureCount++;
        char serialMsg[50];
        snprintf(serialMsg, sizeof(serialMsg), "断网重连中%d/%d", staFailureCount, MAX_WIFI_RETRY_COUNT);
        displayNetworkStatus(serialMsg);
        Serial.printf("[WiFi] status=%d, 重连 %d/%d\n", wifiStatus, staFailureCount, MAX_WIFI_RETRY_COUNT);
        
        if (staFailureCount <= 3) {
          // 前3次：快速 reconnect
          Serial.println(F("  → WiFi.reconnect()"));
          WiFi.reconnect();
        } else if (staFailureCount <= 6) {
          // 第4-6次：完整 disconnect + begin（WiFi驱动可能需要重置）
          Serial.println(F("  → disconnect + begin 重连"));
          WiFi.disconnect(false);
          delay(500);
          WiFi.begin(ssid.c_str(), password.c_str());
          buzzz(100, 500);
        } else if (staFailureCount <= MAX_WIFI_RETRY_COUNT) {
          // 第7-10次：完全重启WiFi驱动（最彻底的修复）
          Serial.println(F("  → 完全重启WiFi驱动"));
          WiFi.disconnect(true);
          delay(300);
          WiFi.mode(WIFI_OFF);
          delay(300);
          WiFi.mode(WIFI_STA);
          delay(200);
          WiFi.begin(ssid.c_str(), password.c_str());
          buzzz(100, 500);
        } else {
          // 超过最大重试次数：进入AP模式
           char serialMsg[80];
           snprintf(serialMsg, sizeof(serialMsg), "STA->AP失败%d, 进入AP模式", staFailureCount);
           displayNetworkStatus(serialMsg);
           currentNetworkState = NET_STATE_STA_FAILED;
           startAPMode();
           staFailureCount = 0;
           return;
        }

        // 错误提示音（仅在工作时段）- WiFi断连：3声短促，重复2次
        errlinkCount++;
        getCurrentTime(&timeinfo);  // 更新时间
        if (errlinkCount >= 10 && timeinfo.tm_hour >= BUZZER_START_HOUR && timeinfo.tm_hour < BUZZER_END_HOUR) {
          for (int round = 0; round < 2; round++) {
            buzzz(700, 100);
            buzzz(1000, 100);
            buzzz(1600, 100);
          }
        }
      }
      // ===== WiFi连接正常 =====
      else {
         currentNetworkState = NET_STATE_STA_CONNECTED;
         staFailureCount = 0;
         errlinkCount = 0;
        // 设置MQTT回调，只需一次
        static bool mqttCallbackSet = false;
        if (!mqttCallbackSet) {
          client.setCallback(setMQTTCallback);
          mqttCallbackSet = true;
        }

        // 定期发送心跳（和MQTT_KEEPALIVE一致）- 独立于MQTT连接状态
        static unsigned long lastHeartbeat = 0;
        if (millis() - lastHeartbeat >= MQTT_KEEPALIVE * 1000UL) {
          lastHeartbeat = millis();
          sendHeartbeat();
        }

        // MQTT状态检查
        if (!client.connected()) {
          Serial.println(F("  → MQTT未连接，尝试重连"));
           if (!reconnectMQTT()) {
              Serial.println(F("→MQTT重连失败"));
               // MQTT失联：2声长响 (仅在工作时段)
              getCurrentTime(&timeinfo);  // 更新时间，取时间时一定要先用getCurrentTime
              Serial.printf("当前小时=%d, BUZZER_START_HOUR=%d, BUZZER_END_HOUR=%d\n", timeinfo.tm_hour, BUZZER_START_HOUR, BUZZER_END_HOUR);
              if (timeinfo.tm_hour >= BUZZER_START_HOUR && timeinfo.tm_hour < BUZZER_END_HOUR) {
                buzzz(300, 50);
                delay(60);
                buzzz(200, 30);
              }
           }
          }
      }
    }
  }
}

// ========================================================
// 后台管家任务：负责在 Core 0 运行上述所有逻辑
// ========================================================
void NetworkMaintenanceTask(void *pvParameters) {
    for (;;) {
        unsigned long current = millis();
        if (isAPModeActive()) {
            dnsServer.processNextRequest();
            server.handleClient();
            vTaskDelay(pdMS_TO_TICKS(50));  // 防止DNS/Server阻塞
        }
        if (!firmupdate) {
            handleNetworkStatus(current, getUTCTimestamp());
            if (WiFi.status() == WL_CONNECTED) {
                if (!client.connected()) {
                    // 添加重连间隔检查，避免频繁重连
                    static unsigned long lastMQTTRetry = 0;
                    if (millis() - lastMQTTRetry >= MQTT_RETRY_INTERVAL) {
                        lastMQTTRetry = millis();
                        reconnectMQTT();
                    }
                }
                client.loop();
            }
        }
        yield();  // 让出CPU，防止看门狗超时
        vTaskDelay(pdMS_TO_TICKS(200));  // 减少延迟时间
    }
}

void processQueuedCmd(const QueuedCmd& cmd) {
    Serial.printf(">>> QUEUE RECV cmd=%d\n", cmd.cmd);
    unsigned long current = millis();
    
    if (cmd.cmd == 1) {
        sync_WebData(); 
    } else if (cmd.cmd == 2) {
        #ifdef USE_SMARTLOCK
        Serial.println(F("执行队列中的开锁命令"));
        lockActuator.unlock("usercenter", "");
        #endif
    } else if (cmd.cmd == 3) {
        #ifdef USE_SMARTLOCK
        auto safeCpy = [](char* d, const char* s, size_t n) { strncpy(d, s, n - 1); d[n - 1] = '\0'; };
        safeCpy(apiParams[0].value, "opendoor", sizeof(apiParams[0].value));
        safeCpy(apiParams[1].value, lockActuator.getPassword(), sizeof(apiParams[1].value));
        safeCpy(apiParams[2].value, lockActuator.getType(), sizeof(apiParams[2].value));
        getJsonData(macAddress, apiParams);
        delay(500);
        lastConnectionCheck = current;
        #endif
    } else if (cmd.cmd == 4) {
        #ifdef USE_AUDIO
        CLEAR_AUDIO_QUEUE();
        PLAY_SD(cmd.filename);
        #endif
    } else if (cmd.cmd == 5) {
        #ifdef USE_STOCK
        displayChineseString(cmd.filename, 0, 0, TFT_RED, TFT_BLACK, 1, 240);
        #endif
    } else if (cmd.cmd == 6) {
        int alarmType = atoi(cmd.filename);
        Serial.printf("执行队列中的长鸣指令, type=%d\n", alarmType);
        unsigned long startTime = millis();
        
        if (alarmType == 1 || alarmType == 2 || alarmType == 4) {
            digitalWrite(PCF_INT_PIN, HIGH);
            buzzz(2000, 100);
            delay(500);
            digitalWrite(PCF_INT_PIN, LOW);
            while(millis() - startTime < 4000) {
                if (alarmType == 4) digitalWrite(PCF_INT_PIN, HIGH);
                buzzz(1800, 100);
                digitalWrite(PCF_INT_PIN, LOW);                  
                delay(30);                    
                yield();
            }
            buzzError();
        } else if (alarmType == 3) {
            digitalWrite(PCF_INT_PIN, HIGH);
            delay(100);
            digitalWrite(PCF_INT_PIN, LOW);
            delay(400);
            while(millis() - startTime < 5000) {
                digitalWrite(PCF_INT_PIN, HIGH);
                delay(100);
                digitalWrite(PCF_INT_PIN, LOW);
                yield();
            }
            buzzError();
        }
        digitalWrite(PCF_INT_PIN, LOW);
    }
}

#endif