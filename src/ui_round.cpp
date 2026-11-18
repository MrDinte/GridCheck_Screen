// The round 360x360 layout for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 / 1.8B.
// Six pages, in step with the web dashboard's round layout (round.html): Now, Battery,
// Solar, Money, Today and Settings. Turn the knob (or swipe) to change page; the edge
// ring shows the battery charge (solar progress on the Solar page).
#include <ctime>
#include <strings.h>

#include "ui.h"
#include "ui_common.h"

#ifndef EH_VERSION
#define EH_VERSION "0.0.0"
#endif

namespace ui {

static constexpr int S = board::SCREEN_W;  // 360
static constexpr int C = S / 2;            // centre
enum { P_NOW, P_BATT, P_SOLAR, P_MONEY, P_TODAY, P_SET, PAGES };

static lv_obj_t *tiles, *ring, *statusDot, *statusLbl, *dots[PAGES];
static int page = 0;
// Now
static lv_obj_t *nSolar, *nHouse, *nGrid, *nGridSub, *nBattLbl, *nBatt, *nBattSub, *nSetup, *nBody, *nAway;
// Battery
static lv_obj_t *bBody, *bCap, *bFill, *bPct, *bState, *bEta;
// Solar
static lv_obj_t *sToday, *sTarget, *sTmrw;
// Money
static lv_obj_t *mSpent, *mEarned, *mBuy, *mSell;
// Today
static lv_obj_t *tSolar, *tHouse, *tImp, *tExp, *tSelf;
// Settings
static lv_obj_t *xAddr, *xWifi, *xPal, *xTheme, *xFw, *xReset;
static uint32_t installAskedAt = 0;

static constexpr int BATT_W = 112, BATT_H = 54, BATT_PAD = 5, BATT_BORDER = 5;

// ---- helpers -------------------------------------------------------------------
static lv_obj_t* centred(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int y, const char* text = "") {
  lv_obj_t* l = label(parent, font, color, text);
  lv_obj_set_width(l, 260);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_recolor(l, true);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
}

/** A big number with a small unit after it ("0.68 kW"), centred as one. */
static lv_obj_t* bigValue(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* unit, int y, lv_obj_t** unitOut = nullptr) {
  lv_obj_t* row = plain(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 5, 0);
  // the en dash is only in GridCheck's own number fonts; the built-in ones get a hyphen
  lv_obj_t* v = label(row, font, color, font == &eh_num_56 || font == &eh_num_76 ? "\xE2\x80\x93" : "-");
  lv_obj_t* u = label(row, &lv_font_montserrat_16, C_MUTED, unit);
  lv_obj_set_style_pad_bottom(u, font->line_height > 60 ? 12 : 7, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
  if (unitOut) *unitOut = u;
  return v;
}

/** "Spent ........ $1.26" rows on the Money and Today pages. */
static lv_obj_t* listRow(lv_obj_t* parent, const char* name, lv_color_t color, int y) {
  lv_obj_t* r = plain(parent);
  lv_obj_set_size(r, 236, 34);
  lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_t* k = label(r, &lv_font_montserrat_16, C_MUTED, name);
  lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = label(r, &lv_font_montserrat_22, color, "-");
  lv_label_set_recolor(v, true);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return v;
}

static lv_obj_t* pill(lv_obj_t* parent, int w, int y, lv_event_cb_t cb, lv_event_code_t code = LV_EVENT_CLICKED) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, 36);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_bg_color(b, C_TILE, 0);
  lv_obj_set_style_bg_color(b, C_FAINT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 18, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = label(b, &lv_font_montserrat_16, C_INK);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, code, nullptr);
  return l;
}

/** "2 h 6 min"; false (and nothing written) when the time isn't known. */
static bool duration(char* buf, size_t n, float hours) {
  if (!(hours >= 0) || std::isinf(hours)) return false;
  const int m = (int)lroundf(hours * 60);
  if (m < 60) snprintf(buf, n, "%d min", m < 1 ? 1 : m);
  else if (m / 60 >= 48) snprintf(buf, n, "%d days", m / 60 / 24);
  else if (m % 60) snprintf(buf, n, "%d h %d min", m / 60, m % 60);
  else snprintf(buf, n, "%d h", m / 60);
  return true;
}

