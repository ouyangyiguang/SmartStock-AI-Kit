#include "BLE.h"
#include "function.h"
#include <algorithm>
#include <WiFi.h>
#include <Preferences.h>
#ifdef USE_SMARTLOCK
#include "dev_lock/SmartLock.h"
#endif
#include "utils/pins.h"

struct QueuedCmd {
    int cmd;
    char filename[32];
};

// 全局变量定义
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
bool deviceConnected = false;
std::string SERVICE_UUID = "";
std::string CHARACTERISTIC_UUID = "";
std::string CHARACTERISTIC_UUID_TX = "";
std::string MAC_ADR = "";
std::string DEV_NAME = "";
extern QueueHandle_t syncQueue;
std::string newSsid; 
std::string newPassword;

std::string receivedData = "";
unsigned long lastReceiveTime = 0;
const unsigned long DATA_TIMEOUT = 1000; 

// 静态JsonDocument，避免每次解析都分配堆栈
static DynamicJsonDocument bleDoc(1536);

// 蓝牙服务器回调
void ServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    Serial.println(F("✅ 蓝牙设备已连接")); 
    apModeStartTime = millis();
    deviceConnected = true;
    buzzSuccess();
    receivedData.clear();
    receivedData.shrink_to_fit(); 
    bleDoc.clear();  // 清空旧数据
}

void ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    Serial.println(F("蓝牙设备已断开"));
    deviceConnected = false;
    apModeStartTime = millis();
    NimBLEDevice::startAdvertising();
    buzzClear();
    receivedData.clear();
    receivedData.shrink_to_fit(); 
    bleDoc.clear();  // 清空旧数据
}

// 延迟重启任务

