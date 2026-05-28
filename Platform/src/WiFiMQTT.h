#pragma once
#ifndef WIFI_MQTT_UTILS_H
#define WIFI_MQTT_UTILS_H

#include <PubSubClient.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "utils/qrcode.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include "utils/pins.h"
#ifdef USE_STOCK
#include <TFT_eSPI.h>
extern TFT_eSPI tft;
#endif

#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include "esp_system.h"
#include <esp_efuse.h>
#include <esp_mac.h>
#include <vector>
#include <algorithm>
#include <Ticker.h> // 非阻塞定时器


extern WebServer server;
extern Preferences preferences;
extern String ssid;
extern String password;
extern String macAddress;
extern const char* firmwaretype;
extern DNSServer dnsServer;

enum NetworkStatus {
  NETWORK_DISCONNECTED,
  NETWORK_AP_MODE,
  NETWORK_STA_CONNECTING,
  NETWORK_STA_CONNECTED,
  NETWORK_MQTT_CONNECTED,
  NETWORK_SCANNING
};

// 网络配置结构体
struct NetworkConfig {
  const char* mqttServer;
  int mqttPort;
  const char* mqttUser;
  const char* mqttPassword;
  const char* apiUrl;
  const char* apiData;
  const char* firmwaretype;
  const char* APNamePrefix;
  const char* version;
  const char* createUID;
  const char* firmware_url;
  const char* bluename;
};

// API参数结构体
struct ApiParam {
  char key[16];
  char value[32];
};

// 函数声明
String getStoredSSID();
String getStoredPassword();
void initNetwork(const NetworkConfig& config);
//NetworkStatus checkNetworkStatus();
bool reconnectMQTT();
void resetWiFiSettings();
void performSafeOTAUpdate();
bool isAPModeActive();
bool isWiFiConnected();
bool isMQTTConnected();
void startAPMode();
void startMQTT();

// HTML页面处理函数
void handleRoot();
void handleRescan();
void handleScan();
void handleSubmit();
void handleReset();
void resetAndReconnectWiFi(String newSsid, String newPass);
void clearWifiConfig();
void sendDeviceData(const String& mac, const String& create_uid, const String& firmwaretype);
void getMacAddress();
String getJsonData(const String& mac, const ApiParam apiParams[]);
bool setstatusMqtt(const String& mqtttopic, const String& payload);  // 优化: 返回bool表示是否发送成功
bool sendHeartbeat();  // 优化: 返回bool表示心跳是否发送成功
void syncNTPTime();
void getCurrentTime(struct tm *timeinfo);
time_t getUTCTimestamp();
void setSystemTimeFromTimestamp(unsigned long timestamp);
bool isHttpRequestComplete();
void resetHttpRequest();
void performOTAUpdate_32(const String& firmware_url);
// 全局变量声明
struct NTPStatus {
    bool synced;
    unsigned long lastSync;
};
extern NTPStatus ntpStatus;
extern struct tm timeinfo;
extern unsigned long lastTimeSync;
extern ApiParam apiParams[];

class ConnectionPool {
public:
    static WiFiClient& getHttpClient();
    static void releaseHttpClient();
    
private:
    static const int MAX_CLIENTS;
    static WiFiClient httpClients[];  // 声明数组
    static bool inUse[];             // 声明数组
};

extern WiFiClient globalClient;
extern PubSubClient client;
extern uint8_t wifiRetryCount;
extern bool isFirstBoot;
extern bool apModeActive;
#endif