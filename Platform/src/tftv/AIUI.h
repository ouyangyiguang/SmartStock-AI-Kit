#ifndef AI_UI_H
#define AI_UI_H

#include <lvgl.h>
#include "LVGLGlobal.h"

#define AI_UI_POOL_SIZE 4

struct BubbleItem {
    lv_obj_t* container;
    lv_obj_t* bg;
    lv_obj_t* label;
    lv_obj_t* avatar;
};

class AIUI {
public:
    static AIUI& get();
    void init(lv_obj_t* parent);
    void addBubble(const char* text, bool isUser);
    void updateLastBubbleText(const char* text);
    void removeLastBubble();
    void setStatus(const char* status);
    void clearDialog();
    void scrollToBottom();
    void setCountdown(int seconds);

private:
    AIUI() : _parent(nullptr), _page(nullptr), _content(nullptr), _statusLabel(nullptr), _spacer(nullptr), _poolIndex(0), _msgCount(0) {}
    
    lv_obj_t* _parent;
    lv_obj_t* _page;
    lv_obj_t* _content;
    lv_obj_t* _statusLabel;
    lv_obj_t* _spacer;
    lv_obj_t* _countdownLabel;

    BubbleItem _pool[AI_UI_POOL_SIZE];
    int _poolIndex;
    int _msgCount;
};

#endif