#include "MicModule.h"
#include <driver/i2s.h>
#include "../AudioPlayer.h"
#include "../utils/pins.h" 
#include "SpeechManager.h"
#include <cstring>

static i2s_port_t i2s_port = I2S_NUM_1;
static std::atomic<bool> i2s_initialized{false};
static SemaphoreHandle_t i2sMutex = NULL; // 保护 i2s_read 与 driver_install/uninstall 不并发
static std::atomic<bool> s_isListening{false};
std::atomic<bool> recordingLocked{false};
static std::atomic<bool> s_forceStop{false};
std::atomic<bool> MicModule::_everDetectedSpeech{false};
std::atomic<bool> MicModule::_lastSessionHadSpeech{false};
// 唤醒检测参数
static const int WAKE_THRESHOLD = 29;         // 唤醒能量阈值
static const int WAKE_MIN_COUNT = 2;          // 连续2次触发
// WAKE_WINDOW：从第一次触发开始的总体时间窗口（毫秒）。
// 如果两次触发之间的总耗时超过此值，soundCount 归零重新计数。
// 调低 → 要求触发更紧凑，适合短促声音（拍手、单击）。
// 调高 → 容忍更分散的触发，适合拖沓的声音（咳嗽、连续说话）。
static const unsigned long WAKE_WINDOW = 2000;
// WAKE_MAX_INTERVAL：相邻两次触发之间的最大间隔（毫秒）。
// 若间隔 > 此值，即使仍在 WAKE_WINDOW 内，也视为新序列重头计数。
// 调低 → 触发必须非常密集，防误触更强。
// 调高 → 允许触发之间有更长的停顿，适合气息不连贯的声音（咳嗽间隔可达 2-3s）。
static const unsigned long WAKE_MAX_INTERVAL = 2500;
// 录音参数
extern const unsigned long MAX_RECORD_TIME_NO_SPEECH = 3500;  // 无语音超时（续录需要用户听完思考再开口）
extern const unsigned long MAX_RECORD_TIME_WITH_SPEECH = 9500; // 安全硬上限（有语音时每次刷新，基本不会触发）
extern const unsigned long VAD_TIMEOUT = 3500;               // 静音判定，容忍自然停顿（从2500放宽到4000）
// VAD阈值上限
static const int SPEECH_THRESHOLD = 50;
// 能量平滑器与 DC blocker 状态（模块级，录音开始时重置）
static int energySmootherLast = 0;
static int hpfPrevX = 0;
static int hpfPrevY = 0;
static int preEmphPrev = 0;       // 预加重滤波器状态
// 最小统计法：跟踪过去 5 秒的能量最小值作为真实底噪估计
static int  noiseFloorMin = 30;          // 当前真实底噪估计
static int  noiseFloorWindowMin = 99999; // 当前窗口内的最小
static unsigned long noiseFloorWindowStart = 0;

// 当前帧的零穿越率（0~100，语音一般在 5~40）
static std::atomic<int> s_currentZCR{0};

// WAV文件头和缓冲区
static const int AUDIO_BUFFER_SIZE = 16000 * 2 * 15 + WAV_HEADER_SIZE; // 15秒，适配 MAX_RECORD_TIME_WITH_SPEECH=13.5s
static int16_t* audioBuffer = nullptr;
static size_t audioBufferPos = 0;
static SemaphoreHandle_t audioMutex = NULL;

// 运行时状态
static int soundCount = 0;
static unsigned long lastSoundTime = 0;
static unsigned long recordingStartTime = 0;
static unsigned long lastSpeechTime = 0;
static unsigned long firstSpeechTime = 0;
static unsigned long startupTime = 0;

// 环境噪音基准
static int envBaseEnergy = 10;
// 浮动指示器用：最近一次能量值与唤醒事件标志
static std::atomic<int> s_currentEnergy{0};
static std::atomic<bool> s_wakewordPending{false};
static unsigned long envBaseTime = 0;
static const int ENV_SAMPLE_COUNT = 20;  // 环境噪音采样次数
static const int MIN_WAKE_THRESHOLD = 15; // 【唤醒阈值下限】待机时检测唤醒词的最低阈值，防止底噪误触发

