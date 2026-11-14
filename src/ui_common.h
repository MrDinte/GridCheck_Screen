// Shared look and helpers for the screen pages (ui.cpp, ui_pages.cpp).
#pragma once

#include <lvgl.h>

#include "compat.h"
#include "data.h"
#include "model.h"
#include "net.h"
#include "ota.h"
#include "screen_size.h"

LV_FONT_DECLARE(eh_num_76);
LV_FONT_DECLARE(eh_num_56);
LV_FONT_DECLARE(eh_icons_44);

namespace ui {

// Current palette (set by applyPalette). GridCheck: pink, black and white; amber only for warnings.
inline lv_color_t C_BG = lv_color_hex(0x0b0a0d);
inline lv_color_t C_TILE = lv_color_hex(0x18151b);
inline lv_color_t C_INK = lv_color_hex(0xffffff);
inline lv_color_t C_MUTED = lv_color_hex(0xa39ca8);
inline lv_color_t C_FAINT = lv_color_hex(0x2c2830);
inline lv_color_t C_PINK = lv_color_hex(0xff2d87);   // brand
inline lv_color_t C_SOLAR = lv_color_hex(0xff4b97);
inline lv_color_t C_GRID = lv_color_hex(0xa39ca8);
inline lv_color_t C_BATT = lv_color_hex(0xffa8cd);
inline lv_color_t C_GOOD = lv_color_hex(0xff4b97);
inline lv_color_t C_WARN = lv_color_hex(0xffb020);
inline lv_color_t C_OK = lv_color_hex(0xa39ca8);

constexpr float IDLE_KW = 0.03f;
constexpr int W = board::SCREEN_W;
constexpr int TOP_H = 72, BOT_H = 64;
constexpr int BODY_H = board::SCREEN_H - TOP_H - BOT_H;  // 464

/** Switch colours: 0 GridCheck, 1 Classic, 2 Ocean. Call before building the screen. */
void applyPalette(int palette);
/** A colour as "rrggbb" for LVGL recolour text (#rrggbb text#). */
const char* hx(lv_color_t c);

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
lv_obj_t* plain(lv_obj_t* parent);
void fmtKw(char* buf, size_t n, float v);

// pages in ui_pages.cpp; update functions are cheap unless `full` is set
void buildToday(lv_obj_t* page);
void updateToday(const Live& l, bool full);
void buildSolar(lv_obj_t* page);
void updateSolar(const Live& l, bool full);
void buildMoney(lv_obj_t* page);
void updateMoney(const Live& l, bool full);
void buildFlow(lv_obj_t* page);
void updateFlow(const Live& l, bool full);
void buildBattery(lv_obj_t* page);
void updateBattery(const Live& l, bool full);
void buildHistory(lv_obj_t* page);
void updateHistory(const Live& l, bool full);

}  // namespace ui
