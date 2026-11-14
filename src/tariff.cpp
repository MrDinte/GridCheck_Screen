// Time-of-use prices: which period applies at a given minute and day.
// No hardware code here, so the PC simulator and tests use it too.
#include "net.h"

int periodAt(const Tariff& t, int m, int wday) {
  for (int i = 0; i < TOU_MAX; i++) {
    const TouPeriod& p = t.p[i];
    if (!p.used || p.start == p.end) continue;
    const bool in = p.start < p.end ? (m >= p.start && m < p.end) : (m >= p.start || m < p.end);
    if (!in) continue;
    // a period that wraps past midnight belongs to the day it started on
    const int day = (p.start > p.end && m < p.end) ? (wday + 6) % 7 : wday;
    const bool weekend = day == 0 || day == 6;
    if (p.days == TOU_WEEKDAYS && weekend) continue;
    if (p.days == TOU_WEEKENDS && !weekend) continue;
    return i;
  }
  return -1;
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
