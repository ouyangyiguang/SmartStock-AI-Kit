#pragma once
// ===== 功能配置模块 =====

//#define USE_AUDIO        // 注释掉此行则完全禁用音频
//#define USE_LIGHTS       // 注释掉此行则完全禁用灯光

// ===== 1. 业务逻辑与名称分配 =====
#if defined(USE_LIGHTS)
    #define DEV_MQTT_TOPIC  "Led_house"
    #define DEV_AP_NAME     "AP_LED"
    #define DEV_BLUE_NAME   "ledhouse"
    #define DEV_VER         "2602262"
    #define DEV_FIREURL     "http://mqtt.yodin.com/update/esp32_LED.ino.bin"
    #define JSON_DOC_SIZE   2048

#elif defined(USE_SMARTLOCK)
    #define DEV_MQTT_TOPIC  "Door_lock"
    #define DEV_AP_NAME     "AP_YD_Lock"
    #define DEV_BLUE_NAME   "lock"

    // 门锁模式下的特定参数
    #define PCF_INT_PIN     2 // 门锁模式下，用于唤醒的PCF8574中断引脚这里暂时不用。
    #define DOOR_SENSOR_PIN 15
    #define DOOR_OPEN_PIN 12
    #define DEV_VER         "2602260"
    #define DEV_FIREURL     "http://mqtt.yodin.com/update/esp32_doorLock.ino.bin"
    #define JSON_DOC_SIZE   1024

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #define DEV_MQTT_TOPIC  "Clock_photo"
    #define DEV_AP_NAME     "AP_Esp32S3play"
    #define DEV_BLUE_NAME   "ai"
    #define DEV_VER         "2602262"
    #define DEV_FIREURL     "http://mqtt.yodin.com/update/esp32s3ai.ino.bin"
    #define JSON_DOC_SIZE   4096
    //#define USE_AI 

#elif defined(USE_STOCK)
    #define DEV_MQTT_TOPIC  "Clock_photo"
    #define DEV_AP_NAME     "AP_YDGP股票屏"
    #define DEV_BLUE_NAME   "stock"
    #define JSON_DOC_SIZE   4096
    #define DEV_VER         "2602263"
    #define DEV_FIREURL     "http://mqtt.yodin.com/update/esp32_GP.ino.bin"
#elif defined(USE_BOOKSLOT)
    #define DEV_MQTT_TOPIC  "Switch2gear"
    #define DEV_AP_NAME     "AP_YD_2RSwitch"
    #define DEV_BLUE_NAME   "onoff"
    #define DEV_VER         "2602262"
    #define DEV_FIREURL     "http://mqtt.yodin.com/update/esp32_bookslot.ino.bin"
    #define JSON_DOC_SIZE   1024

#else
    #define DEV_MQTT_TOPIC  "Clock_photo"
    #define DEV_AP_NAME     "AP_YODIN_**"
    #define DEV_BLUE_NAME   "yodin_dev*"
    #define JSON_DOC_SIZE   2048
#endif

    #include <time.h>
    #define NO_BACKGROUND 0xFFFFF0FE
    #define LED_PIN  2     // ESP32内置LED
    #define NET_STACK_SIZE 3072

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define NET_STACK_SIZE 4096  // 增加栈大小以支持OTA下载(需4096+缓冲区)
    #define PCF_INT_PIN 1
    #define I2C_SDA    7
    #define I2C_SCL    6
    #define MOTOR_IN1 38         
    #define MOTOR_IN2 41
    /// SD卡 (使用SPI3总线)
    // #define SD_SCK    14  // SPI3 CLK
    // #define SD_MOSI   10  // SPI3 MOSI
    // #define SD_MISO   1  // SPI3 MISO
    // #define SD_CS     5   // SD卡片选
    // MAX98357A喇叭 + INMP441麦克风 共享I2S总线（BCLK/WS），时分复用
    #define I2S_BCK   15  // Bit Clock  (喇叭+麦克风共享)
    #define I2S_WS    16  // Word Select (喇叭+麦克风共享)
    #define I2S_DOUT  17  // Data Out (喇叭专用)
    #define AMP_SD    18  // 功放关断控制

    #define BUZZER_PIN 42        // 蜂鸣器引脚3
    #define WAKEUP_PIN 8        // 离线语音唤醒模块的检测引脚
    // 麦克风配置 (BCLK/WS共享喇叭，播放时自动让出)
    #define MIC_I2S_WS   16     // Word Select (共享喇叭 WS)
    #define MIC_I2S_SCK  15     // Serial Clock (共享喇叭 BCLK)
    #define MIC_I2S_SD   21     // Serial Data (麦克风专用)

#elif defined(CONFIG_IDF_TARGET_ESP32)

    // 普通 ESP32 专用引脚定义
    // TFT显示屏 (使用VSPI总线)
    #define TFT_MOSI 23   //// SDA
    #define TFT_SCLK 18     // SCL
    #define TFT_CS   -1       // CS 不连接（可以忽略）
    #define TFT_DC   21       // DC 引脚 // AO0
    #define TFT_RST  4       // 复位引脚
    #define TFT_BL   22      // 背光控制
// 普通 ESP32 的 I2S 音频引脚 (门锁版会用到)
    #ifdef USE_AUDIO
        #define I2S_BCK   27  
        #define I2S_WS    26  
        #define I2S_DOUT  25  
        #define AMP_SD    16  
    #endif

    // 普通 ESP32 的 SD 卡引脚 (门锁版会用到)
    #ifdef USE_SD_CARD
        #define SD_SCK    14  
        #define SD_MOSI   13  
        #define SD_MISO   19  
        #define SD_CS     5   
    #endif

    #define PCF_INT_PIN 15  // 门锁模式下唤醒脚    
    #define BUZZER_PIN 17        // 蜂鸣器引脚3


#endif

// 超声波配置
// #define trigPin = 33;       // GPIO12
// #define echoPin = 35;       // GPIO14

//舵机
// #define servoPinA = 15;
// #define servoPinB = 12;

#define MAX_PERIODS 10
#define MAX_PASSWORDS 10
struct SchedulePeriod {
    time_t begin_time;
    time_t end_time;
    bool valid;
    // 其他字段...
};
#define MAX_RETRY_COUNT 5