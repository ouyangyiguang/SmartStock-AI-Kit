#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <Audio.h>
#include <LittleFS.h>
#include <SD.h>
#include <Arduino.h>
#ifdef USE_SD_CARD
#include "utils/SDCardManager.h"
#endif

enum AudioSource {
  SOURCE_LITTLEFS,
  SOURCE_SD_CARD
};

struct AudioCommand {
  enum Type { PLAY_FILE, PLAY_STREAM, STOP } type;
  char data[1024];
  AudioSource source;
};

class AudioPlayer {
public:
  static AudioPlayer& getInstance();
  
  bool begin(uint8_t bckPin, uint8_t wsPin, uint8_t doutPin);  
  
  // 核心接口
  bool play(const char* filename, AudioSource source = SOURCE_LITTLEFS);
  void playStream(String url);
  void stop();
  void clearQueue();
  
  bool isPlaying();
  void setVolume(uint8_t volume);
  void taskHandler();
  void setPlaybackFinishedCallback(void (*callback)());

private:
  AudioPlayer();
  ~AudioPlayer();
  
  Audio _audio;
  bool _initialized = false;
  void (*_playbackFinishedCallback)() = nullptr;
  
  SemaphoreHandle_t _mutex = NULL;
  QueueHandle_t _cmdQueue = NULL;

  static const int MAX_QUEUE = 4;
  struct PlaylistEntry {
    char* filename;        // strdup 出来的，owner
    AudioSource source;
  };
  PlaylistEntry _playList[MAX_QUEUE] = {};
  int _playListCount = 0;
  SemaphoreHandle_t _playListMutex = NULL; // 修复 #2：跨任务保护 _playList
  void checkNextInQueue();
  
  void processPlayFile(const char* path, AudioSource source);
};

// 任务函数声明
void musicTask(void* parameter);

// --- 宏定义：方便调用 ---
#ifdef USE_AUDIO
    #define PLAY_LOCAL(f)  AudioPlayer::getInstance().play(f, SOURCE_LITTLEFS)
    #define PLAY_SD(f)     AudioPlayer::getInstance().play(f, SOURCE_SD_CARD)
    #define PLAY_URL(u)    AudioPlayer::getInstance().playStream(u)
    #define STOP_AUDIO()   AudioPlayer::getInstance().stop()
    #define CLEAR_AUDIO_QUEUE() AudioPlayer::getInstance().clearQueue()
#else
    #define PLAY_LOCAL(f)
    #define PLAY_SD(f)
    #define PLAY_URL(u)
    #define STOP_AUDIO()
    #define CLEAR_AUDIO_QUEUE()
#endif

#endif
