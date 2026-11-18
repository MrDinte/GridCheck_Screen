// Waveshare ESP32-S3-Knob-Touch-LCD-1.8 / 1.8B (the B is the black case): 360x360 round
// ST77916 panel over QSPI, CST816 touch on I2C, a rotary knob and a PWM backlight.
// Pins and the panel's start-up sequence come from Waveshare's ESP-IDF demo for this board.
#include <atomic>
#include <cmath>
#include <cstring>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

namespace board {

// ---- pins (Waveshare demo: user_config.h) ---------------------------------------
static constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
static constexpr int PIN_LCD_CS = 14, PIN_LCD_CLK = 13, PIN_LCD_D0 = 15, PIN_LCD_D1 = 16, PIN_LCD_D2 = 17, PIN_LCD_D3 = 18;
static constexpr gpio_num_t PIN_LCD_RST = GPIO_NUM_21;
static constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_47;
static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
static constexpr gpio_num_t PIN_SDA = GPIO_NUM_11, PIN_SCL = GPIO_NUM_12;
static constexpr gpio_num_t PIN_TOUCH_RST = GPIO_NUM_10;
static constexpr uint8_t TOUCH_ADDR = 0x15;  // CST816
static constexpr gpio_num_t PIN_KNOB_A = GPIO_NUM_8, PIN_KNOB_B = GPIO_NUM_7;

// Most pixels sent at once (main.cpp draws 30 lines at a time; a little spare).
static constexpr size_t MAX_PIXELS = SCREEN_W * 40;

static esp_lcd_panel_io_handle_t io = nullptr;
static SemaphoreHandle_t sent = nullptr;  // one block of pixels has gone out
static uint16_t* dmaBuf = nullptr;        // pixels byte-swapped for the panel
static bool panel_ok = false, touch_ok = false;
static std::atomic<int> turns{0};

bool expanderFound() { return panel_ok; }  // no IO expander here; report whether the panel started
bool touchFound() { return touch_ok; }

// ---- ST77916 panel over QSPI -------------------------------------------------------
// Commands go out as opcode 0x02 + command in bits 8-15 of a 32-bit "command";
// pixels as opcode 0x32 + RAMWR (0x2C), four data lines at once.
struct InitCmd {
  uint8_t cmd, len;
  uint16_t delayMs;
  uint8_t data[14];
};
// The panel's start-up sequence from Waveshare's demo (08_LVGL_Test, not rotated).
static const InitCmd INIT[] = {
    {0xF0, 1, 0, {0x28}},
    {0xF2, 1, 0, {0x28}},
    {0x73, 1, 0, {0xF0}},
    {0x7C, 1, 0, {0xD1}},
    {0x83, 1, 0, {0xE0}},
    {0x84, 1, 0, {0x61}},
    {0xF2, 1, 0, {0x82}},
    {0xF0, 1, 0, {0x00}},
    {0xF0, 1, 0, {0x01}},
    {0xF1, 1, 0, {0x01}},
    {0xB0, 1, 0, {0x56}},
    {0xB1, 1, 0, {0x4D}},
    {0xB2, 1, 0, {0x24}},
    {0xB4, 1, 0, {0x87}},
    {0xB5, 1, 0, {0x44}},
    {0xB6, 1, 0, {0x8B}},
    {0xB7, 1, 0, {0x40}},
    {0xB8, 1, 0, {0x86}},
    {0xBA, 1, 0, {0x00}},
    {0xBB, 1, 0, {0x08}},
    {0xBC, 1, 0, {0x08}},
    {0xBD, 1, 0, {0x00}},
    {0xC0, 1, 0, {0x80}},
    {0xC1, 1, 0, {0x10}},
    {0xC2, 1, 0, {0x37}},
    {0xC3, 1, 0, {0x80}},
    {0xC4, 1, 0, {0x10}},
    {0xC5, 1, 0, {0x37}},
    {0xC6, 1, 0, {0xA9}},
    {0xC7, 1, 0, {0x41}},
    {0xC8, 1, 0, {0x01}},
    {0xC9, 1, 0, {0xA9}},
    {0xCA, 1, 0, {0x41}},
    {0xCB, 1, 0, {0x01}},
    {0xD0, 1, 0, {0x91}},
    {0xD1, 1, 0, {0x68}},
    {0xD2, 1, 0, {0x68}},
    {0xF5, 2, 0, {0x00, 0xA5}},
    {0xDD, 1, 0, {0x4F}},
    {0xDE, 1, 0, {0x4F}},
    {0xF1, 1, 0, {0x10}},
    {0xF0, 1, 0, {0x00}},
    {0xF0, 1, 0, {0x02}},
    {0xE0, 14, 0, {0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}},
    {0xE1, 14, 0, {0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}},
    {0xF0, 1, 0, {0x10}},
    {0xF3, 1, 0, {0x10}},
    {0xE0, 1, 0, {0x07}},
    {0xE1, 1, 0, {0x00}},
    {0xE2, 1, 0, {0x00}},
    {0xE3, 1, 0, {0x00}},
    {0xE4, 1, 0, {0xE0}},
    {0xE5, 1, 0, {0x06}},
    {0xE6, 1, 0, {0x21}},
    {0xE7, 1, 0, {0x01}},
    {0xE8, 1, 0, {0x05}},
    {0xE9, 1, 0, {0x02}},
    {0xEA, 1, 0, {0xDA}},
    {0xEB, 1, 0, {0x00}},
    {0xEC, 1, 0, {0x00}},
    {0xED, 1, 0, {0x0F}},
    {0xEE, 1, 0, {0x00}},
    {0xEF, 1, 0, {0x00}},
    {0xF8, 1, 0, {0x00}},
    {0xF9, 1, 0, {0x00}},
    {0xFA, 1, 0, {0x00}},
    {0xFB, 1, 0, {0x00}},
    {0xFC, 1, 0, {0x00}},
    {0xFD, 1, 0, {0x00}},
    {0xFE, 1, 0, {0x00}},
    {0xFF, 1, 0, {0x00}},
    {0x60, 1, 0, {0x40}},
    {0x61, 1, 0, {0x04}},
    {0x62, 1, 0, {0x00}},
    {0x63, 1, 0, {0x42}},
    {0x64, 1, 0, {0xD9}},
    {0x65, 1, 0, {0x00}},
    {0x66, 1, 0, {0x00}},
    {0x67, 1, 0, {0x00}},
    {0x68, 1, 0, {0x00}},
    {0x69, 1, 0, {0x00}},
    {0x6A, 1, 0, {0x00}},
    {0x6B, 1, 0, {0x00}},
    {0x70, 1, 0, {0x40}},
    {0x71, 1, 0, {0x03}},
    {0x72, 1, 0, {0x00}},
    {0x73, 1, 0, {0x42}},
    {0x74, 1, 0, {0xD8}},
    {0x75, 1, 0, {0x00}},
    {0x76, 1, 0, {0x00}},
    {0x77, 1, 0, {0x00}},
    {0x78, 1, 0, {0x00}},
    {0x79, 1, 0, {0x00}},
    {0x7A, 1, 0, {0x00}},
    {0x7B, 1, 0, {0x00}},
    {0x80, 1, 0, {0x48}},
    {0x81, 1, 0, {0x00}},
    {0x82, 1, 0, {0x06}},
    {0x83, 1, 0, {0x02}},
    {0x84, 1, 0, {0xD6}},
    {0x85, 1, 0, {0x04}},
    {0x86, 1, 0, {0x00}},
    {0x87, 1, 0, {0x00}},
    {0x88, 1, 0, {0x48}},
    {0x89, 1, 0, {0x00}},
    {0x8A, 1, 0, {0x08}},
    {0x8B, 1, 0, {0x02}},
    {0x8C, 1, 0, {0xD8}},
    {0x8D, 1, 0, {0x04}},
    {0x8E, 1, 0, {0x00}},
    {0x8F, 1, 0, {0x00}},
    {0x90, 1, 0, {0x48}},
    {0x91, 1, 0, {0x00}},
    {0x92, 1, 0, {0x0A}},
    {0x93, 1, 0, {0x02}},
    {0x94, 1, 0, {0xDA}},
    {0x95, 1, 0, {0x04}},
    {0x96, 1, 0, {0x00}},
    {0x97, 1, 0, {0x00}},
    {0x98, 1, 0, {0x48}},
    {0x99, 1, 0, {0x00}},
    {0x9A, 1, 0, {0x0C}},
    {0x9B, 1, 0, {0x02}},
    {0x9C, 1, 0, {0xDC}},
    {0x9D, 1, 0, {0x04}},
    {0x9E, 1, 0, {0x00}},
    {0x9F, 1, 0, {0x00}},
    {0xA0, 1, 0, {0x48}},
    {0xA1, 1, 0, {0x00}},
    {0xA2, 1, 0, {0x05}},
    {0xA3, 1, 0, {0x02}},
    {0xA4, 1, 0, {0xD5}},
    {0xA5, 1, 0, {0x04}},
    {0xA6, 1, 0, {0x00}},
    {0xA7, 1, 0, {0x00}},
    {0xA8, 1, 0, {0x48}},
    {0xA9, 1, 0, {0x00}},
    {0xAA, 1, 0, {0x07}},
    {0xAB, 1, 0, {0x02}},
    {0xAC, 1, 0, {0xD7}},
    {0xAD, 1, 0, {0x04}},
    {0xAE, 1, 0, {0x00}},
    {0xAF, 1, 0, {0x00}},
    {0xB0, 1, 0, {0x48}},
    {0xB1, 1, 0, {0x00}},
    {0xB2, 1, 0, {0x09}},
    {0xB3, 1, 0, {0x02}},
    {0xB4, 1, 0, {0xD9}},
    {0xB5, 1, 0, {0x04}},
    {0xB6, 1, 0, {0x00}},
    {0xB7, 1, 0, {0x00}},
    {0xB8, 1, 0, {0x48}},
    {0xB9, 1, 0, {0x00}},
    {0xBA, 1, 0, {0x0B}},
    {0xBB, 1, 0, {0x02}},
    {0xBC, 1, 0, {0xDB}},
    {0xBD, 1, 0, {0x04}},
    {0xBE, 1, 0, {0x00}},
    {0xBF, 1, 0, {0x00}},
    {0xC0, 1, 0, {0x10}},
    {0xC1, 1, 0, {0x47}},
    {0xC2, 1, 0, {0x56}},
    {0xC3, 1, 0, {0x65}},
    {0xC4, 1, 0, {0x74}},
    {0xC5, 1, 0, {0x88}},
    {0xC6, 1, 0, {0x99}},
    {0xC7, 1, 0, {0x01}},
    {0xC8, 1, 0, {0xBB}},
    {0xC9, 1, 0, {0xAA}},
    {0xD0, 1, 0, {0x10}},
    {0xD1, 1, 0, {0x47}},
    {0xD2, 1, 0, {0x56}},
    {0xD3, 1, 0, {0x65}},
    {0xD4, 1, 0, {0x74}},
    {0xD5, 1, 0, {0x88}},
    {0xD6, 1, 0, {0x99}},
    {0xD7, 1, 0, {0x01}},
    {0xD8, 1, 0, {0xBB}},
    {0xD9, 1, 0, {0xAA}},
    {0xF3, 1, 0, {0x01}},
    {0xF0, 1, 0, {0x00}},
    {0x21, 1, 0, {0x00}},
    {0x11, 1, 120, {0x00}},
    {0x29, 1, 0, {0x00}},
    {0x36, 1, 0, {0x00}},
};

static esp_err_t cmd(uint8_t c, const uint8_t* data, size_t len) {
  return esp_lcd_panel_io_tx_param(io, (0x02 << 24) | (c << 8), data, len);
}

static bool IRAM_ATTR colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sent, &woken);
  return woken == pdTRUE;
}

