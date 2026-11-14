#!/bin/sh
# Regenerates the large number fonts and the icon font used by the firmware UI.
# Needs: npm install lv_font_conv   (run from a folder where it is installed, or pass its path)
#
#   sh tools/make_fonts.sh /path/to/node_modules/.bin/lv_font_conv
set -e
CONV="${1:-lv_font_conv}"
cd "$(dirname "$0")/.."
SRC=.pio/libdeps/waveshare_7b/lvgl/scripts/built_in_font
OUT=src/fonts
mkdir -p "$OUT"

# Big numbers: digits, space, $ % - . ~ and the en dash used for "no value".
NUM="0x20,0x24-0x25,0x2D-0x2E,0x30-0x39,0x7E,0x2013"
"$CONV" --size 76 --bpp 4 --format lvgl --no-compress --font "$SRC/Montserrat-Medium.ttf" -r "$NUM" --lv-include lvgl.h -o "$OUT/eh_num_76.c"
"$CONV" --size 56 --bpp 4 --format lvgl --no-compress --font "$SRC/Montserrat-Medium.ttf" -r "$NUM" --lv-include lvgl.h -o "$OUT/eh_num_56.c"

# Icons (Font Awesome 5 Free): sun, home, bolt, battery, solar panel, plug, cog, tower.
"$CONV" --size 44 --bpp 4 --format lvgl --no-compress --font "$SRC/FontAwesome5-Solid+Brands+Regular.woff" \
  -r 0xf185,0xf015,0xf0e7,0xf240,0xf5ba,0xf1e6,0xf013,0xf519 --lv-include lvgl.h -o "$OUT/eh_icons_44.c"
echo "fonts written to $OUT"
