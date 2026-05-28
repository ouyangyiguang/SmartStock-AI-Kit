#include "function.h"
#include "WiFiMQTT.h"
#include "AudioPlayer.h"
#include <esp_partition.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_wifi.h>
#ifdef USE_AI
#include "ai/MicModule.h"
#endif

// RTC 内存中保存的睡眠配置（用于定时唤醒时快速判断，无需联网）
// 注意：RTC_DATA_ATTR 变量需要4字节对齐，使用 uint32_t 确保对齐
RTC_DATA_ATTR uint32_t sleepStartHour = 0;
RTC_DATA_ATTR uint32_t sleepStartMinute = 0;
RTC_DATA_ATTR uint32_t sleepEndHour = 0;
RTC_DATA_ATTR uint32_t sleepEndMinute = 0;
RTC_DATA_ATTR uint32_t sleepConfigValid = 0;  // 标记睡眠配置是否有效，使用 uint32_t 替代 bool 确保对齐

// 保存睡眠前的时间戳，唤醒后用于计算当前时间
RTC_DATA_ATTR uint64_t sleepTimestamp = 0;
RTC_DATA_ATTR uint64_t sleepDurationSeconds = 0;

// 时间锚点：Deep Sleep 唤醒后恢复时间用
RTC_DATA_ATTR time_t lastKnownTime = 0;

