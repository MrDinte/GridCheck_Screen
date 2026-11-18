#include "board.h"

#include <cmath>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "board";

namespace board {

// ---- pins ------------------------------------------------------------------
static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
static constexpr gpio_num_t PIN_SDA = GPIO_NUM_8;
static constexpr gpio_num_t PIN_SCL = GPIO_NUM_9;
static constexpr gpio_num_t PIN_TOUCH_INT = GPIO_NUM_4;

// IO extension (Waveshare I2C expander at 0x24). Confirmed on a real 7B:
// the backlight comes on with this register layout.
static constexpr uint8_t EXP_ADDR = 0x24;
static constexpr uint8_t REG_MODE = 0x02;    // 1 = output
static constexpr uint8_t REG_OUTPUT = 0x03;  // output levels
static constexpr uint8_t REG_PWM = 0x05;     // backlight duty 0-255
static constexpr uint8_t EXIO_TOUCH_RST = 1;
static constexpr uint8_t EXIO_BACKLIGHT = 2;
static constexpr uint8_t EXIO_SD_CS = 4;
static constexpr uint8_t EXIO_USB_SEL = 5;  // low = USB, high = CAN
static constexpr uint8_t EXIO_LCD_VDD = 6;

static constexpr uint8_t GT911_ADDR_A = 0x5D, GT911_ADDR_B = 0x14;

static esp_lcd_panel_handle_t panel = nullptr;
static uint8_t outputs = 0xFF;
static bool expander_ok = false, touch_ok = false;
static uint8_t gtAddr = GT911_ADDR_A;

bool expanderFound() { return expander_ok; }
int knobTurns() { return 0; }  // no knob on the 7"
float batteryVolts() { return NAN; }  // no battery on the 7"
bool touchFound() { return touch_ok; }

// ---- I2C helpers -------------------------------------------------------------
static bool i2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  return i2c_master_write_to_device(I2C_PORT, addr, data, len, pdMS_TO_TICKS(50)) == ESP_OK;
}

static bool i2cProbe(uint8_t addr) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  const esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(30));
  i2c_cmd_link_delete(cmd);
  return r == ESP_OK;
}

static bool expWrite(uint8_t reg, uint8_t value) {
  const uint8_t d[2] = {reg, value};
  return i2cWrite(EXP_ADDR, d, 2);
}

static void expPin(uint8_t pin, bool high) {
  if (high) outputs |= (1u << pin);
  else outputs &= ~(1u << pin);
  expWrite(REG_OUTPUT, outputs);
}

void setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint8_t duty = (uint8_t)((percent > 97 ? 97 : percent) * 255 / 100);
  expWrite(REG_PWM, duty);
  expPin(EXIO_BACKLIGHT, percent > 0);
}

// ---- GT911 touch -------------------------------------------------------------
static bool gtRead(uint16_t reg, uint8_t* buf, size_t len) {
  const uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
  return i2c_master_write_read_device(I2C_PORT, gtAddr, r, 2, buf, len, pdMS_TO_TICKS(20)) == ESP_OK;
}

static void gtWrite8(uint16_t reg, uint8_t v) {
  const uint8_t d[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, v};
  i2cWrite(gtAddr, d, 3);
}

static uint16_t gtResX = SCREEN_W, gtResY = SCREEN_H;

bool touch(uint16_t* x, uint16_t* y) {
  if (!touch_ok) return false;
  static bool last = false;
  static uint16_t lx = 0, ly = 0;
  uint8_t status = 0;
  if (!gtRead(0x814E, &status, 1)) return false;
  if (!(status & 0x80)) {  // no new data: report the previous state
    if (last) { *x = lx; *y = ly; }
    return last;
  }
  const uint8_t points = status & 0x0F;
  if (points > 0 && points <= 5) {
    uint8_t p[4];
    if (gtRead(0x8150, p, 4)) {
      lx = (uint16_t)(p[0] | p[1] << 8);
      ly = (uint16_t)(p[2] | p[3] << 8);
      // Scale if the touch chip is configured for a different resolution.
      if (gtResX && gtResX != SCREEN_W) lx = (uint16_t)((uint32_t)lx * SCREEN_W / gtResX);
      if (gtResY && gtResY != SCREEN_H) ly = (uint16_t)((uint32_t)ly * SCREEN_H / gtResY);
      if (lx >= SCREEN_W) lx = SCREEN_W - 1;
      if (ly >= SCREEN_H) ly = SCREEN_H - 1;
      last = true;
    }
  } else {
    last = false;
  }
  gtWrite8(0x814E, 0);  // ready for the next report
  *x = lx;
  *y = ly;
  return last;
}