static const int HISTORY_SAMPLES = 8000; // 16000Hz * 0.5s = 8000采样
static int16_t* historyBuffer = nullptr;
static int historyPtr = 0;
static bool historyFull = false;
static bool last_s_isListening = false; // 用于捕捉“刚开始录音”的瞬间


WakeupDetectedCallback MicModule::_wakeupCb = nullptr;
AudioDataCallback MicModule::_audioCb = nullptr;

MicModule& MicModule::getInstance() {
    static MicModule instance;
    return instance;
}

void MicModule::suspendI2S() {
    if (i2sMutex == NULL) return;
    if (xSemaphoreTake(i2sMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.println("⚠️ suspendI2S 获取锁失败");
        return;
    }
    if (i2s_initialized) {
        i2s_initialized = false; // 先标记，让 micTask 下一轮跳过
        i2s_driver_uninstall(i2s_port);

        // 关键：卸载 I2S_NUM_1 后，GPIO路由仍指向它（已失效）
        // 必须手动把 GPIO 路由切回喇叭的 I2S_NUM_0，否则喇叭无声
        i2s_pin_config_t spk_pins = {
            .bck_io_num = I2S_BCK,
            .ws_io_num = I2S_WS,
            .data_out_num = I2S_DOUT,
            .data_in_num = I2S_PIN_NO_CHANGE
        };
        i2s_set_pin(I2S_NUM_0, &spk_pins);

        Serial.println("🎤 麦克风I2S已暂停，GPIO已切回喇叭");
    }
    xSemaphoreGive(i2sMutex);
}

void MicModule::resumeI2S() {
    if (i2sMutex == NULL) return;
    if (xSemaphoreTake(i2sMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.println("⚠️ resumeI2S 获取锁失败");
        return;
    }
    if (i2s_initialized) { xSemaphoreGive(i2sMutex); return; } // 已在运行

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_I2S_SCK,
        .ws_io_num = MIC_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_I2S_SD
    };

    if (i2s_driver_install(i2s_port, &cfg, 0, NULL) == ESP_OK) {
        if (i2s_set_pin(i2s_port, &pin_config) == ESP_OK) {
            i2s_initialized = true;
            // 丢弃初始不稳定数据
            int16_t dummyBuf[128];
            size_t dummyRead;
            for (int i = 0; i < 10; i++) {
                i2s_read(i2s_port, dummyBuf, sizeof(dummyBuf), &dummyRead, pdMS_TO_TICKS(10));
            }
            startupTime = millis();
            Serial.println("🎤 麦克风I2S已恢复，GPIO已切到麦克风");
        }
    }
    xSemaphoreGive(i2sMutex);
}

void MicModule::init() {
    if (audioMutex == NULL) audioMutex = xSemaphoreCreateMutex();
    if (i2sMutex == NULL) i2sMutex = xSemaphoreCreateMutex();

    if (historyBuffer == nullptr) {
        historyBuffer = (int16_t*)heap_caps_malloc(HISTORY_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!historyBuffer) {
            historyBuffer = (int16_t*)malloc(HISTORY_SAMPLES * sizeof(int16_t));
        }
        if (historyBuffer) {
            memset(historyBuffer, 0, HISTORY_SAMPLES * sizeof(int16_t));
        } else {
            Serial.println(F("❌ historyBuffer 分配失败"));
        }
    }
    
    if (audioBuffer == nullptr) {
        audioBuffer = (int16_t*)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (audioBuffer) {
            memset(audioBuffer, 0, AUDIO_BUFFER_SIZE);
            Serial.printf("✅ PSRAM 缓冲: %p\n", audioBuffer);
        } else {
            Serial.println("❌ PSRAM 分配失败，使用内部内存");
            audioBuffer = (int16_t*)malloc(AUDIO_BUFFER_SIZE);
            if (audioBuffer) memset(audioBuffer, 0, AUDIO_BUFFER_SIZE);
        }
    }

    // 等待启动音频播完，避免抢占共享 GPIO 导致杂音
    Serial.println("🎤 等待音频播放完毕再初始化麦克风...");
    int waitCount = 0;
    while (AudioPlayer::getInstance().isPlaying() && waitCount < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
    }

    i2s_driver_uninstall(i2s_port);
    delay(100);
    
    // 使用更稳定的I2S配置
    // NOTE: i2s_config_t / i2s_driver_install 在 ESP-IDF v5.x 中已废弃，改用 i2s_std.h 新 API
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,                    // 恢复为4个缓冲区
        .dma_buf_len = 256,                    // 恢复为256长度
        .use_apll = true,                     // 禁用APLL，使用更稳定的时钟
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_I2S_SCK,
        .ws_io_num = MIC_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_I2S_SD
    };

    if (i2s_driver_install(i2s_port, &cfg, 0, NULL) == ESP_OK) {
        if (i2s_set_pin(i2s_port, &pin_config) == ESP_OK) {
            i2s_initialized = true;
            startupTime = millis();
            Serial.println("✅ I2S 初始化完成（稳定配置）");
            
            // 测试麦克风读取
            int16_t testBuf[32];
            size_t testBytesRead = 0;
            esp_err_t testRes = i2s_read(i2s_port, testBuf, sizeof(testBuf), &testBytesRead, pdMS_TO_TICKS(100));
            if (testRes == ESP_OK && testBytesRead > 0) {
                Serial.printf("✅ 麦克风测试成功，读取 %d 字节\n", testBytesRead);
            } else {
                Serial.printf("❌ 麦克风测试失败，错误码: %d\n", testRes);
            }
        } else {
            Serial.println("❌ I2S 引脚配置失败");
        }

    Serial.println("⏳ 清理启动初期杂音...");
    int16_t dummyBuf[128];
    size_t dummyRead;
    for(int i = 0; i < 20; i++) { // 连续空读一段时间，丢弃初始不稳定数据
        i2s_read(i2s_port, dummyBuf, sizeof(dummyBuf), &dummyRead, pdMS_TO_TICKS(10));
    }
    startupTime = millis(); // 重新标记真正的可用时间


    } else {
        Serial.println("❌ I2S 驱动安装失败");
    }





    static bool isTaskCreated = false;
    if (!isTaskCreated) {
        xTaskCreate(micTask, "micTask", 6144, NULL, 5, NULL);
        isTaskCreated = true;
    }
}

