// Headless LVGL "viewer" for the HUD project's screen files. Renders each
// screen once (using the same build_xxx_screen()/update_xxx_screen() code
// that runs on the real hardware) and dumps it to a BMP image, so screen
// layout/color changes can be checked without flashing a physical board.
//
// Not a live simulator - no window, no input, no BLE. Just: build the
// screen, let LVGL render it into an offscreen buffer, save that buffer as
// an image. Re-run after every edit.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include "lvgl.h"
#include "shared_state.h"

#define SCREEN_W 240
#define SCREEN_H 240

// Real hardware defines this in ble_obd.ino; the sim provides its own with
// representative demo values so the screens have something to show instead
// of all-default placeholders.
OBDState obdState;

static uint16_t framebuffer[SCREEN_W * SCREEN_H];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      framebuffer[y * SCREEN_W + x] = color_p->full;
      color_p++;
    }
  }
  lv_disp_flush_ready(drv);
}

// Minimal 24-bit BMP writer - no external dependencies. BMP rows are
// bottom-up and BGR order; 240*3=720 bytes/row is already a multiple of 4
// so no row padding is needed.
static void write_bmp(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "failed to open %s for writing\n", path);
    return;
  }
  uint32_t rowSize = SCREEN_W * 3;
  uint32_t pixelDataSize = rowSize * SCREEN_H;
  uint32_t fileSize = 54 + pixelDataSize;

  uint8_t fileHeader[14] = {
    'B', 'M',
    (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
    0, 0, 0, 0,
    54, 0, 0, 0
  };
  uint8_t infoHeader[40] = { 40, 0, 0, 0 };
  int32_t w = SCREEN_W, h = SCREEN_H;
  memcpy(&infoHeader[4], &w, 4);
  memcpy(&infoHeader[8], &h, 4);
  infoHeader[12] = 1; infoHeader[13] = 0;   // planes = 1
  infoHeader[14] = 24; infoHeader[15] = 0;  // bits per pixel
  memcpy(&infoHeader[20], &pixelDataSize, 4);

  fwrite(fileHeader, 1, 14, f);
  fwrite(infoHeader, 1, 40, f);

  for (int y = SCREEN_H - 1; y >= 0; y--) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t px = framebuffer[y * SCREEN_W + x];
      // RGB565 -> RGB888
      uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((px >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (px & 0x1F) * 255 / 31;
      uint8_t bgr[3] = { b, g, r };
      fwrite(bgr, 1, 3, f);
    }
  }
  fclose(f);
  printf("wrote %s\n", path);
}

// Declared in the real screen_*.ino files - link against them unmodified.
void build_tire_screen(lv_obj_t *parent);
void build_oil_screen(lv_obj_t *parent);
void build_gforce_screen(lv_obj_t *parent);

static void render_screen(const char *name, void (*build_fn)(lv_obj_t *)) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_size(scr, SCREEN_W, SCREEN_H);
  lv_scr_load(scr);
  build_fn(scr);
  // lv_scr_load() goes through LVGL's animation subsystem even for an
  // instant (0ms) transition - the actual "swap active screen" step only
  // happens inside the animation's ready callback, which only fires once
  // LVGL's tick counter has advanced. Without this, every render after the
  // first would silently keep showing the previously loaded screen.
  for (int i = 0; i < 10; i++) {
    lv_tick_inc(16);
    lv_timer_handler();
  }
  char path[256];
  snprintf(path, sizeof(path), "output/%s.bmp", name);
  write_bmp(path);
  // Deliberately not deleting `scr` - lv_scr_load() for the next screen
  // still references the previously active one internally, and this is a
  // one-shot render-and-exit tool so there's nothing to clean up for.
}

int main() {
  // Representative demo values so the screens aren't all "--" placeholders.
  obdState.connected = true;
  obdState.oilTempC = 97.0f;
  obdState.tireTempFL_C = 68.0f;
  obdState.tireTempFR_C = 74.0f;
  obdState.tireTempRL_C = 91.0f;
  obdState.tireTempRR_C = 55.0f;
  obdState.gForceX = 0.42f;
  obdState.gForceY = -0.18f;
  obdState.yawRateRaw = 12.0f;
  obdState.vehicleSpeedKmh = 96.0f;

  lv_init();

  static lv_disp_draw_buf_t draw_buf;
  static lv_color_t buf1[SCREEN_W * 10];
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  render_screen("tire", build_tire_screen);
  render_screen("oil", build_oil_screen);
  render_screen("gforce", build_gforce_screen);

  return 0;
}
