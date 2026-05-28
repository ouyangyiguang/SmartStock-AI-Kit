#ifndef CN_PRINT_H
#define CN_PRINT_H

#include <lvgl.h>
#include "font/FontManager.h"
#include <map>
#include <vector>

class CnPrint {
public:
    // 对齐方式
    enum Alignment { 
        ALIGN_TL, ALIGN_TC, ALIGN_TR, 
        ALIGN_CL, ALIGN_CENTER, ALIGN_CR, 
        ALIGN_BL, ALIGN_BC, ALIGN_BR 
    };

private:
    CnPrint() : _timer(nullptr), _currentParent(nullptr) {}
    std::map<unsigned int, lv_obj_t*> obj_map;
    // 内部实现
    bool create(uint32_t id, const char* text, int x, int y,
               uint32_t color, uint8_t size, Alignment align, int width);
    bool createBg(uint32_t id, const char* text, int x, int y,
                 uint32_t color, uint8_t size, Alignment align,
                 uint8_t bgA, uint32_t bgC, int radius, int width);
    bool update(uint32_t id, const char* text);

    bool has(uint32_t id);
    void show(const char* text, uint32_t color, uint8_t size, Alignment align, uint32_t time);
    void clear();
    
    lv_align_t convertAlign(Alignment align);
    const lv_font_t* getFont(uint8_t size);
    lv_obj_t* createText(const char* text, int x, int y, uint32_t color, uint8_t size, Alignment align, int width);
    
    std::map<uint32_t, lv_obj_t*> _labels;
    std::vector<lv_obj_t*> _msgs;
    lv_timer_t* _timer;
    lv_obj_t* _currentParent;

public:
    static CnPrint& get();

    void setParent(lv_obj_t* parent) {
        _currentParent = parent;
    }

    lv_obj_t* getParent() {
        return _currentParent ? _currentParent : lv_scr_act();
    }
    void remove(unsigned int id);
    // 简化重载 - 修改默认对齐方式为左上角对齐
    static bool t(uint32_t id, const char* text) {
        return get().create(id, text, 0, 0, 0xFFFFFF, 20, ALIGN_TL, 0);
    }
    
    static bool t(uint32_t id, const char* text, uint32_t color) {
        return get().create(id, text, 0, 0, color, 20, ALIGN_TL, 0);
    }
    
    static bool t(uint32_t id, const char* text, int x, int y) {
        return get().create(id, text, x, y, 0xFFFFFF, 20, ALIGN_TL, 0);
    }
    
    static bool t(uint32_t id, const char* text, int x, int y, uint32_t color) {
        return get().create(id, text, x, y, color, 20, ALIGN_TL, 0);
    }
    
    static bool t(uint32_t id, const char* text, int x, int y, uint32_t color, uint8_t size) {
        return get().create(id, text, x, y, color, size, ALIGN_TL, 0);
    }
    
    static bool t(uint32_t id, const char* text, int x, int y, uint32_t color, uint8_t size, Alignment align) {
        return get().create(id, text, x, y, color, size, align, 0);
    }
    
    static bool t(uint32_t id, const char* text, int x, int y, uint32_t color, uint8_t size, Alignment align, int width) {
        return get().create(id, text, x, y, color, size, align, width);
    }

    // 带背景的文本 - 修改默认对齐方式为左上角对齐
    static bool b(uint32_t id, const char* text,
                 int x = 0, int y = 0,
                 uint32_t color = 0xFFFFFF,
                 uint8_t size = 20,
                 Alignment align = ALIGN_TL,  // 改为左上角对齐
                 uint8_t bgA = 100,
                 uint32_t bgC = 0x000000,
                 int radius = 5,
                 int width = 0) {
        return get().createBg(id, text, x, y, color, size, align, bgA, bgC, radius, width);
    }

    // 临时消息 - 保持居中对齐（通常消息应该居中显示）
    static void m(const char* text) {
        get().show(text, 0xFFFFFF, 20, ALIGN_CENTER, 3000);
    }
    
    static void m(const char* text, uint32_t color) {
        get().show(text, color, 20, ALIGN_CENTER, 3000);
    }
    
    static void m(const char* text, uint32_t color, uint8_t size) {
        get().show(text, color, size, ALIGN_CENTER, 3000);
    }
    
    static void m(const char* text, uint32_t color, uint8_t size, Alignment align) {
        get().show(text, color, size, align, 3000);
    }
    
    static void m(const char* text, uint32_t color, uint8_t size, Alignment align, uint32_t time) {
        get().show(text, color, size, align, time);
    }

    // 更新文本
    static bool u(uint32_t id, const char* text) {
        return get().update(id, text);
    }

    // 删除文本
    static void r(uint32_t id) { // 改为 void
        get().remove((unsigned int)id);
    }

    // 检查是否存在
    static bool h(uint32_t id) {
        return get().has(id);
    }

    // 清空所有消息
    static void c() {
        get().clear();
    }
};

// 极致简化宏
#define txt(id, ...) CnPrint::t(id, __VA_ARGS__)
#define txtBg(id, ...) CnPrint::b(id, __VA_ARGS__)
#define msg(...) CnPrint::m(__VA_ARGS__)
#define upd(id, text) CnPrint::u(id, text)
#define del(id) CnPrint::r(id)
#define clr() CnPrint::c()

// 对齐简写
#define A_TL CnPrint::ALIGN_TL
#define A_TC CnPrint::ALIGN_TC
#define A_TR CnPrint::ALIGN_TR
#define A_CL CnPrint::ALIGN_CL
#define A_CT CnPrint::ALIGN_CENTER
#define A_CR CnPrint::ALIGN_CR
#define A_BL CnPrint::ALIGN_BL
#define A_BC CnPrint::ALIGN_BC
#define A_BR CnPrint::ALIGN_BR


// 极致简化
// txt(1003, "思考中", 0, 60, 0x0000FF, 16, _TC);
// msg("完成!", 0x00FF00);
// upd(1003, "新内容");
// del(1003);

// CnPrint::t(1003, "思考中", 0, 60, 0x0000FF, 16, CnPrint::TC);

// // 各种简化版本
// CnPrint::t(1004, "红色文本", 10, 20, 0xFF0000);
// CnPrint::t(1005, "默认位置颜色");
// CnPrint::t(1006, "蓝色文本", 0x0000FF);

// // 带背景
// CnPrint::b(1007, "警告", 50, 50, 0xFFFFFF, 20, CnPrint::C, 100, 0xFF0000, 10);

// // 消息
// CnPrint::m("操作成功!", 0x00FF00, 20, CnPrint::C, 2000);
// CnPrint::m("绿色消息", 0x00FF00);
// CnPrint::m("默认消息");

// // 更新、删除等
// CnPrint::u(1003, "新文本");
// CnPrint::r(1003);
// CnPrint::c();
#endif