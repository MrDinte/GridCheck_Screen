"""Builds include/lv_conf.h from LVGL's template with GridCheck's settings.

    python3 tools/make_lv_conf.py
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
src = (root / '.pio/libdeps/waveshare_7b/lvgl/lv_conf_template.h').read_text()

src = src.replace('#if 0 /*Set it to "1" to enable content*/', '#if 1 /*Set it to "1" to enable content*/', 1)


def setdef(name, value):
    global src
    pat = re.compile(r'^(\s*#define\s+' + re.escape(name) + r')\s+.*$', re.M)
    assert pat.search(src), name
    src = pat.sub(lambda m: f'{m.group(1)} {value}', src, count=1)


setdef('LV_COLOR_DEPTH', '16')
setdef('LV_COLOR_16_SWAP', '0')
setdef('LV_MEM_CUSTOM', '1')          # use the system heap (large blocks land in PSRAM)
setdef('LV_TICK_CUSTOM', '1')
setdef('LV_TICK_CUSTOM_INCLUDE', '"esp_timer.h"')
setdef('LV_TICK_CUSTOM_SYS_TIME_EXPR', '((uint32_t)(esp_timer_get_time() / 1000LL))')
setdef('LV_DISP_DEF_REFR_PERIOD', '25')
setdef('LV_INDEV_DEF_READ_PERIOD', '25')
setdef('LV_DPI_DEF', '160')
setdef('LV_USE_LOG', '0')
setdef('LV_SPRINTF_USE_FLOAT', '1')  # allow %.1f in label text
setdef('LV_USE_PERF_MONITOR', '0')
setdef('LV_FONT_DEFAULT', '&lv_font_montserrat_20')
for size in (14, 16, 18, 20, 22, 24, 28, 32, 40, 48):
    setdef(f'LV_FONT_MONTSERRAT_{size}', '1')
setdef('LV_FONT_CUSTOM_DECLARE', 'LV_FONT_DECLARE(eh_num_76) LV_FONT_DECLARE(eh_num_56) LV_FONT_DECLARE(eh_icons_44)')
setdef('LV_THEME_DEFAULT_DARK', '1')
setdef('LV_BUILD_EXAMPLES', '0')
setdef('LV_MEMCPY_MEMSET_STD', '1')
setdef('LV_USE_QRCODE', '1')         # the round screen's "Change Wi-Fi" QR code

(root / 'include').mkdir(exist_ok=True)
(root / 'include/lv_conf.h').write_text(src)
print('include/lv_conf.h written')