void MicModule::micTask(void* param) {
    int16_t buf[256];
    size_t bytes_read = 0;

    while (true) {
        if (!i2s_initialized) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

        // Check for force stop flag
        if (s_forceStop) {
            s_forceStop = false;
            if (!s_isListening) {
                recordingLocked = false;
                if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    audioBufferPos = 0;
                    xSemaphoreGive(audioMutex);
                }
                Serial.println("🛑 Force stop 生效");
            }
        }


        // 读取 I2S（加锁保护，避免与 suspendI2S 卸载 driver 并发）
        esp_err_t res = ESP_FAIL;
        if (i2sMutex && xSemaphoreTake(i2sMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (i2s_initialized) {
                res = i2s_read(i2s_port, buf, sizeof(buf), &bytes_read, pdMS_TO_TICKS(30));
            }
            xSemaphoreGive(i2sMutex);
        }
        if (res != ESP_OK || bytes_read == 0) { vTaskDelay(1); continue; }

        int samples = bytes_read / 2;
        int energy = calculateEnergy(buf, samples);
        s_currentEnergy = energy; // 暴露给浮动指示器

        // ==========================================================
        // 【新增功能：预录音逻辑】
        if (!s_isListening) {
            if (historyBuffer) {
                for (int i = 0; i < samples; i++) {
                    historyBuffer[historyPtr++] = buf[i];
                    if (historyPtr >= HISTORY_SAMPLES) {
                        historyPtr = 0;
                        historyFull = true;
                    }
                }
            }
            last_s_isListening = false;
        }
        else if (s_isListening && !last_s_isListening) {
            // 刚触发录音的瞬间：把历史数据“砰”地一下塞进 audioBuffer 开头
            if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                int copyCount = 0;
                if (historyBuffer) {
                    copyCount = historyFull ? HISTORY_SAMPLES : historyPtr;
                    if (historyFull) {
                        memcpy((int16_t*)audioBuffer + WAV_HEADER_SIZE/2, historyBuffer + historyPtr, (HISTORY_SAMPLES - historyPtr) * 2);
                        memcpy((int16_t*)audioBuffer + WAV_HEADER_SIZE/2 + (HISTORY_SAMPLES - historyPtr), historyBuffer, historyPtr * 2);
                    } else {
                        memcpy((int16_t*)audioBuffer + WAV_HEADER_SIZE/2, historyBuffer, historyPtr * 2);
                    }
                }
                audioBufferPos = WAV_HEADER_SIZE/2 + copyCount;
                last_s_isListening = true; // 标记已经补偿过了
                xSemaphoreGive(audioMutex);
                Serial.printf("🎬 预录音补偿: %d samples\n", copyCount);
            }
        }


        
        if (!s_isListening && !AudioPlayer::getInstance().isPlaying()) {
            static unsigned long lastDebugPrint = 0;
            if (millis() - lastDebugPrint > 5000) {  // 减少到5秒一次
                lastDebugPrint = millis();
                //Serial.printf("🔊 energy=%d envBase=%d\n", energy, envBaseEnergy);
            }
        }

        if (s_isListening && !recordingLocked && !AudioPlayer::getInstance().isPlaying()) {
            Serial.print(F("^"));
            if (xSemaphoreTake(audioMutex, 0) == pdTRUE) {
                size_t currentBytePos = audioBufferPos * 2;
                if (audioBuffer && (currentBytePos + bytes_read) < AUDIO_BUFFER_SIZE) {
                    memcpy(((uint8_t*)audioBuffer) + currentBytePos, buf, bytes_read);
                    audioBufferPos += samples;
                }
                xSemaphoreGive(audioMutex);
            }

            unsigned long now = millis();
            
            // 【升级】VAD：宽松灵敏模式 - 录音中只要稍微高于底噪就算语音
            // 真实底噪估计：取 noiseFloorMin 和 envBaseEnergy 较大者
            int noiseFloor = (noiseFloorMin > envBaseEnergy) ? noiseFloorMin : envBaseEnergy;
            if (noiseFloor < 3) noiseFloor = 3;

            // 谱减法 lite：减去 0.8× 底噪（保守减，保留更多弱语音）
            int cleanEnergy = energy - (noiseFloor * 8 / 10);
            if (cleanEnergy < 0) cleanEnergy = 0;

            // SNR 乘法阈值：1.4× 底噪（约 +3dB，捕获轻声/呼吸声/小声词）
            bool snrPass = (energy * 10 > noiseFloor * 14);

            bool absPass = (energy >= 8);

            bool zcrPass = (s_currentZCR >= 2 && s_currentZCR <= 120);

            if (cleanEnergy > 0 && (snrPass || absPass) && zcrPass) {
                static int lastEnergy = 0;
                int energyDelta = energy - lastEnergy;
                lastEnergy = energy;
                bool isImpulse = (energy > 1500 && energyDelta > energy * 0.9);
                if (!isImpulse) {
                    lastSpeechTime = now;
                    firstSpeechTime = now;
                    if (!_everDetectedSpeech) {
                        Serial.printf("🗣️ 首次检测到语音 e=%d noise=%d clean=%d zcr=%d\n",
                                      energy, noiseFloor, cleanEnergy, (int)s_currentZCR);
                    }
                    _everDetectedSpeech = true;
                } else {
                    static unsigned long lastFilterPrint = 0;
                    if (now - lastFilterPrint > 500) {
                        lastFilterPrint = now;
                        Serial.printf("🛡️ 冲击噪声拦截: e=%d delta=%d\n", energy, energyDelta);
                    }
                }
            }

            bool shouldStop = false;
            const char* reason = "";

            if (!_everDetectedSpeech) {
                // 从未检测到语音：无语音超时
                if (now - recordingStartTime > MAX_RECORD_TIME_NO_SPEECH) {
                    shouldStop = true;
                    reason = "无语音超时";
                }
            } else {
                // 已检测到语音：VAD 静音判定（容忍自然停顿）
                if (now - lastSpeechTime > VAD_TIMEOUT) {
                    shouldStop = true;
                    reason = "VAD静音";
                }
                // 安全硬上限：从首次语音起的总录音时长
                else if (now - firstSpeechTime > MAX_RECORD_TIME_WITH_SPEECH) {
                    shouldStop = true;
                    reason = "录音总时长超时";
                }
            }

            if (shouldStop) {
                unsigned long totalMs = now - recordingStartTime;
                Serial.printf("🛑 停止录音! 原因: %s (total=%lu, everDetectedSpeech=%d)\n", 
                    reason, totalMs, (bool)_everDetectedSpeech);
                Serial.printf("📦 audioBufferPos=%d WAV_HEADER_SIZE/2=%d\n", 
                    audioBufferPos, WAV_HEADER_SIZE/2);
                s_isListening = false;
                bool hadSpeech = _everDetectedSpeech;
                _lastSessionHadSpeech = hadSpeech;
                _everDetectedSpeech = false;// 实时变量现在可以放心清零了
                if (_audioCb) _audioCb(nullptr, 0);
            }
        } else if (!AudioPlayer::getInstance().isPlaying()) {
            static unsigned long lastPrint = 0;
            if (millis() - lastPrint > 1000) {
                lastPrint = millis();
            }

            if (detectRepeatSound(energy)) {
                Serial.printf("🔔 唤醒! energy=%d envBase=%d\n", energy, envBaseEnergy);
                s_isListening = true;
                s_wakewordPending = true; // 给指示器显示一次闪烁
                recordingStartTime = millis();
                lastSpeechTime = millis();
                if (_wakeupCb) _wakeupCb();
            }
        }
        vTaskDelay(5);
    }
}

