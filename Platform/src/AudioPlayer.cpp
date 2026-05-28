#include "AudioPlayer.h"
#include <UrlEncode.h>
#include "utils/pins.h"
#ifdef USE_AI
#include "ai/MicModule.h"
#endif
#ifdef USE_AUDIO

#ifdef USE_SD_CARD
#include "utils/SDCardManager.h"
extern SDCardManager sdCard;
#endif

AudioPlayer::AudioPlayer() {
  _mutex = xSemaphoreCreateMutex();
  if (_mutex == NULL) {
    Serial.println(F("❌ 创建音频互斥锁失败"));
  }
  _playListMutex = xSemaphoreCreateMutex();
  if (_playListMutex == NULL) {
    Serial.println(F("❌ 创建播放列表互斥锁失败"));
  }
  _cmdQueue = xQueueCreate(10, sizeof(AudioCommand));
  if (_cmdQueue == NULL) {
    Serial.println(F("❌ 创建音频命令队列失败"));
  }
  _playListCount = 0;
}

AudioPlayer::~AudioPlayer() {
  clearQueue();
  if (_mutex) vSemaphoreDelete(_mutex);
  if (_playListMutex) vSemaphoreDelete(_playListMutex);
  if (_cmdQueue) vQueueDelete(_cmdQueue);
}

AudioPlayer& AudioPlayer::getInstance() {
  static AudioPlayer instance;
  return instance;
}

bool AudioPlayer::begin(uint8_t bckPin, uint8_t wsPin, uint8_t doutPin) {
  if (_initialized) return true;
  _audio.setPinout(bckPin, wsPin, doutPin);
  //#ifdef USE_AUDIO_DIZYI
   // _audio.setVolume(15);      // 1. 降低数字增益，防止沙哑
    _audio.forceMono(true);    // 2. 强制单声道，增加信号稳定性
    //_audio.setTone(-10, -10, -20); // 重点：第一个参数是低音(Gain)，第三个是高音(Gain)
                               // 把高音压低到 -10 可以消除 D 类功放串联的高频啸叫
  //#elif defined(USE_AUDIO)
    
  //#endif
  _audio.setVolume(21);
  _initialized = true;
  
  // 启用音频放大器
  pinMode(AMP_SD, OUTPUT);
  //digitalWrite(AMP_SD, HIGH);
  
  return true;
}

