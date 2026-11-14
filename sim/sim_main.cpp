// Renders the firmware's screens to raw RGB565 files, using the real ui.cpp.
#include <lvgl.h>
#include <cstdlib>
#include <ctime>
#include "model.h"
#include "net.h"
#include "ui.h"
#include "ui_common.h"
#include "data.h"
#include <vector>

static uint32_t fakeMs = 100000;
uint32_t millis() { return fakeMs; }

static Live L;
Live liveGet() { return L; }
void liveSet(const Live& v) { L = v; }

static Settings S;

// ---- fake history: a sunny-ish month -------------------------------------------
void allocateFlows(float solar, float house, float grid, float battery, float f[FLOW_COUNT]) {
  float s = fmaxf(0, solar), b = fmaxf(0, -battery), g = fmaxf(0, grid);
  const float nh = fmaxf(0, house), nb = fmaxf(0, battery), ne = fmaxf(0, -grid);
  f[SOLAR_HOUSE] = fminf(s, nh); s -= f[SOLAR_HOUSE];
  f[SOLAR_BATT] = fminf(s, nb); s -= f[SOLAR_BATT];
  f[SOLAR_GRID] = fminf(s, ne);
  f[BATT_HOUSE] = fminf(b, nh - f[SOLAR_HOUSE]); b -= f[BATT_HOUSE];
  f[BATT_GRID] = fminf(b, ne - f[SOLAR_GRID]);
  f[GRID_HOUSE] = fminf(g, nh - f[SOLAR_HOUSE] - f[BATT_HOUSE]); g -= f[GRID_HOUSE];
  f[GRID_BATT] = fminf(g, nb - f[SOLAR_BATT]);
}

static std::vector<MinuteSample> mins;
static std::vector<DayRecord> dayv;  // 30 days, oldest first; last = today
static void makeHistory(time_t now) {
  struct tm t; localtime_r(&now, &t);
  float soc = 40;
  for (int d = 29; d >= 0; d--) {
    DayRecord r; struct tm dt = t; dt.tm_mday -= d; dt.tm_hour = 12; mktime(&dt);
    r.date = (dt.tm_year + 1900) * 10000 + (dt.tm_mon + 1) * 100 + dt.tm_mday;
    const float weather = 0.65f + 0.35f * (0.5f + 0.5f * sinf(d * 1.7f));
    const int lastMin = d == 0 ? t.tm_hour * 60 + t.tm_min : 1440;
    for (int m = 0; m < lastMin; m++) {
      const float h = m / 60.0f;
      const float pv = 7.0f * powf(fmaxf(0, sinf(3.14159f * (h - 6) / 13)), 1.3f) * weather;
      const float load = 0.4f + 1.2f * expf(-powf(h - 7.5f, 2) / 1.3f) + 1.9f * expf(-powf(h - 18.5f, 2) / 3.9f) + ((m / 37) % 9 == 0 ? 1.5f : 0);
      float ess = 0; const float sur = pv - load;
      if (sur > 0 && soc < 100) ess = fminf(sur, 5); if (sur < 0 && soc > 10) ess = -fminf(-sur, 5);
      soc = fminf(100, fmaxf(0, soc + ess / 60 / 13.5f * 100));
      const float grid = load + ess - pv, hh = 1 / 60.0f;
      r.solar += pv * hh; r.house += load * hh; r.imp += fmaxf(0, grid) * hh; r.exp += fmaxf(0, -grid) * hh;
      r.chg += fmaxf(0, ess) * hh; r.dis += fmaxf(0, -ess) * hh;
      float f[FLOW_COUNT]; allocateFlows(pv, load, grid, ess, f);
      for (int k = 0; k < FLOW_COUNT; k++) r.flows[k] += f[k] * hh;
      r.spent += fmaxf(0, grid) * hh * 0.325f; r.earned += fmaxf(0, -grid) * hh * 0.06f;
      dt.tm_hour = m / 60; dt.tm_min = m % 60; dt.tm_sec = 0;
      const time_t ts = mktime(&dt);
      if (now - ts < 24 * 3600) mins.push_back({(uint32_t)ts, pv, load, grid, ess, soc});
    }
    dayv.push_back(r);
  }
}
namespace data {
int minutes(MinuteSample* out, int max) { int n = 0; for (auto& m : mins) if (n < max) out[n++] = m; return n; }
void days(DayRecord* out, int n) { for (int i = 0; i < n; i++) out[i] = dayv[30 - n + i]; }
DayRecord today() { return dayv.back(); }
Forecast forecast() { Forecast f; f.ok = true; f.fromWeather = true; f.today = 31.6f; f.tomorrow = 27.9f; return f; }
uint32_t version() { return 1; }
}
namespace net {
Settings settings() { return S; }
uint32_t settingsVersion() { return 1; }
bool apMode() { return std::getenv("SIM_AP") != nullptr; }
bool connected() { return !apMode(); }
std::string address() { return apMode() ? std::string("Join Wi-Fi \"GridCheck-Setup\"") : std::string("http://192.168.1.77/"); }
std::string ssidInUse() { return "HomeWiFi"; }
void factoryReset() {}
void restart() {}
void setPalette(uint8_t p) { S.palette = p; }
void setDimAfter(uint8_t m) { S.dimAfterMin = m; }
void setTheme(uint8_t t) { S.theme = t; }
}