// 仅负责将存储在 RTC 的值写入系统时钟。实际的时间计算和判断逻辑在 checkDeepSleepFromRTC() 中处理，以确保在 Deep Sleep 唤醒后能正确恢复时间并判断是否继续睡眠。
void restoreTimeFromRTC() {
    if (lastKnownTime > 0) {
        struct timeval tv = { .tv_sec = lastKnownTime, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        Serial.printf("[TIME] 从RTC恢复: %ld\n", (long)lastKnownTime);
    }
}

void saveTimeToRTC(time_t now) {
    lastKnownTime = now;
    Serial.printf("[TIME] 保存锚点: %ld\n", (long)now);
}



#ifdef USE_SD_CARD
  #include "utils/SDCardManager.h"
  extern SDCardManager sdCard; //
#endif
#ifdef USE_LIGHTS
  #include "dev_led/LightControl.h"
  extern LightControl lights; 
#endif
SemaphoreHandle_t spiMutex = nullptr;
SchedulePeriod schedule[MAX_PERIODS];
String passwords[MAX_PASSWORDS];
unsigned long lastNTPSync = 0;

//StaticJsonDocument<3072> globalDoc;//全局持久化 JSON 文档（推荐用于复杂项目）
// 解析JSON数据
// ⚠️ 修复 #7 提醒：返回值是全局 globalDoc 的引用 (live view)。
// - 多次调用 parseJSON 会清空旧内容，旧引用立即失效（悬空）。
// - 不要跨函数边界长期保存返回的 JsonObject；只在同一调用栈内使用。
// - 全局 globalWebData 复用本 doc，需要主调用方持 globalDataMutex 保护。
JsonObject parseJSON(const String &json) {
    //static StaticJsonDocument<1024> doc;
    //// 关键：globalWebData时文档必须全局
    // static一定要，不要可以编译成功，但会内存溢出
    // 把 StaticJsonDocument 设为静态的 让 doc 保持长期有效static 会让 doc 保留在内存中，不会在函数结束时被销毁。
    globalDoc.clear();// 清空旧内容，避免 static 重复污染
    if (json.isEmpty()) {
        Serial.println(F("空的 JSON 字符串"));
        return JsonObject();//return doc.createNestedArray("empty");  // 返回一个空的数组
    }
    
    DeserializationError error = deserializeJson(globalDoc, json);
    if (error) {
        Serial.print(F("JSON解析失败："));
        Serial.println(error.c_str());
        Serial.println(F("原始数据："));
        Serial.println(json);
        return JsonObject();
    }
    Serial.print(String("解析后的JSON：") + json);
    // ===================== 处理时间段数据 =====================
    if (globalDoc["data"].containsKey("period")) {
        //SchedulePeriod schedule[MAX_PERIODS] = {};
        
        JsonArray periods = globalDoc["data"]["period"].as<JsonArray>();
        if (periods.size() > 0) {
            // 处理新时间段数据
            for (int i = 0; i < periods.size() && i < MAX_PERIODS; i++) {
                schedule[i].begin_time = periods[i]["b_t"].as<long>();
                schedule[i].end_time = periods[i]["e_t"].as<long>();
                
                // 验证时间有效性（非零值）
                schedule[i].valid = (schedule[i].begin_time != 0 && 
                                    schedule[i].end_time != 0);
                
                // 如果无效则显式重置
                if (!schedule[i].valid) {
                    schedule[i] = {0, 0, false};
                }
            }
            saveSchedule("office_hours", schedule);
        } else {
            //Serial.println(F("警告：period 数组为空"));
        }
    } else {
       // Serial.println(F("警告：无 period 字段"));
    }
    
    // ===================== 处理密码数据 =====================
    if (globalDoc["data"].containsKey("pwdlist")) {

       //移到ino中处理
    }

    if (!globalDoc.containsKey("data")) {
        Serial.println(F("JSON中未包含 'data' 字段"));
        return JsonObject();
    }

    //return globalDoc["data"].as<JsonObject>();
    JsonObject dataObj = globalDoc["data"].as<JsonObject>();
    // 删除某些字段
    //dataObj.remove("pic");
    return dataObj;
}

// 保存时间段数据
void saveSchedule(const char* name, SchedulePeriod schedule[]) {
    Preferences prefs;
    prefs.begin(name, false);
    
    // 清除旧的命名空间
    prefs.clear();    
    int validCount = 0;
    
    for (int i = 0; i < MAX_PERIODS; i++) {
        char key[20];
        sprintf(key, "period_%d", i);
        
        if (schedule[i].valid) {
            // 保存有效时段
            prefs.putBytes(key, &schedule[i], sizeof(SchedulePeriod));
            validCount++;
        }
        // 无效时段无需存储
    }
    
    // 保存有效时段数量
    prefs.putInt("valid_count", validCount);
    
    prefs.end();
    Serial.printf("已保存 %d 个时间段到 %s\n", validCount, name);
}

// 加载时间段数据
void loadSchedule(const char* name, SchedulePeriod schedule[]) {
    Preferences prefs;
    bool needClear = false;
    prefs.begin(name, true);
    
    int validCount = prefs.getInt("valid_count", 0);
    int count = min(validCount, MAX_PERIODS);
    
    for (int i = 0; i < count; i++) {
        char key[20];
        sprintf(key, "period_%d", i);
        
        size_t structSize = sizeof(SchedulePeriod);
        size_t bytesRead = prefs.getBytes(key, &schedule[i], structSize);
        
        if (bytesRead != structSize) {
            Serial.printf("数据读取失败: key=%s, expected=%zu, got=%zu，标记清除\n", key, structSize, bytesRead);
            needClear = true;
            schedule[i] = {0, 0, false};
        } else if (schedule[i].begin_time >= schedule[i].end_time) {
            schedule[i].valid = false;
            Serial.printf("时段 %d 时间无效，已标记为无效\n", i);
        }
    }
    
    for (int i = count; i < MAX_PERIODS; i++) {
        schedule[i] = {0, 0, false};
    }
    
    prefs.end();
    
    if (needClear) {
        Serial.printf("清除 %s 中的旧数据...\n", name);
        Preferences prefsClear;
        prefsClear.begin(name, false);
        prefsClear.clear();
        prefsClear.end();
        Serial.printf("已清除旧数据，下次启动将使用新格式\n");
    } else {
        Serial.printf("已加载 %d 个时间段从 %s\n", count, name);
    }
}


void initSystem() {
    spiMutex = xSemaphoreCreateMutex();    
    Serial.printf("\n\nESP32-xx启动...芯片型号: %s\n", ESP.getChipModel());
    Serial.printf("CPU频率: %dMHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash大小: %dMB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("可用堆内存: %d\n", ESP.getFreeHeap());

    if (psramFound()) {
        Serial.printf("PSRAM大小: %dMB\n", ESP.getPsramSize() / (1024 * 1024));
    } else {
        Serial.println(F("未检测到PSRAM"));
    }
}

void initSpiffs() {
  Serial.println(F("列出所有分区:"));
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *part = esp_partition_get(it);
    Serial.printf("Partition: %s, type: %d, subtype: %d, offset: 0x%x, size: 0x%x\n", part->label, part->type, part->subtype, part->address, part->size);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);

  // 查找分区 - 名称匹配分区表中的"spiffs"
  const esp_partition_t *fs_part = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, 
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
    "spiffs"  // 匹配分区表名称
  );
  
  if (!fs_part) {
    Serial.println(F("未找到文件系统分区！"));
    return;
  }
  
  Serial.printf("文件系统地址: 0x%06X ~ 0x%06X\n", 
    fs_part->address, 
    fs_part->address + fs_part->size
  );

  // 初始化LittleFS
  if (!LittleFS.begin(false)) {  // 挂载到/fs路径
    Serial.println(F("LittleFS挂载失败！尝试格式化..."));
    if (LittleFS.begin(true)) {
      Serial.println(F("LittleFS格式化成功"));
    } else {
      Serial.println(F("格式化失败！"));
      return;
    }
  }
  
  Serial.printf("总空间: %d 字节\n", LittleFS.totalBytes());
  Serial.printf("已用空间: %d 字节\n", LittleFS.usedBytes());

  // 列出文件（注意路径前缀）
  File root = LittleFS.open("/");
  while (File file = root.openNextFile()) {
    Serial.printf("文件: %s  大小: %d字节\n", 
      file.name(), 
      file.size()
    );
  }
  root.close();
}