static bool panelBegin() {
  spi_bus_config_t bus = {};
  bus.data0_io_num = PIN_LCD_D0;
  bus.data1_io_num = PIN_LCD_D1;
  bus.sclk_io_num = PIN_LCD_CLK;
  bus.data2_io_num = PIN_LCD_D2;
  bus.data3_io_num = PIN_LCD_D3;
  bus.max_transfer_sz = MAX_PIXELS * sizeof(uint16_t);
  if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t cfg = {};
  cfg.cs_gpio_num = PIN_LCD_CS;
  cfg.dc_gpio_num = -1;
  cfg.spi_mode = 0;
  cfg.pclk_hz = 40 * 1000 * 1000;
  cfg.trans_queue_depth = 10;
  cfg.on_color_trans_done = colorDone;
  cfg.lcd_cmd_bits = 32;
  cfg.lcd_param_bits = 8;
  cfg.flags.quad_mode = 1;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &cfg, &io) != ESP_OK) return false;

  // hardware reset
  gpio_config_t g = {};
  g.pin_bit_mask = 1ULL << PIN_LCD_RST;
  g.mode = GPIO_MODE_OUTPUT;
  gpio_config(&g);
  gpio_set_level(PIN_LCD_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_LCD_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(150));

  const uint8_t madctl = 0x00, colmod = 0x55;  // normal orientation, 16-bit colour
  bool ok = cmd(0x36, &madctl, 1) == ESP_OK && cmd(0x3A, &colmod, 1) == ESP_OK;
  for (const InitCmd& c : INIT) {
    ok = ok && cmd(c.cmd, c.data, c.len) == ESP_OK;
    if (c.delayMs) vTaskDelay(pdMS_TO_TICKS(c.delayMs));
  }
  return ok;
}

