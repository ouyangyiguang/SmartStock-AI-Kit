#ifndef LVGL_GLOBAL_H
#define LVGL_GLOBAL_H

#include <mutex>
#include <lvgl.h>

// 获取递归锁，支持同一线程多次加锁
inline std::recursive_mutex& getLvglMutex() {
    static std::recursive_mutex m;
    return m;
}

/**
 * 自动管理锁的宏：利用 lock_guard 的生命周期自动解锁
 */
#define LVGL_LOCK() \
    for (bool _lvgl_do_once = true; _lvgl_do_once; ) \
        for (std::lock_guard<std::recursive_mutex> _lvgl_lock(getLvglMutex()); _lvgl_do_once; _lvgl_do_once = false)

#define LVGL_UNLOCK() do {} while(0)

// AIUI 气泡池大小（建议偶数，3轮对话）
#define AIUI_POOL_SIZE 6

#endif