bool MicModule::detectRepeatSound(int energy) {
    unsigned long now = millis();
    static int lastEnergyValue = 0;
    
    // 录音期间不检测唤醒
    if (s_isListening) return false;
    
    // 处理音频期间不检测唤醒
    if (SpeechManager::isProcessingAudio()) return false;
    
    // 音频结束后冷却期内不检测唤醒
    if (!SpeechManager::canWakeup()) return false;
    
    if (soundCount > 0 && (now - lastSoundTime > WAKE_WINDOW)) {
        soundCount = 0;
    }
    
    // 修复环境噪音基准计算
    if (envBaseTime == 0) envBaseTime = now;
    if (now - envBaseTime < 3000 && soundCount == 0) {  // 调整为3秒
        static int envSum = 0;
        static int envN = 0;
        
        // 修复：移除过度过滤，计算所有信号
        // 只过滤掉明显异常的低值（小于1）
        if (energy >= 1) {  
            envSum += energy;
            envN++;
        }
        
        if (envN >= ENV_SAMPLE_COUNT) {
            envBaseEnergy = envSum / envN;
            // 【修复】放宽上限到 300，避免噪环境下负 delta 诡异触发
            if (envBaseEnergy < 5)   envBaseEnergy = 5;
            if (envBaseEnergy > 300) envBaseEnergy = 300;
        }
    }
    
    if (!s_isListening && !AudioPlayer::getInstance().isPlaying()) {
        static int envUpdateCounter = 0;

        // 1. 延长冷启动屏蔽期，并在屏蔽期内快速学习底噪
        if (now - startupTime < 4000) {
            if (energy > 0) envBaseEnergy = (envBaseEnergy * 31 + energy) / 32;
            return false;
        }

        // 2. 抑制极短时间内的连续触发（防止电磁干扰脉冲）
        // 如果能量极大且间隔极短，视为噪声
        if (energy > 1000 && (now - lastSoundTime < 200)) {
            return false;
        }

        envUpdateCounter++;
        if (envUpdateCounter >= 100) {  // 调整为正常频率
            envUpdateCounter = 0;
            
            // 以真实底噪估计 noiseFloorMin 为上限参考，避免语音被误当底噪学习
            int learnCap = noiseFloorMin * 3;
            if (learnCap < 100) learnCap = 100;
            if (energy >= 1 && energy <= learnCap) {
                if (energy > envBaseEnergy) {
                    envBaseEnergy = (envBaseEnergy * 63 + energy) / 64;
                } else {
                    envBaseEnergy = (envBaseEnergy * 15 + energy) / 16;
                }
            }

            // 【修复】放宽上限到 300
            if (envBaseEnergy < 5)   envBaseEnergy = 5;
            if (envBaseEnergy > 300) envBaseEnergy = 300;
        }
    }
    
    if (s_isListening || AudioPlayer::getInstance().isPlaying()) return false;
    //if (now - startupTime < 2000) return false;  // 恢复正常启动等待时间
    if (now - startupTime < 5000) { // 从 2000 延长到 5000
    // 在这 5 秒内，只更新环境基准，不进行任何唤醒判定
    if (energy > 0) {
        envBaseEnergy = (envBaseEnergy * 7 + energy) / 8; // 快速收敛基准
    }
    return false; 
    }

    if ((energy > envBaseEnergy * 20 && envBaseEnergy > 0) || energy > 800) {
    Serial.printf("🛡️ 强冲击拦截: %d\n", energy);
    soundCount = 0; // 彻底重置计数
    return false;
    }

    // =================================================================
    // 【唤醒阈值】比 VAD 严格，防误触发
    //   - SNR: 2.5× 底噪 (≈ +8dB)，远高于 VAD 的 1.4×
    //   - ZCR: 3~80，比 VAD 窄，拒绝尖啫/电流啸叫
    //   - 绝对下限: MIN_WAKE_THRESHOLD=40，极安静环境也不会误触
    // =================================================================
    int wakeNoiseFloor = (noiseFloorMin > envBaseEnergy) ? noiseFloorMin : envBaseEnergy;
    if (wakeNoiseFloor < 5) wakeNoiseFloor = 5;

    int dynamicThreshold = wakeNoiseFloor * 20 / 10;  // 2.0× SNR
    if (dynamicThreshold < MIN_WAKE_THRESHOLD) dynamicThreshold = MIN_WAKE_THRESHOLD;

    // 唤醒 ZCR：比 VAD 严格，只接受语音典型范围
    bool zcrPass = (s_currentZCR >= 3 && s_currentZCR <= 80);

    
    
    // 实时调试输出
    static unsigned long lastDebug = 0;
    if (now - lastDebug > 15000) {
        lastDebug = now;
        Serial.printf("🔊 唤醒: e=%d noise=%d env=%d thr=%d zcr=%d\n", 
                      energy, noiseFloorMin, envBaseEnergy, dynamicThreshold, (int)s_currentZCR);
    }
    
    // 检查麦克风是否正常工作
    static unsigned long lastEnergyCheck = 0;
    static int zeroEnergyCount = 0;
    if (now - lastEnergyCheck > 1000) {
        lastEnergyCheck = now;
        if (energy == 0) {
            zeroEnergyCount++;
            if (zeroEnergyCount > 10) {
                Serial.println("⚠️ 警告：麦克风可能未正常工作，持续检测到能量为0");
                zeroEnergyCount = 0;
            }
        } else {
            zeroEnergyCount = 0;
        }
    }
    
    if (energy > dynamicThreshold && zcrPass) {
        // 检测能量陡峭跳变，过滤强物理冲击（仅针对>800的能量）
        int energyDelta = energy - lastEnergyValue;
        if (energy > 800 && energyDelta > energy * 0.85) {
            Serial.printf("🛡️ 拦截强物理冲击: energy=%d, delta=%d\n", energy, energyDelta);
            soundCount = 0;
            lastEnergyValue = energy;
            return false;
        }
        
        // 超过阈值时不更新底噪，防止声音被更新到底噪里
        // (envBase更新逻辑在后面)
        
        unsigned long interval = now - lastSoundTime;
        
        // 缩短最小间隔限制，允许正常的快速连读
        if (soundCount == 0 || (interval > 100 && interval < WAKE_MAX_INTERVAL)) {  
            soundCount++;
            lastSoundTime = now;
            Serial.printf("🎯 唤醒计数:%d 能量:%d 阈值:%d 间隔:%lums\n", 
                         soundCount, energy, dynamicThreshold, interval);
        } else if (interval >= WAKE_MAX_INTERVAL) {
            soundCount = 1;
            lastSoundTime = now;
        } else if (interval <= 100) {
            // 抑制极短间隔的噪声
        }
    } else if (soundCount > 0 && now - lastSoundTime > 500) {  // 恢复正常重置时间
        soundCount = 0;
    }
    
    if (soundCount >= WAKE_MIN_COUNT) {
        soundCount = 0;
        Serial.printf("🔔 成功唤醒! 最终能量:%d 阈值:%d\n", energy, dynamicThreshold);
        lastEnergyValue = energy;
        return true;
    }
    lastEnergyValue = energy;
    return false;
}


