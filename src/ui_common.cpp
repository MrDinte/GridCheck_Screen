// Look and helpers shared by both screen layouts (ui.cpp + ui_pages.cpp for the 7",
// ui_round.cpp for the 1.8" round knob).
#include "ui_common.h"

#include <cstring>
#include <ctime>

namespace ui {

void applyPalette(int p, bool light) {
  struct Pal { uint32_t bg, tile, ink, muted, faint, accent, solar, grid, batt, good, warn, ok; };
  static const Pal lights[3] = {
      // GridCheck light: white, pink and near-black (same as the web dashboard)
      {0xffffff, 0xf6f1f4, 0x141116, 0x6d6671, 0xe6dde3, 0xff2d87, 0xd3175f, 0x7f7886, 0xe85e9d, 0xd3175f, 0xd97706, 0x6d6671},
      // Classic light
      {0xf4f6f7, 0xe7ecef, 0x15212b, 0x5b6975, 0xcdd6dc, 0x0c8f84, 0xc76a00, 0x6550e8, 0x0c8f84, 0x0c8f84, 0xdf4a2b, 0xb7770b},
      // Ocean light
      {0xf5f9ff, 0xe7effb, 0x0c1a2c, 0x51637b, 0xd0dcee, 0x0a8ec4, 0xa06d00, 0x6b7fa3, 0x0a8ec4, 0x0a8ec4, 0xd4512b, 0x8a6a00},
  };
  static const Pal pals[3] = {
      // GridCheck: pink, black and white
      {0x0b0a0d, 0x18151b, 0xffffff, 0xa39ca8, 0x2c2830, 0xff2d87, 0xff4b97, 0xa39ca8, 0xffa8cd, 0xff4b97, 0xffb020, 0xa39ca8},
      // Classic: navy, amber, violet and teal
      {0x0d151b, 0x16212a, 0xe7edf1, 0x8d9ba7, 0x26343f, 0x2fd4c0, 0xfbbf3c, 0x9b8cff, 0x2fd4c0, 0x2fd4c0, 0xff7c5e, 0xfcd35c},
      // Ocean: deep blue, yellow sun and cyan
      {0x071320, 0x0f2034, 0xeaf3ff, 0x8ea4c2, 0x1c3350, 0x3fd0ff, 0xffd23f, 0x8fa5cc, 0x3fd0ff, 0x3fd0ff, 0xff8a5c, 0xffd23f},
  };
  const int i = p >= 0 && p < 3 ? p : 0;
  const Pal& c = light ? lights[i] : pals[i];
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
// Time-of-use price right now (only when the clock is set and periods exist).
TouNow touNow(const Tariff& t) {
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

void priceText(char* out, size_t n, float c) {
  if (c < 0.005f) snprintf(out, n, "free");
  else if (fabsf(c - roundf(c)) < 0.05f) snprintf(out, n, "%.0fc", c);
  else snprintf(out, n, "%.1fc", c);
}

StatusNow statusNow(const Live& l, const Settings& s, bool brief) {
  StatusNow r;
  auto put = [&](lv_color_t c, const char* fmt, auto... args) {
    snprintf(r.text, sizeof r.text, fmt, args...);
    r.color = c;
  };
  const ota::Status os = ota::status();
  const bool installing = os.state == ota::State::Downloading || os.state == ota::State::Writing;
  const TouNow tou = touNow(s.tariff);
  const bool fresh = l.ok && millis() - l.updatedMs < 30000;
  if (installing) {
    put(C_OK, "Updating firmware %d%%", os.percent);
  } else if (os.state == ota::State::Done) {
    put(C_OK, "%s", brief ? "Restarting..." : "Update installed - restarting");
  } else if (!s.demo && !s.host.length()) {
    put(C_OK, "%s", "Not set up yet");
  } else if (!net::connected() && !s.demo) {
    const std::string why = net::wifiProblem(brief);
    if (!why.empty()) put(C_WARN, "%s", why.c_str());
    else put(C_MUTED, "%s", net::apMode() ? (brief ? "Can't join Wi-Fi" : "Can't join Wi-Fi - fix it from your phone") : "Joining Wi-Fi...");
  } else if (!fresh) {
    put(C_MUTED, "%s", l.updatedMs ? (brief ? "Can't reach inverter" : "Can't reach inverter - swipe for details") : "Connecting to inverter...");
  } else if (tou.on && tou.free) {
    put(C_GOOD, brief ? "Free power until %s" : "Free power until %s - use it now", tou.until);
  } else if (l.grid < -0.3f) {
    put(C_GOOD, "%s", brief ? "Spare solar - use it now" : "Spare solar - run big appliances now");
  } else if (l.solar > l.house + 0.2f && l.battery > 0.1f) {
    put(C_GOOD, "%s", brief ? "Solar charging battery" : "Solar is charging the battery");
  } else if (l.grid > 0.3f && tou.on && tou.top) {
    put(C_WARN, brief ? "%.10s until %s" : "%.10s price until %s - hold off", periodName(s.tariff, tou.idx), tou.until);
  } else if (l.grid > 0.3f) {
    put(C_WARN, "%s", brief ? "On grid power" : "On grid power - hold off big loads");
  } else if (l.battery < -0.1f) {
    put(C_OK, "%s", "Running on battery");
  } else if (l.solar > 0.1f) {
    put(C_GOOD, "%s", "Running on solar");
  } else {
    put(C_OK, "%s", "All quiet");
  }
  return r;
}

}  // namespace ui
