#!/bin/sh
# Builds a PC program that renders the firmware's screens to PNG (needs gcc, python3 + pillow).
#   sh sim/build.sh && sh sim/render.sh                         7" screen  -> build/render
#   BOARD=knob18 sh sim/build.sh && sh sim/render_round.sh      1.8" round -> build/render_round
set -e
cd "$(dirname "$0")"
LV=../.pio/libdeps/waveshare_7b/lvgl
[ -d "$LV" ] || LV=../.pio/libdeps/knob18/lvgl
BOARD=${BOARD:-7b}
mkdir -p build/lvgl build/app-$BOARD
VER=$(sed -n 's/.*-DEH_VERSION=\\"\([^\\]*\)\\".*/\1/p' ../platformio.ini | head -1)
FLAGS="-O1 -DLV_CONF_INCLUDE_SIMPLE -DEH_VERSION=\"$VER\" -I. -I$LV -I$LV/src -I../src"
for f in $(find $LV/src -name '*.c'); do
  o=build/lvgl/$(echo "$f" | md5sum | cut -c1-12).o
  [ -f "$o" ] || gcc $FLAGS -c "$f" -o "$o"
done
for f in ../src/fonts/*.c; do gcc $FLAGS -c "$f" -o build/lvgl/$(basename "$f" .c).o; done
A=build/app-$BOARD
if [ "$BOARD" = knob18 ]; then
  FLAGS="$FLAGS -DGC_KNOB18"
  UI="ui_round"
  OUT=build/render_round
else
  UI="ui ui_pages"
  OUT=build/render
fi
rm -f $A/*.o
for u in $UI ui_common tariff; do g++ -std=c++17 $FLAGS -c ../src/$u.cpp -o $A/$u.o; done
g++ -std=c++17 $FLAGS -c sim_main.cpp -o $A/sim_main.o
g++ build/lvgl/*.o $A/*.o -o $OUT -lm
echo "built sim/$OUT"