void MicModule::startListening() {
    Serial.println("🎙️ startListening() 被调用");
    Serial.printf("   isPlaying=%d, recordingLocked=%d\n", AudioPlayer::getInstance().isPlaying(), (bool)recordingLocked);
    recordingLocked = false;
    
    int retry = 0;
    while (AudioPlayer::getInstance().isPlaying() && retry < 25) {
        Serial.printf("   等待播放完成... retry=%d\n", retry);
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    resetEnergySmoother();
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        audioBufferPos = WAV_HEADER_SIZE / 2;
        recordingStartTime = millis();
        lastSpeechTime = millis();
        firstSpeechTime = millis();
        _everDetectedSpeech = false;// 清除实时检测状态，准备新一轮录音
        _lastSessionHadSpeech = false;// 【新增】清除上一轮快照
        s_isListening = true;
        xSemaphoreGive(audioMutex);
    } else {
        Serial.println("   ❌ 获取信号量失败");
    }
}

void MicModule::stopListening() {
    bool wasListening = s_isListening;
    s_isListening = false;
    recordingLocked = false;  // 重置锁定状态
    soundCount = 0;

    if (wasListening && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_audioCb && audioBufferPos > WAV_HEADER_SIZE / 2) {
            _audioCb(audioBuffer, audioBufferPos * 2);
        } else if (_audioCb) {
            _audioCb(nullptr, 0);
        }
        audioBufferPos = 0;
        xSemaphoreGive(audioMutex);
    }
}

