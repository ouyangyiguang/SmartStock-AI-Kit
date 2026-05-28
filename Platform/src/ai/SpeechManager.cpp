#include "SpeechManager.h"
#include "MicModule.h"
#include "../AudioPlayer.h"
#include "../tftv/ScreenManager.h"
#include "../tftv/AIUI.h"
#include "../tftv/StockUI.h"
#include <HTTPClient.h>
#include "WiFiMQTT.h"

// 静态内部变量
static bool waitingForWakeupSound = false;
std::atomic<bool> SpeechManager::processingAudio{false};
unsigned long SpeechManager::waitWakeupStartTime = 0;
QueueHandle_t SpeechManager::_audioQueue = NULL;
TaskHandle_t SpeechManager::_workerTask = NULL;
static unsigned long audioFinishedTime = 0; 
static const unsigned long WAKEUP_COOLDOWN_MS = 2500;  // 录音结束后5秒内不检测唤醒 
static const unsigned long TTS_FINISH_DEBOUNCE_MS = 800;  // TTS播完后等多久才续录（过滤硬件buffer残留）
static unsigned long lastPlayingTime = 0;  // 新增：TTS播放时间跟踪
static std::atomic<bool> _ttsEverPlayed{false}; // 关键：TTS是否真正开始播放过（防止流启动延迟误触发续录）
// 核心标志位：是否处于连续对话（自动续录）模式
static std::atomic<bool> _isContinuingConversation{false};
static int _continuationCount = 0;      // 当前续录次数，>=5 后需重新唤醒

bool SpeechManager::isProcessingAudio() {
    return processingAudio;
}

bool SpeechManager::canWakeup() {
    if (audioFinishedTime == 0) return true;
    return (millis() - audioFinishedTime) > WAKEUP_COOLDOWN_MS;
}

SpeechManager& SpeechManager::getInstance() {
    static SpeechManager instance;
    return instance;
}

void SpeechManager::resetToIdle() {
    audioFinishedTime = millis();
    switchToState(STATE_IDLE);
}

void SpeechManager::init() {
    MicModule::getInstance().init();
    MicModule::getInstance().setWakeupCallback(onWakeupDetected);
    MicModule::getInstance().setAudioDataCallback(onAudioData);
    switchToState(STATE_IDLE);

    _audioQueue = xQueueCreate(2, sizeof(AudioTaskParams*));
    if (!_audioQueue) {
        Serial.println(F("❌ AudioWorker 队列创建失败"));
        return;
    }
    xTaskCreatePinnedToCore(audioWorker, "AudioWorker", WORKER_STACK, NULL, 2, &_workerTask, 1);
    if (!_workerTask) {
        Serial.println(F("❌ AudioWorker 任务创建失败"));
    }
}

void SpeechManager::start() { 
    switchToState(STATE_IDLE); 
}

void SpeechManager::stop() { 
    switchToState(STATE_IDLE); 
    MicModule::getInstance().stopListeningImmediately(); 
}

