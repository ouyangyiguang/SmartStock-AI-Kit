#ifndef MICMODULE_H
#define MICMODULE_H

#include <Arduino.h>
#include <atomic>

typedef void (*WakeupDetectedCallback)();
typedef void (*AudioDataCallback)(const int16_t* data, size_t size);

extern std::atomic<bool> recordingLocked;

static const int WAV_HEADER_SIZE = 44;

// 录音参数（定义在 MicModule.cpp）
extern const unsigned long MAX_RECORD_TIME_NO_SPEECH;
extern const unsigned long MAX_RECORD_TIME_WITH_SPEECH;
extern const unsigned long VAD_TIMEOUT;

class MicModule {
public:
    static MicModule& getInstance();
    void init();
    void setWakeupCallback(WakeupDetectedCallback cb) { _wakeupCb = cb; }
    void setAudioDataCallback(AudioDataCallback cb) { _audioCb = cb; }
    
    void startListening();
    void stopListening();
    void stopListeningImmediately();
    void freezeRecording(); // 停止写入但不清除缓冲（用于超时提交）
    void setRecordingLocked(bool lock) { recordingLocked = lock; }
    bool isListening();
    
    static void suspendI2S();   // 暂停麦克风I2S，释放共享引脚给喇叭
    static void resumeI2S();    // 恢复麦克风I2S，重新占用共享引脚

    // ===== 状态查询（供浮动指示器使用） =====
    static int  getCurrentEnergy();   // 0~1000 的瞬时能量
    static int  getEnvBaseEnergy();   // 环境基准能量
    static bool isI2sActive();        // I2S 是否在运行
    static bool consumeWakeword();    // 读取并清除一次"唤醒事件"标志
    
    static uint8_t* getRecordedBuffer();
    static size_t getRecordedLen();
    static void fillWavHeader();
    static int calculateEnergy(int16_t* samples, int count);
    static void resetEnergySmoother();
    static bool detectRepeatSound(int energy);
    bool getEverDetectedSpeech() { return _lastSessionHadSpeech; } // 读取快照（录音结束后）
    static bool hasLiveSpeech() { return _everDetectedSpeech; }    // 读取实时标志（录音中）
private:
    MicModule() = default;
    static void micTask(void* parameter);
    static std::atomic<bool> _everDetectedSpeech;
    static std::atomic<bool> _lastSessionHadSpeech;
    static WakeupDetectedCallback _wakeupCb;
    static AudioDataCallback _audioCb;
};

#endif
