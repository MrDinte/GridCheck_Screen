#!/bin/sh
# Builds a PC program that renders the firmware's screens to PNG (needs gcc, python3 + pillow).
#   sh sim/build.sh && sh sim/render.sh
set -e
cd "$(dirname "$0")"
LV=../.pio/libdeps/waveshare_7b/lvgl
mkdir -p build
FLAGS="-O1 -DLV_CONF_INCLUDE_SIMPLE -I. -I$LV -I$LV/src -I../src"
for f in $(find $LV/src -name '*.c'); do
  o=build/$(echo "$f" | md5sum | cut -c1-12).o
  [ -f "$o" ] || gcc $FLAGS -c "$f" -o "$o"
done
for f in ../src/fonts/*.c; do gcc $FLAGS -c "$f" -o build/$(basename "$f" .c).o; done
g++ -std=c++17 $FLAGS -c ../src/ui.cpp -o build/ui.o
g++ -std=c++17 $FLAGS -c ../src/ui_pages.cpp -o build/ui_pages.o
g++ -std=c++17 $FLAGS -c ../src/tariff.cpp -o build/tariff.o
g++ -std=c++17 $FLAGS -c ../src/brands.cpp -o build/brands.o
g++ -std=c++17 $FLAGS -c sim_main.cpp -o build/sim_main.o
g++ build/*.o -o build/render -lm
echo "built sim/build/render"