// 辅助函数：URL编码处理
String urlEncode(String text) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < text.length(); i++) {
        c = text.charAt(i);
        if (isalnum(c)) {
            encodedString += c;
        } else if (c == ' ') {
            encodedString += '+';
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) code0 = c - 10 + 'A';
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

void SpeechManager::poll() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 50) return;
    lastPoll = millis();

    if (waitingForWakeupSound) {
        if (!AudioPlayer::getInstance().isPlaying() && (millis() - waitWakeupStartTime > 200)) {
            waitingForWakeupSound = false;
            audioFinishedTime = millis();            
            _isContinuingConversation = false; // 唤醒音播完，绝对开启新对话  
            Serial.println("🎤 唤醒音播完，开启新对话录音...");
            switchToState(STATE_RECORDING);
        }
        return; // 只要处于唤醒音播放期间，不执行后面的 switch 逻辑
    }

    // 动态气泡文字：录音时光标闪烁，处理时显示状态
    static unsigned long lastBlinkTime = 0;
    static bool blinkOn = true;
    SystemState curState = _currentState;

    if (curState == STATE_RECORDING && millis() - lastBlinkTime > 500) {
        lastBlinkTime = millis();
        blinkOn = !blinkOn;
        AIUI::get().updateLastBubbleText(blinkOn ? "正在听_" : "正在听");
    }

    switch (curState) {
        case STATE_PLAYING:
            // TTS播放结束，自动触发续录
            if (AudioPlayer::getInstance().isPlaying()) {
                // 只要还在播，就不断更新这个时间点
                lastPlayingTime = millis();
                _ttsEverPlayed = true; // 标记确实开始播了
            } else {
                // 关键守卫：必须检测到 TTS 至少播放过一次，才允许判定"播完"
                // 否则 playStream 异步启动期间 isPlaying=false 会被误判为"已经播完"
                if (_ttsEverPlayed && millis() - lastPlayingTime > TTS_FINISH_DEBOUNCE_MS) { 
                    audioFinishedTime = millis();
                    _continuationCount++;
                    if (_continuationCount >= 5) {
                        Serial.printf("🛑 续录已达 %d 次，停止自动续录，需重新唤醒\n", _continuationCount);
                        _isContinuingConversation = false;
                        _ttsEverPlayed = false;
                        switchToState(STATE_IDLE);
                    } else {
                        Serial.printf("🎵 TTS 播放完毕，续录 %d/5...\n", _continuationCount);
                        _isContinuingConversation = true; 
                        _ttsEverPlayed = false;
                        switchToState(STATE_RECORDING);
                    }
                }
            }
            break;
        case STATE_RECORDING:
            if (millis() - _stateStartTime > MAX_RECORD_TIME_WITH_SPEECH) {
                Serial.println("⚠️ 录音超时限制，提交已有音频");
                auto& mic = MicModule::getInstance();
                mic.freezeRecording();
                size_t pcmLen = mic.getRecordedLen();
                if (pcmLen > WAV_HEADER_SIZE) {
#ifdef VAD_SKIP_NO_VOICE
                    if (!MicModule::hasLiveSpeech()) {
                        Serial.println(F("⚠️ 超时且未检测到语音，跳过 HTTP"));
                        if (_isContinuingConversation) {
                            ScreenManager::getInstance().showAIResponse("无响应互动结束");
                        } else {
                            ScreenManager::getInstance().showAIResponse("未检测到语音");
                        }
                        switchToState(STATE_IDLE);
                        break;
                    }
#endif
                    mic.fillWavHeader();
                    processingAudio = true;
                    switchToState(STATE_PROCESSING);
                    sendAudioToAI(mic.getRecordedBuffer(), pcmLen);
                } else {
                    switchToState(STATE_IDLE);
                }
            }
            break;
    }
}

void SpeechManager::switchToState(SystemState newState) {
    // 避免重复状态切换
    if (_currentState == newState) return;
    
    _currentState = newState;
    _stateStartTime = millis();

    switch (newState) {
        case STATE_IDLE:
            processingAudio = false;
            _isContinuingConversation = false;
            _continuationCount = 0;
            audioFinishedTime = millis();  // 设置冷却时间，防止立即唤醒
            MicModule::getInstance().stopListeningImmediately();
            ScreenManager::getInstance().showAIState("#888888 待机中", "正在唤醒", 0x888888, 0x888888);
            break;
        case STATE_RECORDING:
            // 优化：先启动录音再更新UI，避免延迟
            MicModule::getInstance().startListening();
            ScreenManager::getInstance().switchToAI();
            AIUI::get().addBubble("正在听_", true);
            ScreenManager::getInstance().showAIState("#ffaa00 正在听...", "请说话", 0xFFA500, 0x00FF00);
            ScreenManager::getInstance().updateAIInteraction();
            break;
        case STATE_PROCESSING:
            AIUI::get().updateLastBubbleText("识别中...");
            ScreenManager::getInstance().showAIState("#00e5ff 识别中...", "", 0xFFA500, 0xFFA500);
            ScreenManager::getInstance().updateAIInteraction();
            break;
        case STATE_PLAYING:
            lastPlayingTime = millis();  // 防止 poll() 读到 0 而误判 TTS 播完
            _ttsEverPlayed = false;      // 进入 PLAYING 时重置，等待 isPlaying() 真正返回 true
            ScreenManager::getInstance().updateAIInteraction();
            ScreenManager::getInstance().showAIState("#00ff00 AI说", "回复中", 0x00FF00, 0x00FF00);
            break;
    }
}