void draw(int x1, int y1, int x2, int y2, const void* pixels) {
  if (!panel_ok) return;
  const size_t w = x2 - x1 + 1, h = y2 - y1 + 1, n = w * h;
  if (n > MAX_PIXELS) return;  // main.cpp never sends more
  // LVGL keeps RGB565 little-endian; the panel wants the high byte first.
  const uint16_t* src = (const uint16_t*)pixels;
  for (size_t i = 0; i < n; i++) dmaBuf[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
  const uint8_t ca[4] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
  const uint8_t ra[4] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};
  cmd(0x2A, ca, 4);
  cmd(0x2B, ra, 4);
  esp_lcd_panel_io_tx_color(io, (0x32 << 24) | (0x2C << 8), dmaBuf, n * sizeof(uint16_t));
  xSemaphoreTake(sent, pdMS_TO_TICKS(100));  // dmaBuf is reused next time
}

// ---- backlight (PWM on GPIO 47) ----------------------------------------------------
static void backlightBegin() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_3;
  t.freq_hz = 20000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&t);
  ledc_channel_config_t c = {};
  c.gpio_num = PIN_BACKLIGHT;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = LEDC_CHANNEL_1;
  c.timer_sel = LEDC_TIMER_3;
  c.duty = 0;
  ledc_channel_config(&c);
}

void setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, percent * 255 / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

// ---- CST816 touch ---------------------------------------------------------------------
static bool touchRead(uint8_t reg, uint8_t* buf, size_t len) {
  return i2c_master_write_read_device(I2C_PORT, TOUCH_ADDR, &reg, 1, buf, len, pdMS_TO_TICKS(20)) == ESP_OK;
}

static void touchBegin() {
  i2c_config_t c = {};
  c.mode = I2C_MODE_MASTER;
  c.sda_io_num = PIN_SDA;
  c.scl_io_num = PIN_SCL;
  c.sda_pullup_en = GPIO_PULLUP_ENABLE;
  c.scl_pullup_en = GPIO_PULLUP_ENABLE;
  c.master.clk_speed = 300000;
  if (i2c_param_config(I2C_PORT, &c) != ESP_OK || i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return;
  gpio_config_t g = {};
  g.pin_bit_mask = 1ULL << PIN_TOUCH_RST;
  g.mode = GPIO_MODE_OUTPUT;
  gpio_config(&g);
  gpio_set_level(PIN_TOUCH_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_TOUCH_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(60));
  // normal mode, and stay awake (otherwise it sleeps and stops answering between touches)
  const uint8_t normal[2] = {0x00, 0x00}, awake[2] = {0xFE, 0x01};
  i2c_master_write_to_device(I2C_PORT, TOUCH_ADDR, normal, 2, pdMS_TO_TICKS(20));
  i2c_master_write_to_device(I2C_PORT, TOUCH_ADDR, awake, 2, pdMS_TO_TICKS(20));
  uint8_t id = 0;
  touch_ok = touchRead(0xA7, &id, 1);
  ESP_LOGI(TAG, "touch: %s (chip id 0x%02x)", touch_ok ? "found" : "not answering", id);
}

bool touch(uint16_t* x, uint16_t* y) {
  uint8_t d[7];
  if (!touchRead(0x00, d, sizeof d) || (d[2] & 0x0F) == 0) return false;
  const uint16_t tx = ((d[3] & 0x0F) << 8) | d[4], ty = ((d[5] & 0x0F) << 8) | d[6];
  *x = tx < SCREEN_W ? tx : SCREEN_W - 1;
  *y = ty < SCREEN_H ? ty : SCREEN_H - 1;
  return true;
}

// ---- rotary knob ------------------------------------------------------------------------
// Each click pulls one of the two lines low and back: A for clockwise, B for anticlockwise.
// Checked every 3 ms and counted when the line comes back up, as in Waveshare's demo.
struct Line {
  gpio_num_t pin;
  uint8_t level, count;
  int step;
};
static Line lineA{PIN_KNOB_A, 1, 0, +1}, lineB{PIN_KNOB_B, 1, 0, -1};

static void knobLine(Line& l) {
  const uint8_t now = (uint8_t)gpio_get_level(l.pin);
  if (now == 0) {
    l.count = now != l.level ? 0 : l.count + 1;
  } else if (now != l.level && ++l.count >= 2) {
    l.count = 0;
    turns += l.step;
  } else {
    l.count = 0;
  }
  l.level = now;
}

static void knobTick(void*) {
  knobLine(lineA);
  knobLine(lineB);
}

static void knobBegin() {
  gpio_config_t g = {};
  g.pin_bit_mask = (1ULL << PIN_KNOB_A) | (1ULL << PIN_KNOB_B);
  g.mode = GPIO_MODE_INPUT;
  g.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&g);
  lineA.level = (uint8_t)gpio_get_level(PIN_KNOB_A);
  lineB.level = (uint8_t)gpio_get_level(PIN_KNOB_B);
  esp_timer_create_args_t a = {};
  a.callback = knobTick;
  a.name = "knob";
  esp_timer_handle_t t;
  if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 3000);
}