// ---- RGB panel -----------------------------------------------------------------
static bool startPanel() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  // 1024x600 timing used by Espressif for Waveshare's 1024x600 RGB panels.
  cfg.timings.pclk_hz = 21 * 1000 * 1000;
  cfg.timings.h_res = SCREEN_W;
  cfg.timings.v_res = SCREEN_H;
  cfg.timings.hsync_pulse_width = 24;
  cfg.timings.hsync_back_porch = 160;
  cfg.timings.hsync_front_porch = 160;
  cfg.timings.vsync_pulse_width = 2;
  cfg.timings.vsync_back_porch = 23;
  cfg.timings.vsync_front_porch = 12;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  // Bounce buffers: the LCD DMA reads from small buffers in internal RAM that
  // the CPU refills from the PSRAM frame buffer, so PSRAM hiccups don't show.
  cfg.bounce_buffer_size_px = SCREEN_W * 10;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = 46;
  cfg.vsync_gpio_num = 3;
  cfg.de_gpio_num = 5;
  cfg.pclk_gpio_num = 7;
  cfg.disp_gpio_num = -1;
  // B3..B7, G2..G7, R3..R7
  const int data[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;

  esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "RGB panel: %s", esp_err_to_name(e));
    return false;
  }
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  return true;
}

void draw(int x1, int y1, int x2, int y2, const void* pixels) {
  if (panel) esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, pixels);
}

// ---- start-up ----------------------------------------------------------------
bool begin() {
  i2c_config_t ic = {};
  ic.mode = I2C_MODE_MASTER;
  ic.sda_io_num = PIN_SDA;
  ic.scl_io_num = PIN_SCL;
  ic.sda_pullup_en = GPIO_PULLUP_ENABLE;
  ic.scl_pullup_en = GPIO_PULLUP_ENABLE;
  ic.master.clk_speed = 400000;
  i2c_param_config(I2C_PORT, &ic);
  i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(20));

  expander_ok = i2cProbe(EXP_ADDR);
  ESP_LOGI(TAG, "IO extension at 0x%02X: %s", EXP_ADDR, expander_ok ? "found" : "NOT FOUND");

  // All expander pins outputs. LCD power on, backlight off for now, touch in
  // reset, SD deselected, USB (not CAN) on the USB port.
  expWrite(REG_MODE, 0xFF);
  outputs = 0xFF;
  outputs &= ~(1u << EXIO_USB_SEL);
  outputs &= ~(1u << EXIO_BACKLIGHT);
  outputs &= ~(1u << EXIO_TOUCH_RST);
  expWrite(REG_OUTPUT, outputs);
  expWrite(REG_PWM, 0);

  // GT911 takes address 0x5D when INT is low as reset is released.
  gpio_set_direction(PIN_TOUCH_INT, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_TOUCH_INT, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  expPin(EXIO_TOUCH_RST, true);
  vTaskDelay(pdMS_TO_TICKS(120));
  gpio_set_direction(PIN_TOUCH_INT, GPIO_MODE_INPUT);
  vTaskDelay(pdMS_TO_TICKS(50));
  if (i2cProbe(GT911_ADDR_A)) gtAddr = GT911_ADDR_A;
  else if (i2cProbe(GT911_ADDR_B)) gtAddr = GT911_ADDR_B;
  touch_ok = i2cProbe(gtAddr);
  if (touch_ok) {
    uint8_t res[4];
    if (gtRead(0x8048, res, 4)) {
      gtResX = (uint16_t)(res[0] | res[1] << 8);
      gtResY = (uint16_t)(res[2] | res[3] << 8);
      if (gtResX < 100 || gtResY < 100) gtResX = SCREEN_W, gtResY = SCREEN_H;
    }
  }
  ESP_LOGI(TAG, "touch GT911: %s (0x%02X, %ux%u)", touch_ok ? "found" : "NOT FOUND", gtAddr, gtResX, gtResY);

  const bool ok = startPanel();
  ESP_LOGI(TAG, "RGB panel: %s", ok ? "running" : "FAILED");
  return ok;
}

}  // namespace board
