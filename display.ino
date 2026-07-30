#include <lvgl.h>

// All 3 screens are plain LVGL widgets (labels/lines) now - no canvas
// buffers needed anywhere in this project.

LV_FONT_DECLARE(lv_font_montserrat_14);

// --- SCREEN MODULE LINKS ---
void build_tire_screen(lv_obj_t *parent);
void build_oil_screen(lv_obj_t *parent);
void build_gforce_screen(lv_obj_t *parent);
void update_tire_screen();
void update_oil_screen();
void update_gforce_screen();

// One permanent root canvas screen that never gets deleted
static lv_obj_t *root_view_port = nullptr;

// One permanent floating page-indicator container above the sliding layouts.
// 3 screen dots + 1 BLE-connection-status dot, separated with a visible gap
// (matches the reference mockup).
static lv_obj_t *floating_indicator = nullptr;
static lv_obj_t *indicator_dots[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *ble_status_dot = nullptr;

// Sub-containers for the 3 screens: 0=tire, 1=oil, 2=gforce
static lv_obj_t *layout_views[3] = { nullptr, nullptr, nullptr };

#define DOT_SPACING   16
#define DOT_GAP       14   // extra separation before the BLE status dot

void build_global_page_indicator() {
  if (floating_indicator != nullptr) return;

  floating_indicator = lv_obj_create(root_view_port);
  lv_obj_set_size(floating_indicator, 100, 25);
  lv_obj_align(floating_indicator, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_opa(floating_indicator, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(floating_indicator, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(floating_indicator, LV_SCROLLBAR_MODE_OFF);
  lv_obj_move_to_index(floating_indicator, -1);

  // Screen dots centered slightly left to leave room for the separated BLE dot
  int baseX = -14;
  for (int i = 0; i < 3; i++) {
    indicator_dots[i] = lv_obj_create(floating_indicator);
    lv_obj_set_size(indicator_dots[i], 8, 8);
    lv_obj_align(indicator_dots[i], LV_ALIGN_CENTER, baseX + (i - 1) * DOT_SPACING, 0);
    lv_obj_set_style_radius(indicator_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(indicator_dots[i], lv_color_make(80, 80, 80), 0);
  }

  // Separated BLE connection status dot
  ble_status_dot = lv_obj_create(floating_indicator);
  lv_obj_set_size(ble_status_dot, 8, 8);
  lv_obj_align(ble_status_dot, LV_ALIGN_CENTER, baseX + DOT_SPACING + DOT_GAP, 0);
  lv_obj_set_style_radius(ble_status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ble_status_dot, lv_color_make(120, 30, 30), 0);  // starts "disconnected" red
}

void update_ble_status_indicator(bool connected) {
  if (ble_status_dot == nullptr) return;
  lv_obj_set_style_bg_color(ble_status_dot,
    connected ? lv_color_make(40, 180, 220) : lv_color_make(120, 30, 30), 0);
}

void update_indicator_highlight(int activeID) {
  for (int i = 0; i < 3; i++) {
    if (indicator_dots[i] == nullptr) continue;
    if (i == activeID) {
      lv_obj_set_style_bg_color(indicator_dots[i], lv_color_white(), 0);
    } else {
      lv_obj_set_style_bg_color(indicator_dots[i], lv_color_make(80, 80, 80), 0);
    }
  }
}

void build_single_screen(int id) {
  if (layout_views[id] != nullptr) return;

  layout_views[id] = lv_obj_create(root_view_port);
  lv_obj_set_size(layout_views[id], 240, 240);
  lv_obj_center(layout_views[id]);
  lv_obj_set_scrollbar_mode(layout_views[id], LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_width(layout_views[id], 0, 0);
  lv_obj_set_style_radius(layout_views[id], 0, 0);

  if (id == 0) {
    build_tire_screen(layout_views[id]);
  } else if (id == 1) {
    build_oil_screen(layout_views[id]);
  } else if (id == 2) {
    build_gforce_screen(layout_views[id]);
  }
}

// Called every loop() with whatever screen is currently active - pushes the
// latest polled OBD values into that screen's widgets. Cheap no-op if the
// screen hasn't been built yet.
void refresh_active_screen(int activeID) {
  if (layout_views[activeID] == nullptr) return;
  switch (activeID) {
    case 0: update_tire_screen(); break;
    case 1: update_oil_screen(); break;
    case 2: update_gforce_screen(); break;
  }
}

void update_sliding_window(int currentID, int dir) {
  int total = 3;
  int prevID = (currentID - 1 + total) % total;
  int nextID = (currentID + 1) % total;

  if (root_view_port == nullptr) {
    root_view_port = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root_view_port, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_view_port, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(root_view_port, LV_SCROLLBAR_MODE_OFF);
    lv_scr_load(root_view_port);
  }

  build_global_page_indicator();

  build_single_screen(currentID);
  build_single_screen(prevID);
  build_single_screen(nextID);

  if (floating_indicator != nullptr) {
    lv_obj_move_to_index(floating_indicator, -1);
  }

  for (int i = 0; i < total; i++) {
    if (layout_views[i] != nullptr) {
      if (i == currentID || i == prevID || i == nextID) {
        lv_obj_clear_flag(layout_views[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(layout_views[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  lv_anim_del(layout_views[currentID], NULL);
  if (layout_views[prevID] != nullptr) lv_anim_del(layout_views[prevID], NULL);
  if (layout_views[nextID] != nullptr) lv_anim_del(layout_views[nextID], NULL);

  if (dir == 0) {
    if (layout_views[prevID] != nullptr) lv_obj_set_x(layout_views[prevID], -240);
    if (layout_views[currentID] != nullptr) lv_obj_set_x(layout_views[currentID], 0);
    if (layout_views[nextID] != nullptr) lv_obj_set_x(layout_views[nextID], 240);
  } else if (dir == 1) {
    if (layout_views[nextID] != nullptr) lv_obj_set_x(layout_views[nextID], 240);

    lv_anim_t a_curr, a_prev;
    lv_anim_init(&a_curr);
    lv_anim_set_var(&a_curr, layout_views[currentID]);
    lv_anim_set_values(&a_curr, lv_obj_get_x(layout_views[currentID]), 0);
    lv_anim_set_time(&a_curr, 200);
    lv_anim_set_exec_cb(&a_curr, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_path_cb(&a_curr, lv_anim_path_ease_out);
    lv_anim_start(&a_curr);

    if (layout_views[prevID] != nullptr) {
      lv_anim_init(&a_prev);
      lv_anim_set_var(&a_prev, layout_views[prevID]);
      lv_anim_set_values(&a_prev, lv_obj_get_x(layout_views[prevID]), -240);
      lv_anim_set_time(&a_prev, 200);
      lv_anim_set_exec_cb(&a_prev, (lv_anim_exec_xcb_t)lv_obj_set_x);
      lv_anim_set_path_cb(&a_prev, lv_anim_path_ease_out);
      lv_anim_start(&a_prev);
    }
  } else if (dir == -1) {
    if (layout_views[prevID] != nullptr) lv_obj_set_x(layout_views[prevID], -240);

    lv_anim_t a_curr, a_next;
    lv_anim_init(&a_curr);
    lv_anim_set_var(&a_curr, layout_views[currentID]);
    lv_anim_set_values(&a_curr, lv_obj_get_x(layout_views[currentID]), 0);
    lv_anim_set_time(&a_curr, 200);
    lv_anim_set_exec_cb(&a_curr, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_path_cb(&a_curr, lv_anim_path_ease_out);
    lv_anim_start(&a_curr);

    if (layout_views[nextID] != nullptr) {
      lv_anim_init(&a_next);
      lv_anim_set_var(&a_next, layout_views[nextID]);
      lv_anim_set_values(&a_next, lv_obj_get_x(layout_views[nextID]), 240);
      lv_anim_set_time(&a_next, 200);
      lv_anim_set_exec_cb(&a_next, (lv_anim_exec_xcb_t)lv_obj_set_x);
      lv_anim_set_path_cb(&a_next, lv_anim_path_ease_out);
      lv_anim_start(&a_next);
    }
  }

  update_indicator_highlight(currentID);

  for (int i = 0; i < total; i++) {
    if (i != currentID && i != prevID && i != nextID) {
      if (layout_views[i] != nullptr) {
        lv_obj_del(layout_views[i]);
        layout_views[i] = nullptr;
      }
    }
  }
}
