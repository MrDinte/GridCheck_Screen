// Wi-Fi, saved settings and the phone settings page.
//
// First start (or no Wi-Fi saved): the board opens a hotspot called
// "GridCheck-Setup". Join it from a phone and the setup page opens.
// Afterwards the same page is at the board's IP address, shown on screen.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

// Electricity prices, in cents (as on the bill), with time of use: named
// periods such as Off-peak, Shoulder, Peak or a free window, each with its own
// times, days and import price. Any time no period covers uses `buy`.
enum TouDays : uint8_t { TOU_EVERY_DAY = 0, TOU_WEEKDAYS = 1, TOU_WEEKENDS = 2 };
static constexpr int TOU_MAX = 6;
struct TouPeriod {
  bool used = false;
  char name[16] = "";           // e.g. "Off-peak", "Shoulder", "Peak", "Free"
  uint16_t start = 0, end = 0;  // minutes after midnight; may wrap past midnight
  uint8_t days = TOU_EVERY_DAY;
  float price = 0;              // c/kWh
};
struct Tariff {
  bool set = false;             // false: example prices, never entered by the user
  float buy = 30, sell = 5;     // c/kWh; `buy` applies when no period does
  float supply = 0;             // c/day
  char baseName[16] = "";       // name for "all other times", e.g. "Shoulder" ("" = Standard)
  TouPeriod p[TOU_MAX];
  // feed-in paid only at set times (e.g. 16:00 to 21:00): `sell` then, `sellOther` the rest of the time.
  // Added in 0.7.9 at the end, so settings saved by older versions still load (see TariffV1).
  bool sellTimed = false;
  uint8_t sellDays = TOU_EVERY_DAY;
  uint16_t sellStart = 16 * 60, sellEnd = 21 * 60;
  float sellOther = 0;
  // a daily limit on the kWh paid at `sell` (e.g. the first 10 kWh); 0 = no limit
  float sellCapKWh = 0, sellAfterCap = 0;
  bool hasPeriods() const {
    for (const auto& x : p)
      if (x.used) return true;
    return false;
  }
};
/** The prices as saved by 0.7.8 and earlier: Tariff without the feed-in times. */
struct TariffV1 {
  bool set;
  float buy, sell, supply;
  char baseName[16];
  TouPeriod p[TOU_MAX];
};
/** Is minute m of weekday wday inside start..end (may wrap past midnight) on those days? */
bool inWindow(uint16_t start, uint16_t end, uint8_t days, int m, int wday);
/** Feed-in price (c/kWh) at a given minute of the day and day of the week. */
float sellAt(const Tariff& t, int minuteOfDay, int wday);
/** Which period applies (index into p[]), or -1 for the normal `buy` price. wday: 0 = Sunday. */
int periodAt(const Tariff& t, int minuteOfDay, int wday);
/** Import price (c/kWh) at a given minute of the day and day of the week. */
float priceAt(const Tariff& t, int minuteOfDay, int wday);
/** Name of a period index from periodAt ("Standard" for -1). */
const char* periodName(const Tariff& t, int index);
/** Minutes from (minuteOfDay, wday) until the price period changes (max 7 days). */
int minutesUntilChange(const Tariff& t, int minuteOfDay, int wday);
/** "" if the periods make sense, otherwise what's wrong (same start and end, or two periods overlap). */
std::string tariffProblem(const Tariff& t);

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
  std::string supplier;    // who the grid power comes from, e.g. "AGL" ("" = not set)
  float lat = NAN, lon = NAN, kwp = 0;  // optional, for the Open-Meteo solar forecast
  // screen
  uint8_t palette = 0;          // 0 GridCheck (pink), 1 Classic, 2 Ocean
  uint8_t theme = 0;            // 0 dark, 1 light, 2 auto (light by day, dark during the night hours below)
  uint8_t dimAfterMin = 5;      // dim after this many minutes without a touch; 0 = always on
  uint8_t dimLevel = 10;        // brightness when dimmed, %; 0 = backlight off
  bool nightDim = true;         // also dim overnight
  uint16_t nightStart = 20 * 60, nightEnd = 7 * 60;  // minutes after midnight
  std::string updateUrl;        // where to look for firmware updates ("" = GridCheck's own site)
  // Tailscale (round screen only): read the inverter through a home device when away
  bool tsOn = false;
  std::string tsKey;            // auth key (never shown back on the phone page)
  std::string tsHome;           // the home device's Tailscale name or 100.x address
};

/** Electricity retailers offered on screen and on the phone page (any name can be typed on the phone). */
inline const char* const SUPPLIERS[] = {"1st Energy", "ActewAGL", "AGL", "Alinta Energy", "Amber Electric", "Aurora Energy", "Diamond Energy",
                                        "Dodo", "Energy Locals", "EnergyAustralia", "ENGIE", "Ergon Energy", "GloBird Energy", "Lumo Energy",
                                        "Momentum Energy", "Nectr", "Origin Energy", "OVO Energy", "Powershop", "ReAmped Energy", "Red Energy",
                                        "Simply Energy", "Sumo", "Synergy", "Tango Energy"};
inline constexpr int SUPPLIER_COUNT = sizeof SUPPLIERS / sizeof SUPPLIERS[0];
static constexpr size_t SUPPLIER_MAX = 31;

/** Palette names, index = Settings::palette. */
inline const char* const PALETTE_NAMES[3] = {"GridCheck", "Classic", "Ocean"};
/** Theme names, index = Settings::theme. */
inline const char* const THEME_NAMES[3] = {"Dark", "Light", "Auto"};
/** Should the screen be light right now? (Auto: light outside the night hours.) */
bool themeIsLight(const Settings& s);
/** Timeout choices offered on screen and on the phone page (minutes; 0 = always on). */
inline const uint8_t DIM_CHOICES[5] = {0, 1, 5, 15, 30};

namespace net {
void begin();
/** A copy of the current settings (safe from any task). */
Settings settings();
/** Bumped every time settings are saved, so the inverter task can reconnect. */
uint32_t settingsVersion();
bool apMode();             // setup hotspot is running
/** "Change Wi-Fi" on the screen: open the setup hotspot now (it closes after 10 minutes,
 *  on closeSetupHotspot(), or when new Wi-Fi is saved and the screen restarts). */
void openSetupHotspot();
void closeSetupHotspot();
/** The hotspot was opened from the screen and is still meant to be open. */
bool setupHotspotByHand();
bool connected();          // joined home Wi-Fi
std::string address();     // what to type on a phone
std::string ssidInUse();
/** Wipe every saved setting (Wi-Fi, inverter, PIN) and restart. */
void factoryReset();
/** Restart the screen (settings and history are kept). */
void restart();
/** Change and save one screen setting from the on-screen buttons. */
void setPalette(uint8_t palette);
void setTheme(uint8_t theme);
void setDimAfter(uint8_t minutes);
void setBrightness(uint8_t percent);
/** Wi-Fi signal in dBm (e.g. -58), or 0 when not on Wi-Fi. */
int wifiRssi();
/** Save prices and the grid supplier changed on the screen (period names and times are kept). */
void setPrices(const Tariff& tariff, const std::string& supplier);
}  // namespace net
