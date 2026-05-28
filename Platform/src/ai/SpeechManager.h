#ifndef SPEECHMANAGER_H
#define SPEECHMANAGER_H

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "MicModule.h"

enum SystemState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_PROCESSING,
    STATE_PLAYING
};
struct AudioTaskParams {
    uint8_t* pcmBuffer;
    size_t pcmLen;
};
class SpeechManager {
public:
    static SpeechManager& getInstance();
    void init();
    void start();
    void stop();
    void poll();
    
    void switchToState(SystemState newState);
    void resetToIdle();
    SystemState getState() { return _currentState; }
    static bool isProcessingAudio();
    static bool canWakeup();
    
private:
    SpeechManager() = default;
    static void onWakeupDetected();
    static void onAudioData(const int16_t* audioData, size_t dataSize);
    std::atomic<SystemState> _currentState{STATE_IDLE};
    unsigned long _stateStartTime = 0;
    static unsigned long waitWakeupStartTime;
    static std::atomic<bool> processingAudio;
    static void sendAudioToAI(uint8_t* pcmBuffer, size_t pcmLen);
    static void updateUIAfterAI(const String& aiText, const String& userText, const String& speed);

    static QueueHandle_t _audioQueue;
    static TaskHandle_t _workerTask;
    static const uint32_t WORKER_STACK = 12288;
    static void audioWorker(void* param);
    static void processAudioJob(AudioTaskParams* params);
};

#endif