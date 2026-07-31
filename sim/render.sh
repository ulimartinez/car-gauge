#!/bin/bash
# Fast path: compiles main.cpp + the 3 real screen_*.ino files, links
# against the prebuilt liblvgl.a, and runs the result to produce
# output/{tire,oil,gforce}.bmp. Run this after every screen-file edit.
set -e
cd "$(dirname "$0")"

if [ ! -f build/liblvgl.a ]; then
  echo "build/liblvgl.a not found - running build_lvgl.sh first..."
  ./build_lvgl.sh
fi

LVGL_DIR="/Users/dateutli/Documents/Arduino/libraries/lvgl"
mkdir -p output

clang++ -std=c++14 \
  -I"$LVGL_DIR" -I.. \
  -DLV_CONF_PATH="/Users/dateutli/Documents/Arduino/libraries/lv_conf.h" \
  -include compat.h \
  -x c++ main.cpp \
  -x c++ ../screen_tire.ino \
  -x c++ ../screen_oil.ino \
  -x c++ ../screen_gforce.ino \
  -x none build/liblvgl.a \
  -o build/car_gauge_sim

./build/car_gauge_sim