// 播放蜂鸣器音调
void buzzz(int freq, int duration) {
    #ifdef USE_AI
    MicModule::getInstance().stopListeningImmediately();
    #endif
    tone(BUZZER_PIN, freq, duration); 
    delay(duration/2); 
    noTone(BUZZER_PIN);
    delay(duration / 2);
}


void buzzSuccess() {
  buzzz(1500,600); // 长鸣提示成功
}

void buzzError() {
  for (int i = 0; i < 6; i++) {
    buzzz(50,50); 
    delay(50);
  }
}
void buzzClear() {
  for (int i = 0; i < 3; i++) {  // 快速三次蜂鸣音
    buzzz(500,150);  // 每次蜂鸣持续150毫秒，模拟哒哒哒的节奏
    delay(100);
  }
}

bool isValidJSON(const String &str) {
  return (str.indexOf('{') != -1) && 
         (str.indexOf('}') != -1); // 简易校验
}

void alarm_buzz(JsonArray alarm_time, const char* hourmin) {
    if (!alarm_time.isNull()) {
        for (JsonVariant value : alarm_time) {
            if (strcmp(hourmin, value.as<const char*>()) == 0) {
                // 130ms(响) + 60ms(停) = 190ms 
                // 73 次循环总计约 13.87 秒
                for (int ii = 0; ii < 73; ii++) { 
                    tone(BUZZER_PIN, 1000); 
                    delay(130);             
                    noTone(BUZZER_PIN);     
                    delay(60);             
                }
                // 响完后跳出，避免如果有多个相同闹钟设置导致重复执行
                break; 
            }
        }
    }
}

TimeHM parseTime(const String &timeStr) {
  TimeHM t = {0, 0};
  if (timeStr.length() != 5) return t; // 形如 "HH:mm"
  if (timeStr.charAt(2) != ':') return t;

  t.hour = timeStr.substring(0, 2).toInt();
  t.minute = timeStr.substring(3, 5).toInt();

  if (t.hour < 0 || t.hour > 23 || t.minute < 0 || t.minute > 59) {
    t.hour = 0;
    t.minute = 0;
  }
  return t;
}


bool isValidTime(const TimeHM &t) {
    // 排除 00:00（解析失败的默认值）和无效范围
    return (t.hour > 0 || t.minute > 0) && 
           (t.hour >= 0 && t.hour <= 23 && t.minute >= 0 && t.minute <= 59);
}

bool isInTimeRange(const struct tm &now, const TimeHM &start, const TimeHM &end) {
  uint16_t nowMinutes = now.tm_hour * 60 + now.tm_min;
  uint16_t startMinutes = start.hour * 60 + start.minute;
  uint16_t endMinutes = end.hour * 60 + end.minute;

  if (startMinutes <= endMinutes) {
    return (nowMinutes >= startMinutes && nowMinutes < endMinutes);
  } else {
    // 跨天，比如 22:30 - 07:00
    return (nowMinutes >= startMinutes) || (nowMinutes < endMinutes);
  }
}

unsigned long getRemainingSeconds(const struct tm &localTime, const TimeHM &end) {
    uint16_t nowMinutes = localTime.tm_hour * 60 + localTime.tm_min;
    uint16_t endMinutes = end.hour * 60 + end.minute;

    uint16_t diff;
    
    // 首先检查是否在跨天睡眠区间内
    // 睡眠区间是21:18-10:48（跨天）
    // 如果当前时间在00:00-10:48之间，说明是跨天情况
    if (sleepStartHour > sleepEndHour) {
        // 跨天睡眠区间
        if (nowMinutes >= (sleepStartHour * 60 + sleepStartMinute)) {
            // 当前时间在第一天晚上部分（21:18-23:59）
            // 计算到第二天10:48的时间
            diff = (24 * 60 - nowMinutes) + endMinutes;
        } else if (nowMinutes < endMinutes) {
            // 当前时间在第二天凌晨部分（00:00-10:48）
            diff = endMinutes - nowMinutes;
        } else {
            // 不应该到达这里
            diff = 0;
        }
    } else {
        // 同一天睡眠区间
        if (nowMinutes < endMinutes) {
            diff = endMinutes - nowMinutes;
        } else {
            // 已经过了结束时间
            diff = 0;
        }
    }


    
    return (unsigned long)diff * 60;
}

