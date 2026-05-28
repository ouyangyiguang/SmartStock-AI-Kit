#pragma once
#include <NimBLEDevice.h>
#include "NimBLEAdvertising.h"
#include "NimBLEAdvertisedDevice.h" // 确保有这个
#include <Preferences.h>
#include <ArduinoJson.h>
// 蓝牙全局变量
extern NimBLEServer* pServer;
extern NimBLECharacteristic* pCharacteristic;
extern bool deviceConnected;
extern std::string SERVICE_UUID;
extern std::string CHARACTERISTIC_UUID;
extern unsigned long apModeStartTime;
// 蓝牙回调类
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic,
                NimBLEConnInfo& connInfo) override;
};



// 蓝牙功能函数
void initBluetooth(const String &mac,const String &bluename);
void stopNimBLE();
void updateBLEStatus();
void sendBLEMessage(NimBLEConnInfo& connInfo, const std::string& msg);
void generateUUIDsFromMAC(const String &mac);
void sendNotificationToApp(const String& jsonStr);
bool parseAndProcessJson(String &jsonStr, NimBLEConnInfo& connInfo);
void processReceivedData(NimBLEConnInfo& connInfo);

void delayedRestartTask(void* param);
String hashMacTo4Hex(const String &rawMac);
void restartBLEAdvertising();
