// Live readings shared between the inverter task and the screen.
#pragma once

#include "compat.h"

struct Live {
  bool ok = false;           // last read succeeded
  bool demo = false;         // simulated data
  uint32_t updatedMs = 0;    // millis() of the last good read
  float solar = 0;           // kW
  float grid = 0;            // kW, + import / - export
  float battery = 0;         // kW, + charging / - discharging
  float house = 0;           // kW
  float soc = NAN;           // %
  float solarToday = NAN;    // kWh
  float solarYesterday = NAN;
  float houseToday = NAN;
  // battery details (optional registers)
  float capacityKWh = NAN;   // rated capacity
  float availKWh = NAN;      // energy available to discharge (above reserve)
  float roomKWh = NAN;       // room left to charge
  float reserve = NAN;       // discharge cut-off %
  float soh = NAN;           // state of health %
  float tempC = NAN;         // average cell temperature
  bool remote = false;       // read through Tailscale (round screen away from home)
  char error[120] = "";
};

/** Thread-safe copy of the latest readings. */
Live liveGet();
void liveSet(const Live& v);
