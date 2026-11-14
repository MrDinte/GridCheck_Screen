// Waveshare ESP32-S3-Touch-LCD-7B: 1024x600 RGB panel, GT911 touch and the
// on-board I2C "IO extension" chip (backlight, LCD power, touch reset, SD select).
#pragma once

#include <cstdint>

#include "screen_size.h"

namespace board {

/** Power up the panel and touch, start the RGB driver. Returns false if the panel failed. */
bool begin();

/** Copy a block of RGB565 pixels to the screen (used by LVGL). */
void draw(int x1, int y1, int x2, int y2, const void* pixels);

/** Read one touch point. Returns false when the screen isn't being touched. */
bool touch(uint16_t* x, uint16_t* y);

/** Backlight brightness 0-100 %. 0 switches it off. Safe to call from any task. */
void setBrightness(uint8_t percent);

bool expanderFound();
bool touchFound();

}  // namespace board
