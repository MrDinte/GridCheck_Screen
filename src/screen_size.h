// Panel size, shared by the board driver and the UI (and the PC renderer in sim/).
#pragma once
namespace board {
#ifdef GC_KNOB18
constexpr int SCREEN_W = 360;  // Waveshare ESP32-S3-Knob-Touch-LCD-1.8: round
constexpr int SCREEN_H = 360;
#else
constexpr int SCREEN_W = 1024;  // Waveshare ESP32-S3-Touch-LCD-7B
constexpr int SCREEN_H = 600;
#endif
}  // namespace board