// 修改后的 play 函数：支持加入队列或直接打断
bool AudioPlayer::play(const char* filename, AudioSource source) {
  if (_cmdQueue == NULL) {
    Serial.println(F("❌ 音频命令队列未初始化"));
    return false;
  }
  if (isPlaying()) {
    if (filename && _playListMutex && xSemaphoreTake(_playListMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (_playListCount < MAX_QUEUE) {
        _playList[_playListCount].filename = strdup(filename);
        _playList[_playListCount].source   = source;
        _playListCount++;
        Serial.printf("已加入排队: %s\n", filename);
      }
      xSemaphoreGive(_playListMutex);
    }
  } else {
    Serial.printf("开始播放: %s\n", filename);
    AudioCommand cmd = {AudioCommand::PLAY_FILE, "", source};
    strncpy(cmd.data, filename, sizeof(cmd.data) - 1);
    cmd.data[sizeof(cmd.data) - 1] = '\0';
    xQueueSend(_cmdQueue, &cmd, 0);
  }
  return true;
}

void AudioPlayer::playStream(String url) {
    if (_cmdQueue == NULL) {
        Serial.println(F("❌ 音频命令队列未初始化"));
        return;
    }

    AudioCommand cmd;
    cmd.type = AudioCommand::PLAY_STREAM;
    cmd.source = SOURCE_LITTLEFS; // 默认值

    // 使用 strncpy 安全拷贝，确保不会溢出 cmd.data 缓冲区
    // 即使外部 String 销毁了，cmd 结构体已经持有了文字副本
    strncpy(cmd.data, url.c_str(), sizeof(cmd.data) - 1);
    cmd.data[sizeof(cmd.data) - 1] = '\0'; 

    if (xQueueSend(_cmdQueue, &cmd, 0) != pdPASS) {
        Serial.println(F("❌ 音频命令队列已满，无法发送播放请求"));
    }
}

void AudioPlayer::stop() {
  clearQueue();
  AudioCommand cmd = {AudioCommand::STOP, "", SOURCE_LITTLEFS};
  xQueueSend(_cmdQueue, &cmd, 0);
}

void AudioPlayer::clearQueue() {
  if (_playListMutex && xSemaphoreTake(_playListMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
  for (int i = 0; i < MAX_QUEUE; i++) {
    if (_playList[i].filename) {
      free(_playList[i].filename);
      _playList[i].filename = nullptr;
      _playList[i].source = SOURCE_LITTLEFS;
    }
  }
  _playListCount = 0;
  if (_playListMutex) xSemaphoreGive(_playListMutex);
}

// 核心逻辑：文件读取
void AudioPlayer::processPlayFile(const char* path, AudioSource source) {
  // 播放前暂停麦克风I2S（共享引脚）
  #ifdef USE_AI
  MicModule::suspendI2S();
  #endif
  // 播放前打开放大器
  digitalWrite(AMP_SD, HIGH);
  
  if (source == SOURCE_SD_CARD) {
    #ifdef USE_SD_CARD
    // 首次使用时挂载，之后保持挂载状态
    if (!sdCard.isInitialized()) {
      Serial.println(F("🔄 挂载SD卡..."));
      sdCard.requestInit();
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    if (sdCard.isInitialized()) {
      File file = sdCard.openFile(path);
      if (file) {
        file.close();  // 关闭文件句柄
        _audio.connecttoFS(SD, path);
      } else if (LittleFS.exists(path)) {
        _audio.connecttoFS(LittleFS, path);
      } else {
        Serial.printf("❌ 文件不存在: %s\n", path);
        if (LittleFS.exists("/sderr.wav")) {
          Serial.println(F("🔊 播放错误提示音: sderr.wav"));
          _audio.connecttoFS(LittleFS, "/sderr.wav");
        } else {
          Serial.println(F("⚠️ sderr.wav也不存在，跳过播放"));
          checkNextInQueue();
        }
      }
    } else if (LittleFS.exists(path)) {
      _audio.connecttoFS(LittleFS, path);
    } else {
      Serial.printf("❌ 文件不存在: %s\n", path);
      checkNextInQueue();
    }
    #else
    if (LittleFS.exists(path)) {
      _audio.connecttoFS(LittleFS, path);
    } else {
      checkNextInQueue();
    }
    #endif
  } else {
    if (LittleFS.exists(path)) {
       _audio.connecttoFS(LittleFS, path);
    } else {
       checkNextInQueue();
    }
  }
}

// 检查队列并播放下一个
void AudioPlayer::checkNextInQueue() {
    char* nextFile = nullptr;
    AudioSource nextSource = SOURCE_LITTLEFS;
    if (!_playListMutex || xSemaphoreTake(_playListMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (_playListCount > 0) {
        nextFile   = _playList[0].filename;
        nextSource = _playList[0].source;
        for (int i = 0; i < _playListCount - 1; i++) {
            _playList[i] = _playList[i + 1];
        }
        _playList[_playListCount - 1] = {nullptr, SOURCE_LITTLEFS};
        _playListCount--;
    }
    xSemaphoreGive(_playListMutex);
    if (nextFile) {
        // 修复 #3：保留原始 source（之前硬编码为 SD_CARD 会让 LittleFS 文件找不到）
        AudioCommand cmd = {AudioCommand::PLAY_FILE, "", nextSource};
        strncpy(cmd.data, nextFile, sizeof(cmd.data) - 1);
        cmd.data[sizeof(cmd.data) - 1] = '\0';
        xQueueSend(_cmdQueue, &cmd, 0);
        Serial.printf("正在自动播放下一个: %s (source=%d)\n", nextFile, (int)nextSource);
        free(nextFile);
    }
}
void AudioPlayer::setPlaybackFinishedCallback(void (*callback)()) {
  _playbackFinishedCallback = callback;
}

void AudioPlayer::taskHandler() {
  if (_mutex == NULL || _cmdQueue == NULL || !_initialized) {
      return; 
  }
  AudioCommand cmd;
  // 1. 处理新指令
  if (xQueueReceive(_cmdQueue, &cmd, 0) == pdPASS) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100))) {
      if (_audio.isRunning()) _audio.stopSong();
      
      switch (cmd.type) {
        case AudioCommand::PLAY_FILE:
          processPlayFile(cmd.data, cmd.source);
          break;
        case AudioCommand::PLAY_STREAM:
          #ifdef USE_AI
          MicModule::suspendI2S();
          #endif
          Serial.printf("🎵 开始播放流媒体: %s\n", cmd.data);
          digitalWrite(AMP_SD, HIGH);  // 打开放大器
          vTaskDelay(50);  // 新增：放大器开启后短暂延迟
          _audio.connecttohost(cmd.data);
          break;
        case AudioCommand::STOP:
          if (_audio.isRunning()) _audio.stopSong();
          #ifdef USE_AI
          MicModule::resumeI2S();
          #endif
          break;
      }
      xSemaphoreGive(_mutex);
    }
  }

  // 2. 维持音频循环和检测播放完成
  if (xSemaphoreTake(_mutex, 0)) {
    _audio.loop();
    
    static bool wasPlaying = false;
    bool currentlyPlaying = _audio.isRunning();

    // 关键点：检测从"正在播放"变为"停止播放"的时刻
    if (wasPlaying && !currentlyPlaying) {
        Serial.println(F("当前音频播完了"));
        
        // 优化：延迟关闭放大器确保音频完全停止
        vTaskDelay(100);
        digitalWrite(AMP_SD, LOW); // 关闭放大器减少底噪
        
        // 播放结束后恢复麦克风I2S（共享引脚）
        #ifdef USE_AI
        // 队列为空时才恢复，避免反复切换；读取 _playListCount 加锁保护
        bool queueEmpty = true;
        if (_playListMutex && xSemaphoreTake(_playListMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            queueEmpty = (_playListCount == 0);
            xSemaphoreGive(_playListMutex);
        }
        if (queueEmpty) {
            MicModule::resumeI2S();
        }
        #endif
        
        xSemaphoreGive(_mutex); // 先给锁，防止 checkNextInQueue 里的指令处理死锁
        checkNextInQueue();
    } else {
        xSemaphoreGive(_mutex);
    }
    wasPlaying = currentlyPlaying;
  }
}

bool AudioPlayer::isPlaying() { 
    return _initialized && _audio.isRunning(); 
}

void AudioPlayer::setVolume(uint8_t volume) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100))) {
        _audio.setVolume(volume);
        xSemaphoreGive(_mutex);
    }
}

/**
 * 供 FreeRTOS 调用的任务包装函数
 */
void musicTask(void* parameter) {
    while (true) {
        AudioPlayer::getInstance().taskHandler();
        // 优化：从2ms增加到10ms延迟，大幅降低CPU负载，同时保持音频流畅
        vTaskDelay(pdMS_TO_TICKS(2)); 
    }
}

#endif