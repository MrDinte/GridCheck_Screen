#include "data.h"

#include <ctime>
#include <mutex>
#include <vector>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"

static const char* TAG = "data";

// ---- shared maths (same as the PC version) ----------------------------------
void allocateFlows(float solar, float house, float grid, float battery, float f[FLOW_COUNT]) {
  float s = fmaxf(0, solar), b = fmaxf(0, -battery), g = fmaxf(0, grid);
  const float needHouse = fmaxf(0, house), needBatt = fmaxf(0, battery), needExport = fmaxf(0, -grid);
  f[SOLAR_HOUSE] = fminf(s, needHouse); s -= f[SOLAR_HOUSE];
  f[SOLAR_BATT] = fminf(s, needBatt); s -= f[SOLAR_BATT];
  f[SOLAR_GRID] = fminf(s, needExport); s -= f[SOLAR_GRID];
  f[BATT_HOUSE] = fminf(b, needHouse - f[SOLAR_HOUSE]); b -= f[BATT_HOUSE];
  f[BATT_GRID] = fminf(b, needExport - f[SOLAR_GRID]); b -= f[BATT_GRID];
  f[GRID_HOUSE] = fminf(g, needHouse - f[SOLAR_HOUSE] - f[BATT_HOUSE]); g -= f[GRID_HOUSE];
  f[GRID_BATT] = fminf(g, needBatt - f[SOLAR_BATT]);
  for (int i = 0; i < FLOW_COUNT; i++) f[i] = fmaxf(0, f[i]);
}

namespace data {

static constexpr int MINUTES = 24 * 60;
static constexpr size_t KEEP_DAYS = 400;
static constexpr uint32_t MAGIC_DAYS = 0x45484431;  // "EHD1"
static constexpr uint32_t MAGIC_MIN = 0x45484D31;   // "EHM1"

static std::mutex lock;
static MinuteSample* ring = nullptr;  // MINUTES entries, in PSRAM
static int ringHead = 0, ringCount = 0;
static std::vector<DayRecord> past;    // completed days, oldest first
static DayRecord cur;                  // today so far
static uint32_t ver = 1;
static bool mounted = false;
static uint32_t lastSaveMs = 0;
static Forecast fc;

// minute averaging and energy integration
static uint32_t accMinute = 0;
static int accN = 0;
static float accS = 0, accH = 0, accG = 0, accB = 0, accSoc = 0;
static uint32_t lastMs = 0;
static Live last;

static uint32_t dateKey(const struct tm& t) { return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }

// ---- files -----------------------------------------------------------------
static void save() {
  if (!mounted) return;
  FILE* f = fopen("/data/days.bin", "wb");
  if (f) {
    const uint32_t hdr[2] = {MAGIC_DAYS, (uint32_t)past.size()};
    fwrite(hdr, sizeof hdr, 1, f);
    if (!past.empty()) fwrite(past.data(), sizeof(DayRecord), past.size(), f);
    fwrite(&cur, sizeof cur, 1, f);
    fclose(f);
  }
  f = fopen("/data/min.bin", "wb");
  if (f) {
    const uint32_t hdr[3] = {MAGIC_MIN, (uint32_t)ringHead, (uint32_t)ringCount};
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(ring, sizeof(MinuteSample), MINUTES, f);
    fclose(f);
  }
  lastSaveMs = millis();
}

static void load() {
  FILE* f = fopen("/data/days.bin", "rb");
  if (f) {
    uint32_t hdr[2];
    if (fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == MAGIC_DAYS && hdr[1] <= KEEP_DAYS) {
      past.resize(hdr[1]);
      if (hdr[1] && fread(past.data(), sizeof(DayRecord), hdr[1], f) != hdr[1]) past.clear();
      if (fread(&cur, sizeof cur, 1, f) != 1) cur = DayRecord();
    }
    fclose(f);
  }
  f = fopen("/data/min.bin", "rb");
  if (f) {
    uint32_t hdr[3];
    if (fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == MAGIC_MIN && hdr[1] < MINUTES && hdr[2] <= MINUTES &&
        fread(ring, sizeof(MinuteSample), MINUTES, f) == MINUTES) {
      ringHead = hdr[1];
      ringCount = hdr[2];
    }
    fclose(f);
  }
  ESP_LOGI(TAG, "history: %u past days, %d minute samples", (unsigned)past.size(), ringCount);
}

// ---- forecast (optional, needs internet) --------------------------------------
static bool parseTwo(const std::string& body, const char* key, float& a, float& b) {
  const size_t k = body.find(key);
  if (k == std::string::npos) return false;
  const size_t p = body.find('[', k);
  if (p == std::string::npos) return false;
  char* end = nullptr;
  a = strtof(body.c_str() + p + 1, &end);
  if (!end || *end != ',') return false;
  b = strtof(end + 1, &end);
  return end != nullptr;
}

static void forecastTask(void*) {
  for (;;) {
    const Settings s = net::settings();
    bool fetched = false;
    if (net::connected() && !std::isnan(s.lat) && !std::isnan(s.lon) && s.kwp > 0) {
      char url[220];
      snprintf(url, sizeof url,
               "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&daily=shortwave_radiation_sum&timezone=auto&forecast_days=2",
               s.lat, s.lon);
      esp_http_client_config_t cfg = {};
      cfg.url = url;
      cfg.crt_bundle_attach = esp_crt_bundle_attach;
      cfg.timeout_ms = 10000;
      esp_http_client_handle_t c = esp_http_client_init(&cfg);
      if (c && esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        std::string body;
        char buf[256];
        int n;
        while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0 && body.size() < 4096) body.append(buf, n);
        float mj0, mj1;
        if (esp_http_client_get_status_code(c) == 200 && parseTwo(body, "\"shortwave_radiation_sum\"", mj0, mj1)) {
          // MJ/m2 of sunshine -> kWh for this system (rough: 85% after losses)
          std::lock_guard<std::mutex> g(lock);
          fc.ok = true;
          fc.fromWeather = true;
          fc.today = mj0 / 3.6f * s.kwp * 0.85f;
          fc.tomorrow = mj1 / 3.6f * s.kwp * 0.85f;
          fetched = true;
          ESP_LOGI(TAG, "forecast: today %.1f kWh, tomorrow %.1f kWh", fc.today, fc.tomorrow);
        } else {
          ESP_LOGW(TAG, "forecast: unexpected reply (HTTP %d)", esp_http_client_get_status_code(c));
        }
      } else {
        ESP_LOGW(TAG, "forecast: couldn't reach Open-Meteo");
      }
      if (c) esp_http_client_cleanup(c);
    }
    if (!fetched && (std::isnan(s.lat) || s.kwp <= 0)) {
      std::lock_guard<std::mutex> g(lock);
      fc = Forecast();
    }
    vTaskDelay(pdMS_TO_TICKS(fetched ? 3600 * 1000 : 10 * 60 * 1000));
  }
}

