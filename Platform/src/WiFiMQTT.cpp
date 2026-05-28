#include "WiFiMQTT.h"
#include "BLE.h"
#include <functional>

extern QueueHandle_t syncQueue;

extern void setMQTTCallback(char* topic, byte* payload, unsigned int length);

const int ConnectionPool::MAX_CLIENTS = 1;  // 定义常量
WiFiClient ConnectionPool::httpClients[ConnectionPool::MAX_CLIENTS];
bool ConnectionPool::inUse[ConnectionPool::MAX_CLIENTS] = {false};
struct QueuedCmd {
    int cmd;
    char filename[32];
};
NTPStatus ntpStatus = {false, 0};
struct tm timeinfo = {0};
unsigned long lastTimeSync = 0;
String ssid = "";
String password = "";
String macAddress = "";
const byte DNS_PORT = 53;
const char* firmwaretype = "";
uint8_t wifiRetryCount = 0;
bool isFirstBoot = true;

DNSServer dnsServer;
WiFiClient globalClient;// 只用于 MQTT
PubSubClient client(globalClient);
WebServer server(80);

WiFiClient& ConnectionPool::getHttpClient() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!ConnectionPool::inUse[i]) {  // 使用静态成员
            ConnectionPool::inUse[i] = true;
            ConnectionPool::httpClients[i].stop();
            return ConnectionPool::httpClients[i];
        }
    }
    static WiFiClient emergencyClient;
    return emergencyClient;
}

void ConnectionPool::releaseHttpClient() {
    // 修复 #8：每次调用只释放一个 inUse 客户端，避免并发场景下把他人占用的连接也 stop。
    // MAX_CLIENTS=1 时与全部释放等价；扩展为多客户端时语义才正确。
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ConnectionPool::inUse[i]) {
            ConnectionPool::httpClients[i].stop();
            ConnectionPool::inUse[i] = false;
            return;
        }
    }
}

#if defined(ESP32)
  Preferences preferences;
#endif


// 网络配置
NetworkConfig networkConfig;
unsigned long lastHeartbeatTime = 0;
unsigned long lastReconnectAttempt = 0;
uint8_t reconnectAttempts = 0;
bool apModeActive = false;
bool scanInProgress = false;