int knobTurns() { return turns.exchange(0); }

// ---- battery (GPIO 1 = ADC1 channel 0, through a 1:2 divider, as in Waveshare's ADC demo) ----
static adc_oneshot_unit_handle_t adc = nullptr;
static adc_cali_handle_t adcCali = nullptr;
static float battV = NAN;

static void batteryBegin() {
  adc_oneshot_unit_init_cfg_t u = {};
  u.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&u, &adc) != ESP_OK) {
    adc = nullptr;
    return;
  }
  adc_oneshot_chan_cfg_t c = {};
  c.atten = ADC_ATTEN_DB_12;
  c.bitwidth = ADC_BITWIDTH_12;
  adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &c);
  adc_cali_curve_fitting_config_t k = {};
  k.unit_id = ADC_UNIT_1;
  k.atten = ADC_ATTEN_DB_12;
  k.bitwidth = ADC_BITWIDTH_12;
  if (adc_cali_create_scheme_curve_fitting(&k, &adcCali) != ESP_OK) adcCali = nullptr;
}

float batteryVolts() {
  if (!adc) return NAN;
  int raw = 0, mv = 0;
  if (adc_oneshot_read(adc, ADC_CHANNEL_0, &raw) != ESP_OK) return battV;
  if (adcCali) adc_cali_raw_to_voltage(adcCali, raw, &mv);
  else mv = raw * 3300 / 4095;
  const float v = mv * 2 / 1000.0f;  // undo the divider
  battV = std::isnan(battV) ? v : battV * 0.9f + v * 0.1f;  // smooth out the ADC noise
  return battV;
}

// ---- start-up ---------------------------------------------------------------------------
bool begin() {
  backlightBegin();
  setBrightness(0);
  sent = xSemaphoreCreateBinary();
  dmaBuf = (uint16_t*)heap_caps_malloc(MAX_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  panel_ok = dmaBuf && sent && panelBegin();
  ESP_LOGI(TAG, "panel: %s", panel_ok ? "started" : "FAILED");
  touchBegin();
  knobBegin();
  batteryBegin();
  return panel_ok;
}

}  // namespace board
