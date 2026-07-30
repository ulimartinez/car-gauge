#include <lvgl.h>
#include <math.h>
#include "shared_state.h"
#include "tire_types.h"

// Confidence: FL/FR (D926/D927) are high-confidence confirmed front tire
// temps. RL/RR (D922/D923) are UNCONFIRMED which physical corner they
// actually are - see docs/findings.md and docs/ble-capture/README.md in the
// broadcaster repo. Values will still display, just don't trust the L/R
// labeling on the rear pair yet.
//
// Layout: just 4 large bold numbers, one per quadrant, colored by temp
// threshold - no rings/arcs/wheel graphic.
//
// QuadrantLabel/QuadrantArc struct defs live in tire_types.h, not here -
// see that file for why (Arduino prototype-hoisting gotcha).

LV_FONT_DECLARE(lv_font_montserrat_28);

static QuadrantLabel q_fl, q_fr, q_rl, q_rr;
static QuadrantArc arc_fl, arc_fr, arc_rl, arc_rr;

#define TIRE_GOOD_MAX_C   70.0f
#define TIRE_WARM_MAX_C   90.0f

static lv_color_t tempColor(float c) {
  if (isnan(c)) return lv_color_make(90, 95, 100);              // gray = no data
  if (c <= TIRE_GOOD_MAX_C) return lv_color_make(51, 180, 110); // green
  if (c <= TIRE_WARM_MAX_C) return lv_color_make(230, 180, 40); // amber
  return lv_color_make(230, 60, 50);                            // red
}

static lv_obj_t* create_label(lv_obj_t *parent, int x, int y) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "--\xC2\xB0" "C");
  lv_obj_align(lbl, LV_ALIGN_CENTER, x, y);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  return lbl;
}

static QuadrantLabel create_quadrant_label(lv_obj_t *parent, int x, int y) {
  QuadrantLabel q;
  q.shadow = create_label(parent, x + 1, y + 1);
  q.main = create_label(parent, x, y);
  return q;
}

// Single arc layer. Spans the full 0-90 quadrant edge-to-edge so its flat
// (non-rounded) end caps are cut radially right at 0/90/180/270 - i.e.
// parallel to whichever crosshair line crosses that exact point. The
// crosshair is drawn on top afterwards, which is what actually creates the
// visible break between adjacent quadrants rather than an angular gap.
static lv_obj_t* create_arc_layer(lv_obj_t *parent, int startAngle, int endAngle, int width, lv_opa_t opa) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 220, 220);
  lv_obj_center(arc);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_make(90, 95, 100), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(arc, startAngle, endAngle);
  lv_arc_set_angles(arc, startAngle, endAngle);
  lv_obj_set_scrollbar_mode(arc, LV_SCROLLBAR_MODE_OFF);
  return arc;
}

static QuadrantArc create_quadrant_arc(lv_obj_t *parent, int startAngle, int endAngle) {
  QuadrantArc q;
  // Halo first (wider, faint) so the core renders on top of it.
  q.halo = create_arc_layer(parent, startAngle, endAngle, 30, LV_OPA_30);
  q.core = create_arc_layer(parent, startAngle, endAngle, 14, LV_OPA_COVER);
  return q;
}

void build_tire_screen(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // Crosshair pulled in to stay entirely within the ring's inner hole
  // (halo inner edge is ~radius 80) instead of running edge-to-edge. Kept
  // clear of the ring on purpose - a straight line that's supposed to
  // touch both the ring and the true bezel edge is the most sensitive
  // possible shape for revealing the panel's small physical misalignment;
  // keeping it fully contained sidesteps that instead of fixing the
  // underlying offset.
  static lv_point_t h_pts[2] = { { 0, 108 }, { 216, 108 } };
  static lv_point_t v_pts[2] = { { 108, 0 }, { 108, 216 } };
  lv_style_t *crosshair_style = new lv_style_t();
  lv_style_init(crosshair_style);
  lv_style_set_line_color(crosshair_style, lv_color_white());
  lv_style_set_line_width(crosshair_style, 1);
  lv_obj_t *hline = lv_line_create(parent);
  lv_line_set_points(hline, h_pts, 2);
  lv_obj_add_style(hline, crosshair_style, 0);
  lv_obj_t *vline = lv_line_create(parent);
  lv_line_set_points(vline, v_pts, 2);
  lv_obj_add_style(vline, crosshair_style, 0);

  // Quadrant arcs: RR=0-90, RL=90-180, FL=180-270, FR=270-360 (LVGL angle
  // convention: 0=3 o'clock, clockwise). Each spans its full 90deg edge to
  // edge, meeting exactly at the cardinal points.
  arc_rr = create_quadrant_arc(parent, 0, 90);
  arc_rl = create_quadrant_arc(parent, 90, 180);
  arc_fl = create_quadrant_arc(parent, 180, 270);
  arc_fr = create_quadrant_arc(parent, 270, 360);

  // FL/FR/RL/RR, pulled in from the old +-60 position so they sit clear of
  // the ring's halo (inner edge now reaches roughly radius 80) while still
  // comfortably clear of the center crosshair.
  q_fl = create_quadrant_label(parent, -42, -42);
  q_fr = create_quadrant_label(parent, 42, -42);
  q_rl = create_quadrant_label(parent, -42, 42);
  q_rr = create_quadrant_label(parent, 42, 42);

  update_tire_screen();
}

static void setCorner(QuadrantLabel &q, QuadrantArc &arc, float c) {
  if (q.main == nullptr) return;
  char buf[16];
  if (isnan(c)) {
    snprintf(buf, sizeof(buf), "--\xC2\xB0" "C");
  } else {
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0" "C", c);
  }
  lv_label_set_text(q.main, buf);
  lv_label_set_text(q.shadow, buf);
  lv_color_t col = tempColor(c);
  if (arc.core) lv_obj_set_style_arc_color(arc.core, col, LV_PART_INDICATOR);
  if (arc.halo) lv_obj_set_style_arc_color(arc.halo, col, LV_PART_INDICATOR);
}

void update_tire_screen() {
  setCorner(q_fl, arc_fl, obdState.tireTempFL_C);
  setCorner(q_fr, arc_fr, obdState.tireTempFR_C);
  setCorner(q_rl, arc_rl, obdState.tireTempRL_C);
  setCorner(q_rr, arc_rr, obdState.tireTempRR_C);
}