void MicModule::stopListeningImmediately() { 
    s_forceStop = true;
    s_isListening = false;
    recordingLocked = false;
    stopListening(); 
}

void MicModule::freezeRecording() {
    // 停止 micTask 写入但不触发回调/不清除缓冲
    s_isListening = false;
    recordingLocked = true;
    soundCount = 0;
}

bool MicModule::isListening() { return s_isListening; }

void MicModule::resetEnergySmoother() {
    energySmootherLast = 0;
    hpfPrevX = 0;
    hpfPrevY = 0;
    preEmphPrev = 0;
}

// DC blocker (高通滤波)，滤除次声波/直流偏置，保留语音
static int dcBlock(int x) {
    static const int R = 32604; // Q15: 0.995, 截止 ~12Hz @ 16kHz
    int y = x - hpfPrevX + ((R * hpfPrevY) >> 15);
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    hpfPrevX = x;
    hpfPrevY = y;
    return y;
}

// 预加重滤波器：y[n] = x[n] - 0.97*x[n-1]
// 压低 50/100Hz 工频噪声，提亮 1-4kHz 语音带
static int preEmphasis(int x) {
    static const int K = 31785; // Q15: 0.97
    int y = x - ((K * preEmphPrev) >> 15);
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    preEmphPrev = x;
    return y;
}

