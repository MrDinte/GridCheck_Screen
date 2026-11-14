// Small helpers shared by the firmware and the PC renderer in sim/.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }
#else
uint32_t millis();
#endif

using std::isnan;

template <class T>
inline T constrain(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

#ifndef PI
#define PI 3.14159265f
#endif