// 保存睡眠配置到 RTC 内存
void saveSleepConfigToRTC(uint8_t startH, uint8_t startM, uint8_t endH, uint8_t endM) {
    sleepStartHour = startH;
    sleepStartMinute = startM;
    sleepEndHour = endH;
    sleepEndMinute = endM;
    sleepConfigValid = 1;
    
    Serial.printf("[SLEEP] 配置: %02d:%02d-%02d:%02d\n", startH, startM, endH, endM);
}

// 检查系统时间是否有效
bool isSystemTimeValid() {
    time_t now;
    time(&now);
    return now >= 1704067200; // 2024-01-01 00:00:00
}


// [优化] 检查是否需要深度睡眠
bool checkDeepSleepFromRTC() {
    Serial.println(F("========== >>> RTC CHECK START <<< =========="));
    Serial.println(F(">>> DEBUG: checkDeepSleepFromRTC CALLED"));
    if (!sleepConfigValid) {
        Serial.println(F("[SLEEP] A-睡眠配置无效"));
        return false;
    }
    
    // 验证睡眠时间配置有效性
    TimeHM startTime = {(uint8_t)sleepStartHour, (uint8_t)sleepStartMinute};
    TimeHM endTime = {(uint8_t)sleepEndHour, (uint8_t)sleepEndMinute};
    
    // 如果 startTime 和 endTime 都是 0，说明未设置有效睡眠时间
    if (sleepStartHour == 0 && sleepStartMinute == 0 && 
        sleepEndHour == 0 && sleepEndMinute == 0) {
        Serial.println(F("[SLEEP] B-睡眠时间未设置(全为0)"));
        sleepConfigValid = 0;
        return false;
    }
    
    // 验证时间范围合理性
    if (!isValidTime(startTime) || !isValidTime(endTime)) {
        Serial.printf("[SLEEP] 睡眠时间无效: %02d:%02d - %02d:%02d\n", 
            sleepStartHour, sleepStartMinute, sleepEndHour, sleepEndMinute);
        sleepConfigValid = 0;
        return false;
    }
    
    // 记录补偿前的原始时间，用于判断时间来源
    time_t now_before;
    time(&now_before);
    bool rawTimeInvalid = (now_before < 1704067200);  // 原始时间是否无效
    
    // 补偿系统时间
    if (!compensateSystemTime()) {
        Serial.println(F("[SLEEP] 系统时间补偿失败"));
    }
    
    // 获取当前时间（补偿后）
    struct tm localTime;
    time_t now;
    time(&now);
    localtime_r(&now, &localTime);
    
    // 判断最终时间来源
    bool timeFromRTC = rawTimeInvalid || !isNTPTimeSynced();
    
    // 验证系统时间有效性
    if (!isSystemTimeValid()) {
        Serial.println(F("[SLEEP] 系统时间无效，跳过深度睡眠"));
        return false;
    }
    
    // 额外验证 localTime 有效性 (tm_year 是从 1900 开始的年数)
    if (localTime.tm_year < 70) {  // 1970年之前
        Serial.println(F("[SLEEP] 本地时间无效"));
        return false;
    }

    bool inRange = isInTimeRange(localTime, startTime, endTime);
    
    if (inRange) {
        unsigned long remainingSeconds = getRemainingSeconds(localTime, endTime);
        Serial.printf("[SLEEP] 系统时间: %02d:%02d:%02d (%s)\n", 
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
            timeFromRTC ? "RTC恢复" : "NTP同步");
        Serial.printf("[SLEEP] 睡眠区间: %02d:%02d - %02d:%02d, 剩余%lu秒\n",
            sleepStartHour, sleepStartMinute, sleepEndHour, sleepEndMinute, remainingSeconds);
        Serial.printf("[SLEEP] 在睡眠时间段内，准备进入深度睡眠\n");
        return true; 
    } else {
        sleepTimestamp = 0;
        sleepDurationSeconds = 0;
        Serial.printf("[SLEEP] 当前时间: %02d:%02d:%02d (%s)\n", 
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
            timeFromRTC ? "RTC恢复" : "NTP同步");
        Serial.printf("[SLEEP] 睡眠区间: %02d:%02d - %02d:%02d, 不在睡眠时间段内\n",
            sleepStartHour, sleepStartMinute, sleepEndHour, sleepEndMinute);
        return false;
    }
}

