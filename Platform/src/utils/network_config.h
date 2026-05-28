#pragma once
#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

/**
 * 网络稳定性配置文件
 * 调整这些参数以优化您的特定网络环境
 */

// ================== WiFi 重连策略 ==================
/** WiFi连接失败后的最大重试次数 */
#define MAX_WIFI_RETRY_COUNT 10  // 增加重试次数，适应不稳定网络

/** AP模式下尝试恢复STA连接的间隔（毫秒） */
#define AP_STA_RECOVERY_INTERVAL 45000  // 45秒

/** AP模式下检查客户端活动的间隔（毫秒） */
#define AP_ACTIVITY_CHECK_INTERVAL 10000  // 10秒

// ================== AP 模式配置 ==================
/** AP模式下无活动时自动重启的超时时间（毫秒） */
#define AP_IDLE_TIMEOUT 300000UL  // 5分钟

// ================== 内存管理 ==================
/** 堆内存临界值，触发立即重启（字节） */
#define CRITICAL_HEAP_THRESHOLD 500

/** 堆内存警告值，定期输出警告日志（字节） */
#define WARNING_HEAP_THRESHOLD 12000

/** 堆内存警告的最大间隔（毫秒） */
#define HEAP_WARNING_INTERVAL 30000  // 30秒

// ================== MQTT 配置 ==================
/** MQTT重连最小间隔（毫秒） */
#define MQTT_RETRY_INTERVAL 10000  // 10秒，避免频繁重连

/** MQTT连接超时时间（毫秒） */
#define MQTT_CONNECT_TIMEOUT 12000

/** MQTT保活间隔（秒） */
#define MQTT_KEEPALIVE 90  //sees主动探测连接状态，所以这里长一点没有问题。

/** MQTT缓冲区大小（字节） */
#define MQTT_BUFFER_SIZE 256

/** MQTT最大离线时间，超过此时间后设备重启（毫秒） */
#define MQTT_MAX_OFFLINE_TIME 10 * 300000  // 50分钟

// ================== 数据同步 ==================
/** Web数据同步间隔（毫秒，8小时） */
//#define WEB_DATA_SYNC_INTERVAL (8UL * 3600 * 1000)
// 10分钟 = 10 * 60秒 * 1000毫秒
#define WEB_DATA_SYNC_INTERVAL (10UL * 60 * 1000)

// ================== 蜂鸣器时间段配置 ==================
/** 蜂鸣器工作的开始小时（24小时制） */
#define BUZZER_START_HOUR 7

/** 蜂鸣器工作的结束小时（24小时制） */
#define BUZZER_END_HOUR 22

// ================== 网络状态管理 ==================
/** 网络状态枚举 */
typedef enum {
    NET_STATE_UNKNOWN,
    NET_STATE_INIT,
    NET_STATE_AP_SETUP,         // AP模式配置阶段
    NET_STATE_STA_CONNECTED,    // STA正常连接
    NET_STATE_STA_RECONNECTING, // STA重连中
    NET_STATE_STA_FAILED        // STA失败，准备转AP
} NetworkState;

#endif // NETWORK_CONFIG_H