void SpeechManager::onWakeupDetected() {
    if (ScreenManager::getInstance().hasStockUI()) {
        StockUI::get().clearTip();
    }
    ScreenManager::getInstance().switchToAI();
    _isContinuingConversation = false;
    _continuationCount = 0; // 新的唤醒词触发，重置续录计数
    if (WiFi.status() != WL_CONNECTED) {
        ScreenManager::getInstance().showAIResponse("网络未连接");
        return;
    }
    int randomSound = random(5);
    String soundPath = "/hi/" + String(randomSound) + ".wav";
    Serial.printf("🔊 播放随机唤醒词: %s\n", soundPath.c_str());
    AudioPlayer::getInstance().play(soundPath.c_str(), SOURCE_LITTLEFS);
    
    waitingForWakeupSound = true;
    waitWakeupStartTime = millis();
}

void SpeechManager::onAudioData(const int16_t* audioData, size_t dataSize) {
    auto& sm = SpeechManager::getInstance();
    
    // 1. 内存安全检查优化
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 30000) {  // 从40000降低到30000
        Serial.printf("⚠️ 内存过低(%u)，清理对话历史\n", freeHeap);
        AIUI::get().clearDialog();
    }

    if (!(dataSize == 0 && sm.getState() == STATE_RECORDING)) return;

    auto& mic = MicModule::getInstance();
    mic.stopListeningImmediately();

    // 2. 读取录音状态
    bool hasVoice = mic.getEverDetectedSpeech(); 
    size_t pcmLen = mic.getRecordedLen();
    Serial.printf("📊 onAudioData: hasVoice=%d pcmLen=%u continuing=%d\n",
                  (int)hasVoice, (unsigned)pcmLen, (int)_isContinuingConversation);

    if (pcmLen < 2000) {
        Serial.println("⚠️ 录音太短，放弃处理");
        AIUI::get().removeLastBubble();
        if (!_isContinuingConversation && ScreenManager::getInstance().hasStockUI()) {
            StockUI::get().showTip("录音太短，请重试");
        }
        sm.resetToIdle();
        return;
    }

// #define VAD_SKIP_NO_VOICE  // 取消注释启用：无语音时跳过 HTTP 上传
#ifdef VAD_SKIP_NO_VOICE
    if (!hasVoice) {
        Serial.println(F("⚠️ 未检测到语音，跳过 HTTP 提交"));
        if (_isContinuingConversation) {
            ScreenManager::getInstance().showAIResponse("无响应互动结束");
        } else {
            ScreenManager::getInstance().showAIResponse("未检测到语音");
        }
        sm.resetToIdle();
        return;
    }
#endif

    sm.switchToState(STATE_PROCESSING);
    processingAudio = true;
    mic.fillWavHeader();
    uint8_t* pcmBuffer = mic.getRecordedBuffer();

    // 使用异步处理避免阻塞主线程
    sendAudioToAI(pcmBuffer, pcmLen);
}

// AudioWorker: 用已有任务栈处理音频上传，避免动态创建任务耗尽 DRAM
void SpeechManager::audioWorker(void* param) {
    AudioTaskParams* job = NULL;
    while (true) {
        if (xQueueReceive(_audioQueue, &job, portMAX_DELAY) == pdTRUE) {
            if (job) {
                processAudioJob(job);
                delete job;
            }
        }
    }
}

