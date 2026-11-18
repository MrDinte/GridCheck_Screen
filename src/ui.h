// The on-screen dashboard (LVGL). Swipe left/right between pages.
#pragma once

namespace ui {
void create();
/** Refresh values from liveGet(); call a couple of times a second. */
void update();
/** Jump to a page (0 = Now, 1 = Settings & info). */
void showPage(int page);
/** Turn the knob: + clockwise (next page), - anticlockwise. The 7" has no knob. */
void knob(int clicks);
/** Rebuild every page (after the colours change). */
void rebuild();
/** Open the power prices editor (from Settings & info; also used by the PC renderer). */
void openPrices();
/** Choose the History / Money / Energy flow ranges (days); used by the PC renderer. */
void setRanges(int history, int money, int flow);
}  // namespace ui
