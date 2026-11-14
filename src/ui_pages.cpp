// The dashboard pages beyond "Now": Today, Solar vs forecast, Spent & earned,
// Energy flow (Sankey), Battery and History. Same content as the web dashboard.
#include <ctime>

#include "ui_common.h"

namespace ui {

// ---- shared bits -----------------------------------------------------------
static lv_obj_t* pageTitle(lv_obj_t* page, const char* text) {
  lv_obj_t* t = label(page, &lv_font_montserrat_28, C_INK, text);
  lv_obj_set_pos(t, 32, 8);
  return t;
}

static lv_obj_t* tile(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* o = plain(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C_TILE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, 16, 0);
  lv_obj_set_style_pad_all(o, 16, 0);
  return o;
}

/** A tile with a small caption and a big value + unit. Returns the value label. */
static lv_obj_t* statTile(lv_obj_t* parent, int x, int y, int w, int h, const char* caption, const lv_font_t* font,
                          lv_color_t color, const char* unit, lv_obj_t** unitOut = nullptr) {
  lv_obj_t* t = tile(parent, x, y, w, h);
  label(t, &lv_font_montserrat_18, C_MUTED, caption);
  lv_obj_t* row = plain(t);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t* v = label(row, font, color, "\xE2\x80\x93");
  lv_obj_t* u = label(row, &lv_font_montserrat_20, C_MUTED, unit);
  lv_obj_set_style_pad_bottom(u, font == &eh_num_56 ? 8 : 4, 0);
  if (unitOut) *unitOut = u;
  return v;
}

static void setKwh(lv_obj_t* l, float v) {
  if (std::isnan(v)) lv_label_set_text(l, "\xE2\x80\x93");
  else lv_label_set_text_fmt(l, v < 100 ? "%.1f" : "%.0f", v);
}

/** Segmented range switch, e.g. "7 days | 30 days". */
struct Seg {
  lv_obj_t* obj;
  int value;           // currently selected option's value
  const int* values;
  bool changed = true;
};

static void segEvent(lv_event_t* e) {
  Seg* s = (Seg*)lv_event_get_user_data(e);
  const uint16_t i = lv_btnmatrix_get_selected_btn(s->obj);
  if (i == LV_BTNMATRIX_BTN_NONE) return;
  s->value = s->values[i];
  s->changed = true;
}

static void makeSeg(lv_obj_t* page, Seg& s, const char** map, const int* values, int initial) {
  s.values = values;
  s.value = values[initial];
  s.obj = lv_btnmatrix_create(page);
  lv_btnmatrix_set_map(s.obj, map);
  lv_btnmatrix_set_btn_ctrl_all(s.obj, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s.obj, true);
  lv_btnmatrix_set_btn_ctrl(s.obj, initial, LV_BTNMATRIX_CTRL_CHECKED);
  int n = 0;
  while (map[n][0]) n++;
  lv_obj_set_size(s.obj, 130 * n, 52);
  lv_obj_align(s.obj, LV_ALIGN_TOP_RIGHT, -28, 4);
  lv_obj_set_style_bg_color(s.obj, C_TILE, 0);
  lv_obj_set_style_border_width(s.obj, 0, 0);
  lv_obj_set_style_radius(s.obj, 26, 0);
  lv_obj_set_style_pad_all(s.obj, 4, 0);
  lv_obj_set_style_pad_gap(s.obj, 4, 0);
  lv_obj_set_style_bg_opa(s.obj, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(s.obj, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(s.obj, 22, LV_PART_ITEMS);
  lv_obj_set_style_text_color(s.obj, C_MUTED, LV_PART_ITEMS);
  lv_obj_set_style_text_font(s.obj, &lv_font_montserrat_18, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(s.obj, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(s.obj, C_INK, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s.obj, C_BG, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(s.obj, segEvent, LV_EVENT_VALUE_CHANGED, &s);
}

static void dayLabel(char* buf, size_t n, uint32_t daysAgo, int total) {
  if (n < 24) { buf[0] = 0; return; }
  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  t.tm_mday -= daysAgo;
  t.tm_hour = 12;
  mktime(&t);
  if (daysAgo == 0) snprintf(buf, n, "Today");
  else if (total <= 7) strftime(buf, n, "%a", &t);
  else snprintf(buf, n, "%d/%d", t.tm_mday, t.tm_mon + 1);
}

/** Bar/line chart with a styled background, value labels on the y axis divided by `ydiv`. */
struct ChartBox {
  lv_obj_t* chart;
  lv_obj_t* xlabels[30];
  int nx = 0;
  float ydiv = 100;
  const char* yfmt = "%.0f";
};

static void chartDrawEvent(lv_event_t* e) {
  auto* box = (ChartBox*)lv_event_get_user_data(e);
  lv_obj_draw_part_dsc_t* d = lv_event_get_draw_part_dsc(e);
  if (d->part == LV_PART_TICKS && d->id == LV_CHART_AXIS_PRIMARY_Y && d->text) {
    lv_snprintf(d->text, d->text_length, box->yfmt, d->value / box->ydiv);
  }
  if (d->part == LV_PART_TICKS && d->id == LV_CHART_AXIS_SECONDARY_Y && d->text) {
    lv_snprintf(d->text, d->text_length, "%d%%", (int)d->value);
  }
}

static void makeChart(lv_obj_t* page, ChartBox& b, lv_chart_type_t type, int x, int y, int w, int h, int xLabels) {
  b.chart = lv_chart_create(page);
  lv_obj_set_pos(b.chart, x + 80, y);  // y-axis labels are drawn in the 80 px to the left
  lv_obj_set_size(b.chart, w - 80, h);
  lv_chart_set_type(b.chart, type);
  lv_chart_set_div_line_count(b.chart, 5, 0);
  lv_obj_set_style_bg_opa(b.chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b.chart, 0, 0);
  lv_obj_set_style_line_color(b.chart, C_FAINT, 0);
  lv_obj_set_style_pad_all(b.chart, 0, 0);
  lv_obj_set_style_pad_column(b.chart, type == LV_CHART_TYPE_BAR ? 4 : 0, 0);
  lv_obj_set_style_text_color(b.chart, C_MUTED, LV_PART_TICKS);
  lv_obj_set_style_text_font(b.chart, &lv_font_montserrat_16, LV_PART_TICKS);
  lv_obj_set_style_line_color(b.chart, C_FAINT, LV_PART_TICKS);
  lv_chart_set_axis_tick(b.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 1, true, 80);
  lv_obj_set_style_radius(b.chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_size(b.chart, 0, LV_PART_INDICATOR);  // no point markers on lines
  lv_obj_set_style_line_width(b.chart, 3, LV_PART_ITEMS);
  lv_obj_add_event_cb(b.chart, chartDrawEvent, LV_EVENT_DRAW_PART_BEGIN, &b);
  b.nx = xLabels;
  for (int i = 0; i < xLabels; i++) {
    b.xlabels[i] = label(page, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_style_text_align(b.xlabels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(b.xlabels[i], 80);
  }
}

/** Place `count` x-axis labels evenly under the plot area. */
static void placeXLabels(ChartBox& b, int count, const char* const* texts) {
  const lv_coord_t x0 = lv_obj_get_x(b.chart), w = lv_obj_get_width(b.chart) - lv_obj_get_style_pad_right(b.chart, 0);
  const lv_coord_t y = lv_obj_get_y(b.chart) + lv_obj_get_height(b.chart) + 6;
  for (int i = 0; i < b.nx; i++) {
    if (i >= count || !texts[i] || !texts[i][0]) {
      lv_obj_add_flag(b.xlabels[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(b.xlabels[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(b.xlabels[i], texts[i]);
    lv_obj_set_pos(b.xlabels[i], x0 + (lv_coord_t)((i + 0.5f) * w / count) - 40, y);
  }
}

static int niceMax(float v, const int* steps, int n) {
  for (int i = 0; i < n; i++)
    if (steps[i] >= v) return steps[i];
  return steps[n - 1];
}

// ============================================================================
// Today
// ============================================================================
static lv_obj_t *tSolar, *tHouse, *tSelf, *tImp, *tExp, *tChg, *tDis, *tSplitNote;
static lv_obj_t *tBarHouse, *tBarBatt, *tBarGrid, *tLegHouse, *tLegBatt, *tLegGrid;

void buildToday(lv_obj_t* page) {
  pageTitle(page, "Today");
  const int x0 = 32, gap = 14, wAll = W - 64;
  const int w3 = (wAll - 2 * gap) / 3, w4 = (wAll - 3 * gap) / 4;
  tSolar = statTile(page, x0, 58, w3, 132, "Solar made", &eh_num_56, C_SOLAR, "kWh");
  tHouse = statTile(page, x0 + w3 + gap, 58, w3, 132, "Home used", &eh_num_56, C_INK, "kWh");
  tSelf = statTile(page, x0 + 2 * (w3 + gap), 58, w3, 132, "Self-powered", &eh_num_56, C_PINK, "%");
  const char* caps[4] = {"From grid", "To grid", "Battery in", "Battery out"};
  lv_obj_t** vals[4] = {&tImp, &tExp, &tChg, &tDis};
  const lv_color_t cols[4] = {C_GRID, C_GRID, C_BATT, C_BATT};
  for (int i = 0; i < 4; i++) *vals[i] = statTile(page, x0 + i * (w4 + gap), 204, w4, 108, caps[i], &lv_font_montserrat_40, cols[i], "kWh");

  lv_obj_t* cap = label(page, &lv_font_montserrat_18, C_MUTED, "Where your solar went");
  lv_obj_set_pos(cap, x0, 334);
  tSplitNote = label(page, &lv_font_montserrat_18, C_MUTED, "");
  lv_obj_set_width(tSplitNote, 400);
  lv_obj_set_style_text_align(tSplitNote, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(tSplitNote, W - 32 - 400, 334);
  lv_obj_t* bar = plain(page);
  lv_obj_set_pos(bar, x0, 364);
  lv_obj_set_size(bar, wAll, 22);
  lv_obj_set_style_radius(bar, 11, 0);
  lv_obj_set_style_clip_corner(bar, true, 0);
  lv_obj_set_style_bg_color(bar, C_FAINT, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_t** segs[3] = {&tBarHouse, &tBarBatt, &tBarGrid};
  const lv_color_t sc[3] = {C_SOLAR, C_BATT, C_GRID};
  for (int i = 0; i < 3; i++) {
    *segs[i] = plain(bar);
    lv_obj_set_size(*segs[i], 0, 22);
    lv_obj_set_style_bg_color(*segs[i], sc[i], 0);
    lv_obj_set_style_bg_opa(*segs[i], LV_OPA_COVER, 0);
  }
  lv_obj_t* leg = plain(page);
  lv_obj_set_pos(leg, x0, 398);
  lv_obj_set_size(leg, wAll, 30);
  lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(leg, 36, 0);
  lv_obj_t** legs[3] = {&tLegHouse, &tLegBatt, &tLegGrid};
  for (int i = 0; i < 3; i++) {
    *legs[i] = label(leg, &lv_font_montserrat_18, C_MUTED, "");
    lv_label_set_recolor(*legs[i], true);
  }
}

void updateToday(const Live& l, bool) {
  const DayRecord d = data::today();
  const bool have = d.date != 0;
  const float solar = !std::isnan(l.solarToday) ? l.solarToday : have ? d.solar : NAN;
  const float house = !std::isnan(l.houseToday) ? l.houseToday : have ? d.house : NAN;
  setKwh(tSolar, solar);
  setKwh(tHouse, house);
  setKwh(tImp, have ? d.imp : NAN);
  setKwh(tExp, have ? d.exp : NAN);
  setKwh(tChg, have ? d.chg : NAN);
  setKwh(tDis, have ? d.dis : NAN);
  if (have && house > 0.05f) lv_label_set_text_fmt(tSelf, "%d", (int)lroundf(fmaxf(0, 1 - d.imp / house) * 100));
  else lv_label_set_text(tSelf, "\xE2\x80\x93");

  const float sol = have ? d.solar : 0;
  const float toGrid = fminf(d.exp, sol), toBatt = fminf(d.chg, sol - toGrid), toHouse = fmaxf(0, sol - toGrid - toBatt);
  const int wAll = W - 64;
  lv_obj_set_width(tBarHouse, sol > 0 ? (lv_coord_t)(wAll * toHouse / sol) : 0);
  lv_obj_set_width(tBarBatt, sol > 0 ? (lv_coord_t)(wAll * toBatt / sol) : 0);
  lv_obj_set_width(tBarGrid, sol > 0 ? (lv_coord_t)(wAll * toGrid / sol) : 0);
  lv_label_set_text_fmt(tLegHouse, "#%s " LV_SYMBOL_STOP "# House  #%s %.1f kWh#", hx(C_SOLAR), hx(C_INK), toHouse);
  lv_label_set_text_fmt(tLegBatt, "#%s " LV_SYMBOL_STOP "# Battery  #%s %.1f kWh#", hx(C_BATT), hx(C_INK), toBatt);
  lv_label_set_text_fmt(tLegGrid, "#%s " LV_SYMBOL_STOP "# Grid  #%s %.1f kWh#", hx(C_GRID), hx(C_INK), toGrid);
  if (sol > 0.05f) lv_label_set_text_fmt(tSplitNote, "%d%% kept at home", (int)lroundf((toHouse + toBatt) / sol * 100));
  else lv_label_set_text(tSplitNote, have ? "No solar yet today" : "Waiting for the clock to be set");
}

// ============================================================================
// Solar vs forecast: half circle of ticks
// ============================================================================
static lv_obj_t *sMeter, *sNum, *sSub, *sNote, *sYday, *sTmrw, *sTmrwCap;
static lv_meter_indicator_t* sInd;

void buildSolar(lv_obj_t* page) {
  pageTitle(page, "Solar today vs forecast");
  sMeter = lv_meter_create(page);
  lv_obj_set_size(sMeter, 500, 500);
  lv_obj_align(sMeter, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_style_bg_opa(sMeter, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sMeter, 0, 0);
  lv_obj_set_style_pad_all(sMeter, 0, 0);
  lv_obj_set_style_size(sMeter, 0, LV_PART_INDICATOR);  // no centre dot
  lv_meter_scale_t* sc = lv_meter_add_scale(sMeter);
  lv_meter_set_scale_ticks(sMeter, sc, 37, 9, 40, C_FAINT);
  lv_meter_set_scale_range(sMeter, sc, 0, 100, 180, 180);
  sInd = lv_meter_add_scale_lines(sMeter, sc, C_SOLAR, C_SOLAR, false, 0);
  lv_meter_set_indicator_start_value(sMeter, sInd, 0);
  lv_meter_set_indicator_end_value(sMeter, sInd, 0);

  lv_obj_t* box = plain(page);
  lv_obj_set_size(box, 420, 150);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 196);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* row = plain(box);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 8, 0);
  sNum = label(row, &eh_num_76, C_INK, "\xE2\x80\x93");
  lv_obj_t* u = label(row, &lv_font_montserrat_24, C_MUTED, "kWh");
  lv_obj_set_style_pad_bottom(u, 12, 0);
  sSub = label(box, &lv_font_montserrat_20, C_MUTED, "");

  sNote = label(page, &lv_font_montserrat_18, C_MUTED, "");
  lv_obj_set_width(sNote, W - 64);
  lv_obj_set_style_text_align(sNote, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sNote, LV_ALIGN_BOTTOM_MID, 0, -14);

  lv_obj_t* l = tile(page, 40, 250, 200, 110);
  label(l, &lv_font_montserrat_18, C_MUTED, "Yesterday");
  sYday = label(l, &lv_font_montserrat_40, C_INK, "\xE2\x80\x93");
  lv_obj_align(sYday, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t* r = tile(page, W - 240, 250, 200, 110);
  sTmrwCap = label(r, &lv_font_montserrat_18, C_MUTED, "Tomorrow");
  lv_obj_align(sTmrwCap, LV_ALIGN_TOP_RIGHT, 0, 0);
  sTmrw = label(r, &lv_font_montserrat_40, C_SOLAR, "\xE2\x80\x93");
  lv_obj_align(sTmrw, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

/** Average solar of the last 7 complete days, or NAN. */
static float typicalSolar() {
  DayRecord d[8];
  data::days(d, 8);
  float sum = 0;
  int n = 0;
  for (int i = 0; i < 7; i++)
    if (d[i].date && d[i].solar > 0.1f) sum += d[i].solar, n++;
  return n ? sum / n : NAN;
}

void updateSolar(const Live& l, bool full) {
  static float typical = NAN;
  if (full) typical = typicalSolar();
  const Forecast fc = data::forecast();
  const float target = fc.ok ? fc.today : typical;
  const float solar = !std::isnan(l.solarToday) ? l.solarToday : data::today().date ? data::today().solar : NAN;
  if (std::isnan(solar)) lv_label_set_text(sNum, "\xE2\x80\x93");
  else lv_label_set_text_fmt(sNum, "%.1f", solar);
  if (std::isnan(l.solarYesterday)) lv_label_set_text(sYday, "\xE2\x80\x93");
  else lv_label_set_text_fmt(sYday, "%.1f", l.solarYesterday);
  if (fc.ok && !std::isnan(fc.tomorrow)) {
    lv_label_set_text_fmt(sTmrw, "~%d", (int)lroundf(fc.tomorrow));
  } else {
    lv_label_set_text(sTmrw, "\xE2\x80\x93");
  }
  const int pct = (!std::isnan(solar) && target > 0) ? (int)constrain(solar / target * 100, 0.0f, 100.0f) : 0;
  lv_meter_set_indicator_end_value(sMeter, sInd, pct);
  if (std::isnan(target)) {
    lv_label_set_text(sSub, "kWh of solar today");
    lv_label_set_text(sNote, "Add your location and panel size on the phone settings page for a weather forecast.");
    return;
  }
  lv_label_set_text_fmt(sSub, "so far, of ~%d kWh %s", (int)lroundf(target), fc.ok ? "forecast" : "(7-day average)");
  if (!std::isnan(solar) && solar >= target)
    lv_label_set_text_fmt(sNote, "Beat the %s by %.1f kWh", fc.ok ? "forecast" : "average", solar - target);
  else
    lv_label_set_text_fmt(sNote, "%d%% of the %s so far%s", pct, fc.ok ? "forecast" : "average",
                          fc.ok ? "" : " - add your location on the phone settings page for a weather forecast");
}

// ============================================================================
// Spent & earned
// ============================================================================
static Seg mRange;
static const char* M_MAP[] = {"7 days", "30 days", ""};
static const int M_VALUES[] = {7, 30};
static ChartBox mChart;
static lv_chart_series_t *mSpent, *mEarned;
static lv_obj_t *mSpentL, *mEarnedL, *mNetL, *mNetCap, *mNote;

void buildMoney(lv_obj_t* page) {
  pageTitle(page, "Spent & earned");
  makeSeg(page, mRange, M_MAP, M_VALUES, 0);
  const int gap = 14, w3 = (W - 64 - 2 * gap) / 3;
  mSpentL = statTile(page, 32, 66, w3, 92, "Spent on grid power", &lv_font_montserrat_40, C_INK, "");
  mEarnedL = statTile(page, 32 + w3 + gap, 66, w3, 92, "Earned from export", &lv_font_montserrat_40, C_PINK, "");
  lv_obj_t* t = tile(page, 32 + 2 * (w3 + gap), 66, w3, 92);
  mNetCap = label(t, &lv_font_montserrat_18, C_MUTED, "Net");
  mNetL = label(t, &lv_font_montserrat_40, C_INK, "\xE2\x80\x93");
  lv_obj_align(mNetL, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  makeChart(page, mChart, LV_CHART_TYPE_BAR, 24, 176, W - 56, 200, 30);
  mChart.ydiv = 100;
  mChart.yfmt = "$%.2f";
  mSpent = lv_chart_add_series(mChart.chart, C_INK, LV_CHART_AXIS_PRIMARY_Y);
  mEarned = lv_chart_add_series(mChart.chart, C_PINK, LV_CHART_AXIS_PRIMARY_Y);
  mNote = label(page, &lv_font_montserrat_16, C_MUTED, "");
  lv_obj_set_width(mNote, W - 64);
  lv_obj_set_style_text_align(mNote, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(mNote, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void updateMoney(const Live&, bool full) {
  if (!full && !mRange.changed) return;
  mRange.changed = false;
  const int n = mRange.value;
  static DayRecord d[30];
  data::days(d, n);
  const Tariff tf = net::settings().tariff;
  const float supply = tf.supply / 100.0f;
  float spent = 0, earned = 0, peak = 0.5f;
  for (int i = 0; i < n; i++) {
    if (!d[i].date) continue;
    spent += d[i].spent + supply;
    earned += d[i].earned;
    peak = fmaxf(peak, fmaxf(d[i].spent + supply, d[i].earned));
  }
  lv_label_set_text_fmt(mSpentL, "$%.2f", spent);
  lv_label_set_text_fmt(mEarnedL, "$%.2f", earned);
  const float net = spent - earned;
  lv_label_set_text_fmt(mNetL, "$%.2f", fabsf(net));
  lv_obj_set_style_text_color(mNetL, net >= 0 ? C_INK : C_PINK, 0);
  lv_label_set_text_fmt(mNetCap, "Net %s, %s", net >= 0 ? "cost" : "gain", n == 7 ? "this week" : "30 days");

  static const int steps[] = {100, 200, 500, 1000, 2000, 2500, 5000, 10000, 20000};
  const int maxC = niceMax(peak * 100, steps, 9);
  lv_chart_set_point_count(mChart.chart, n);
  lv_chart_set_range(mChart.chart, LV_CHART_AXIS_PRIMARY_Y, 0, maxC);
  lv_chart_set_axis_tick(mChart.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 5, 1, true, 80);
  lv_chart_set_div_line_count(mChart.chart, 5, 0);
  lv_obj_set_style_pad_column(mChart.chart, n <= 7 ? 40 : 4, 0);
  static char labels[30][24];
  const char* texts[30];
  for (int i = 0; i < n; i++) {
    mSpent->y_points[i] = d[i].date ? (lv_coord_t)lroundf((d[i].spent + supply) * 100) : LV_CHART_POINT_NONE;
    mEarned->y_points[i] = d[i].date ? (lv_coord_t)lroundf(d[i].earned * 100) : LV_CHART_POINT_NONE;
    const int ago = n - 1 - i;
    if (n <= 7 || ago % 5 == 0) dayLabel(labels[i], sizeof labels[i], ago, n);
    else labels[i][0] = 0;
    texts[i] = labels[i];
  }
  lv_chart_refresh(mChart.chart);
  placeXLabels(mChart, n, texts);

  const DayRecord& t = d[n - 1];
  char note[200];
  int k = 0;
  if (t.date) k += snprintf(note + k, sizeof note - k, "Today so far: $%.2f spent, $%.2f earned", t.spent + supply, t.earned);
  else k += snprintf(note + k, sizeof note - k, "Costs start once the clock is set and readings come in");
  if (supply > 0) k += snprintf(note + k, sizeof note - k, "  -  includes $%.2f/day supply", supply);
  if (!tf.set) snprintf(note + k, sizeof note - k, "  -  example prices: set yours on the phone settings page");
  lv_label_set_text(mNote, note);
}

// ============================================================================
// Energy flow (Sankey)
// ============================================================================
static Seg fRange;
static const char* F_MAP[] = {"Today", "7 days", "30 days", ""};
static const int F_VALUES[] = {1, 7, 30};
static lv_obj_t *fCanvas, *fNote;
static lv_color_t* fBuf = nullptr;
static constexpr int FC_W = W - 64, FC_H = 330;
static lv_obj_t *fNodeName[6], *fNodeVal[6];

void buildFlow(lv_obj_t* page) {
  pageTitle(page, "Energy flow");
  makeSeg(page, fRange, F_MAP, F_VALUES, 0);
  if (!fBuf) fBuf = (lv_color_t*)lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(FC_W, FC_H));
  fCanvas = lv_canvas_create(page);
  lv_canvas_set_buffer(fCanvas, fBuf, FC_W, FC_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(fCanvas, 32, 72);
  lv_canvas_fill_bg(fCanvas, C_BG, LV_OPA_COVER);
  for (int i = 0; i < 6; i++) {
    fNodeName[i] = label(page, &lv_font_montserrat_18, C_MUTED, "");
    fNodeVal[i] = label(page, &lv_font_montserrat_24, C_INK, "");
  }
  fNote = label(page, &lv_font_montserrat_18, C_MUTED, "");
  lv_obj_set_width(fNote, W - 64);
  lv_obj_set_style_text_align(fNote, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(fNote, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void quad(lv_obj_t* c, float x0, float a0, float b0, float x1, float a1, float b1, lv_color_t col) {
  lv_point_t p[4] = {{(lv_coord_t)x0, (lv_coord_t)a0}, {(lv_coord_t)(x1 + 1), (lv_coord_t)a1},
                     {(lv_coord_t)(x1 + 1), (lv_coord_t)b1}, {(lv_coord_t)x0, (lv_coord_t)b0}};
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col;
  d.bg_opa = LV_OPA_COVER;
  lv_canvas_draw_polygon(c, p, 4, &d);
}

void updateFlow(const Live&, bool full) {
  if (!full && !fRange.changed) return;
  fRange.changed = false;
  const int n = fRange.value;
  static DayRecord d[30];
  data::days(d, n);
  float f[FLOW_COUNT] = {};
  for (int i = 0; i < n; i++)
    if (d[i].date)
      for (int k = 0; k < FLOW_COUNT; k++) f[k] += d[i].flows[k];

  struct Node { const char* name; lv_color_t color; float value, y, h, off; };
  Node src[3] = {{"Solar", C_SOLAR}, {"From battery", C_BATT}, {"From grid", C_GRID}};
  Node dst[3] = {{"Home", C_INK}, {"To battery", C_BATT}, {"To grid", C_GRID}};
  struct L { int s, t; float v; };
  const L links[7] = {{0, 0, f[SOLAR_HOUSE]}, {0, 1, f[SOLAR_BATT]}, {0, 2, f[SOLAR_GRID]}, {1, 0, f[BATT_HOUSE]},
                      {1, 2, f[BATT_GRID]},   {2, 0, f[GRID_HOUSE]}, {2, 1, f[GRID_BATT]}};
  float total = 0;
  for (const auto& k : links) {
    src[k.s].value += k.v;
    dst[k.t].value += k.v;
    total += k.v;
  }
  lv_canvas_fill_bg(fCanvas, C_BG, LV_OPA_COVER);
  for (int i = 0; i < 6; i++) {
    lv_obj_add_flag(fNodeName[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(fNodeVal[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (total < 0.01f) {
    lv_label_set_text(fNote, "Collecting data... the flow fills in as readings come in.");
    lv_obj_invalidate(fCanvas);
    return;
  }

  const float minShown = total * 0.004f, GAP = 22, PAD_B = 26, AVAIL = FC_H - PAD_B;
  auto shown = [&](Node* ns) { int c = 0; for (int i = 0; i < 3; i++) c += ns[i].value > minShown; return c; };
  const float scale = (AVAIL - GAP * (std::max(std::max(shown(src), shown(dst)), 1) - 1)) / total;
  auto layout = [&](Node* ns) {
    float y = 0;
    for (int i = 0; i < 3; i++) {
      ns[i].h = ns[i].value > minShown ? ns[i].value * scale : 0;
      ns[i].y = y;
      ns[i].off = 0;
      if (ns[i].h > 0) y += ns[i].h + GAP;
    }
    const float used = y - GAP;
    for (int i = 0; i < 3; i++) ns[i].y += (AVAIL - used) / 2;
  };
  layout(src);
  layout(dst);

  const float NODE_W = 14, X0 = 190, X1 = FC_W - 190 - NODE_W;
  const int SLICES = 36;
  for (const auto& k : links) {
    if (k.v <= minShown) continue;
    Node &a = src[k.s], &b = dst[k.t];
    const float h = k.v * scale, ya = a.y + a.off, yb = b.y + b.off;
    a.off += h;
    b.off += h;
    float px = X0 + NODE_W, pa = ya, pb = ya + h;
    for (int i = 1; i <= SLICES; i++) {
      const float t = (float)i / SLICES, e = t * t * (3 - 2 * t);
      const float x = X0 + NODE_W + (X1 - X0 - NODE_W) * t, y = ya + (yb - ya) * e;
      const lv_color_t col = lv_color_mix(lv_color_mix(b.color, a.color, (uint8_t)(255 * t)), C_BG, 115);
      quad(fCanvas, px, pa, pb, x, y, y + h, col);
      px = x, pa = y, pb = y + h;
    }
  }
  int li = 0;
  auto node = [&](Node& nd, float x, bool left, float& prevLabelY) {
    if (nd.h <= 0) return;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = nd.color;
    r.radius = 3;
    lv_canvas_draw_rect(fCanvas, (lv_coord_t)x, (lv_coord_t)nd.y, (lv_coord_t)NODE_W, (lv_coord_t)std::max(2.0f, nd.h), &r);
    // labels, kept at least 52 px apart
    float cy = std::max(nd.y + nd.h / 2, prevLabelY + 52);
    prevLabelY = cy;
    lv_label_set_text(fNodeName[li], nd.name);
    lv_label_set_text_fmt(fNodeVal[li], nd.value < 100 ? "%.1f kWh" : "%.0f kWh", nd.value);
    lv_obj_set_style_text_color(fNodeVal[li], nd.color, 0);
    for (lv_obj_t* o : {fNodeName[li], fNodeVal[li]}) {
      lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(o, 170);
      lv_obj_set_style_text_align(o, left ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    }
    const lv_coord_t lx = 32 + (lv_coord_t)(left ? x - 12 - 170 : x + NODE_W + 12);
    lv_obj_set_pos(fNodeName[li], lx, 72 + (lv_coord_t)cy - 26);
    lv_obj_set_pos(fNodeVal[li], lx, 72 + (lv_coord_t)cy - 4);
    li++;
  };
  float py = -100;
  for (auto& nd : src) node(nd, X0, true, py);
  py = -100;
  for (auto& nd : dst) node(nd, X1, false, py);
  lv_obj_invalidate(fCanvas);

  const float solar = src[0].value, house = dst[0].value;
  char note[160];
  int k = 0;
  if (solar > 0.05f) k += snprintf(note, sizeof note, "%d%% of your solar used at home", (int)lroundf((f[SOLAR_HOUSE] + f[SOLAR_BATT]) / solar * 100));
  if (house > 0.05f) snprintf(note + k, sizeof note - k, "%shome %d%% self-powered", k ? "  -  " : "", (int)lroundf((1 - f[GRID_HOUSE] / house) * 100));
  lv_label_set_text(fNote, note);
}

// ============================================================================
// Battery
// ============================================================================
static lv_obj_t *bArc, *bPct, *bState, *bFacts[7];

void buildBattery(lv_obj_t* page) {
  bArc = lv_arc_create(page);
  lv_obj_set_size(bArc, 340, 340);
  lv_obj_set_pos(bArc, 70, (BODY_H - 340) / 2);
  lv_arc_set_rotation(bArc, 270);
  lv_arc_set_bg_angles(bArc, 0, 360);
  lv_arc_set_range(bArc, 0, 100);
  lv_arc_set_value(bArc, 0);
  lv_obj_remove_style(bArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(bArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(bArc, 22, LV_PART_MAIN);
  lv_obj_set_style_arc_width(bArc, 22, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(bArc, C_FAINT, LV_PART_MAIN);
  lv_obj_set_style_arc_color(bArc, C_BATT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(bArc, false, LV_PART_INDICATOR);

  lv_obj_t* row = plain(page);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  bPct = label(row, &eh_num_76, C_INK, "\xE2\x80\x93");
  lv_obj_t* u = label(row, &lv_font_montserrat_28, C_MUTED, "%");
  lv_obj_set_style_pad_bottom(u, 12, 0);
  lv_obj_align_to(row, bArc, LV_ALIGN_CENTER, 0, -14);
  bState = label(page, &lv_font_montserrat_20, C_BATT, "");
  lv_obj_set_width(bState, 300);
  lv_obj_set_style_text_align(bState, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(bState, bArc, LV_ALIGN_CENTER, 0, 52);

  const char* names[7] = {"Time", "Stored", "Power", "Today in / out", "Reserve", "Health", "Temperature"};
  lv_obj_t* col = plain(page);
  lv_obj_set_pos(col, 480, 22);
  lv_obj_set_size(col, W - 480 - 40, BODY_H - 44);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  for (int i = 0; i < 7; i++) {
    lv_obj_t* r = plain(col);
    lv_obj_set_size(r, LV_PCT(100), 58);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(r, C_FAINT, 0);
    lv_obj_set_style_border_width(r, i < 6 ? 1 : 0, 0);
    lv_obj_t* k = label(r, &lv_font_montserrat_20, C_MUTED, names[i]);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    bFacts[i] = label(r, &lv_font_montserrat_24, C_INK, "\xE2\x80\x93");
    lv_obj_align(bFacts[i], LV_ALIGN_RIGHT_MID, 0, 0);
    if (i == 0) lv_obj_set_user_data(r, k);  // so the "Time" caption can change
  }
}

static void duration(char* buf, size_t n, float hours) {
  if (!(hours >= 0) || std::isinf(hours)) { snprintf(buf, n, "\xE2\x80\x93"); return; }
  const int m = (int)lroundf(hours * 60);
  if (m < 60) snprintf(buf, n, "%d min", std::max(1, m));
  else if (m / 60 >= 48) snprintf(buf, n, "%d days", m / 60 / 24);
  else if (m % 60) snprintf(buf, n, "%d h %d min", m / 60, m % 60);
  else snprintf(buf, n, "%d h", m / 60);
}

void updateBattery(const Live& l, bool) {
  const bool fresh = l.ok && millis() - l.updatedMs < 30000;
  lv_obj_t* timeCap = (lv_obj_t*)lv_obj_get_user_data(lv_obj_get_parent(bFacts[0]));
  char b[48];
  if (!fresh || std::isnan(l.soc)) {
    lv_label_set_text(bPct, "\xE2\x80\x93");
    lv_arc_set_value(bArc, 0);
    lv_label_set_text(bState, "");
    for (auto* f : bFacts) lv_label_set_text(f, "\xE2\x80\x93");
    return;
  }
  const int soc = (int)lroundf(l.soc);
  const bool low = !std::isnan(l.reserve) ? l.soc <= l.reserve + 5 : l.soc <= 15;
  lv_label_set_text_fmt(bPct, "%d", soc);
  lv_arc_set_value(bArc, soc);
  lv_obj_set_style_arc_color(bArc, low ? C_WARN : C_BATT, LV_PART_INDICATOR);
  fmtKw(b, sizeof b, l.battery);
  if (l.battery > IDLE_KW) {
    lv_label_set_text_fmt(bState, "Charging %s kW", b);
    lv_label_set_text(timeCap, "Full in");
    char t[32];
    duration(t, sizeof t, !std::isnan(l.roomKWh) ? l.roomKWh / l.battery : NAN);
    lv_label_set_text(bFacts[0], t);
  } else if (l.battery < -IDLE_KW) {
    lv_label_set_text_fmt(bState, "Discharging %s kW", b);
    lv_label_set_text(timeCap, "Lasts");
    char t[32] = "~";
    duration(t + 1, sizeof t - 1, !std::isnan(l.availKWh) ? l.availKWh / -l.battery : NAN);
    lv_label_set_text(bFacts[0], t);
  } else {
    lv_label_set_text(bState, "Idle");
    lv_label_set_text(timeCap, "Status");
    lv_label_set_text(bFacts[0], soc >= 99 ? "Full" : (!std::isnan(l.reserve) && l.soc <= l.reserve + 0.5f) ? "At reserve" : "Holding");
  }
  lv_obj_set_style_text_color(bState, low ? C_WARN : C_BATT, 0);
  if (!std::isnan(l.capacityKWh)) lv_label_set_text_fmt(bFacts[1], "%.1f of %.1f kWh", l.soc / 100 * l.capacityKWh, l.capacityKWh);
  else lv_label_set_text(bFacts[1], "\xE2\x80\x93");
  lv_label_set_text_fmt(bFacts[2], "%s%s kW", l.battery < -IDLE_KW ? "-" : "", b);
  const DayRecord d = data::today();
  if (d.date) lv_label_set_text_fmt(bFacts[3], "%.1f / %.1f kWh", d.chg, d.dis);
  else lv_label_set_text(bFacts[3], "\xE2\x80\x93");
  if (!std::isnan(l.reserve)) lv_label_set_text_fmt(bFacts[4], "%d%%", (int)lroundf(l.reserve));
  if (!std::isnan(l.soh)) lv_label_set_text_fmt(bFacts[5], "%.1f%%", l.soh);
  if (!std::isnan(l.tempC)) lv_label_set_text_fmt(bFacts[6], "%.1f C", l.tempC);
}

// ============================================================================
// History: 1 day of power, or 7 / 30 days of daily energy
// ============================================================================
static Seg hRange;
static const char* H_MAP[] = {"1 day", "7 days", "30 days", ""};
static const int H_VALUES[] = {1, 7, 30};
static ChartBox hLine, hBars;
static lv_chart_series_t *hlSolar, *hlHouse, *hlGrid, *hlSoc, *hbSolar, *hbHouse, *hbImp;
static lv_obj_t *hTitle, *hLegend, *hTotals;
static constexpr int H_POINTS = 144;  // 10-minute buckets

void buildHistory(lv_obj_t* page) {
  hTitle = pageTitle(page, "Last 24 hours");
  makeSeg(page, hRange, H_MAP, H_VALUES, 0);
  makeChart(page, hLine, LV_CHART_TYPE_LINE, 24, 78, W - 56, 290, 9);
  hLine.ydiv = 100;
  hLine.yfmt = "%.0f kW";
  lv_chart_set_point_count(hLine.chart, H_POINTS);
  lv_chart_set_axis_tick(hLine.chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 3, 1, true, 50);
  lv_chart_set_range(hLine.chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
  lv_obj_set_style_pad_right(hLine.chart, 56, 0);
  hlSolar = lv_chart_add_series(hLine.chart, C_SOLAR, LV_CHART_AXIS_PRIMARY_Y);
  hlHouse = lv_chart_add_series(hLine.chart, C_INK, LV_CHART_AXIS_PRIMARY_Y);
  hlGrid = lv_chart_add_series(hLine.chart, C_GRID, LV_CHART_AXIS_PRIMARY_Y);
  hlSoc = lv_chart_add_series(hLine.chart, C_BATT, LV_CHART_AXIS_SECONDARY_Y);

  makeChart(page, hBars, LV_CHART_TYPE_BAR, 24, 78, W - 56, 290, 30);
  hBars.ydiv = 1;
  hBars.yfmt = "%.0f kWh";
  hbSolar = lv_chart_add_series(hBars.chart, C_SOLAR, LV_CHART_AXIS_PRIMARY_Y);
  hbHouse = lv_chart_add_series(hBars.chart, C_INK, LV_CHART_AXIS_PRIMARY_Y);
  hbImp = lv_chart_add_series(hBars.chart, C_GRID, LV_CHART_AXIS_PRIMARY_Y);

  hLegend = label(page, &lv_font_montserrat_18, C_MUTED, "");
  lv_label_set_recolor(hLegend, true);
  lv_obj_set_pos(hLegend, 32, BODY_H - 34);
  hTotals = hLegend;
}

void updateHistory(const Live&, bool full) {
  if (!full && !hRange.changed) return;
  hRange.changed = false;
  const int n = hRange.value;
  const bool day = n == 1;
  lv_obj_add_flag(day ? hBars.chart : hLine.chart, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(day ? hLine.chart : hBars.chart, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < hBars.nx; i++) lv_obj_add_flag(hBars.xlabels[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < hLine.nx; i++) lv_obj_add_flag(hLine.xlabels[i], LV_OBJ_FLAG_HIDDEN);

  if (day) {
    lv_label_set_text(hTitle, "Last 24 hours");
    // 24 h of samples is ~35 KB: keep it in PSRAM (large mallocs go there), not in scarce internal RAM.
    static MinuteSample* m = (MinuteSample*)malloc(sizeof(MinuteSample) * 24 * 60);
    const int cnt = m ? data::minutes(m, 24 * 60) : 0;
    const uint32_t now = (uint32_t)time(nullptr), start = now - 24 * 3600;
    // average into 10-minute buckets
    static float s[H_POINTS], h[H_POINTS], g[H_POINTS], c[H_POINTS];
    static int k[H_POINTS];
    for (int i = 0; i < H_POINTS; i++) s[i] = h[i] = g[i] = c[i] = 0, k[i] = 0;
    float hi = 1, lo = 0;
    for (int i = 0; i < cnt; i++) {
      if (m[i].t < start) continue;
      const int b = std::min(H_POINTS - 1, (int)((m[i].t - start) / 600));
      s[b] += m[i].solar, h[b] += m[i].house, g[b] += m[i].grid, c[b] += m[i].soc, k[b]++;
    }
    for (int i = 0; i < H_POINTS; i++) {
      if (!k[i]) {
        hlSolar->y_points[i] = hlHouse->y_points[i] = hlGrid->y_points[i] = hlSoc->y_points[i] = LV_CHART_POINT_NONE;
        continue;
      }
      s[i] /= k[i], h[i] /= k[i], g[i] /= k[i], c[i] /= k[i];
      hi = fmaxf(hi, fmaxf(s[i], fmaxf(h[i], g[i])));
      lo = fminf(lo, g[i]);
      hlSolar->y_points[i] = (lv_coord_t)lroundf(s[i] * 100);
      hlHouse->y_points[i] = (lv_coord_t)lroundf(h[i] * 100);
      hlGrid->y_points[i] = (lv_coord_t)lroundf(g[i] * 100);
      hlSoc->y_points[i] = (lv_coord_t)lroundf(c[i]);
    }
    const int top = (int)ceilf(hi), bottom = (int)floorf(lo);
    lv_chart_set_range(hLine.chart, LV_CHART_AXIS_PRIMARY_Y, bottom * 100, top * 100);
    lv_chart_set_axis_tick(hLine.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, std::min(top - bottom + 1, 9), 1, true, 80);
    lv_chart_set_div_line_count(hLine.chart, std::min(top - bottom + 1, 9), 0);
    lv_chart_refresh(hLine.chart);
    // time labels every 3 hours
    static char lab[9][12];
    const char* texts[9];
    for (int i = 0; i < 9; i++) {
      const time_t t = start + i * 3 * 3600;
      struct tm tm;
      localtime_r(&t, &tm);
      strftime(lab[i], sizeof lab[i], "%l %p", &tm);
      texts[i] = lab[i][0] == ' ' ? lab[i] + 1 : lab[i];
    }
    // labels mark bucket centres of 8 equal spans; use 8 spans
    placeXLabels(hLine, 8, texts);
    if (cnt)
      lv_label_set_text_fmt(hLegend, "#%s " LV_SYMBOL_STOP "# Solar     #%s " LV_SYMBOL_STOP "# Home     #%s " LV_SYMBOL_STOP "# Grid (+ in / - out)     #%s " LV_SYMBOL_STOP "# Battery %%",
                            hx(C_SOLAR), hx(C_INK), hx(C_GRID), hx(C_BATT));
    else
      lv_label_set_text(hLegend, "Collecting data... the graph fills in as readings come in.");
    return;
  }

  lv_label_set_text_fmt(hTitle, "Last %d days", n);
  static DayRecord d[30];
  data::days(d, n);
  float peak = 1, solar = 0, house = 0, imp = 0, exp = 0;
  for (int i = 0; i < n; i++) {
    if (!d[i].date) continue;
    peak = fmaxf(peak, fmaxf(d[i].solar, fmaxf(d[i].house, d[i].imp)));
    solar += d[i].solar, house += d[i].house, imp += d[i].imp, exp += d[i].exp;
  }
  static const int steps[] = {5, 10, 20, 25, 30, 40, 50, 60, 80, 100, 150, 200};
  const int top = niceMax(peak, steps, 12);
  lv_chart_set_point_count(hBars.chart, n);
  lv_chart_set_range(hBars.chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  lv_chart_set_axis_tick(hBars.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 1, true, 80);
  lv_chart_set_div_line_count(hBars.chart, 6, 0);
  lv_obj_set_style_pad_column(hBars.chart, n <= 7 ? 30 : 3, 0);
  static char labels[30][24];
  const char* texts[30];
  for (int i = 0; i < n; i++) {
    hbSolar->y_points[i] = d[i].date ? (lv_coord_t)lroundf(d[i].solar) : LV_CHART_POINT_NONE;
    hbHouse->y_points[i] = d[i].date ? (lv_coord_t)lroundf(d[i].house) : LV_CHART_POINT_NONE;
    hbImp->y_points[i] = d[i].date ? (lv_coord_t)lroundf(d[i].imp) : LV_CHART_POINT_NONE;
    const int ago = n - 1 - i;
    if (n <= 7 || ago % 5 == 0) dayLabel(labels[i], sizeof labels[i], ago, n);
    else labels[i][0] = 0;
    texts[i] = labels[i];
  }
  lv_chart_refresh(hBars.chart);
  placeXLabels(hBars, n, texts);
  lv_label_set_text_fmt(hTotals,
                        "#%s " LV_SYMBOL_STOP "# Solar #%s %.0f kWh#    #%s " LV_SYMBOL_STOP "# Home #%s %.0f kWh#    "
                        "#%s " LV_SYMBOL_STOP "# From grid #%s %.0f kWh#    To grid #%s %.0f kWh#    Self-powered #%s %d%%#",
                        hx(C_SOLAR), hx(C_INK), solar, hx(C_INK), hx(C_INK), house, hx(C_GRID), hx(C_INK), imp, hx(C_INK), exp, hx(C_PINK),
                        house > 0 ? (int)lroundf(fmaxf(0, 1 - imp / house) * 100) : 0);
}

void setRanges(int history, int money, int flow) {
  auto pick = [](Seg& s, int v, int count) {
    for (int i = 0; i < count; i++)
      if (s.values[i] == v) {
        lv_btnmatrix_set_btn_ctrl(s.obj, i, LV_BTNMATRIX_CTRL_CHECKED);
        s.value = v;
        s.changed = true;
      }
  };
  pick(hRange, history, 3);
  pick(mRange, money, 2);
  pick(fRange, flow, 3);
}

}  // namespace ui