void SpeechManager::processAudioJob(AudioTaskParams* params) {
    String aiText, userText, speed, acmd;
    HTTPClient http;
    http.begin("http://run.yodin.com:5001/voice_chat");
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-API-Token", "yodin_esp32_2026secKey");
    http.setTimeout(8000);
    int httpCode = http.POST(params->pcmBuffer, params->pcmLen);
    if (httpCode == HTTP_CODE_OK) {
        DynamicJsonDocument doc(8192);
        deserializeJson(doc, http.getStream());
        userText = doc["text"].as<String>();
        Serial.printf("[SpeechMgr] 识别到: %s\n", userText.c_str());
        bool textValid = (userText.length() > 0 && userText.length() < 256);
        if (!textValid) {
            Serial.print("[识别空]"); http.end();
            AIUI::get().removeLastBubble();
            ScreenManager::getInstance().showAIResponse("无响应互动结束");
            SpeechManager::getInstance().resetToIdle();
            return;
        }
        Serial.print("[识别有]");
        AIUI::get().updateLastBubbleText(userText.c_str()); http.end();
        ScreenManager::getInstance().showAIState("#00e5ff 思考中...", "", 0xFFA500, 0xFFA500);
        if (userText.length() > 0) {
            HTTPClient http2;
            http2.begin("http://run.yodin.com:5001/text_chat");
            http2.addHeader("Content-Type", "application/json");
            http2.addHeader("X-API-Token", "yodin_esp32_2026secKey");
            http2.setTimeout(8000);
            String requestBody;
            { StaticJsonDocument<384> reqDoc; reqDoc["text"] = userText; serializeJson(reqDoc, requestBody); }
            int httpCode2 = http2.POST(requestBody);
            if (httpCode2 == HTTP_CODE_OK) {
                DynamicJsonDocument doc2(8192);
                deserializeJson(doc2, http2.getStream());
                aiText = doc2["ai_text"].as<String>();
                speed = doc2["speed"].as<String>();
                acmd = doc2["cmd"].as<String>();
                Serial.printf("[SpeechMgr] AI回复: %s, cmd: %s\n", aiText.c_str(), acmd.c_str());
            } else Serial.printf("❌ text_chat 请求失败: %d\n", httpCode2);
            http2.end();
        }
    } else {
        Serial.printf("❌ voice_chat 请求失败: %d\n", httpCode); http.end();
        AIUI::get().removeLastBubble();
        AIUI::get().setCountdown(0);
        if (ScreenManager::getInstance().hasStockUI()) StockUI::get().showTip("语音识别失败");
        SpeechManager::getInstance().resetToIdle();
    }
    if (acmd.length() > 0 && acmd.length() < 32) {
        if (acmd == "UIPHOTO") { ScreenManager::getInstance().showAIResponse("好，切换到相册"); vTaskDelay(800); ScreenManager::getInstance().switchToPhotos(); SpeechManager::getInstance().resetToIdle(); }
        else if (acmd == "UISTOCK") { ScreenManager::getInstance().showAIResponse("好，切换到股票"); vTaskDelay(800); ScreenManager::getInstance().switchToStock(); SpeechManager::getInstance().resetToIdle(); }
        else if (acmd == "CMD_ON" || acmd == "CMD_OFF") { SpeechManager::getInstance().resetToIdle(); }
        else if (aiText.length() > 0) updateUIAfterAI(aiText, userText, speed);
    } else {
        if (userText.isEmpty() || userText == " -(未检测到语音)-") {
            AIUI::get().removeLastBubble();
            if (_isContinuingConversation) ScreenManager::getInstance().showAIResponse("无响应互动结束");
            SpeechManager::getInstance().resetToIdle();
        } else if (aiText.length() > 0) updateUIAfterAI(aiText, userText, speed);
        else SpeechManager::getInstance().resetToIdle();
    }
}

void SpeechManager::sendAudioToAI(uint8_t* pcmBuffer, size_t pcmLen) {
    AudioTaskParams* taskParams = new AudioTaskParams{pcmBuffer, pcmLen};
    if (xQueueSend(_audioQueue, &taskParams, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println(F("❌ AudioWorker 队列满"));
        delete taskParams;
        SpeechManager::getInstance().resetToIdle();
    }
}

// 新增UI更新函数
void SpeechManager::updateUIAfterAI(const String& aiText, const String& userText, const String& speed) {
    auto& sm = SpeechManager::getInstance();

    if (aiText.length() > 0) {


        ScreenManager::getInstance().showAIResponse(aiText.c_str());        
        String ttsUrl = "http://run.yodin.com:5001/tts1?text=" + urlEncode(aiText) + "&speed=" + speed + "&token=yodin_esp32_2026secKey";

        sm.switchToState(STATE_PLAYING);
        AudioPlayer::getInstance().playStream(ttsUrl);
    } else {
        if (!_isContinuingConversation) {
            if (ScreenManager::getInstance().hasStockUI()) {
                StockUI::get().showTip("AI未返回回复");
            }
            ScreenManager::getInstance().switchToStock();
        }
        sm.resetToIdle();
    }
}
