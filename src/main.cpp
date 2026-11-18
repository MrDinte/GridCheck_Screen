// GridCheck firmware for the Waveshare ESP32-S3-Touch-LCD-7B and the round
// ESP32-S3-Knob-Touch-LCD-1.8 / 1.8B (ESP-IDF 5).
//
// Reads a Sigenergy system over Modbus TCP on your Wi-Fi and shows it on the
// screen. Settings are changed from a phone (see net.h).

#include <mutex>

#include "board.h"
#include "data.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "model.h"
#include "modbus.h"
#include "net.h"
#include "ota.h"
#include "ui.h"
#include "ui_common.h"
#include <ctime>
#ifdef GC_KNOB18
#include "vpn.h"
#endif

static const char* TAG = "gridcheck";

// ---- shared live data ------------------------------------------------------
static std::mutex liveLock;
static Live liveData;

Live liveGet() {
  std::lock_guard<std::mutex> g(liveLock);
  return liveData;
}

void liveSet(const Live& v) {
  std::lock_guard<std::mutex> g(liveLock);
  liveData = v;
}

// ---- inverter task -----------------------------------------------------------
// Runs on its own so a slow or unreachable inverter never freezes the screen.
static void inverterTask(void*) {
  SigenergyReader reader;
  uint32_t seenVersion = 0, failures = 0;
  Live cur;
  for (;;) {
    const Settings s = net::settings();
    if (net::settingsVersion() != seenVersion) {
      seenVersion = net::settingsVersion();
      // Only start again when the inverter settings changed (not for colours, brightness...),
      // so the readings don't blank out when something else is saved.
      static std::string host;
      static uint16_t port = 0;
      static uint8_t plant = 0;
      static bool demo = false, first = true;
      if (first || s.host != host || s.port != port || s.plantId != plant || s.demo != demo) {
        first = false;
        host = s.host;
        port = s.port;
        plant = s.plantId;
        demo = s.demo;
        reader.configure(s.host, s.port, s.plantId);
        cur = Live();
        failures = 0;
        liveSet(cur);
      }
    }
    if (s.demo) {
      demoFill(cur);
      liveSet(cur);
      data::addReading(cur);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (s.host.empty() || !net::connected()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    bool ok = reader.poll(cur);
#ifdef GC_KNOB18
    // Away from home: when the inverter doesn't answer on this network but Tailscale is up,
    // read through the home device (Home Assistant's Modbus proxy) instead. Every 2 minutes,
    // check whether the local network works again (back home).
    static uint32_t lastLanCheck = 0;
    if (!ok && !reader.viaVpn() && s.tsOn && vpn::ready()) {
      ESP_LOGI(TAG, "inverter not answering here: reading through Tailscale");
      reader.useVpn(true);
      lastLanCheck = millis();
      ok = reader.poll(cur);
    } else if (reader.viaVpn() && (!s.tsOn || millis() - lastLanCheck > 120000)) {
      lastLanCheck = millis();
      if (!s.tsOn || lanReachable(s.host, s.port)) {
        ESP_LOGI(TAG, "inverter answers on this network again");
        reader.useVpn(false);
      }
    }
    cur.remote = reader.viaVpn();
#endif
    if (ok) {
      data::addReading(cur);
      if (failures) ESP_LOGI(TAG, "inverter: reading OK");
      failures = 0;
    } else if (failures++ % 10 == 0) {
      ESP_LOGW(TAG, "inverter: %s", cur.error);
    }
    liveSet(cur);
    // Every 3 s when working; back off to 30 s while the inverter isn't answering.
    const uint32_t wait = failures ? std::min<uint32_t>(30000, 3000 * (1 + failures)) : 3000;
    vTaskDelay(pdMS_TO_TICKS(wait));
  }
}

// ---- LVGL ------------------------------------------------------------------------
static constexpr int BUF_LINES = 30;

static void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  board::draw(a->x1, a->y1, a->x2, a->y2, px);
  lv_disp_flush_ready(drv);
}

#ifdef GC_KNOB18
// The round panel takes areas that start on an even pixel and are an even number wide and high.
static void evenArea(lv_disp_drv_t*, lv_area_t* a) {
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}
#endif

// ---- screen dimming -------------------------------------------------------------
static volatile uint32_t lastTouchMs = 0;
static bool dimmed = false;
static bool swallow = false;  // the touch that wakes the screen shouldn't press anything

static void readTouch(lv_indev_drv_t*, lv_indev_data_t* d) {
  uint16_t x, y;
  const bool down = board::touch(&x, &y);
  if (down) lastTouchMs = millis();
  if (down && dimmed) swallow = true;
  if (swallow) {
    if (!down) swallow = false;
    d->state = LV_INDEV_STATE_REL;
    return;
  }
  if (down) {
    d->state = LV_INDEV_STATE_PR;
    d->point.x = x;
    d->point.y = y;
  } else {
    d->state = LV_INDEV_STATE_REL;
  }
}

static void uiTask(void*) {
  lv_init();
  const size_t px = board::SCREEN_W * BUF_LINES;
  // Draw buffers in internal RAM: drawing there is fast and keeps PSRAM free for the screen refresh.
  auto* b1 = (lv_color_t*)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto* b2 = (lv_color_t*)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1) b1 = (lv_color_t*)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!b2) b2 = (lv_color_t*)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  static lv_disp_draw_buf_t buf;
  lv_disp_draw_buf_init(&buf, b1, b2, px);

  static lv_disp_drv_t disp;
  lv_disp_drv_init(&disp);
  disp.hor_res = board::SCREEN_W;
  disp.ver_res = board::SCREEN_H;
  disp.flush_cb = flush;
  disp.draw_buf = &buf;