// 初始化网络
void initNetwork(const NetworkConfig& config) {
  networkConfig = config;
  firmwaretype = config.firmwaretype;
  
    // 在初始化时保留 MQTT 专用资源
    globalClient.setNoDelay(true); // 启用 TCP_NODELAY
    globalClient.setTimeout(200);  // 短超时
    
    client.setClient(globalClient);
    client.setBufferSize(256);    // 明确设置缓冲区

  // 获取MAC地址
  getMacAddress();
  Serial.print(F("设备MAC地址: "));
  Serial.println(macAddress);  
  
  // 每次都重新读取WiFi凭证
  Serial.println(F("[WiFi] 读取存储的WiFi凭证..."));
  preferences.begin("wifi-config", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  
  // 判断是否有有效凭证
  bool hasValidCredentials = (ssid.length() > 0 && password.length() > 0);
  
  if (hasValidCredentials) {
    Serial.printf("[WiFi] ✓ 找到保存的凭证: SSID=%s\n", ssid.c_str());
    Serial.printf("password=%s\n", password.c_str());
    Serial.println(F("[WiFi] 尝试STA模式连接..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // 激进的连接尝试（有凭证时应该充分等待）
    int retries = 0;
    int maxRetries = 20;  // 40 x 500ms = 20秒 - 给WiFi充分时间连接
    while (WiFi.status() != WL_CONNECTED && retries < maxRetries) {
      delay(300);
       Serial.print(F("."));
       retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("[WiFi] ✓ STA模式连接成功!"));
      Serial.print(F("IP: "));
      Serial.println(WiFi.localIP());
      WiFi.setAutoReconnect(true);
      
      // 延迟socket相关操作，给socket栈初始化时间
      Serial.println(F("[WiFi] 等待socket栈初始化..."));
      delay(1000);
      
      // 启动WebServer（STA模式）
      server.begin();
      sendDeviceData(macAddress, networkConfig.createUID, networkConfig.firmwaretype);
      // 不在此处调用sendDeviceData，让loop()处理
      return;
    } else {
      // 连接失败但有凭证 → 保持STA模式在后台继续尝试
      Serial.printf("\n[WiFi] ⚠️  STA连接失败 (尝试%d次/20秒)，切换为后台重试模式\n", retries);
      Serial.println(F("[WiFi] 设备将在后台定期重试，或通过配置页面修改凭证"));
      
      // 保持STA模式，让loop()中的定时器处理重连
      WiFi.disconnect(false);
      delay(500);
      
      // 启动WebServer供用户重配置
      server.begin();
      return;
    }
  } else {
    // 无有效凭证 → 检查是否首次启动
    Serial.println(F("[WiFi] ❌ 未找到保存的凭证"));
    
    if (isFirstBoot) {
      Serial.println(F("[WiFi] 首次启动，启动AP模式进行配置..."));
      startAPMode();
      isFirstBoot = false;
    } else {
      // 非首次但无凭证 → 进入AP模式
      Serial.println(F("[WiFi] 启动AP模式进行配置..."));
      startAPMode();
    }
    return;
  }
}
String escapeHTML(const String& input) {
  String output = input;
  output.replace("&", "&amp;");
  output.replace("<", "&lt;");
  output.replace(">", "&gt;");
  output.replace("\"", "&quot;");
  output.replace("'", "&#39;");
  return output;
}
void clearWifiConfig() {
  Serial.println(F("[Step1] 清除 Preferences 配置信息..."));
  Preferences preferences;
  if (preferences.begin("wifi-config", false)) {
    preferences.clear();
    preferences.end();
    Serial.println(F("[OK] NVS 配置已清除"));
  }

  Serial.println(F("[Step2] 断开WiFi连接并清除保存的凭证..."));
  WiFi.disconnect(true, true);
  delay(100);

  Serial.println(F("[Step3] 恢复WiFi出厂设置..."));
  esp_wifi_restore();
  delay(100);


  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.println(F("[Step5] 重启 ESP 完成配置清除"));
  ESP.restart();
}
//void handleRoot();
//void handleRescan();
void startMQTT() {
      client.setServer(networkConfig.mqttServer, networkConfig.mqttPort);// 设置MQTT
      client.setKeepAlive(MQTT_KEEPALIVE);
      client.setSocketTimeout(2);  // socket超时2秒，快速失败
      if (reconnectMQTT()) {
        Serial.println(F("MQTT连接成功!"));
      }
}
// 启动AP模式
void startAPMode() {
  apModeActive = true;
  Serial.println(F("启动AP模式..."));
  
  // 关闭WebServer（防止冲突）
  server.stop();
  delay(100);
  
  // 确保WiFi完全关闭
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  // 生成唯一的AP名称
  String apName = String(networkConfig.APNamePrefix) + macAddress.substring(macAddress.length() - 4);
  
  // 启动AP模式（只调用一次）
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apName.c_str(), NULL, 1, false, 1)) {  // 信道1，不隐藏，最多连接1个
    Serial.println(F("AP启动失败!"));
    return;
  }
  
  // 设置AP的IP地址（必须在启动服务器之前）
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  delay(100);
  
  // 启动DNS服务器
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  if (!dnsServer.start(DNS_PORT, "*", WiFi.softAPIP())) {
    Serial.println(F("DNS服务器启动失败!"));
  }
  
  // 配置Web服务器路由
  server.on("/", handleRoot);
  server.on("/rescan", handleRescan);
  server.on("/scan", handleScan);
  server.on("/hotspot-detect.html", []() { handleRoot(); });
  server.on("/generate_204", []() { 
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/ncsi.txt", []() { server.send(200, "text/plain", "OK"); });
  server.on("/connecttest.txt", []() { server.send(200, "text/plain", "OK"); });
  server.on("/library/test/success.html", []() { handleRoot(); });
  server.on("/success.txt", []() { server.send(200, "text/plain", "OK"); });
  server.on("/fwlink", []() { 
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/reset", HTTP_GET, handleReset);
  
  // 处理未知路径（DNS重定向到配置页面）
  server.onNotFound([]() {
    String host = server.hostHeader();
    // 常见的captive portal检测域名
    if (host.endsWith("captive.apple.com") || 
        host.endsWith("g.cn") ||
        host.endsWith("connectivitycheck.android.com") ||
        host.endsWith("connectivitycheck.gstatic.com") ||
        host.endsWith("msftconnecttest.com") ||
        host.endsWith("clients3.google.com") ||
        host.endsWith("detectportal.firefox.com") ||
        host == "" || host == WiFi.softAPIP().toString()) {
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", "");
    } else {
      handleRoot();
    }
  });
  
  // 启动Web服务器（必须在配置IP和路由之后）
  server.begin();
  
  Serial.println(F("✅ AP模式已启动"));
  Serial.print(F("📍 AP IP: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("📡 AP名称: "));
  Serial.println(apName);
  Serial.println(F("🌐 访问地址: http://192.168.4.1"));

  QueuedCmd startSync = {5, "AP模式已启动"};
  if (syncQueue != NULL) {
      if (xQueueSend(syncQueue, &startSync, 0) == pdTRUE) {
      }
  }

}

bool reconnectMQTT() {
    const uint32_t INITIAL_RETRY_DELAY = 1000U;
    const uint32_t MAX_RETRY_DELAY = 30000UL;
    const float BACKOFF_FACTOR = 1.5f;
    const uint32_t MAX_OFFLINE_TIME = 10 * 300000UL;
    const uint8_t MAX_SUBSCRIBE_ATTEMPTS = 3;
    const uint8_t MAX_DNS_RETRY = 3;

    static uint32_t lastAttemptTime = 0;
    static uint32_t retryDelay = INITIAL_RETRY_DELAY;
    static uint32_t firstFailureTime = 0;
    static uint8_t subscribeAttempts = 0;
    static bool connecting = false;
    static bool subscribed = false;
    static IPAddress mqttServerIP;
    static IPAddress lastSuccessfulIP;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[MQTT] WiFi未连接，终止重连"));
        connecting = false;
        return false;
    }

    if (client.connected() && subscribed) {
        return true;
    }

    if (firstFailureTime > 0 && (millis() - firstFailureTime > MAX_OFFLINE_TIME)) {
        Serial.println(F("\n[MQTT] 超过最大离线时间，可重启..."));
        #if !defined(USE_SMARTLOCK) && !defined(USE_LIGHT) && !defined(USE_BOOKSLOT)
          delay(1000);
          ESP.restart();
        #endif
    }

    if (!client.connected()) {
        if (!connecting) {
            if (millis() - lastAttemptTime >= retryDelay) {
                Serial.printf("[MQTT] 尝试连接（间隔%.1fs）\n", retryDelay / 1000.0);

                int dnsRetryCount = 0;
                bool dnsResolved = false;
                bool usedCachedIP = false;

                if (lastSuccessfulIP != IPAddress(0, 0, 0, 0)) {
                    Serial.printf("尝试使用上次成功的IP: %s\n", lastSuccessfulIP.toString().c_str());
                    mqttServerIP = lastSuccessfulIP;
                    dnsResolved = true;
                    usedCachedIP = true;
                } else {
                    while (dnsRetryCount < MAX_DNS_RETRY) {
                        if (WiFi.hostByName(networkConfig.mqttServer, mqttServerIP)) {
                            if (mqttServerIP != IPAddress(0, 0, 0, 0)) {
                                Serial.printf("DNS解析成功: %s -> %s\n",
                                              networkConfig.mqttServer,
                                              mqttServerIP.toString().c_str());
                                dnsResolved = true;
                                break;
                            } else {
                                Serial.println(F("DNS解析返回0.0.0.0，视为失败"));
                            }
                        }

                        dnsRetryCount++;
                        if (dnsRetryCount < MAX_DNS_RETRY) {
                            Serial.printf("DNS解析失败 (%d/%d), 1秒后重试...\n",
                                          dnsRetryCount, MAX_DNS_RETRY);
                            delay(1000);
                        }
                    }
                }

                if (!dnsResolved) {
                    Serial.println(F("DNS解析最终失败!"));
                    retryDelay = min<uint32_t>(
                        static_cast<uint32_t>(retryDelay * BACKOFF_FACTOR),
                        MAX_RETRY_DELAY);
                    Serial.printf("[MQTT] %.1f秒后再次尝试\n", retryDelay / 1000.0);
                    lastAttemptTime = millis();
                    return false;
                }

                WiFiClient portTestClient;
                Serial.printf("测试端口 %d 连通性...", networkConfig.mqttPort);
                if (portTestClient.connect(mqttServerIP, networkConfig.mqttPort)) {
                    Serial.println(F("测试端口成功"));
                    portTestClient.stop();
                } else {
                    Serial.println(F("测试端口失败"));
                    portTestClient.stop();
                    // 端口测试失败也要恢复蓝牙
                    initBluetooth(macAddress, networkConfig.bluename);
                    lastAttemptTime = millis();
                    retryDelay = min<uint32_t>(retryDelay * BACKOFF_FACTOR, MAX_RETRY_DELAY);
                    return false;
                }

                client.setServer(mqttServerIP, networkConfig.mqttPort);
                client.setSocketTimeout(2);  // 每次重连前重新设置超时
                globalClient.setTimeout(3000);  // WiFiClient超时2秒
                connecting = true;

                Serial.printf("连接MQTT: %s@%s:%d (%s)\n",
                              macAddress.c_str(),
                              mqttServerIP.toString().c_str(),
                              networkConfig.mqttPort,
                              usedCachedIP ? "缓存IP" : "DNS解析");

                Serial.printf("用户名: '%s', 密码: '%s'\n",
                              networkConfig.mqttUser,
                              networkConfig.mqttPassword);

                if (client.connected()) {
                    client.disconnect();
                    delay(50);
                }

                // 停止蓝牙，释放WiFi资源（解决WiFi/BLE冲突）
                stopNimBLE();
                delay(300);
                yield();  // 喂狗，防止看门狗超时
                bool connectResult = client.connect(macAddress.c_str(),
                                                    networkConfig.mqttUser,
                                                    networkConfig.mqttPassword);
                
                yield();  // 喂狗，防止connect阻塞过长
                
                lastAttemptTime = millis();
                subscribeAttempts = 0;
                subscribed = false;

                if (firstFailureTime == 0) {
                    firstFailureTime = millis();
                }

                Serial.printf("连接结果: %s\n", connectResult ? "成功" : "失败");

                if (connectResult) {
                    lastSuccessfulIP = mqttServerIP;
                    // 恢复蓝牙（MQTT连接成功后重新初始化）
                    initBluetooth(macAddress, networkConfig.bluename);
                }
            }
        } else {
            if (client.connected()) {
                Serial.println(F("[MQTT] 连接成功"));
                connecting = false;
                lastSuccessfulIP = mqttServerIP;
            } else if (millis() - lastAttemptTime > 12000) {
                connecting = false;
                int state = client.state();
                Serial.printf("[MQTT] 连接失败（代码 %d）：", state);

                const char* errorMsg = "未知错误";
                switch (state) {
                    case -4: errorMsg = "DNS解析失败"; break;
                    case -3: errorMsg = "TCP连接超时"; break;
                    case -2: errorMsg = "TCP连接被拒"; break;
                    case -1: errorMsg = "TCP连接错误"; break;
                    case 1: errorMsg = "协议版本错误"; break;
                    case 2: errorMsg = "客户端ID无效"; break;
                    case 3: errorMsg = "MQTT服务不可用"; break;
                    case 4: errorMsg = "用户名/密码错误"; break;
                    case 5: errorMsg = "未通过认证"; break;
                }
                Serial.println(errorMsg);

                retryDelay = min<uint32_t>(
                    static_cast<uint32_t>(retryDelay * BACKOFF_FACTOR),
                    MAX_RETRY_DELAY);
                Serial.printf("[MQTT] %.1f秒后再次尝试\n", retryDelay / 1000.0);

                if (state == 4 || state == 5) {
                    Serial.println(F("检测到认证问题，重置MQTT凭证"));
                    networkConfig.mqttUser = "";
                    networkConfig.mqttPassword = "";
                }
            }
        }
    }

    if (client.connected() && !subscribed) {
        if (subscribeAttempts < MAX_SUBSCRIBE_ATTEMPTS) {
            const String topic = "ctl/dev/" + macAddress;

            if (client.subscribe(topic.c_str())) {
                Serial.printf("[MQTT] 订阅成功：%s\n", topic.c_str());
                subscribed = true;
                retryDelay = INITIAL_RETRY_DELAY;
                firstFailureTime = 0;
                return true;
            } else {
                subscribeAttempts++;
                Serial.printf("[MQTT] 订阅尝试 %d/%d 失败\n",
                              subscribeAttempts, MAX_SUBSCRIBE_ATTEMPTS);
            }
        } else {
            Serial.println(F("[MQTT] 订阅失败，断开连接"));
            client.disconnect();
            connecting = false;
            subscribed = false;
        }
    }

    return client.connected() && subscribed;
}


// 优化: 添加返回值跟踪发送状态
bool setstatusMqtt(const String& mqtttopic, const String& payload){
  if (apModeActive) return false; // AP 模式下不发送
  if (client.connected()) {
    bool published = client.publish(mqtttopic.c_str(), payload.c_str());
    if (!published) {
        Serial.printf("⚠️  MQTT publish 失败: %s\n", mqtttopic.c_str());
    }
    return published;
  }
  return false;
}
// 发送心跳 - 优化: 添加返回值和失败检测
bool sendHeartbeat() { 
    String jsonPayload = "{\"zt\":\"1\",\"vr\":\"\"}";
    String mqtttopic = "dev/ol/" + macAddress;
    bool result = setstatusMqtt(mqtttopic, jsonPayload);
    if (result) {
        Serial.print(F("♥"));
    }
    return result;
}

// 路由1：显示配置页面
void handleRoot() {
  Serial.println(F("[handleRoot] 请求进入配置页面"));
  String responseHTML = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>WiFi 配置</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1.0">
    <style>body {margin: 0;padding: 0;background: #382F21;font-family: Arial, sans-serif;color:#ffffff;}
    .header {font-size: 24px;color: #CFBE89;background: rgba(0,0,0,0.7);text-align: center;padding: 20px;}
    .container {padding: 20px;background: rgba(0,0,0,0.5);border-radius: 8px;margin: 20px;}
    input, select, button {width: 100%;padding: 12px 10px;margin: 8px 0;border: none;border-radius: 4px;background-color: #CFBE89;color: #fff;font-size: 16px;box-sizing: border-box;}
    input::placeholder {color: #F1F1F1;opacity: 0.7;}
    button {background-color: #B79C4D !important;color: #000;cursor: pointer;font-weight: bold;}
    .footer {text-align: center;color: #fff;padding: 20px 0;}
    .footer div {margin: 5px 0;font-size: 12px;color: #c9c9c9;}
    .status {text-align: center;color: #FFD700;padding: 10px;font-size: 14px;}
    .qr-container {text-align: center;margin: 20px 0;}
  </style>
</head>
<body>
    <div class="header">WiFi 配置</div>
    <div class="container">
        <div class="status" id="status">正在扫描WiFi列表...</div>
        <div class="scan-info">正在扫描WIFI时请稍等几秒，或手动填写。</div>
        <form action="/submit" method="POST">
            <select name="select" id="wifiSelect" onchange="document.getElementById('ssidInput').value=this.value">
                <option value="" selected>请选择WiFi网络</option>
            </select>
            <input type="text" name="ssid" id="ssidInput" placeholder="请选择WIFI或直接输入WIFI名称" required>
            <input type="password" name="password" placeholder="输入WiFi密码" required>
            <button type="submit">连接WiFi</button>
        </form>
        <a href="#" id="rescanLink">重新扫描WiFi</a>
        <div class="device-id">设备绑定码: )rawliteral" + macAddress + R"rawliteral(</div>
    </div>
    <div class="footer">
        <div>- 珠海优点信息有限公司 -</div>
        <div>Yodin.com</div>
        <div>Tzzs.com</div>
        <div>Dcdy.com</div>
    </div>
    <script>
    function loadWiFiList() {
      document.getElementById('status').innerText = '正在扫描WiFi列表...';
      fetch("/scan").then(resp => resp.json())
      .then(data => {
        const statusEl = document.getElementById('status');
        const selectEl = document.getElementById('wifiSelect');
        selectEl.innerHTML = '<option value="" selected>请选择WiFi网络</option>';
        if(data.length === 0) {
            statusEl.innerText = '没有发现WiFi';
            return;
        }
        statusEl.innerText = '扫描到 ' + data.length + ' 个网络';
        data.forEach(net => {
            const option = document.createElement('option');
            option.value = net.ssid;
            option.textContent = net.ssid + ' ' + net.lockIcon + ' (' + net.rssi + 'dBm)';
            selectEl.appendChild(option);
        });
      }).catch(() => {
        document.getElementById('status').innerText = 'WiFi 扫描失败，请刷新重试';
      });
    }
    window.onload = loadWiFiList;
    document.getElementById('rescanLink').addEventListener('click', function(event) {
      event.preventDefault();
      loadWiFiList();
    });
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", responseHTML);
}


// 路由2：WiFi 扫描接口
void handleScan() {
  Serial.println(F("[handleScan] 开始WiFi扫描..."));

  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);
  Serial.printf("[handleScan] 扫描完毕，找到 %d 个网络\n", n);

  String jsonResponse = "[";
  bool first = true;

  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;

    int encryption = WiFi.encryptionType(i);
    String lockIcon = (encryption == WIFI_AUTH_OPEN) ? "🔓" : "🔒";

    if (!first) jsonResponse += ",";
    jsonResponse += "{\"ssid\":\"" + ssid + "\",\"lockIcon\":\"" + lockIcon + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    first = false;
  }
  jsonResponse += "]";
  server.send(200, "application/json", jsonResponse);
}

// 添加重新扫描处理函数
void handleRescan() {
    scanInProgress = false; // 重置扫描状态
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

// 处理提交请求
void handleSubmit() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");
    
    // 验证输入
    if (newSsid.length() == 0) {
      server.send(400, "text/html", "<meta charset='utf-8'><h1>SSID不能为空</h1>");
      return;
    }
    
    if (newPassword.length() < 6) {
      server.send(400, "text/html", "<meta charset='utf-8'><h1>密码长度至少6位</h1>");
      return;
    }
    
    // 保存凭证到NVS
    Serial.printf("[WiFi] 保存新凭证: SSID=%s\n", newSsid.c_str());
    preferences.begin("wifi-config", false);
    preferences.putString("ssid", newSsid);
    preferences.putString("password", newPassword);
    preferences.end();
    
    // 验证保存
    preferences.begin("wifi-config", true);
    String savedSsid = preferences.getString("ssid", "");
    String savedPwd = preferences.getString("password", "");
    preferences.end();
    
    if (savedSsid == newSsid && savedPwd == newPassword) {
      Serial.println(F("[WiFi] ✓ 凭证保存验证成功"));
      
      // 发送响应
      server.send(200, "text/html", "<meta charset='utf-8'><h1>凭证已保存!</h1><p>设备正在尝试连接WiFi...</p>");
      delay(300);
      
      // 立即断开AP并尝试连接新WiFi（不重启）
      Serial.println(F("[WiFi] 断开AP模式，准备连接新WiFi..."));
      dnsServer.stop();
      server.stop();
      WiFi.disconnect(true);  // 关闭AP和STA
      delay(300);
      
      // 切换到STA模式并连接
      Serial.println(F("[WiFi] 启动STA模式连接新WiFi..."));
      WiFi.mode(WIFI_STA);
      WiFi.begin(newSsid.c_str(), newPassword.c_str());
      
      // 激进的连接尝试（有凭证应充分等待）
      int retries = 0;
      int maxRetries = 20;  // 20 x 500ms = 10秒 - 充分等待
      while (WiFi.status() != WL_CONNECTED && retries < maxRetries) {
        delay(500);
        Serial.print(F("."));
        retries++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("[WiFi] ✓ 新WiFi连接成功!"));
        Serial.print(F("IP: "));
        Serial.println(WiFi.localIP());
        WiFi.setAutoReconnect(true);
        
        // 减少DNS等待时间
        delay(500);
        
        // 启动WebServer（STA模式）
        server.begin();
        
        // 异步发送设备数据
        xTaskCreate([](void* param) {
          delay(1000);
          sendDeviceData(macAddress, networkConfig.createUID, networkConfig.firmwaretype);
          vTaskDelete(NULL);
        }, "SendDeviceData", 1536, NULL, 1, NULL);
      } else {
        // 连接失败，保持STA模式并启动WebServer供重配
        Serial.println(F("\n[WiFi] ⚠️  新WiFi连接失败(尝试10秒)，保持STA模式等待"));
        Serial.println(F("[WiFi] 设备将在后台定期重试连接"));
        
        // 保持STA模式
        WiFi.disconnect(false);
        delay(200);
        
        // 启动WebServer允许用户重新配置
        server.begin();
      }
    } else {
      Serial.println(F("[WiFi] ✗ 凭证保存验证失败"));
      server.send(500, "text/html", "<meta charset='utf-8'><h1>保存凭证失败</h1>");
    }
  } else {
    server.send(400, "text/html", "<h1>无效请求</h1>");
  }
}

// 处理复位请求
void handleReset() {
  resetWiFiSettings();
  server.send(200, "text/html", "<meta charset='utf-8'><h1>设置已重置! 设备将重启...</h1>");
  delay(1000);
  ESP.restart();
}

// 重置WiFi设置
void resetWiFiSettings() {

    preferences.begin("wifi-config", false);
    preferences.clear();
    preferences.end();

  ssid = "";
  password = "";
}

void getMacAddress() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char macStr[13];
  snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macAddress = String(macStr);
}

void syncNTPTime() {
    Serial.println(F("⏱️  Starting NTP synchronization..."));
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("❌ WiFi not connected, skipping NTP sync."));
        return;
    }
    // 设置时区为中国标准时间 (UTC+8)
    // 使用 GMT-8 表示东8区（中国）
    setenv("TZ", "GMT-8", 1);
    tzset();
    
    const char* ntpServers[] = {
        "pool.ntp.org",
        "ntp.aliyun.com",
        "cn.pool.ntp.org"        
    };
    
    // 注意：configTime的第一个参数是GMT偏移（秒），但我们已经设置了时区
    // 所以这里使用0，让时区设置生效
    configTime(0, 0, ntpServers[0], ntpServers[1], ntpServers[2]);
    
    // 多次等待直到同步
    int waitCount = 0;
    const int maxWait = 10;  // 最多等待10次，每次1秒 = 10秒超时
    time_t beforeSync = time(NULL);  // 记录同步前的时间
    
    while (waitCount < maxWait) {
        if (getLocalTime(&timeinfo)) {
            ntpStatus.synced = true;
            ntpStatus.lastSync = millis();
            
            // 验证时间是否真的更新了（防止NTP返回初始值）
            time_t afterSync = time(NULL);
            if (afterSync > (beforeSync + 86400)) {  // 至少比之前快1天
                Serial.printf(
                    "✅ NTP synchronized: %02d:%02d:%02d (同步耗时%ds)\n",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, waitCount + 1
                );
                return;
            }
        }
        Serial.print(F("."));
        waitCount++;
        delay(1000);
    }
    
    ntpStatus.synced = false;  // 超时标记为失败
    Serial.println(F("\n❌ NTP Sync Failed after 10s wait"));
}


void setSystemTimeFromTimestamp(unsigned long timestamp) {
    time_t now = timestamp;
    struct timeval tv = {
        .tv_sec = now,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    
    // 设置时区为中国标准时间
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 更新全局时间结构
    localtime_r(&now, &timeinfo);
    
    ntpStatus.synced = true;
    ntpStatus.lastSync = millis();
    lastTimeSync = millis();  // 记录同步时间
    
    Serial.printf("✅ 系统时间已设置: %02d:%02d:%02d\n", 
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}
// 获取当前时间（本地时间）
void getCurrentTime(struct tm *timeinfo) {
    time_t now;
    time(&now);
    localtime_r(&now, timeinfo);
}

// 获取UTC时间戳
time_t getUTCTimestamp() {
    time_t now;
    time(&now);
    return now;
}
// 状态检查函数
bool isAPModeActive() { return apModeActive; }
bool isWiFiConnected() { return WiFi.status() == WL_CONNECTED; }
bool isMQTTConnected() { return client.connected(); }

// 设备数据发送到API
String getJsonData(const String& mac, const ApiParam apiParams[]) {
    // 检查WiFi连接状态
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[ERROR] WiFi not connected"));
        return "";
    }

    // 添加额外延迟确保WiFi完全就绪
    delay(200);

    // 检查可用内存
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 8192) {  // 至少需要8KB
        Serial.printf("[ERROR] 内存不足: %d bytes\n", freeHeap);
        return "";
    }

    WiFiClient& hclient = ConnectionPool::getHttpClient();
    HTTPClient hhttp;

    String payload = "";
    
    // 构建URL
    String url = String(networkConfig.apiData) + "?mac=" + mac;
    for (int i = 0; apiParams[i].value[0] != '\0'; ++i) {
        url += "&" + String(apiParams[i].key) + "=" + String(apiParams[i].value);
    }

    Serial.print(F("[DEBUG] URL = "));
    Serial.println(url);

    // 配置连接参数
    hhttp.setTimeout(3000);
    hhttp.setConnectTimeout(2000);
    hhttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    // 开始连接
    if (!hhttp.begin(hclient, url)) {
        Serial.println(F("[ERROR] HTTP begin failed"));
        hhttp.end();
        hclient.stop();
        ConnectionPool::releaseHttpClient();
        return "";
    }

    // 请求头
    hhttp.addHeader("Content-Type", "application/json");
    hhttp.addHeader("Connection", "close");

    // 发送GET请求
    int httpCode = hhttp.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        payload = hhttp.getString();
        Serial.print(F("[INFO] 获取成功"));
    } else {
        Serial.printf("[WARN] HTTP error, code: %d\n", httpCode);
    }

    // 完整清理：必须按顺序
    hhttp.end();
    delay(50);  // 给socket时间关闭
    hclient.stop();
    ConnectionPool::releaseHttpClient();
    
    Serial.printf("[MEM] Free Heap: %d bytes\n", ESP.getFreeHeap());
    return payload;
}

void sendDeviceData(const String& mac, const String& create_uid, const String& firmwaretype) {
    // 优化: 检查WiFi和内存状态，避免资源浪费
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("❌ WiFi未连接，无法发送设备数据"));
        return;
    }
    
    // 检查可用堆内存，避免内存溢出
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 8192) {  // 需要至少8KB
        Serial.printf("❌ 可用内存不足: %d bytes\n", freeHeap);
        return;
    }
    
    WiFiClient& hclient = ConnectionPool::getHttpClient();
    HTTPClient hhttp;

  Serial.println(F("📤 发送设备数据到API..."));
  hhttp.setTimeout(5000);  // 设置超时时间5秒
  hhttp.setConnectTimeout(3000);  // 连接超时
  
  // 使用正确的API端点
  if (!hhttp.begin(hclient, networkConfig.apiUrl)) {
      Serial.println(F("❌ HTTP begin 失败"));
      ConnectionPool::releaseHttpClient();
      return;
  }
  hhttp.addHeader("Content-Type", "application/json");

  // 创建JSON体 - 根据实际API要求调整字段名
  String requestBody = "{\"macunique\":\"" + mac + 
                       "\",\"create_uid\":\"" + create_uid + 
                       "\",\"firmwaretype\":\"" + firmwaretype + "\"}";

  Serial.print(F("请求体: "));
  Serial.println(requestBody);
  
  // 发送POST请求
  int httpResponseCode = hhttp.POST(requestBody);

  if (httpResponseCode == HTTP_CODE_OK) {
    String response = hhttp.getString();
    Serial.print(F("✅ API响应: "));
    Serial.println(response);
  } else {
    Serial.printf("❌ API请求失败, 错误码: %d\n", httpResponseCode);    
    // 重试逻辑（最多3次）
    for (int i = 0; i < 3; i++) {
      delay(1000);
      Serial.printf("🔄 重试发送设备数据 (%d/3)...\n", i+1);
      // 重试前检查WiFi连接
      if (WiFi.status() != WL_CONNECTED) {
          Serial.println(F("❌ WiFi连接丢失，停止重试"));
          break;
      }
      httpResponseCode = hhttp.POST(requestBody);      
      if (httpResponseCode == HTTP_CODE_OK) {
        Serial.println(F("✅ 重试成功!"));
        break;
      }
    }
  }

  hhttp.end();  // 结束请求
  
  // 优化: 显式释放资源
  ConnectionPool::releaseHttpClient();
  Serial.printf("[MEM] Free Heap: %d bytes\n", ESP.getFreeHeap());
}


void performOTAUpdate_32(const String& firmware_url) {
  Serial.println(F("Starting OTA update..."));
  
  // 断开MQTT连接，停止接收消息
  client.disconnect();
  Serial.println(F("MQTT disconnected"));
  Serial.printf("BLUE前[MEM] Free Heap: %d bytes\n", ESP.getFreeHeap());
                // 停止蓝牙，释放WiFi资源（解决WiFi/BLE冲突）
                stopNimBLE();
                delay(300);
                yield();  // 喂狗，防止看门狗超时
  Serial.printf("BLUE后[MEM] Free Heap: %d bytes\n", ESP.getFreeHeap());
  HTTPClient http;
  static int lastDisplayedPercent = -1;
  
  // 一定要用动态分配OTA缓冲区（16KB），否则一直占用内存导致音频都放不了。
  uint8_t* otaBuffer = (uint8_t*)malloc(16384);
  if (otaBuffer == NULL) {
    client.setCallback(setMQTTCallback); // 恢复MQTT回调，允许接收重启命令
    Serial.println(F("❌ 无法分配OTA缓冲区内存，升级失败"));
    return;
  }
  
  Serial.printf("✅ 成功分配OTA缓冲区: %d bytes\n", 16384);
  Serial.printf("📊 当前可用内存: %d bytes\n", ESP.getFreeHeap());
  
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(firmware_url);
  
  int httpCode = http.GET();
  Serial.printf("HTTP Code: %d\n", httpCode);
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    free(otaBuffer); // 释放内存
    return;
  }
    
  // 初始化显示
  #ifdef USE_STOCK
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(80, 100);
  tft.println("0%%");
  tft.fillRect(20, 130, 200, 16, TFT_DARKGREY);
  #endif
  
  // 处理重定向
  if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
    String newUrl = http.header("Location");
    Serial.printf("Redirect to: %s\n", newUrl.c_str());
    if (newUrl.length() > 0) {
      http.end();
      http.begin(newUrl);
      httpCode = http.GET();
    }
  }
  
  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *client = http.getStreamPtr();
    size_t totalSize = http.getSize();
    Serial.printf("Total size: %d\n", totalSize);
    
    if (Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN)) {
      size_t written = 0;
      
      while (client->connected() && (totalSize <= 0 || written < totalSize)) {
        size_t available = client->available();
        
        if (available > 0) {
          size_t readBytes = client->readBytes(otaBuffer, min(available, (size_t)16384));
          size_t chunkWritten = Update.write(otaBuffer, readBytes);
          written += chunkWritten;
          delay(1); // 喂狗
          
          if (totalSize > 0) {
            int currentPercent = (written * 100LL) / totalSize;
            if (currentPercent - lastDisplayedPercent >= 5) {
              lastDisplayedPercent = currentPercent;
              Serial.printf("Progress: %d%%\n", currentPercent);

                tone(BUZZER_PIN, currentPercent*20, 310-(currentPercent*3)); 
                delay(100); 
              #ifdef USE_STOCK
              tft.fillRect(70, 100, 60, 20, TFT_BLACK);
              tft.setCursor(80, 100);
              tft.printf("%d%%", currentPercent);
              tft.fillRect(22, 132, 196 * currentPercent / 100, 12, TFT_GREEN);
              #endif
            }
          }
        } else {
          delay(5);
          if (totalSize > 0 && written >= totalSize) break;
          if (totalSize <= 0 && !client->available() && written > 0) {
            delay(50);
            if (!client->available()) break;
          }
        }
      }
      
      Serial.printf("Written: %d, Total: %d\n", written, totalSize);
      
      if (Update.end(true)) {
        Serial.println(F("\nUpdate success!"));
        #ifdef USE_STOCK
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(3);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(90, 100);
        tft.println("OK!");
        #endif
        free(otaBuffer); // 升级成功，释放内存
        delay(1000);
        ESP.restart();
      } else {
        Serial.printf("Write failed: %s\n", Update.errorString());
        #ifdef USE_STOCK
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(80, 100);
        tft.println("FAIL");
        #endif
        free(otaBuffer); // 升级失败，释放内存
      }
    } else {
      free(otaBuffer); // Update.begin失败，释放内存
    }
  } else {
    free(otaBuffer); // HTTP请求失败，释放内存
  }
  
  http.end();
}