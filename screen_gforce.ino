#include <lvgl.h>
#include <math.h>
#include "shared_state.h"

// Stripped back to just a wire grid with a centered x/y axis - a plain
// reference frame, no data plotted on it yet. update_gforce_screen() is a
// no-op for now since there's nothing dynamic on screen.

#define GRID_MIN 10
#define GRID_MAX 230
#define GRID_STEP 30
#define GRID_CENTER 120

// lv_line_set_points() stores a pointer to the points array, not a copy -
// each line needs its own persistent (heap) storage, not a shared static
// array reused across loop iterations.
static void add_line(lv_obj_t *parent, int x1, int y1, int x2, int y2, lv_color_t color, int width) {
  lv_point_t *pts = new lv_point_t[2];
  pts[0].x = x1; pts[0].y = y1;
  pts[1].x = x2; pts[1].y = y2;
  lv_style_t *style = new lv_style_t();
  lv_style_init(style);
  lv_style_set_line_color(style, color);
  lv_style_set_line_width(style, width);
  lv_obj_t *line = lv_line_create(parent);
  lv_line_set_points(line, pts, 2);
  lv_obj_add_style(line, style, 0);
}

void build_gforce_screen(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  lv_color_t gridColor = lv_color_make(40, 45, 52);

  // Faint wire grid, skipping the center line (drawn separately, bolder)
  for (int pos = GRID_MIN; pos <= GRID_MAX; pos += GRID_STEP) {
    if (pos == GRID_CENTER) continue;
    add_line(parent, pos, GRID_MIN, pos, GRID_MAX, gridColor, 1);
    add_line(parent, GRID_MIN, pos, GRID_MAX, pos, gridColor, 1);
  }

  // Centered x/y axis, bolder and brighter than the grid
  lv_color_t axisColor = lv_color_make(90, 100, 110);
  add_line(parent, GRID_MIN, GRID_CENTER, GRID_MAX, GRID_CENTER, axisColor, 2);
  add_line(parent, GRID_CENTER, GRID_MIN, GRID_CENTER, GRID_MAX, axisColor, 2);
}

void update_gforce_screen() {
  // Nothing dynamic yet - just the reference grid for now.
}
