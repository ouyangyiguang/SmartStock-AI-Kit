#pragma once

#include <Arduino.h>
#include "utils/pins.h"
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "time.h"

struct BuzzCommand {
  int freq;
  int duration;
};
struct TimeHM {
    uint8_t hour;
    uint8_t minute;
};

// 功能函数声明
void initSystem();
void initSpiffs();
// void buzzWorker(void* param);
// void initBuzz();
// void sBuzz(int freq, int duration);
// void buzz(int frequency, int duration);
void buzzz(int frequency, int duration);
void buzzSuccess();
void buzzError();
void buzzClear();

bool isValidJSON(const String &str);
JsonObject parseJSON(const String &json);
void saveSchedule(const char* name, SchedulePeriod schedule[]);
void loadSchedule(const char* name, SchedulePeriod schedule[]);

void alarm_buzz(JsonArray alarm_time,const char* hourmin);

TimeHM parseTime(const String &str);
bool isValidTime(const TimeHM &t);
bool isInTimeRange(const struct tm &localTime, const TimeHM &start, const TimeHM &end);
unsigned long getRemainingSeconds(const struct tm &localTime, const TimeHM &end);
void enterDeepSleepSegmented(unsigned long seconds);

// RTC 睡眠检查函数
bool checkDeepSleepFromRTC();
void saveSleepConfigToRTC(uint8_t startH, uint8_t startM, uint8_t endH, uint8_t endM);
void updateRTCSystemTime();  // 更新RTC中的系统时间
bool isSystemTimeValid();    // 检查系统时间是否有效
bool compensateSystemTime(); // 补偿系统时间
bool isNTPTimeSynced();      // 检查NTP时间同步状态

extern unsigned long lastNTPSync;  // 声明为外部变量
extern SchedulePeriod schedule[MAX_PERIODS];
extern String passwords[MAX_PASSWORDS];
// extern DynamicJsonDocument globalDoc; // 关键：文档必须全局
// extern JsonObject globalWebData;
extern StaticJsonDocument<JSON_DOC_SIZE> globalDoc;      // 告诉编译器变量在别处定义
extern JsonObject globalWebData;

// RTC 睡眠相关变量
extern RTC_DATA_ATTR uint32_t sleepStartHour;
extern RTC_DATA_ATTR uint32_t sleepStartMinute;
extern RTC_DATA_ATTR uint32_t sleepEndHour;
extern RTC_DATA_ATTR uint32_t sleepEndMinute;
extern RTC_DATA_ATTR uint32_t sleepConfigValid;
extern RTC_DATA_ATTR uint64_t sleepTimestamp;
extern RTC_DATA_ATTR uint64_t sleepDurationSeconds;

// 时间锚点函数
void restoreTimeFromRTC();
void saveTimeToRTC(time_t now);