// ---- public ----------------------------------------------------------------
void begin() {
  ring = (MinuteSample*)heap_caps_calloc(MINUTES, sizeof(MinuteSample), MALLOC_CAP_SPIRAM);
  past.reserve(KEEP_DAYS);
  esp_vfs_fat_mount_config_t mc = {};
  mc.format_if_mount_failed = true;
  mc.max_files = 4;
  mc.allocation_unit_size = 4096;
  static wl_handle_t wl = WL_INVALID_HANDLE;
  const esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/data", "storage", &mc, &wl);
  mounted = e == ESP_OK;
  if (mounted) load();
  else ESP_LOGE(TAG, "storage not available (%s): history won't survive a restart", esp_err_to_name(e));
  lastSaveMs = millis();
  xTaskCreate(forecastTask, "forecast", 8192, nullptr, 1, nullptr);
}

void addReading(const Live& l) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return;  // clock not set yet: can't tell which day it is
  struct tm t;
  localtime_r(&now, &t);
  const uint32_t key = dateKey(t);
  const uint32_t ms = millis();
  const Settings s = net::settings();

  std::lock_guard<std::mutex> g(lock);
  if (cur.date != key) {
    if (cur.date) {
      past.push_back(cur);
      if (past.size() > KEEP_DAYS) past.erase(past.begin());
    }
    cur = DayRecord();
    cur.date = key;
    ver++;
  }

  // Integrate energy since the last reading (skip gaps longer than 5 minutes).
  if (lastMs && ms - lastMs < 5 * 60 * 1000) {
    const float h = (ms - lastMs) / 3600000.0f;
    cur.solar += fmaxf(0, last.solar) * h;
    cur.house += fmaxf(0, last.house) * h;
    cur.imp += fmaxf(0, last.grid) * h;
    cur.exp += fmaxf(0, -last.grid) * h;
    cur.chg += fmaxf(0, last.battery) * h;
    cur.dis += fmaxf(0, -last.battery) * h;
    float f[FLOW_COUNT];
    allocateFlows(last.solar, last.house, last.grid, last.battery, f);
    for (int i = 0; i < FLOW_COUNT; i++) cur.flows[i] += f[i] * h;
    cur.spent += fmaxf(0, last.grid) * h * priceAt(s.tariff, t.tm_hour * 60 + t.tm_min, t.tm_wday) / 100.0f;
    cur.earned += fmaxf(0, -last.grid) * h * s.tariff.sell / 100.0f;
  }
  lastMs = ms;
  last = l;

  // Average into one sample per minute.
  const uint32_t minute = (uint32_t)now / 60 * 60;
  if (accN && accMinute != minute) {
    MinuteSample& m = ring[ringHead];
    m = {accMinute, accS / accN, accH / accN, accG / accN, accB / accN, accSoc / accN};
    ringHead = (ringHead + 1) % MINUTES;
    if (ringCount < MINUTES) ringCount++;
    accN = 0;
    accS = accH = accG = accB = accSoc = 0;
    ver++;
  }
  accMinute = minute;
  accS += l.solar;
  accH += l.house;
  accG += l.grid;
  accB += l.battery;
  accSoc += std::isnan(l.soc) ? 0 : l.soc;
  accN++;

  if (ms - lastSaveMs > 10 * 60 * 1000) save();
}

int minutes(MinuteSample* out, int max) {
  std::lock_guard<std::mutex> g(lock);
  const uint32_t cutoff = (uint32_t)time(nullptr) - 24 * 3600;
  int n = 0;
  for (int i = 0; i < ringCount && n < max; i++) {
    const MinuteSample& m = ring[(ringHead - ringCount + i + MINUTES) % MINUTES];
    if (m.t >= cutoff) out[n++] = m;
  }
  return n;
}

void days(DayRecord* out, int n) {
  const time_t now = time(nullptr);
  std::lock_guard<std::mutex> g(lock);
  for (int i = 0; i < n; i++) {
    out[i] = DayRecord();
    if (now < 1700000000) continue;
    struct tm t;
    localtime_r(&now, &t);
    t.tm_mday -= (n - 1 - i);
    t.tm_hour = 12;
    mktime(&t);
    const uint32_t key = dateKey(t);
    if (cur.date == key) out[i] = cur;
    else
      for (const auto& d : past)
        if (d.date == key) out[i] = d;
  }
}

DayRecord today() {
  std::lock_guard<std::mutex> g(lock);
  return cur;
}

Forecast forecast() {
  std::lock_guard<std::mutex> g(lock);
  return fc;
}

uint32_t version() { return ver; }

}  // namespace data
