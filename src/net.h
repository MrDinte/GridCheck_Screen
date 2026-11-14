// Wi-Fi, saved settings and the phone settings page.
//
// First start (or no Wi-Fi saved): the board opens a hotspot called
// "GridCheck-Setup". Join it from a phone and the setup page opens.
// Afterwards the same page is at the board's IP address, shown on screen.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

// Electricity prices, in cents (as on the bill). Windows give a different
// import price for part of each day, e.g. a free 11:00-14:00 window (price 0).
struct TariffWindow {
  bool used = false;
  uint16_t start = 0, end = 0;  // minutes after midnight; may wrap past midnight
  float price = 0;              // c/kWh
};
struct Tariff {
  bool set = false;             // false: example prices, never entered by the user
  float buy = 30, sell = 5;     // c/kWh
  float supply = 0;             // c/day
  TariffWindow win[4];
};
/** Import price (c/kWh) at a given minute of the day. */
float priceAt(const Tariff& t, int minuteOfDay);

struct Settings {
  std::string ssid, pass;  // home Wi-Fi
  std::string host;        // inverter IP
  uint16_t port = 502;
  uint8_t plantId = 247;
  bool demo = false;       // show simulated data instead of the inverter
  std::string tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";  // POSIX time zone (default Sydney/Melbourne)
  uint8_t brightness = 80;
  std::string pinSalt, pinHash;  // optional settings PIN (hash only)
  Tariff tariff;
  float lat = NAN, lon = NAN, kwp = 0;  // optional, for the Open-Meteo solar forecast
  // screen
  uint8_t palette = 0;          // 0 GridCheck (pink), 1 Classic, 2 Ocean
  uint8_t dimAfterMin = 5;      // dim after this many minutes without a touch; 0 = always on
  uint8_t dimLevel = 10;        // brightness when dimmed, %; 0 = backlight off
  bool nightDim = true;         // also dim overnight
  uint16_t nightStart = 20 * 60, nightEnd = 7 * 60;  // minutes after midnight
  std::string updateUrl;        // where to look for firmware updates ("" = GridCheck's own site)
};

/** Palette names, index = Settings::palette. */
inline const char* const PALETTE_NAMES[3] = {"GridCheck", "Classic", "Ocean"};
/** Timeout choices offered on screen and on the phone page (minutes; 0 = always on). */
inline const uint8_t DIM_CHOICES[5] = {0, 1, 5, 15, 30};

namespace net {
void begin();
/** A copy of the current settings (safe from any task). */
Settings settings();
/** Bumped every time settings are saved, so the inverter task can reconnect. */
uint32_t settingsVersion();
bool apMode();             // setup hotspot is running
bool connected();          // joined home Wi-Fi
std::string address();     // what to type on a phone
std::string ssidInUse();
/** Wipe every saved setting (Wi-Fi, inverter, PIN) and restart. */
void factoryReset();
/** Change and save one screen setting from the on-screen buttons. */
void setPalette(uint8_t palette);
void setDimAfter(uint8_t minutes);
}  // namespace net