void delayedRestartTask(void* param) {
    // 1. 等待回复发送给 App
    vTaskDelay(pdMS_TO_TICKS(2000)); 
    
    Serial.println(F("[SYSTEM] 正在准备深度清理并重启..."));

    // 2. 保存 WiFi 数据
    Preferences prefs;
    if (prefs.begin("wifi-config", false)) {
        prefs.putString("ssid", String(newSsid.c_str()));
        prefs.putString("password", String(newPassword.c_str()));
        prefs.end();
        Serial.println(F("[SYSTEM] WiFi 数据已保存"));
    }

    WiFi.disconnect(true, true); 
    vTaskDelay(pdMS_TO_TICKS(200)); 
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(200)); 
    
    // 5. 停止蓝牙并反初始化，释放射频资源
    // NimBLEDevice::getAdvertising()->stop();
    // NimBLEDevice::deinit(true); 
    
    // Serial.println(F("[SYSTEM] 硬件清理完成，执行重启..."));
    // buzzz(1000, 500); 
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    buzzz(1000, 500); 
    vTaskDelay(pdMS_TO_TICKS(100)); 
    delay(2000);
    // 6. 执行重启
    ESP.restart();
    vTaskDelete(NULL);
}
// 解析并处理 JSON 指令
bool parseAndProcessJson(String &jsonStr, NimBLEConnInfo& connInfo) {
    bleDoc.clear();
    DeserializationError error = deserializeJson(bleDoc, jsonStr);
    
    if (error) {
        Serial.print(F("JSON解析失败: "));
        Serial.println(error.c_str());
        Serial.printf("原始JSON: %s\n", jsonStr.c_str());
        return false;
    }

    // 检查是否是init消息
    if (bleDoc.containsKey("type") && bleDoc["type"].as<String>() == "init") {
        Serial.println(F("收到init消息，忽略"));
        return true;
    }

    if (bleDoc.containsKey("action")) {
        const char* action = bleDoc["action"] | "";
        Serial.printf("收到action: %s\n", action);
        
        if (strcmp(action, "setwifi") == 0) {
            Serial.println(F("处理setwifi命令"));
            newSsid = bleDoc["name"].as<std::string>();
            newPassword = bleDoc["pwd"].as<std::string>();
            if (!newSsid.empty()) {
                sendBLEMessage(connInfo, R"({"status":"ok"})");
                xTaskCreate(delayedRestartTask, "RestartTask", 2048, NULL, 1, NULL);
                return true;
            }
        }else if (strcmp(action, "scan") == 0 || strcmp(action, "sync") == 0) {
            Serial.printf("处理%s命令\n", action);
            QueuedCmd startSync = {1, ""};
            if (syncQueue != NULL) {
                xQueueSend(syncQueue, &startSync, 0);
            }
            sendBLEMessage(connInfo, R"({"status":"ok"})");
            return true;
        }else if (strcmp(action, "setpwd") == 0) {
            Serial.println(F("处理setpwd命令"));
            buzzz(1500, 600);
            buzzz(1500, 300);
            //bool requestSync = true;
            //xQueueSend(syncQueue, &requestSync, portMAX_DELAY);
            #ifdef USE_SMARTLOCK
            if (bleDoc.containsKey("data") && bleDoc["data"].containsKey("pwdlist")) {
            int pwdCount = 1;
            JsonArray pwd_list = bleDoc["data"]["pwdlist"].as<JsonArray>();
            if (!pwd_list.isNull()) {
                for (JsonVariant value : pwd_list) {
                    if (pwdCount >= MAX_PASSWORDS) {break;}
                    String password = value.as<String>();
                    if (password.length() > 0) {
                        passwordManager.write(pwdCount, password);
                        Serial.println("写入密码槽位 " + String(pwdCount) + ": " + password);
                        pwdCount++;
                    }
                }
            }
            } else {
                Serial.println(F("setpwd命令缺少data或pwdlist字段"));
            }
            #else
            Serial.println(F("setpwd命令：智能锁功能未启用"));
            #endif
            sendBLEMessage(connInfo, R"({"status":"ok"})");
            return true;
        }else if (strcmp(action, "setdev") == 0) {
            Serial.println(F("处理setdev命令"));
            buzzz(1500, 300);
            buzzz(1500, 600);
            //bool requestSync = true;
            //xQueueSend(syncQueue, &requestSync, portMAX_DELAY);

            // ===================== 处理时间段数据 =====================
            if (bleDoc.containsKey("data") && bleDoc["data"].containsKey("period")) {
                SchedulePeriod schedule[MAX_PERIODS] = {};
                
                JsonArray periods = bleDoc["data"]["period"].as<JsonArray>();
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
                    Serial.println(F("已保存时间段数据"));
                } else {
                    Serial.println(F("警告：period 数组为空"));
                }
            } else {
                Serial.println(F("警告：无 period 字段"));
            }
            sendBLEMessage(connInfo, R"({"status":"ok"})");
            return true;
        }else if (strcmp(action, "unlock") == 0) {
            Serial.println(F("处理unlock命令"));
            #ifdef USE_SMARTLOCK
            if (!bleDoc.isNull() && bleDoc.containsKey("data") && bleDoc["data"].containsKey("mac")) {
                String macAddr = bleDoc["data"]["mac"].as<String>();
                Serial.printf("收到MAC地址: %s, 本地MAC: %s\n", macAddr.c_str(), MAC_ADR.c_str());
                if (macAddr == String(MAC_ADR.c_str())) {
                    Serial.println(F("准备异步执行开门动作"));
                    String uid = bleDoc["data"].containsKey("uid") ? bleDoc["data"]["uid"].as<String>() : "";
                    String sttype = passwordManager.readConfig("sttype");
                    Serial.printf("sttype值: %s\n", sttype.c_str());
                    
                    // 异步执行开门动作 - 使用syncQueue发送命令码2
                    QueuedCmd unlockCmd = {2, ""};
                    if (syncQueue != NULL) {
                        if (xQueueSend(syncQueue, &unlockCmd, 0) == pdTRUE) {
                            Serial.println(F("开锁命令已加入队列"));
                        } else {
                            Serial.println(F("命令队列已满"));
                        }
                    } else {
                        Serial.println(F("命令队列未初始化"));
                    }
                } else {
                    Serial.println("MAC地址不匹配: " + macAddr + " != " + String(MAC_ADR.c_str()));
                }
            } else {
                Serial.println(F("unlock命令缺少data或mac字段"));
            }
            #else
            Serial.println(F("unlock命令：智能锁功能未启用"));
            #endif
            sendBLEMessage(connInfo, R"({"status":"ok"})");
            return true;
        } else {
            Serial.printf("未知的action: %s\n", action);
        }
    } else {
        Serial.println(F("JSON中没有action字段"));
    }
    return false;
}

// 处理接收到的数据流
void processReceivedData(NimBLEConnInfo& connInfo) {
    if (receivedData.empty()) return;

    // 查找所有完整的JSON对象（从{开始，到}结束）
    size_t pos = 0;
    bool processed = false;
    
    while (pos < receivedData.length()) {
        size_t start = receivedData.find('{', pos);
        if (start == std::string::npos) break;
        
        // 查找匹配的结束大括号
        int braceCount = 0;
        size_t end = start;
        bool inString = false;
        char lastChar = 0;
        
        for (size_t i = start; i < receivedData.length(); i++) {
            char c = receivedData[i];
            
            // 处理字符串内的字符（忽略字符串内的大括号）
            if (c == '"' && lastChar != '\\') {
                inString = !inString;
            }
            
            if (!inString) {
                if (c == '{') {
                    braceCount++;
                } else if (c == '}') {
                    braceCount--;
                    if (braceCount == 0) {
                        end = i;
                        break;
                    }
                }
            }
            
            lastChar = c;
        }
        
        if (braceCount == 0 && end > start) {
            // 提取完整的JSON对象
            std::string jsonPart = receivedData.substr(start, end - start + 1);
            String jsonStr = String(jsonPart.c_str());
            Serial.printf("尝试解析JSON: %s\n", jsonStr.c_str());
            
            if (parseAndProcessJson(jsonStr, connInfo)) {
                processed = true;
            }
            
            // 移动到下一个位置
            pos = end + 1;
        } else {
            // 没有找到完整的JSON对象，等待更多数据
            break;
        }
    }
    
    if (processed) {
        // 如果成功处理了JSON，清除已处理的数据
        receivedData.erase(0, pos);
        receivedData.shrink_to_fit();
    }
    
    // 如果数据过长，清理一下
    if (receivedData.length() > 2048) {
        receivedData.clear();
        receivedData.shrink_to_fit();
    }
}