int MicModule::calculateEnergy(int16_t* samples, int count) {
    if (count == 0) return 0;
    long long sum = 0;
    int zcr = 0;
    int prevSign = 0;
    for (int i = 0; i < count; i++) {
        int dc = dcBlock(samples[i]);
        int pe = preEmphasis(dc);   // 预加重后再算能量
        sum += abs(pe);
        // 零穿越率累加（忽略微弱信号遮藏噪声）
        if (abs(pe) > 30) {
            int sign = (pe > 0) ? 1 : -1;
            if (prevSign != 0 && sign != prevSign) zcr++;
            prevSign = sign;
        }
    }

    int currentEnergy = (int)(sum / count);
    int smoothedEnergy = (currentEnergy * 7 + energySmootherLast * 3) / 10;
    energySmootherLast = smoothedEnergy;

    // ZCR 归一化为每 256 采样概念值
    s_currentZCR = (count > 0) ? (zcr * 256) / count : 0;

    // 【新增】最小统计法更新底噪估计
    unsigned long now = millis();
    if (noiseFloorWindowStart == 0) noiseFloorWindowStart = now;
    if (smoothedEnergy < noiseFloorWindowMin) noiseFloorWindowMin = smoothedEnergy;
    if (now - noiseFloorWindowStart > 5000) {
        // 上变慢（防被语音拿高）、下变快（环境变安静立刻跟进）
        if (noiseFloorWindowMin > noiseFloorMin) {
            noiseFloorMin = (noiseFloorMin * 7 + noiseFloorWindowMin) / 8;
        } else {
            noiseFloorMin = noiseFloorWindowMin;
        }
        if (noiseFloorMin < 3)   noiseFloorMin = 3;
        if (noiseFloorMin > 300) noiseFloorMin = 300;
        noiseFloorWindowMin = 99999;
        noiseFloorWindowStart = now;
    }

    return smoothedEnergy;
}
uint8_t* MicModule::getRecordedBuffer() { return (uint8_t*)audioBuffer; }
size_t MicModule::getRecordedLen() { return audioBufferPos * 2; }

