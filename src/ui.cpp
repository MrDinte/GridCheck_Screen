#include "ui.h"

#include "ui_common.h"

#include <strings.h>
#include <time.h>

#ifndef EH_VERSION
#define EH_VERSION "0.2.0"
#endif

namespace ui {

// Icon glyphs in eh_icons_44 (Font Awesome 5)
#define ICON_SUN "\xEF\x86\x85"
#define ICON_HOME "\xEF\x80\x95"
#define ICON_BATT "\xEF\x89\x80"
#define ICON_BOLT "\xEF\x83\xA7"

// ---- widgets ---------------------------------------------------------------
static lv_obj_t *clockLbl, *dateLbl, *chipSolar, *chipAside;
static lv_obj_t *statusPill, *statusDot, *statusLbl, *hintLbl, *homeBtn, *homeIcon, *setBtn, *setIcon;
static constexpr int PAGES = 8;  // Now, Today, Solar, Money, Flow, Battery, History, Info
static lv_obj_t *dots[PAGES];
static lv_obj_t *tiles;

struct Node {
  lv_obj_t *badge, *icon, *label, *value, *unit, *sub;
  lv_obj_t *ring = nullptr;  // battery charge ring
};
static Node nSolar, nBatt, nGrid, nHouse;

struct Link {
  lv_point_t pts[2];
  lv_obj_t *line, *dot[3];
  lv_color_t color;
  int dir = 0;            // +1 node -> hub, -1 hub -> node, 0 idle
  uint32_t periodMs = 0;
};
static Link lSolar, lBatt, lGrid, lHouse;

static lv_obj_t *iWifi, *iAddr, *iInv, *iErr, *iUpd, *iVer, *resetBtn, *resetLbl, *palLbl, *dimLbl;
static bool forceFull = true;

// ---- helpers ---------------------------------------------------------------
void applyPalette(int p) {
  struct Pal { uint32_t bg, tile, ink, muted, faint, accent, solar, grid, batt, good, warn, ok; };
  static const Pal pals[3] = {
      // GridCheck: pink, black and white
      {0x0b0a0d, 0x18151b, 0xffffff, 0xa39ca8, 0x2c2830, 0xff2d87, 0xff4b97, 0xa39ca8, 0xffa8cd, 0xff4b97, 0xffb020, 0xa39ca8},
      // Classic: navy, amber, violet and teal
      {0x0d151b, 0x16212a, 0xe7edf1, 0x8d9ba7, 0x26343f, 0x2fd4c0, 0xfbbf3c, 0x9b8cff, 0x2fd4c0, 0x2fd4c0, 0xff7c5e, 0xfcd35c},
      // Ocean: deep blue, yellow sun and cyan
      {0x071320, 0x0f2034, 0xeaf3ff, 0x8ea4c2, 0x1c3350, 0x3fd0ff, 0xffd23f, 0x8fa5cc, 0x3fd0ff, 0x3fd0ff, 0xff8a5c, 0xffd23f},
  };
  const Pal& c = pals[p >= 0 && p < 3 ? p : 0];
  C_BG = lv_color_hex(c.bg);
  C_TILE = lv_color_hex(c.tile);
  C_INK = lv_color_hex(c.ink);
  C_MUTED = lv_color_hex(c.muted);
  C_FAINT = lv_color_hex(c.faint);
  C_PINK = lv_color_hex(c.accent);
  C_SOLAR = lv_color_hex(c.solar);
  C_GRID = lv_color_hex(c.grid);
  C_BATT = lv_color_hex(c.batt);
  C_GOOD = lv_color_hex(c.good);
  C_WARN = lv_color_hex(c.warn);
  C_OK = lv_color_hex(c.ok);
}

const char* hx(lv_color_t c) {
  static char buf[12][8];
  static int i = 0;
  i = (i + 1) % 12;
  const uint32_t v = lv_color_to32(c);
  snprintf(buf[i], sizeof buf[i], "%06lx", (unsigned long)(v & 0xFFFFFF));
  return buf[i];
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

void fmtKw(char* buf, size_t n, float v) {
  const float a = fabsf(v);
  if (isnan(v)) snprintf(buf, n, "\xE2\x80\x93");
  else if (a < 10) snprintf(buf, n, "%.2f", a);
  else snprintf(buf, n, "%.1f", a);
}

// ---- top & bottom bars -----------------------------------------------------
static lv_obj_t* chip(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_style_bg_color(c, C_TILE, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 22, 0);
  lv_obj_set_style_pad_hor(c, 18, 0);
  lv_obj_set_height(c, 44);
  lv_obj_set_width(c, LV_SIZE_CONTENT);
  lv_obj_t* l = label(c, &lv_font_montserrat_20, C_INK);
  lv_obj_center(l);
  lv_label_set_recolor(l, true);
  return l;
}

static void buildBars(lv_obj_t* scr) {
  lv_obj_t* top = plain(scr);
  lv_obj_set_size(top, W, TOP_H);
  lv_obj_set_pos(top, 0, 0);
  clockLbl = label(top, &lv_font_montserrat_32, C_INK, "--:--");
  lv_obj_align(clockLbl, LV_ALIGN_LEFT_MID, 32, 0);
  dateLbl = label(top, &lv_font_montserrat_20, C_MUTED);
  lv_obj_align_to(dateLbl, clockLbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 16, -3);

  lv_obj_t* row = plain(top);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 44);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_align(row, LV_ALIGN_RIGHT_MID, -28, 0);
  chipSolar = chip(row);
  chipAside = chip(row);

  lv_obj_t* bot = plain(scr);
  lv_obj_set_size(bot, W, BOT_H);
  lv_obj_set_pos(bot, 0, board::SCREEN_H - BOT_H);
  statusPill = lv_obj_create(bot);
  lv_obj_remove_style_all(statusPill);
  lv_obj_set_style_radius(statusPill, 22, 0);
  lv_obj_set_style_bg_opa(statusPill, LV_OPA_20, 0);
  lv_obj_set_style_pad_hor(statusPill, 18, 0);
  lv_obj_set_size(statusPill, LV_SIZE_CONTENT, 44);
  lv_obj_set_flex_flow(statusPill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusPill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(statusPill, 10, 0);
  lv_obj_align(statusPill, LV_ALIGN_LEFT_MID, 24, -4);
  statusDot = lv_obj_create(statusPill);
  lv_obj_remove_style_all(statusDot);
  lv_obj_set_size(statusDot, 12, 12);
  lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, 0);
  statusLbl = label(statusPill, &lv_font_montserrat_20, C_MUTED, "Starting...");

  // page dots: tap one to jump to that page
  for (int i = 0; i < PAGES; i++) {
    dots[i] = lv_obj_create(bot);
    lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 18, 5);
    lv_obj_set_style_radius(dots[i], 3, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_align(dots[i], LV_ALIGN_CENTER, (i - (PAGES - 1) / 2.0f) * 24, -4);
    lv_obj_add_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(dots[i], 18);
    lv_obj_add_event_cb(dots[i], [](lv_event_t* e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
  }

  // Home and Settings buttons, on every page
  auto navBtn = [&](lv_obj_t** icon, const char* sym, int page, int x) {
    lv_obj_t* b = lv_btn_create(bot);
    lv_obj_set_size(b, 64, 46);
    lv_obj_set_style_bg_color(b, C_TILE, 0);
    lv_obj_set_style_bg_color(b, C_FAINT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, x, -4);
    *icon = label(b, &lv_font_montserrat_22, C_MUTED, sym);
    lv_obj_center(*icon);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED, (void*)(intptr_t)page);
    return b;
  };
  setBtn = navBtn(&setIcon, LV_SYMBOL_SETTINGS, PAGES - 1, -24);
  homeBtn = navBtn(&homeIcon, LV_SYMBOL_HOME, 0, -24 - 64 - 12);

  // only shown while the screen isn't on Wi-Fi yet: how to reach the setup page
  hintLbl = label(bot, &lv_font_montserrat_18, C_MUTED);
  lv_obj_align(hintLbl, LV_ALIGN_RIGHT_MID, -24 - 2 * 64 - 12 - 18, -4);
}

// ---- now page: four sources around a hub with moving dots -------------------
static constexpr int HUB_X = W / 2, HUB_Y = BODY_H / 2 + 6, HUB_R = 46;
static constexpr int BADGE_R = 58;

static void dotAnim(void* var, int32_t v) {
  Link* k = (Link*)lv_obj_get_user_data((lv_obj_t*)var);
  // v runs 0..999 over one period; the three dots are spaced a third apart
  for (int i = 0; i < 3; i++) {
    if (k->dot[i] != var) continue;
    int t = (v + i * 333) % 1000;
    if (k->dir < 0) t = 999 - t;
    const int x = k->pts[0].x + (k->pts[1].x - k->pts[0].x) * t / 1000;
    const int y = k->pts[0].y + (k->pts[1].y - k->pts[0].y) * t / 1000;
    lv_obj_set_pos(k->dot[i], x - 5, y - 5);
  }
}

static void buildLink(lv_obj_t* parent, Link& k, int nx, int ny, lv_color_t color) {
  const float dx = HUB_X - nx, dy = HUB_Y - ny, len = sqrtf(dx * dx + dy * dy);
  const float ux = dx / len, uy = dy / len;
  k.pts[0] = {(lv_coord_t)(nx + ux * (BADGE_R + 14)), (lv_coord_t)(ny + uy * (BADGE_R + 14))};
  k.pts[1] = {(lv_coord_t)(HUB_X - ux * (HUB_R + 12)), (lv_coord_t)(HUB_Y - uy * (HUB_R + 12))};
  k.color = color;
  k.dir = 0;
  k.periodMs = 0;
  k.line = lv_line_create(parent);
  lv_line_set_points(k.line, k.pts, 2);
  lv_obj_set_style_line_width(k.line, 4, 0);
  lv_obj_set_style_line_color(k.line, C_FAINT, 0);
  lv_obj_set_style_line_rounded(k.line, true, 0);
  for (int i = 0; i < 3; i++) {
    k.dot[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(k.dot[i]);
    lv_obj_set_size(k.dot[i], 10, 10);
    lv_obj_set_style_radius(k.dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k.dot[i], color, 0);
    lv_obj_set_style_bg_opa(k.dot[i], LV_OPA_COVER, 0);
    lv_obj_set_user_data(k.dot[i], &k);
    lv_obj_add_flag(k.dot[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void setLink(Link& k, float kw, bool intoHub) {
  const int dir = fabsf(kw) > IDLE_KW ? (intoHub ? 1 : -1) : 0;
  // Faster dots for more power, but calm: ~4.5 s at a trickle down to ~1.2 s at 8 kW.
  uint32_t period = (uint32_t)constrain(4500.0f / (0.6f + fabsf(kw) * 0.4f), 1200.0f, 6000.0f);
  period = period / 100 * 100;
  if (dir == k.dir && (dir == 0 || period == k.periodMs)) return;
  k.dir = dir;
  k.periodMs = period;
  for (int i = 0; i < 3; i++) {
    lv_anim_del(k.dot[i], dotAnim);
    if (dir == 0) {
      lv_obj_add_flag(k.dot[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(k.dot[i], LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, k.dot[i]);
    lv_anim_set_exec_cb(&a, dotAnim);
    lv_anim_set_values(&a, 0, 999);
    lv_anim_set_time(&a, period);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
}

static void buildNode(lv_obj_t* parent, Node& n, int cx, int cy, bool textLeft, lv_color_t color, const char* icon,
                      const char* name) {
  n.badge = lv_obj_create(parent);
  lv_obj_remove_style_all(n.badge);
  lv_obj_set_size(n.badge, BADGE_R * 2, BADGE_R * 2);
  lv_obj_set_pos(n.badge, cx - BADGE_R, cy - BADGE_R);
  lv_obj_set_style_radius(n.badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(n.badge, lv_color_mix(color, C_BG, 40), 0);
  lv_obj_set_style_bg_opa(n.badge, LV_OPA_COVER, 0);
  n.icon = label(n.badge, &eh_icons_44, color, icon);
  lv_obj_center(n.icon);

  lv_obj_t* box = plain(parent);
  lv_obj_set_size(box, 260, 140);
  const int bx = textLeft ? cx - BADGE_R - 28 - 260 : cx + BADGE_R + 28;
  lv_obj_set_pos(box, bx, cy - 70);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, textLeft ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        textLeft ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
  n.label = label(box, &lv_font_montserrat_20, C_MUTED, name);
  lv_label_set_recolor(n.label, true);
  lv_obj_t* row = plain(box);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 8, 0);
  n.value = label(row, &eh_num_56, color, "\xE2\x80\x93");
  n.unit = label(row, &lv_font_montserrat_22, C_MUTED, "kW");
  lv_obj_set_style_pad_bottom(n.unit, 8, 0);
  n.sub = label(box, &lv_font_montserrat_20, C_MUTED, "");
}

static void buildNow(lv_obj_t* page) {
  const int dx = 172, dy = 118;
  // links first so badges draw on top of the line ends
  buildLink(page, lSolar, HUB_X - dx, HUB_Y - dy, C_SOLAR);
  buildLink(page, lBatt, HUB_X + dx, HUB_Y - dy, C_BATT);
  buildLink(page, lGrid, HUB_X - dx, HUB_Y + dy, C_GRID);
  buildLink(page, lHouse, HUB_X + dx, HUB_Y + dy, C_INK);

  lv_obj_t* hub = lv_obj_create(page);
  lv_obj_remove_style_all(hub);
  lv_obj_set_size(hub, HUB_R * 2, HUB_R * 2);
  lv_obj_set_pos(hub, HUB_X - HUB_R, HUB_Y - HUB_R);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, C_TILE, 0);
  lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(hub, C_FAINT, 0);
  lv_obj_set_style_border_width(hub, 2, 0);
  lv_obj_t* core = lv_obj_create(hub);
  lv_obj_remove_style_all(core);
  lv_obj_set_size(core, 18, 18);
  lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(core, C_PINK, 0);
  lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
  lv_obj_center(core);

  buildNode(page, nSolar, HUB_X - dx, HUB_Y - dy, true, C_SOLAR, ICON_SUN, "Solar");
  buildNode(page, nBatt, HUB_X + dx, HUB_Y - dy, false, C_BATT, ICON_BATT, "Battery");
  buildNode(page, nGrid, HUB_X - dx, HUB_Y + dy, true, C_GRID, ICON_BOLT, "Grid");
  buildNode(page, nHouse, HUB_X + dx, HUB_Y + dy, false, C_INK, ICON_HOME, "Home");

  // battery charge ring around the battery badge
  nBatt.ring = lv_arc_create(page);
  lv_obj_set_size(nBatt.ring, BADGE_R * 2 + 22, BADGE_R * 2 + 22);
  lv_obj_set_pos(nBatt.ring, HUB_X + dx - BADGE_R - 11, HUB_Y - dy - BADGE_R - 11);
  lv_arc_set_rotation(nBatt.ring, 270);
  lv_arc_set_bg_angles(nBatt.ring, 0, 360);
  lv_arc_set_range(nBatt.ring, 0, 100);
  lv_obj_remove_style(nBatt.ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(nBatt.ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(nBatt.ring, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(nBatt.ring, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(nBatt.ring, C_FAINT, LV_PART_MAIN);
  lv_obj_set_style_arc_color(nBatt.ring, C_BATT, LV_PART_INDICATOR);
  lv_arc_set_value(nBatt.ring, 0);
}

// ---- info page --------------------------------------------------------------
static lv_obj_t* infoRow(lv_obj_t* parent, const char* name) {
  lv_obj_t* row = plain(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(row, 10, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(row, C_FAINT, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_t* k = label(row, &lv_font_montserrat_20, C_MUTED, name);
  lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = label(row, &lv_font_montserrat_22, C_INK, "-");
  lv_obj_set_width(v, 560);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return v;
}

static void resetEvent(lv_event_t* e) {
  static uint32_t pressedAt = 0;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    pressedAt = millis();
  } else if (code == LV_EVENT_PRESSING && pressedAt) {
    const uint32_t held = millis() - pressedAt;
    if (held >= 5000) {
      lv_label_set_text(resetLbl, "Clearing settings...");
      lv_refr_now(nullptr);
      net::factoryReset();
    } else {
      lv_label_set_text_fmt(resetLbl, "Keep holding... %lu", (unsigned long)(5 - held / 1000));
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    pressedAt = 0;
    lv_label_set_text(resetLbl, "Reset all (hold 5 s)");
  }
}

// Firmware row: when a newer version is online, tap it, then tap again to install.
static uint32_t installAskedAt = 0;
static void firmwareTap(lv_event_t*) {
  if (!ota::updateAvailable()) {
    ota::checkNow();
    return;
  }
  if (installAskedAt && millis() - installAskedAt < 8000) {
    installAskedAt = 0;
    std::string err;
    ota::installOnline(err);
  } else {
    installAskedAt = millis() | 1;
  }
}

static void buildInfo(lv_obj_t* page) {
  lv_obj_t* col = plain(page);
  lv_obj_set_size(col, W - 120, BODY_H - 20);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* t = label(col, &lv_font_montserrat_28, C_INK, "Settings & info");
  lv_obj_set_style_pad_bottom(t, 6, 0);
  iAddr = infoRow(col, "Change settings on your phone");
  iWifi = infoRow(col, "Wi-Fi");
  iInv = infoRow(col, "Inverter");
  iErr = infoRow(col, "Last problem");
  iUpd = infoRow(col, "Last reading");
  iVer = infoRow(col, "Firmware");
  lv_label_set_text(iVer, "GridCheck " EH_VERSION);
  lv_obj_t* verRow = lv_obj_get_parent(iVer);
  lv_obj_add_flag(verRow, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(verRow, firmwareTap, LV_EVENT_CLICKED, nullptr);

  // quick screen settings: tap to step through the choices
  lv_obj_t* row = plain(col);
  lv_obj_set_size(row, LV_PCT(100), 72);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 14, 0);
  auto pill = [&](lv_obj_t** lbl, lv_event_cb_t cb, lv_color_t color) {
    lv_obj_t* b = lv_btn_create(row);
    lv_obj_set_size(b, 268, 56);
    lv_obj_set_style_bg_color(b, C_TILE, 0);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    *lbl = label(b, &lv_font_montserrat_20, color, "");
    lv_obj_center(*lbl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
  };
  pill(&palLbl, [](lv_event_t*) { net::setPalette((uint8_t)((net::settings().palette + 1) % 3)); }, C_INK);
  pill(&dimLbl, [](lv_event_t*) {
    const uint8_t cur = net::settings().dimAfterMin;
    int i = 0;
    while (i < 5 && DIM_CHOICES[i] != cur) i++;
    net::setDimAfter(DIM_CHOICES[(i + 1) % 5]);
  }, C_INK);

  resetBtn = lv_btn_create(row);
  lv_obj_set_size(resetBtn, 300, 56);
  lv_obj_set_style_bg_color(resetBtn, C_TILE, 0);
  lv_obj_set_style_radius(resetBtn, 28, 0);
  lv_obj_set_style_shadow_width(resetBtn, 0, 0);
  resetLbl = label(resetBtn, &lv_font_montserrat_20, C_WARN, "Reset all (hold 5 s)");
  lv_obj_center(resetLbl);
  lv_obj_add_event_cb(resetBtn, resetEvent, LV_EVENT_ALL, nullptr);
}

// ---- build -----------------------------------------------------------------
void create() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildBars(scr);
  tiles = lv_tileview_create(scr);
  lv_obj_set_size(tiles, W, BODY_H);
  lv_obj_set_pos(tiles, 0, TOP_H);
  lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* t[PAGES];
  for (int i = 0; i < PAGES; i++) {
    const lv_dir_t dir = i == 0 ? LV_DIR_RIGHT : i == PAGES - 1 ? LV_DIR_LEFT : (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT);
    t[i] = lv_tileview_add_tile(tiles, i, 0, dir);
    lv_obj_set_scrollbar_mode(t[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(t[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  buildNow(t[0]);
  buildToday(t[1]);
  buildSolar(t[2]);
  buildMoney(t[3]);
  buildFlow(t[4]);
  buildBattery(t[5]);
  buildHistory(t[6]);
  buildInfo(t[7]);
  update();
}

void rebuild() {
  const int page = constrain((int)((lv_obj_get_scroll_x(tiles) + W / 2) / W), 0, 7);
  lv_obj_clean(lv_scr_act());
  create();
  showPage(page);
  forceFull = true;
}

void showPage(int page) { lv_obj_set_tile_id(tiles, page, 0, LV_ANIM_OFF); }

// ---- update ----------------------------------------------------------------
// Time-of-use price right now (only when the clock is set and periods exist).
struct TouNow {
  bool on = false;
  int idx = -1;
  float price = 0;
  bool free = false, top = false;  // top: the dearest price of the day, with cheaper ones about
  char until[8] = "";
};
static TouNow touNow(const Tariff& t) {
  TouNow n;
  const time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  if (!t.hasPeriods() || tm.tm_year < 124) return n;
  const int m = tm.tm_hour * 60 + tm.tm_min;
  n.on = true;
  n.idx = periodAt(t, m, tm.tm_wday);
  n.price = priceAt(t, m, tm.tm_wday);
  float lo = t.buy, hi = t.buy;
  for (const auto& p : t.p)
    if (p.used) lo = fminf(lo, p.price), hi = fmaxf(hi, p.price);
  n.free = n.price < 0.005f;
  n.top = hi - lo > 0.5f && n.price > hi - 0.005f;
  const int until = (m + minutesUntilChange(t, m, tm.tm_wday)) % 1440;
  snprintf(n.until, sizeof n.until, "%02d:%02d", until / 60, until % 60);
  return n;
}

static void priceText(char* out, size_t n, float c) {
  if (c < 0.005f) snprintf(out, n, "free");
  else if (fabsf(c - roundf(c)) < 0.05f) snprintf(out, n, "%.0fc", c);
  else snprintf(out, n, "%.1fc", c);
}

static void setStatus(const char* text, lv_color_t color) {
  lv_label_set_text(statusLbl, text);
  lv_obj_set_style_text_color(statusLbl, color, 0);
  lv_obj_set_style_bg_color(statusDot, color, 0);
  lv_obj_set_style_bg_color(statusPill, color, 0);
}

void update() {
  const Live l = liveGet();
  char buf[96];

  // clock
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, sizeof buf, "%l:%M %p", &t);
    lv_label_set_text(clockLbl, buf[0] == ' ' ? buf + 1 : buf);
    strftime(buf, sizeof buf, "%a %e %b", &t);
    lv_label_set_text(dateLbl, buf);
  } else {
    lv_label_set_text(clockLbl, "--:--");
    lv_label_set_text(dateLbl, net::connected() ? "setting clock..." : "");
  }
  lv_obj_align_to(dateLbl, clockLbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 16, -3);

  // page dots
  const int page = constrain((int)((lv_obj_get_scroll_x(tiles) + W / 2) / W), 0, PAGES - 1);
  for (int i = 0; i < PAGES; i++) lv_obj_set_style_bg_color(dots[i], i == page ? C_INK : C_FAINT, 0);

  // Pages with charts redraw when shown, when the history changes, and every minute.
  static int lastPage = -1;
  static uint32_t lastData = 0, lastFull = 0;
  const bool full = forceFull || page != lastPage || data::version() != lastData || millis() - lastFull > 60000;
  forceFull = false;
  if (full) lastPage = page, lastData = data::version(), lastFull = millis();
  switch (page) {
    case 1: updateToday(l, full); break;
    case 2: updateSolar(l, full); break;
    case 3: updateMoney(l, full); break;
    case 4: updateFlow(l, full); break;
    case 5: updateBattery(l, full); break;
    case 6: updateHistory(l, full); break;
    default: break;
  }

  // where to change settings
  const std::string addr = net::address();
  // On Wi-Fi: the Settings button shows the phone address. Not yet: say how to reach setup,
  // in place of the page dots.
  const bool online = net::connected();
  lv_label_set_text(hintLbl, online ? "" : addr.c_str());
  lv_obj_align(hintLbl, LV_ALIGN_RIGHT_MID, -24 - 2 * 64 - 12 - 18, -4);
  for (int i = 0; i < PAGES; i++) {
    if (online) lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_color(homeIcon, page == 0 ? C_INK : C_MUTED, 0);
  lv_obj_set_style_text_color(setIcon, page == PAGES - 1 ? C_INK : C_MUTED, 0);
  lv_label_set_text(iAddr, addr.length() ? addr.c_str() : "-");
  lv_label_set_text(iWifi, net::connected() ? net::ssidInUse().c_str() : net::apMode() ? "not connected - setup hotspot is on" : "connecting...");
  const Settings s = net::settings();
  if (s.demo) lv_label_set_text(iInv, "Demo data (simulated)");
  else if (s.host.length()) lv_label_set_text_fmt(iInv, "%s:%u  (plant %u)  %s", s.host.c_str(), s.port, s.plantId, l.ok ? "- connected" : "- not reading");
  else lv_label_set_text(iInv, "not set up yet");
  lv_label_set_text(iErr, l.error[0] ? l.error : "none");
  lv_label_set_text_fmt(palLbl, "Colours: %s", PALETTE_NAMES[s.palette % 3]);
  if (s.dimAfterMin) lv_label_set_text_fmt(dimLbl, "Dim after %u min", s.dimAfterMin);
  else lv_label_set_text(dimLbl, "Screen always on");
  if (l.updatedMs) lv_label_set_text_fmt(iUpd, "%lu s ago", (unsigned long)((millis() - l.updatedMs) / 1000));
  else lv_label_set_text(iUpd, "never");

  // firmware updates
  const ota::Status os = ota::status();
  const bool installing = os.state == ota::State::Downloading || os.state == ota::State::Writing;
  if (installing) {
    lv_label_set_text_fmt(iVer, "Installing update... %d%%", os.percent);
    lv_obj_set_style_text_color(iVer, C_OK, 0);
  } else if (os.state == ota::State::Done) {
    lv_label_set_text(iVer, "Update installed - restarting");
    lv_obj_set_style_text_color(iVer, C_GOOD, 0);
  } else if (ota::updateAvailable()) {
    if (installAskedAt && millis() - installAskedAt < 8000)
      lv_label_set_text_fmt(iVer, "Tap again to install %s", os.latest.c_str());
    else
      lv_label_set_text_fmt(iVer, "%s  -  %s available, tap to install", EH_VERSION, os.latest.c_str());
    lv_obj_set_style_text_color(iVer, C_GOOD, 0);
  } else {
    if (os.state == ota::State::Failed) lv_label_set_text(iVer, os.message.c_str());
    else if (os.state == ota::State::Checking) lv_label_set_text(iVer, "GridCheck " EH_VERSION "  -  checking for updates...");
    else if (os.latest.length()) lv_label_set_text(iVer, "GridCheck " EH_VERSION "  -  up to date");
    else lv_label_set_text(iVer, "GridCheck " EH_VERSION);
    lv_obj_set_style_text_color(iVer, os.state == ota::State::Failed ? C_WARN : C_INK, 0);
  }

  // status line
  const TouNow tou = touNow(s.tariff);
  const bool fresh = l.ok && millis() - l.updatedMs < 30000;
  if (installing || os.state == ota::State::Done) {
    char b[60];
    if (installing) snprintf(b, sizeof b, "Updating firmware %d%%", os.percent);
    else snprintf(b, sizeof b, "Update installed - restarting");
    setStatus(b, C_OK);
  } else if (!s.demo && !s.host.length()) {
    setStatus("Not set up yet", C_OK);
  } else if (!net::connected() && !s.demo) {
    setStatus(net::apMode() ? "Can't join Wi-Fi - fix it from your phone" : "Joining Wi-Fi...", C_MUTED);
  } else if (!fresh) {
    setStatus(l.updatedMs ? "Can't reach inverter - swipe for details" : "Connecting to inverter...", C_MUTED);
  } else if (tou.on && tou.free) {
    char b[48];
    snprintf(b, sizeof b, "Free power until %s - use it now", tou.until);
    setStatus(b, C_GOOD);
  } else if (l.grid < -0.3f) {
    setStatus("Spare solar - run big appliances now", C_GOOD);
  } else if (l.solar > l.house + 0.2f && l.battery > 0.1f) {
    setStatus("Solar is charging the battery", C_GOOD);
  } else if (l.grid > 0.3f && tou.on && tou.top) {
    char b[48];
    snprintf(b, sizeof b, "%.10s price until %s - hold off", periodName(s.tariff, tou.idx), tou.until);
    setStatus(b, C_WARN);
  } else if (l.grid > 0.3f) {
    setStatus("On grid power - hold off big loads", C_WARN);
  } else if (l.battery < -0.1f) {
    setStatus("Running on battery", C_OK);
  } else if (l.solar > 0.1f) {
    setStatus("Running on solar", C_GOOD);
  } else {
    setStatus("All quiet", C_OK);
  }

  // top chips
  if (isnan(l.solarToday)) lv_label_set_text(chipSolar, "Solar today  -");
  else lv_label_set_text_fmt(chipSolar, "Solar today  #%s %.1f kWh#", hx(C_SOLAR), l.solarToday);
  if (tou.on) {
    // what grid power costs right now, instead of yesterday's solar
    char pr[12];
    priceText(pr, sizeof pr, tou.price);
    const lv_color_t c = tou.free ? C_GOOD : tou.top ? C_WARN : C_INK;
    if (tou.free && !strcasecmp(periodName(s.tariff, tou.idx), "Free"))
      lv_label_set_text_fmt(chipAside, "#%s Free# until %s", hx(c), tou.until);
    else
      lv_label_set_text_fmt(chipAside, "%s  #%s %s# until %s", periodName(s.tariff, tou.idx), hx(c), pr, tou.until);
  } else if (isnan(l.solarYesterday)) {
    lv_label_set_text(chipAside, "Yesterday -");
  } else {
    lv_label_set_text_fmt(chipAside, "Yesterday  #%s %.1f kWh#", hx(C_SOLAR), l.solarYesterday);
  }

  if (!fresh) {
    // No current reading: show dashes rather than stale or made-up numbers.
    for (Node* n : {&nSolar, &nBatt, &nGrid, &nHouse}) {
      lv_label_set_text(n->value, "\xE2\x80\x93");
      lv_label_set_text(n->sub, "");
    }
    lv_label_set_text(nBatt.label, "Battery");
    lv_arc_set_value(nBatt.ring, 0);
    for (Link* k : {&lSolar, &lGrid, &lBatt, &lHouse}) setLink(*k, 0, true);
    return;
  }

  // now page
  fmtKw(buf, sizeof buf, l.solar);
  lv_label_set_text(nSolar.value, buf);
  fmtKw(buf, sizeof buf, l.battery);
  lv_label_set_text(nBatt.value, buf);
  fmtKw(buf, sizeof buf, l.grid);
  lv_label_set_text(nGrid.value, buf);
  fmtKw(buf, sizeof buf, l.house);
  lv_label_set_text(nHouse.value, buf);

  lv_label_set_text(nSolar.sub, l.solar > IDLE_KW ? "Generating" : "Not generating");
  lv_label_set_text(nGrid.sub, l.grid < -IDLE_KW ? "Exporting" : l.grid > IDLE_KW ? "Importing" : "Idle");
  lv_label_set_text(nBatt.sub, l.battery > IDLE_KW ? "Charging" : l.battery < -IDLE_KW ? "Discharging" : "Idle");
  // what the home is mostly running on
  const float fromSolar = fminf(fmaxf(l.solar, 0), l.house);
  const float fromBatt = fminf(fmaxf(-l.battery, 0), l.house - fromSolar);
  const float fromGrid = fmaxf(l.house - fromSolar - fromBatt, 0);
  const char* mostly = fromSolar >= fromBatt && fromSolar >= fromGrid ? "solar" : fromBatt >= fromGrid ? "battery" : "the grid";
  lv_label_set_text_fmt(nHouse.sub, "Mostly from %s", mostly);

  if (!isnan(l.soc)) {
    lv_label_set_text_fmt(nBatt.label, "Battery  #%s %d%%#", hx(C_INK), (int)lroundf(l.soc));
    lv_arc_set_value(nBatt.ring, (int)lroundf(l.soc));
  }
  const bool low = !isnan(l.soc) && l.soc <= 15;
  lv_obj_set_style_arc_color(nBatt.ring, low ? C_WARN : C_BATT, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(nBatt.value, low ? C_WARN : C_BATT, 0);

  setLink(lSolar, l.solar, true);
  setLink(lGrid, l.grid, l.grid > 0);
  setLink(lBatt, l.battery, l.battery < 0);
  setLink(lHouse, l.house, false);
}

}  // namespace ui