#ifdef GC_KNOB18
  disp.rounder_cb = evenArea;
#endif
  lv_disp_drv_register(&disp);

  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type = LV_INDEV_TYPE_POINTER;
  indev.read_cb = readTouch;
  lv_indev_drv_register(&indev);

  int palette = net::settings().palette;
  bool light = themeIsLight(net::settings());
  ui::applyPalette(palette, light);
  ui::create();
  lv_refr_now(nullptr);
  lastTouchMs = millis();
  int shownBrightness = -1;

  uint32_t lastUi = 0;
  for (;;) {
    const uint32_t wait = lv_timer_handler();
    // Turning the knob changes page; like a touch, it also wakes a dimmed screen (that first turn only wakes it).
    if (const int turns = board::knobTurns()) {
      if (!dimmed) ui::knob(turns);
      lastTouchMs = millis();
      dimmed = false;
    }
    if (millis() - lastUi > 500) {
      lastUi = millis();
      const Settings s = net::settings();
      const bool wantLight = themeIsLight(s);
      if (s.palette != palette || wantLight != light) {  // colours or theme changed (or Auto switched day/night)
        palette = s.palette;
        light = wantLight;
        ui::applyPalette(palette, light);
        ui::rebuild();
      }
      ui::update();

      // Full brightness after a touch; dim after the timeout, and overnight.
      bool night = false;
      const time_t now = time(nullptr);
      if (s.nightDim && now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        const int m = t.tm_hour * 60 + t.tm_min;
        night = s.nightStart < s.nightEnd ? (m >= s.nightStart && m < s.nightEnd) : (m >= s.nightStart || m < s.nightEnd);
      }
      const uint32_t idleMs = millis() - lastTouchMs;
      const uint32_t awakeMs = (s.dimAfterMin ? s.dimAfterMin : 1) * 60000UL;  // at night a touch wakes it for the timeout (or 1 min)
      dimmed = idleMs > awakeMs && (night || s.dimAfterMin > 0);
      const int want = dimmed ? s.dimLevel : s.brightness;
      if (want != shownBrightness) {
        board::setBrightness((uint8_t)want);
        shownBrightness = want;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(std::max<uint32_t>(5, std::min<uint32_t>(wait, 20))));
  }
}

// ---- entry point -----------------------------------------------------------------
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "GridCheck " EH_VERSION " starting");
  board::begin();
  board::setBrightness(0);
  net::begin();
  data::begin();
  ota::begin();
#ifdef GC_KNOB18
  vpn::begin();
#endif
  board::setBrightness(0);  // net::begin applies the saved brightness; wait for the first frame
  ESP_LOGI(TAG, "free: internal %u KB, PSRAM %u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  xTaskCreatePinnedToCore(uiTask, "ui", 12288, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(inverterTask, "inverter", 6144, nullptr, 2, nullptr, 0);
}