// 蓝牙特征值写入回调
void CharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    buzzz(800, 150);
    apModeStartTime = millis();
    
    if (!value.empty()) {
        lastReceiveTime = millis();
        receivedData += value;
        
        if (receivedData.length() > 1536) {
            receivedData.clear();
            receivedData.shrink_to_fit();
            return;
        }

        if (receivedData.find('}') != std::string::npos) {
            processReceivedData(connInfo);
        }
    }
}

// 【修复错误】确保函数签名与头文件 BLE.h 中的 const String &rawMac 一致
String hashMacTo4Hex(const String &input) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < input.length(); i++) {
        hash = ((hash << 5) + hash) + input[i];
    }
    char buf[5];
    sprintf(buf, "%04x", (uint16_t)(hash & 0xFFFF));
    return String(buf);
}

// BLE.cpp 修改建议
void generateUUIDsFromMAC(const String &mac) {
    std::string rawMac = mac.c_str();
    rawMac.erase(remove(rawMac.begin(), rawMac.end(), ':'), rawMac.end());
    for (auto& c : rawMac) c = toupper(c);

    String rawMacStr = String(rawMac.c_str());
    String servicePart = hashMacTo4Hex(rawMacStr);
    
    // 旋转逻辑：后8位 + 前4位
    String rotatedMac = rawMacStr.substring(4) + rawMacStr.substring(0, 4);
    String charPart = hashMacTo4Hex(rotatedMac);
    
    // TX部分逻辑
    String txRotated = rawMacStr.substring(6) + rawMacStr.substring(4, 6);
    String txPart = hashMacTo4Hex(txRotated);
    
    String devNameStr = servicePart + charPart;
    DEV_NAME = std::string(devNameStr.c_str());
    
    String svc = String("0000") + servicePart + "-0000-1000-8000-00805f9b34fb";
    SERVICE_UUID = std::string(svc.c_str());
    
    String chr = String("0000") + charPart + "-0000-1000-8000-00805f9b34fb";
    CHARACTERISTIC_UUID = std::string(chr.c_str());
    
    String txChr = String("0000") + txPart + "-0000-1000-8000-00805f9b34fb";
    CHARACTERISTIC_UUID_TX = std::string(txChr.c_str());
}

// 初始化蓝牙
void initBluetooth(const String &mac, const String &bluename) {
    generateUUIDsFromMAC(mac);
    // 截取 DEV_NAME 前4位，缩短 BLE 广播名（过长在微信小程序/nRF中显示为N/A）
    String shortId = String(DEV_NAME.c_str()).substring(0, 4);
    String deviceName = "Y_" + bluename + shortId;
    Serial.printf(">>> BLE 设备名: %s\n", deviceName.c_str());
    Serial.print(F("正在初始化蓝牙..."));
    
    NimBLEDevice::init(deviceName.c_str());
    Serial.println(F("NimBLEDevice::init 完成"));
    
    NimBLEDevice::setPower(9);
    NimBLEDevice::setSecurityAuth(false, false, false);
    
    pServer = NimBLEDevice::createServer();
    Serial.println(F("createServer 完成"));
    
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID.c_str());
    Serial.println(F("createService 完成"));

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID.c_str(),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());

    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX.c_str(),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    pService->start();
    Serial.println(F("Service start 完成"));

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID.c_str());
    pAdvertising->setName(deviceName.c_str());
    pAdvertising->start();
    Serial.printf(">>> BLE广告已启动: %s\n", deviceName.c_str());
    
    delay(100);
}

void sendBLEMessage(NimBLEConnInfo& connInfo, const std::string& msg) {
    if (pCharacteristic) {
        pCharacteristic->setValue(msg);
        pCharacteristic->notify(connInfo.getConnHandle());
    }
}

void sendNotificationToApp(const String& jsonStr) {
    if (deviceConnected && pTxCharacteristic) {
        pTxCharacteristic->setValue(jsonStr.c_str());
        pTxCharacteristic->notify();
        delay(50);
    }
}

void restartBLEAdvertising() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop();
        delay(100);
        
        pAdvertising->setMinInterval(0x20);
        pAdvertising->setMaxInterval(0x40);
        
        pAdvertising->start();
        delay(50);
    }
}

// 停止BLE，释放WiFi资源（解决WiFi/BLE冲突）
void stopNimBLE() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop();
    }
    NimBLEDevice::deinit(true);  // 完全禁用蓝牙
}