// Time-of-use prices: which period applies at a given minute and day.
// No hardware code here, so the PC simulator and tests use it too.
#include "net.h"

#include <cstdio>
#include <ctime>

bool themeIsLight(const Settings& s) {
  if (s.theme == 1) return true;
  if (s.theme != 2) return false;
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // clock not set yet: stay dark
  struct tm t;
  localtime_r(&now, &t);
  const int m = t.tm_hour * 60 + t.tm_min;
  const bool night = s.nightStart < s.nightEnd ? (m >= s.nightStart && m < s.nightEnd) : (m >= s.nightStart || m < s.nightEnd);
  return !night;
}

bool inWindow(uint16_t start, uint16_t end, uint8_t days, int m, int wday) {
  if (start == end) return false;
  const bool in = start < end ? (m >= start && m < end) : (m >= start || m < end);
  if (!in) return false;
  // a window that wraps past midnight belongs to the day it started on
  const int day = (start > end && m < end) ? (wday + 6) % 7 : wday;
  const bool weekend = day == 0 || day == 6;
  if (days == TOU_WEEKDAYS && weekend) return false;
  if (days == TOU_WEEKENDS && !weekend) return false;
  return true;
}

int periodAt(const Tariff& t, int m, int wday) {
  for (int i = 0; i < TOU_MAX; i++) {
    const TouPeriod& p = t.p[i];
    if (p.used && inWindow(p.start, p.end, p.days, m, wday)) return i;
  }
  return -1;
}

float sellAt(const Tariff& t, int m, int wday) {
  if (!t.sellTimed) return t.sell;
  return inWindow(t.sellStart, t.sellEnd, t.sellDays, m, wday) ? t.sell : t.sellOther;
}

float priceAt(const Tariff& t, int m, int wday) {
  const int i = periodAt(t, m, wday);
  return i < 0 ? t.buy : t.p[i].price;
}

const char* periodName(const Tariff& t, int i) {
  if (i < 0) return t.baseName[0] ? t.baseName : "Standard";
  if (i >= TOU_MAX || !t.p[i].name[0]) return "Period";
  return t.p[i].name;
}

int minutesUntilChange(const Tariff& t, int m, int wday) {
  const int now = periodAt(t, m, wday);
  for (int k = 1; k <= 7 * 1440; k++) {
    const int mm = (m + k) % 1440, wd = (wday + (m + k) / 1440) % 7;
    if (periodAt(t, mm, wd) != now) return k;
  }
  return 7 * 1440;
}

std::string tariffProblem(const Tariff& t) {
  for (const auto& p : t.p)
    if (p.used && p.start == p.end) return std::string(p.name) + ": the start and end times are the same.";
  if (t.sellTimed && t.sellStart == t.sellEnd) return "Feed-in: the start and end times are the same.";
  // periods must not overlap: check every minute of the week
  for (int d = 0; d < 7; d++)
    for (int m = 0; m < 1440; m++) {
      int hit = -1;
      for (int i = 0; i < TOU_MAX; i++) {
        if (!t.p[i].used) continue;
        Tariff one;
        one.p[0] = t.p[i];
        if (periodAt(one, m, d) != 0) continue;
        if (hit >= 0) {
          static const char* DAY[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
          char at[48];
          snprintf(at, sizeof at, " overlap at %02d:%02d on %s.", m / 60, m % 60, DAY[d]);
          return std::string(t.p[hit].name) + " and " + t.p[i].name + at;
        }
        hit = i;
      }
    }
  return "";
}
