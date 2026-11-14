// History kept on the board: one averaged sample per minute for the last 24 h,
// and one record per day (energy totals, energy flows and costs) for ~13 months.
// Saved to the board's flash ("storage" partition) so it survives power cuts.
// Same maths as the GridCheck web dashboard.
#pragma once

#include "model.h"

struct MinuteSample {
  uint32_t t;  // epoch seconds, start of the minute
  float solar, house, grid, battery, soc;
};

enum Flow { SOLAR_HOUSE, SOLAR_BATT, SOLAR_GRID, BATT_HOUSE, BATT_GRID, GRID_HOUSE, GRID_BATT, FLOW_COUNT };

struct DayRecord {
  uint32_t date = 0;  // YYYYMMDD local; 0 = no data for that day
  float solar = 0, house = 0, imp = 0, exp = 0, chg = 0, dis = 0;  // kWh
  float flows[FLOW_COUNT] = {};                                     // kWh
  float spent = 0, earned = 0;                                      // dollars, excluding the supply charge
};

struct Forecast {
  bool ok = false;
  bool fromWeather = false;  // true: Open-Meteo; false: 7-day average
  float today = NAN, tomorrow = NAN;
};

namespace data {
void begin();
/** Call after every good reading. */
void addReading(const Live& l);
/** Last 24 h of minute samples, oldest first. Returns how many. */
int minutes(MinuteSample* out, int max);
/** The last `n` days ending today, oldest first. Days without data have date = 0. */
void days(DayRecord* out, int n);
/** Today's record so far (date 0 until the clock is set). */
DayRecord today();
Forecast forecast();
/** Changes whenever the stored history changes (so screens know to redraw). */
uint32_t version();
}  // namespace data

/** Split power into source -> destination flows (kW). Same rules as the GridCheck web dashboard. */
void allocateFlows(float solar, float house, float grid, float battery, float out[FLOW_COUNT]);
