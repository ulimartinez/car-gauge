#pragma once
#include <lvgl.h>

// Pulled out of screen_tire.ino and included from HUD.ino too - Arduino's
// build auto-generates function prototypes and hoists them above all
// sketch content, so a struct type used in a function signature has to be
// visible that early or the hoisted prototype fails to compile (same class
// of issue as the NimBLEDevice.h include in HUD.ino).

struct QuadrantLabel {
  lv_obj_t *main = nullptr;
  lv_obj_t *shadow = nullptr;
};

// Each quadrant ring is 2 stacked arcs: a wide, low-opacity "halo" behind a
// thin, fully-opaque "core" - the halo fakes a glow since LVGL's arc
// indicator has no native shadow/blur support.
struct QuadrantArc {
  lv_obj_t *halo = nullptr;
  lv_obj_t *core = nullptr;
};
