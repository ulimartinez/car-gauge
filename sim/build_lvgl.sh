#!/bin/bash
# Compiles the vendored LVGL 8.3.11 source (same copy the real Arduino build
# uses) into a static library. Slow (~190 files) but only needs to be re-run
# if LVGL itself or lv_conf.h changes - not on every screen-file edit.
set -e
cd "$(dirname "$0")"

LVGL_DIR="/Users/dateutli/Documents/Arduino/libraries/lvgl"
OBJ_DIR="build/lvgl_obj"
mkdir -p "$OBJ_DIR"

echo "Compiling LVGL sources (one-time, this takes a bit)..."
count=0
for src in $(find "$LVGL_DIR/src" -name '*.c'); do
  rel="${src#$LVGL_DIR/src/}"
  out="$OBJ_DIR/$(echo "$rel" | tr '/' '_').o"
  clang -std=c99 -I"$LVGL_DIR" -DLV_CONF_PATH="/Users/dateutli/Documents/Arduino/libraries/lv_conf.h" -c "$src" -o "$out"
  count=$((count+1))
done
echo "Compiled $count LVGL source files."

ar rcs build/liblvgl.a "$OBJ_DIR"/*.o
echo "-> build/liblvgl.a"