// [优化] 进入深度睡眠
void enterDeepSleepSegmented(unsigned long totalSeconds) {
    // 使用传入的睡眠时间，但最多12小时
    unsigned long actualSleep = totalSeconds;
    if (actualSleep > 43200) actualSleep = 3600;
    if (actualSleep < 10) actualSleep = 10;  // 最小10秒防止立即唤醒
    
    Serial.printf("[SLEEP] 分段睡眠时间: %lu秒\n", actualSleep);

    time_t now;
    time(&now);
    
    // 记录睡眠信息（时间锚点 + 睡眠时长，唤醒后用于补偿）
    if (now >= 1704067200) {
        sleepTimestamp = (uint64_t)now;
        lastKnownTime = now;  // 同步保存时间锚点，确保 restoreTimeFromRTC 有准确值
    }
    sleepDurationSeconds = (uint64_t)actualSleep; 
    sleepConfigValid = 1;

    #ifdef USE_LIGHTS
        lights.clearAll(); 
    #endif
    STOP_AUDIO();
    #ifdef USE_SD_CARD
        sdCard.end(); 
    #endif

    // 1. 彻底断开 MQTT
    if (client.connected()) {
        client.disconnect();
        delay(100);
    }

    // 2. 优化 WiFi 关闭流程：防止驱动卸载超时
    WiFi.disconnect(true); // 断开并擦除配置
    WiFi.mode(WIFI_OFF);
    
    // 给底层协议栈一点时间处理释放请求
    int retry = 0;
    while (WiFi.status() == WL_CONNECTED && retry < 10) {
        delay(100);
        retry++;
    }

    // 调用底层 ESP-IDF 接口强制停止，解决 "wifi:timeout" 报错
    // esp_wifi_stop();
    // esp_wifi_deinit();
    // delay(200);

    // 3. 修复 GPIO 隔离逻辑：增加合法性检查
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
        gpio_num_t gpio = (gpio_num_t)pin;
        
        // 关键修复：只操作硬件上真实存在的引脚，避免 828 错误
        if (!GPIO_IS_VALID_GPIO(gpio)) {
            continue; 
        }

        // 跳过仅输入引脚 (GPIO 34-39)
        if (pin >= 34 && pin <= 39) {
            continue;
        }

        // 设置为浮空状态以节电
        esp_err_t err = gpio_sleep_set_pull_mode(gpio, GPIO_FLOATING);
        if (err != ESP_OK) {
            // 正常情况下不会再报错，除非引脚被系统占用
        }
    }


    // 4. 确保串口打印输出完毕，防止进入睡眠时打印数据丢失
    Serial.flush();
    delay(100);

    // 5. 设置唤醒源并进入深度睡眠
    uint64_t sleepMicroseconds = (uint64_t)actualSleep * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepMicroseconds);
    // 禁用EXT0唤醒，防止GPIO0干扰导致频繁唤醒
    // esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}



// [新增] 检查NTP时间同步状态 判断当前 time() 是否靠谱（是否大于 2024 年）。
bool isNTPTimeSynced() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        time_t now;
        time(&now);
        return now >= 1704067200; // 2024-01-01
    }
    return false;
}

// [优化] 统一时间补偿函数 决定何时用 RTC 内存补，何时用 NTP 同步。
bool compensateSystemTime() {
    time_t now;
    time(&now);
    
    // 检查时间是否有效
    if (now < 1704067200) { // 1970年或无效时间
        if (sleepTimestamp > 0 && sleepDurationSeconds > 0) {
            // 使用RTC存档恢复时间
            now = (time_t)sleepTimestamp + (time_t)sleepDurationSeconds;
            struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            return true;
        }
        return false;
    }
    
    // 如果有RTC存档且NTP未同步，检查差异
    if (sleepTimestamp > 0 && sleepDurationSeconds > 0 && !isNTPTimeSynced()) {
        time_t expectedTime = (time_t)sleepTimestamp + (time_t)sleepDurationSeconds;
        int64_t timeDiff = (int64_t)now - (int64_t)expectedTime;
        
        // 只有在差异非常大（>10分钟）时才使用RTC时间
        if (abs(timeDiff) > 600) { // 10分钟
            struct timeval tv = { .tv_sec = expectedTime, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            return true;
        }
    }
    
    return true;
}

// 更新RTC系统时间
void updateRTCSystemTime() {
    time_t now;
    time(&now);
    if (now >= 1704067200) { 
        sleepTimestamp = (uint64_t)now;
    }
}

