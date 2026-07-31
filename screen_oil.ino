#include <lvgl.h>
#include <math.h>
#include "shared_state.h"

// Oil: DID 0x1310 @ 0x7E0, (raw/100)-40 degC - very high confidence,
// triple-confirmed across independent sources (see docs/findings.md).
//
// Layout: single large bold centered number, colored by range
// (yellow=normal, orange=warm, red=hot). Thresholds are rough guesses -
// adjust once you have real track-temp reference points.

LV_FONT_DECLARE(lv_font_montserrat_48);

static lv_obj_t *oil_temp_value_lbl = nullptr;

#define OIL_YELLOW_MAX_C 90.0f
#define OIL_ORANGE_MAX_C 110.0f

static lv_color_t oilColor(float c) {
  if (isnan(c)) return lv_color_make(90, 95, 100);               // gray = no data
  if (c <= OIL_YELLOW_MAX_C) return lv_color_make(230, 200, 40);  // yellow
  if (c <= OIL_ORANGE_MAX_C) return lv_color_make(235, 140, 40);  // orange
  return lv_color_make(230, 60, 50);                              // red
}

void build_oil_screen(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  oil_temp_value_lbl = lv_label_create(parent);
  lv_label_set_text(oil_temp_value_lbl, "--");
  lv_obj_center(oil_temp_value_lbl);
  lv_obj_set_style_text_color(oil_temp_value_lbl, lv_color_make(90, 95, 100), 0);
  lv_obj_set_style_text_font(oil_temp_value_lbl, &lv_font_montserrat_48, 0);

  update_oil_screen();
}

void update_oil_screen() {
  if (oil_temp_value_lbl == nullptr) return;

  float oc = obdState.oilTempC;
  char buf[16];
  if (isnan(oc)) {
    snprintf(buf, sizeof(buf), "--");
  } else {
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0" "C", oc);
  }
  lv_label_set_text(oil_temp_value_lbl, buf);
  lv_obj_set_style_text_color(oil_temp_value_lbl, oilColor(oc), 0);
  lv_obj_center(oil_temp_value_lbl);
}