namespace ota {
Status status() {
  Status s;
  if (const char* v = std::getenv("SIM_OTA")) s.latest = v;
  if (std::getenv("SIM_OTA_PCT")) s.state = State::Downloading, s.percent = atoi(std::getenv("SIM_OTA_PCT"));
  return s;
}
bool updateAvailable() { return std::getenv("SIM_OTA") != nullptr; }
void checkNow() {}
bool installOnline(std::string&) { return true; }
}

// SIM_TIME=<epoch seconds> pins the clock for repeatable screenshots.
extern "C" time_t time(time_t* t) {
  static const char* fixed = getenv("SIM_TIME");
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  const time_t v = fixed ? (time_t)atoll(fixed) : ts.tv_sec;
  if (t) *t = v;
  return v;
}

static uint16_t fb[1024 * 600];
static void flush(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* px) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * 1024 + x] = (px++)->full;
  lv_disp_flush_ready(d);
}

int main(int argc, char** argv) {
  setenv("TZ", "AEST-10", 1); tzset();
  lv_init();
  static lv_color_t buf[1024 * 100];
  static lv_disp_draw_buf_t db; lv_disp_draw_buf_init(&db, buf, nullptr, 1024 * 100);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd);
  dd.hor_res = 1024; dd.ver_res = 600; dd.flush_cb = flush; dd.draw_buf = &db; lv_disp_drv_register(&dd);

  S.host = "192.168.1.50"; S.demo = false;
  S.tariff.set = true; S.tariff.buy = 32.5f; S.tariff.sell = 6; S.tariff.supply = 110;
  if (const char* tou = getenv("SIM_TOU")) {
    // SIM_TOU=free|peak|off: one period covering the whole day, plus a dearer one elsewhere in the week
    S.tariff.p[0].used = true; S.tariff.p[0].start = 0; S.tariff.p[0].end = 1439;
    S.tariff.p[1].used = true; S.tariff.p[1].start = 1439; S.tariff.p[1].end = 0; S.tariff.p[1].price = 60; strcpy(S.tariff.p[1].name, "Peak");
    if (!strcmp(tou, "free")) { strcpy(S.tariff.p[0].name, "Free"); S.tariff.p[0].price = 0; }
    if (!strcmp(tou, "peak")) { strcpy(S.tariff.p[0].name, "Peak"); S.tariff.p[0].price = 60; S.tariff.p[1].price = 10; strcpy(S.tariff.p[1].name, "Off-peak"); }
    if (!strcmp(tou, "off")) { strcpy(S.tariff.p[0].name, "Off-peak"); S.tariff.p[0].price = 18.5f; }
    if (!strcmp(tou, "plan")) {  // a typical plan: Off-peak, Free window, weekday Peak, Shoulder at other times
      S.tariff = Tariff(); S.tariff.set = true; S.tariff.buy = 28; S.tariff.sell = 6; S.tariff.supply = 110; strcpy(S.tariff.baseName, "Shoulder");
      auto P = [](TouPeriod& p, const char* n, int a, int b, int d, float c) { p.used = true; strcpy(p.name, n); p.start = a; p.end = b; p.days = d; p.price = c; };
      P(S.tariff.p[0], "Off-peak", 22 * 60, 7 * 60, TOU_EVERY_DAY, 18);
      P(S.tariff.p[1], "Free", 11 * 60, 14 * 60, TOU_EVERY_DAY, 0);
      P(S.tariff.p[2], "Peak", 15 * 60, 21 * 60, TOU_WEEKDAYS, 52);
    }
  }
  makeHistory(time(nullptr));
  const char* scene = argc > 1 ? argv[1] : "day";
  L.ok = true; L.updatedMs = fakeMs;
  if (!strcmp(scene, "day")) { L.solarToday = NAN; L.solar = 3.43f; L.battery = 2.72f; L.grid = 0.01f; L.house = 0.71f; L.soc = 58; L.solarToday = 13.0f; L.solarYesterday = 33.5f; L.capacityKWh = 13.5f; L.availKWh = 6.5f; L.roomKWh = 5.7f; L.reserve = 10; L.soh = 99.2f; L.tempC = 26.1f; }
  if (!strcmp(scene, "night")) { L.solar = 0; L.battery = -1.83f; L.grid = 0.02f; L.house = 1.85f; L.soc = 37; L.solarToday = 31.4f; L.solarYesterday = 33.5f; }
  if (!strcmp(scene, "export")) { L.solar = 6.8f; L.battery = 0; L.grid = -5.9f; L.house = 0.9f; L.soc = 100; L.solarToday = 24.2f; L.solarYesterday = 29.9f; }
  if (!strcmp(scene, "error")) { L = Live(); L.ok = false; L.updatedMs = 1; strcpy(L.error, "the inverter hung up while reading 30005 from unit 247. Is another device (e.g. Home Assistant) connected to it?"); }
  if (!strcmp(scene, "setup")) { L = Live(); S.host = ""; }

  if (getenv("SIM_PALETTE")) S.palette = atoi(getenv("SIM_PALETTE"));
  if (getenv("SIM_LIGHT")) S.theme = 1;
  if (getenv("SIM_PALETTE") || getenv("SIM_LIGHT")) ui::applyPalette(S.palette, S.theme == 1);
  ui::create();
  for (int i = 0; i < 60; i++) { fakeMs += 33; lv_tick_inc(33); lv_timer_handler(); }
  if (argc > 2) {
    const char* pages[] = {"now", "today", "solar", "money", "flow", "battery", "history", "info"};
    for (int i = 0; i < 8; i++) if (!strcmp(argv[2], pages[i])) ui::showPage(i);
    if (!strcmp(argv[2], "history7") || !strcmp(argv[2], "history30") || !strcmp(argv[2], "money30") || !strcmp(argv[2], "flow30")) {
      ui::showPage(!strncmp(argv[2], "history", 7) ? 6 : !strncmp(argv[2], "money", 5) ? 3 : 4);
      if (!strcmp(argv[2], "history7")) ui::setRanges(7, 7, 1);
      if (!strcmp(argv[2], "history30")) ui::setRanges(30, 7, 1);
      if (!strcmp(argv[2], "money30")) ui::setRanges(1, 30, 1);
      if (!strcmp(argv[2], "flow30")) ui::setRanges(1, 7, 30);
    }
  }
  ui::update();
  for (int i = 0; i < 30; i++) { fakeMs += 33; lv_tick_inc(33); lv_timer_handler(); }
  FILE* f = fopen(argc > 3 ? argv[3] : "out.raw", "wb"); fwrite(fb, 2, 1024 * 600, f); fclose(f);
  return 0;
}