void MicModule::fillWavHeader() {
    recordingLocked = true; // 发送前锁定，禁止 micTask 写入
    if (!audioBuffer) return;
    size_t dataLen = (audioBufferPos * 2) - WAV_HEADER_SIZE;
    uint32_t fileLen = dataLen + 36;
    uint32_t sampleRate = 16000;
    uint32_t byteRate = sampleRate * 1 * 2;
    
    uint8_t* buf = (uint8_t*)audioBuffer;
    memcpy(buf, "RIFF", 4);
    memcpy(buf + 4, &fileLen, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    uint32_t fmtLen = 16; memcpy(buf + 16, &fmtLen, 4);
    uint16_t fmtTag = 1; memcpy(buf + 20, &fmtTag, 2);
    uint16_t channels = 1; memcpy(buf + 22, &channels, 2);
    memcpy(buf + 24, &sampleRate, 4);
    memcpy(buf + 28, &byteRate, 4);
    uint16_t blockAlign = 2; memcpy(buf + 32, &blockAlign, 2);
    uint16_t bps = 16; memcpy(buf + 34, &bps, 2);
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &dataLen, 4);
}

// ===== 浮动指示器状态查询 =====
int  MicModule::getCurrentEnergy() { return s_currentEnergy; }
int  MicModule::getEnvBaseEnergy() { return envBaseEnergy; }
bool MicModule::isI2sActive()      { return i2s_initialized; }
bool MicModule::consumeWakeword() {
    if (s_wakewordPending) { s_wakewordPending = false; return true; }
    return false;
}