static void time12(char* b, size_t n, int m) {
  m %= 1440;
  if (m == 0) snprintf(b, n, "midnight");
  else if (m == 720) snprintf(b, n, "midday");
  else if (m % 60) snprintf(b, n, "%d:%02d %s", (m / 60 + 11) % 12 + 1, m % 60, m < 720 ? "am" : "pm");
  else snprintf(b, n, "%d %s", (m / 60 + 11) % 12 + 1, m < 720 ? "am" : "pm");
}

// ---- pages ----------------------------------------------------------------------
static void roundNow(lv_obj_t* t) {
  nBody = plain(t);
  lv_obj_set_size(nBody, S, S);
  // away from home, reading through Tailscale
  nAway = centred(nBody, &lv_font_montserrat_14, C_MUTED, 24, LV_SYMBOL_WIFI " via Tailscale");
  lv_obj_add_flag(nAway, LV_OBJ_FLAG_HIDDEN);
  centred(nBody, &lv_font_montserrat_16, C_MUTED, 46, "Solar");
  nSolar = bigValue(nBody, &lv_font_montserrat_40, C_SOLAR, "kW", 64);
  centred(nBody, &lv_font_montserrat_16, C_MUTED, 116, "Home");
  nHouse = bigValue(nBody, &eh_num_56, C_INK, "kW", 132);
  auto col = [&](int x, const char* name, lv_color_t color, lv_obj_t** lbl, lv_obj_t** val, lv_obj_t** sub) {
    lv_obj_t* c = plain(nBody);
    lv_obj_set_size(c, 130, 80);
    lv_obj_set_pos(c, x, 196);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    *lbl = label(c, &lv_font_montserrat_16, C_MUTED, name);
    lv_label_set_recolor(*lbl, true);
    *val = label(c, &lv_font_montserrat_24, color, "-");
    *sub = label(c, &lv_font_montserrat_14, C_MUTED, "");
  };
  lv_obj_t* gridLbl;
  col(C - 132, "Grid", C_GRID, &gridLbl, &nGrid, &nGridSub);
  col(C + 2, "Battery", C_BATT, &nBattLbl, &nBatt, &nBattSub);

  // before set-up, or when Wi-Fi can't be joined: how to reach the phone page
  nSetup = centred(t, &lv_font_montserrat_20, C_INK, 92, "");
  lv_obj_set_width(nSetup, 250);
  lv_obj_align(nSetup, LV_ALIGN_TOP_MID, 0, 92);
  lv_label_set_long_mode(nSetup, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(nSetup, LV_OBJ_FLAG_HIDDEN);
}

static void roundBattery(lv_obj_t* t) {
  // a battery lying on its side, filling left to right with the charge
  const int bx = C - BATT_W / 2 - 4, by = 58;
  bBody = plain(t);
  lv_obj_set_size(bBody, BATT_W, BATT_H);
  lv_obj_set_pos(bBody, bx, by);
  lv_obj_set_style_radius(bBody, 12, 0);
  lv_obj_set_style_border_color(bBody, C_FAINT, 0);
  lv_obj_set_style_border_width(bBody, BATT_BORDER, 0);
  lv_obj_set_style_pad_all(bBody, BATT_PAD, 0);
  bCap = plain(t);
  lv_obj_set_size(bCap, 9, 22);
  lv_obj_set_pos(bCap, bx + BATT_W + 1, by + (BATT_H - 22) / 2);
  lv_obj_set_style_radius(bCap, 4, 0);
  lv_obj_set_style_bg_color(bCap, C_FAINT, 0);
  lv_obj_set_style_bg_opa(bCap, LV_OPA_COVER, 0);
  bFill = plain(bBody);
  lv_obj_set_size(bFill, 0, LV_PCT(100));
  lv_obj_align(bFill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(bFill, 6, 0);
  lv_obj_set_style_bg_color(bFill, C_BATT, 0);
  lv_obj_set_style_bg_opa(bFill, LV_OPA_COVER, 0);

  bPct = bigValue(t, &eh_num_76, C_BATT, "%", 118);
  bState = centred(t, &lv_font_montserrat_20, C_BATT, 204);
  bEta = centred(t, &lv_font_montserrat_16, C_MUTED, 234);
}

static void roundSolar(lv_obj_t* t) {
  centred(t, &lv_font_montserrat_16, C_MUTED, 74, "Solar today");
  sToday = bigValue(t, &eh_num_76, C_SOLAR, "kWh", 94);
  sTarget = centred(t, &lv_font_montserrat_16, C_MUTED, 180);
  centred(t, &lv_font_montserrat_16, C_MUTED, 214, "Tomorrow");
  sTmrw = centred(t, &lv_font_montserrat_22, C_SOLAR, 236);
}

static void roundMoney(lv_obj_t* t) {
  centred(t, &lv_font_montserrat_16, C_MUTED, 56, "Today");
  mSpent = listRow(t, "Spent", C_WARN, 78);
  mEarned = listRow(t, "Earned", C_GOOD, 112);
  lv_obj_t* rule = plain(t);
  lv_obj_set_size(rule, 236, 1);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 152);
  lv_obj_set_style_bg_color(rule, C_FAINT, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  centred(t, &lv_font_montserrat_16, C_MUTED, 162, "Grid power now");
  mBuy = centred(t, &lv_font_montserrat_20, C_INK, 182);
  centred(t, &lv_font_montserrat_16, C_MUTED, 214, "Feed-in now");
  mSell = centred(t, &lv_font_montserrat_20, C_INK, 234);
}

static void roundToday(lv_obj_t* t) {
  centred(t, &lv_font_montserrat_16, C_MUTED, 50, "Today");
  tSolar = listRow(t, "Solar made", C_SOLAR, 72);
  tHouse = listRow(t, "Home used", C_INK, 106);
  tImp = listRow(t, "From grid", C_GRID, 140);
  tExp = listRow(t, "To grid", C_GRID, 174);
  tSelf = listRow(t, "Self-powered", C_BATT, 208);
}

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

static void resetEvent(lv_event_t* e) {
  static uint32_t pressedAt = 0;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    pressedAt = millis();
  } else if (code == LV_EVENT_PRESSING && pressedAt) {
    const uint32_t held = millis() - pressedAt;
    if (held >= 5000) {
      lv_label_set_text(xReset, "Clearing settings...");
      lv_refr_now(nullptr);
      net::factoryReset();
    } else {
      lv_label_set_text_fmt(xReset, "Keep holding... %lu", (unsigned long)(5 - held / 1000));
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    pressedAt = 0;
    lv_label_set_text(xReset, "Reset all (hold 5 s)");
  }
}

static void roundSettings(lv_obj_t* t) {
  centred(t, &lv_font_montserrat_16, C_MUTED, 40, "Settings on your phone");
  xAddr = centred(t, &lv_font_montserrat_18, C_INK, 60);
  lv_label_set_long_mode(xAddr, LV_LABEL_LONG_DOT);
  xWifi = centred(t, &lv_font_montserrat_14, C_MUTED, 84);
  lv_label_set_long_mode(xWifi, LV_LABEL_LONG_DOT);
  xPal = pill(t, 210, 110, [](lv_event_t*) { net::setPalette((uint8_t)((net::settings().palette + 1) % 3)); });
  xTheme = pill(t, 230, 152, [](lv_event_t*) { net::setTheme((uint8_t)((net::settings().theme + 1) % 3)); });
  xFw = pill(t, 250, 194, firmwareTap);
  lv_label_set_long_mode(xFw, LV_LABEL_LONG_DOT);
  lv_obj_set_width(xFw, 230);
  lv_obj_set_style_text_align(xFw, LV_TEXT_ALIGN_CENTER, 0);
  xReset = pill(t, 190, 236, resetEvent, LV_EVENT_ALL);
  lv_obj_set_style_text_color(xReset, C_WARN, 0);
  lv_obj_set_style_text_font(xReset, &lv_font_montserrat_14, 0);
  lv_label_set_text(xReset, "Reset all (hold 5 s)");
  lv_obj_t* b = lv_obj_get_parent(xReset);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(b, C_FAINT, 0);
  lv_obj_set_style_border_width(b, 1, 0);
}

// ---- build ---------------------------------------------------------------------
static void onScroll(lv_event_t*) {
  page = lv_obj_get_index(lv_tileview_get_tile_act(tiles));
}

void create() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // edge ring: battery charge (solar progress on the Solar page)
  ring = lv_arc_create(scr);
  lv_obj_set_size(ring, S - 8, S - 8);
  lv_obj_center(ring);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 100);
  lv_arc_set_value(ring, 0);
  lv_obj_remove_style(ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(ring, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, C_FAINT, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, C_BATT, LV_PART_INDICATOR);

  tiles = lv_tileview_create(scr);
  lv_obj_set_size(tiles, S, S);
  lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* t[PAGES];
  for (int i = 0; i < PAGES; i++) {
    const lv_dir_t dir = i == 0 ? LV_DIR_RIGHT : i == PAGES - 1 ? LV_DIR_LEFT : (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT);
    t[i] = lv_tileview_add_tile(tiles, i, 0, dir);
    lv_obj_set_scrollbar_mode(t[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(t[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_obj_add_event_cb(tiles, onScroll, LV_EVENT_VALUE_CHANGED, nullptr);
  roundNow(t[P_NOW]);
  roundBattery(t[P_BATT]);
  roundSolar(t[P_SOLAR]);
  roundMoney(t[P_MONEY]);
  roundToday(t[P_TODAY]);
  roundSettings(t[P_SET]);

  // status line and page dots along the bottom
  lv_obj_t* st = plain(scr);
  lv_obj_set_size(st, LV_SIZE_CONTENT, 24);
  lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(st, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(st, 7, 0);
  lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 282);
  statusDot = plain(st);
  lv_obj_set_size(statusDot, 8, 8);
  lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, 0);
  statusLbl = label(st, &lv_font_montserrat_14, C_MUTED, "Starting...");
  for (int i = 0; i < PAGES; i++) {
    dots[i] = plain(scr);
    lv_obj_set_size(dots[i], 6, 6);
    lv_obj_set_style_radius(dots[i], 3, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
  }
  update();
}

void rebuild() {
  const int keep = page;
  lv_obj_clean(lv_scr_act());
  create();
  showPage(keep);
}

void showPage(int p) {
  page = (p % PAGES + PAGES) % PAGES;
  lv_obj_set_tile_id(tiles, page, 0, LV_ANIM_OFF);
}

void knob(int clicks) {
  page = ((page + clicks) % PAGES + PAGES) % PAGES;  // round and round
  lv_obj_set_tile_id(tiles, page, 0, LV_ANIM_ON);
}

void setRanges(int, int, int) {}  // the round screen has no range buttons
void openPrices() {}               // prices are set on the phone page

// ---- update --------------------------------------------------------------------
void update() {
  const Live l = liveGet();
  const Settings s = net::settings();
  const bool fresh = l.ok && millis() - l.updatedMs < 30000;
  char b[64];
  const char* dash = "-";                     // built-in fonts
  const char* bigDash = "\xE2\x80\x93";      // GridCheck's number fonts (eh_num_*)

  // page dots: the current one wider
  int x = C - (PAGES * 6 + (PAGES - 1) * 6 + 10) / 2;
  for (int i = 0; i < PAGES; i++) {
    const int w = i == page ? 16 : 6;
    lv_obj_set_size(dots[i], w, 6);
    lv_obj_set_pos(dots[i], x, 314);
    lv_obj_set_style_bg_color(dots[i], i == page ? C_INK : C_FAINT, 0);
    x += w + 6;
  }

  // status line (the Settings page has no room for it)
  const StatusNow stat = statusNow(l, s, true);
  lv_label_set_text(statusLbl, stat.text);
  lv_obj_set_style_text_color(statusLbl, stat.color, 0);
  lv_obj_set_style_bg_color(statusDot, stat.color, 0);
  if (page == P_SET) lv_obj_add_flag(lv_obj_get_parent(statusLbl), LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(lv_obj_get_parent(statusLbl), LV_OBJ_FLAG_HIDDEN);

  // edge ring
  const bool haveSoc = fresh && !std::isnan(l.soc);
  const bool low = haveSoc && (!std::isnan(l.reserve) ? l.soc <= l.reserve + 5 : l.soc <= 15);
  const Forecast fc = data::forecast();
  if (page == P_NOW || page == P_BATT) {
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(ring, haveSoc ? (int)lroundf(l.soc) : 0);
    lv_obj_set_style_arc_color(ring, low ? C_WARN : C_BATT, LV_PART_INDICATOR);
  } else if (page == P_SOLAR) {
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
    const float pct = fc.ok && fc.today > 0 && !std::isnan(l.solarToday) ? fminf(1, l.solarToday / fc.today) * 100 : 0;
    lv_arc_set_value(ring, (int)lroundf(pct));
    lv_obj_set_style_arc_color(ring, C_SOLAR, LV_PART_INDICATOR);
  } else {
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
  }

  // ---- Now
  const bool needSetup = net::apMode() || (!s.demo && !s.host.length());
  if (needSetup) {
    if (net::apMode())
      lv_label_set_text_fmt(nSetup, "To set up, join the Wi-Fi\n#%s GridCheck-Setup#\non your phone", hx(C_PINK));
    else
      lv_label_set_text_fmt(nSetup, "Open\n#%s %s#\non your phone to finish setting up", hx(C_PINK), net::address().c_str());
    lv_obj_clear_flag(nSetup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(nBody, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(nSetup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nBody, LV_OBJ_FLAG_HIDDEN);
  }
  if (fresh && l.remote) lv_obj_clear_flag(nAway, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(nAway, LV_OBJ_FLAG_HIDDEN);
  if (fresh) {
    fmtKw(b, sizeof b, l.solar);
    lv_label_set_text(nSolar, b);
    fmtKw(b, sizeof b, l.house);
    lv_label_set_text(nHouse, b);
    fmtKw(b, sizeof b, l.grid);
    lv_label_set_text_fmt(nGrid, "%s kW", b);
    lv_label_set_text(nGridSub, l.grid < -IDLE_KW ? LV_SYMBOL_UP " exporting" : l.grid > IDLE_KW ? LV_SYMBOL_DOWN " importing" : "idle");
    fmtKw(b, sizeof b, l.battery);
    lv_label_set_text_fmt(nBatt, "%s kW", b);
    lv_label_set_text(nBattSub, l.battery > IDLE_KW ? "charging" : l.battery < -IDLE_KW ? "discharging" : "idle");
    if (!std::isnan(l.soc)) lv_label_set_text_fmt(nBattLbl, "Battery #%s %d%%#", hx(C_INK), (int)lroundf(l.soc));
    lv_obj_set_style_text_color(nBatt, low ? C_WARN : C_BATT, 0);
  } else {
    for (lv_obj_t* o : {nSolar, nGrid, nBatt}) lv_label_set_text(o, dash);
    lv_label_set_text(nHouse, bigDash);
    lv_label_set_text(nGridSub, "");
    lv_label_set_text(nBattSub, "");
    lv_label_set_text(nBattLbl, "Battery");
  }

  // ---- Battery
  if (haveSoc) {
    const int soc = (int)lroundf(l.soc);
    lv_label_set_text_fmt(bPct, "%d", soc);
    const int inner = BATT_W - 2 * (BATT_BORDER + BATT_PAD);
    lv_obj_set_width(bFill, soc <= 0 ? 0 : LV_MAX(6, inner * LV_MIN(soc, 100) / 100));
    lv_obj_set_style_bg_color(bFill, low ? C_WARN : C_BATT, 0);
    lv_obj_set_style_text_color(bPct, low ? C_WARN : C_BATT, 0);
    fmtKw(b, sizeof b, l.battery);
    char eta[40] = "";
    if (l.battery > IDLE_KW) {
      lv_label_set_text_fmt(bState, LV_SYMBOL_CHARGE " Charging %s kW", b);
      char d[24];
      if (duration(d, sizeof d, !std::isnan(l.roomKWh) ? l.roomKWh / l.battery : NAN)) snprintf(eta, sizeof eta, "Full in %s", d);
    } else if (l.battery < -IDLE_KW) {
      lv_label_set_text_fmt(bState, "Discharging %s kW", b);
      char d[24];
      if (duration(d, sizeof d, !std::isnan(l.availKWh) ? l.availKWh / -l.battery : NAN)) snprintf(eta, sizeof eta, "Lasts ~%s", d);
    } else {
      lv_label_set_text(bState, "Idle");
      if (!std::isnan(l.capacityKWh)) snprintf(eta, sizeof eta, "%.1f of %.1f kWh", l.soc / 100 * l.capacityKWh, l.capacityKWh);
    }
    lv_label_set_text(bEta, eta);
    lv_obj_set_style_text_color(bState, low ? C_WARN : C_BATT, 0);
  } else {
    lv_label_set_text(bPct, bigDash);
    lv_obj_set_width(bFill, 0);
    lv_label_set_text(bState, "");
    lv_label_set_text(bEta, "");
  }

  // ---- Solar
  if (!std::isnan(l.solarToday)) lv_label_set_text_fmt(sToday, "%.1f", l.solarToday);
  else lv_label_set_text(sToday, bigDash);
  if (fc.ok && !std::isnan(fc.today)) lv_label_set_text_fmt(sTarget, "of ~%.0f kWh %s", fc.today, fc.fromWeather ? "forecast" : "(7-day average)");
  else lv_label_set_text(sTarget, "");
  if (fc.ok && !std::isnan(fc.tomorrow)) lv_label_set_text_fmt(sTmrw, "~%.0f kWh", fc.tomorrow);
  else lv_label_set_text(sTmrw, dash);

  // ---- Money and Today (from today's running totals)
  const DayRecord d = data::today();
  if (d.date) {
    lv_label_set_text_fmt(mSpent, "$%.2f", d.spent);
    lv_label_set_text_fmt(mEarned, "$%.2f", d.earned);
    lv_label_set_text_fmt(tSolar, "%.1f kWh", d.solar);
    lv_label_set_text_fmt(tHouse, "%.1f kWh", d.house);
    lv_label_set_text_fmt(tImp, "%.1f kWh", d.imp);
    lv_label_set_text_fmt(tExp, "%.1f kWh", d.exp);
    lv_label_set_text_fmt(tSelf, "%d%%", d.house > 0.05f ? (int)lroundf(fmaxf(0, 1 - d.imp / d.house) * 100) : 0);
  } else {
    for (lv_obj_t* o : {mSpent, mEarned, tSolar, tHouse, tImp, tExp, tSelf}) lv_label_set_text(o, dash);
  }
  {
    const Tariff& tf = s.tariff;
    const TouNow tou = touNow(tf);
    char pr[12];
    if (tou.on) {
      priceText(pr, sizeof pr, tou.price);
      int hh, mm;
      char until[16] = "";
      if (sscanf(tou.until, "%d:%d", &hh, &mm) == 2) time12(until, sizeof until, hh * 60 + mm);
      lv_label_set_text_fmt(mBuy, "%s #%s %s# until %s", periodName(tf, tou.idx), hx(tou.free ? C_GOOD : tou.top ? C_WARN : C_INK), pr, until);
    } else {
      priceText(pr, sizeof pr, tf.buy);
      lv_label_set_text_fmt(mBuy, "%s all day", pr);
    }
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    const int m = tm.tm_hour * 60 + tm.tm_min;
    priceText(pr, sizeof pr, sellAt(tf, m, tm.tm_wday));
    if (!pr[0] || !strcmp(pr, "free")) snprintf(pr, sizeof pr, "0c");
    if (tf.sellTimed && tm.tm_year >= 124) {
      const bool in = inWindow(tf.sellStart, tf.sellEnd, tf.sellDays, m, tm.tm_wday);
      char at[16];
      time12(at, sizeof at, in ? tf.sellEnd : tf.sellStart);
      lv_label_set_text_fmt(mSell, "#%s %s# %s %s", hx(in ? C_GOOD : C_MUTED), pr, in ? "until" : "- paid from", at);
    } else {
      lv_label_set_text_fmt(mSell, "%s", pr);
    }
  }

  // ---- Settings
  const std::string addr = net::address();
  lv_label_set_text(xAddr, addr.length() ? addr.c_str() : dash);
  lv_label_set_text_fmt(xWifi, "Wi-Fi: %s%s", net::connected() ? net::ssidInUse().c_str() : net::apMode() ? "not connected" : "connecting...",
                        fresh && l.remote ? "  -  via Tailscale" : "");
  lv_label_set_text_fmt(xPal, "Colours: %s", PALETTE_NAMES[s.palette % 3]);
  lv_label_set_text_fmt(xTheme, "Theme: %s", THEME_NAMES[s.theme % 3]);
  const ota::Status os = ota::status();
  if (os.state == ota::State::Downloading || os.state == ota::State::Writing) {
    lv_label_set_text_fmt(xFw, "Installing update... %d%%", os.percent);
  } else if (os.state == ota::State::Done) {
    lv_label_set_text(xFw, "Update installed - restarting");
  } else if (ota::updateAvailable()) {
    if (installAskedAt && millis() - installAskedAt < 8000) lv_label_set_text_fmt(xFw, "#%s Tap again to install %s#", hx(C_GOOD), os.latest.c_str());
    else lv_label_set_text_fmt(xFw, "#%s %s available - tap#", hx(C_GOOD), os.latest.c_str());
  } else if (os.state == ota::State::Checking) {
    lv_label_set_text(xFw, "Checking for updates...");
  } else {
    lv_label_set_text(xFw, "GridCheck " EH_VERSION);
  }
  lv_label_set_recolor(xFw, true);
}

}  // namespace ui
