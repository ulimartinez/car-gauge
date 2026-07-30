#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <NimBLEDevice.h>  // included here too (not just ble_obd.ino) so Arduino's
                            // auto-generated function prototypes - which get hoisted
                            // above per-file includes - can resolve NimBLE types
#include "shared_state.h"
#include "tire_types.h"  // same hoisting reason as NimBLEDevice.h above

// --- HARDWARE PIN DEFINITIONS (from the CrowPanel 1.28" rotary display, and
//     matching your proven sketch_jul14a reference) ---
#define ENCODER_A_PIN        45
#define ENCODER_B_PIN        42
#define SWITCH_PIN           41
#define POWER_LIGHT_PIN      40
#define SCREEN_BACKLIGHT_PIN 46

// --- SCREENS ---
// 0 = tire temps, 1 = oil temp, 2 = g-force. (No theme palette / diag screens.)
const int TOTAL_SCREENS = 3;
volatile int currentScreenID = 0;
volatile int8_t rotationDirection = 0;
int lastRenderedScreenID = 0;

// --- YOUR EXACT WORKING LOVYANGFX DEVICE CLASS (unchanged) ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 10;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc = 3;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 9;
      cfg.pin_rst = 14;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 240;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// Forward declarations from display.ino
void update_sliding_window(int currentID, int dir);
void refresh_active_screen(int activeID);
void update_ble_status_indicator(bool connected);

// From ble_obd.ino
bool obd_begin();
void obd_poll_tick(int activeScreenID);
bool obd_consume_cancel_press_event();

// High-performance DMA display flush bridge
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  if (lcd.getStartCount() > 0) {
    lcd.endWrite();
  }
  lcd.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::rgb565_t *)&color_p->full);
  lv_disp_flush_ready(disp_drv);
}

void IRAM_ATTR encoderISR() {
  static uint8_t lastStateCLK = 0;
  uint8_t currentStateCLK = digitalRead(ENCODER_A_PIN);
  if (currentStateCLK != lastStateCLK && currentStateCLK == HIGH) {
    rotationDirection = (digitalRead(ENCODER_B_PIN) != currentStateCLK) ? 1 : -1;
  }
  lastStateCLK = currentStateCLK;
}

static void advanceScreen(int dir, const char *source) {
  int prevID = currentScreenID;
  if (dir >= 0) {
    currentScreenID = (currentScreenID + 1) % TOTAL_SCREENS;
  } else {
    currentScreenID = (currentScreenID - 1 + TOTAL_SCREENS) % TOTAL_SCREENS;
  }
  Serial.printf("[%s] -> Screen %d\n", source, currentScreenID);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- HUD booting ---");

  // 1. Wake up hardware power regulators safely
  pinMode(POWER_LIGHT_PIN, OUTPUT);
  digitalWrite(POWER_LIGHT_PIN, LOW);
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  delay(50);

  // 2. Configure rotary controls (manual screen switching still works as a
  //    fallback / for bench testing away from the car)
  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), encoderISR, CHANGE);

  // 3. Fire up display panel
  lcd.init();
  lcd.initDMA();
  lcd.startWrite();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);

  // 4. Initialize LVGL with dual-ping-pong DMA layout buffers
  lv_init();
  static lv_disp_draw_buf_t draw_buf;
  static lv_color_t buf1[240 * 10];
  static lv_color_t buf2[240 * 10];
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 240 * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // 5. Turn on screen backlight
  ledcSetup(0, 5000, 8);
  ledcAttachPin(SCREEN_BACKLIGHT_PIN, 0);
  ledcWrite(0, 255);
  delay(50);

  // 6. Bootstrap screen 0 so something is on screen while BLE connects
  update_sliding_window(currentScreenID, 0);
  lv_timer_handler();

  // 7. Connect to the Veepeak OBD adapter. Retries internally up to 3x, then
  //    gives up quietly - the UI stays fully functional (with stale/blank
  //    readings) even if the car isn't there yet (bench testing) or the
  //    adapter isn't plugged in.
  obd_begin();
  update_ble_status_indicator(obdState.connected);
}

void loop() {
  // 1. LVGL render clock (no delays here)
  static uint32_t lastTick = millis();
  uint32_t currentMillis = millis();
  lv_tick_inc(currentMillis - lastTick);
  lastTick = currentMillis;
  lv_timer_handler();

  // 2. Poll OBD data - only what the active screen needs, cancel switch always
  obd_poll_tick(currentScreenID);
  static bool lastConnState = false;
  if (obdState.connected != lastConnState) {
    lastConnState = obdState.connected;
    update_ble_status_indicator(lastConnState);
  }

  // 3. Cancel-cruise button press -> advance to next screen (edge-triggered,
  //    already debounced/release-gated inside ble_obd.ino)
  if (obd_consume_cancel_press_event()) {
    advanceScreen(1, "cancel_sw");
  }

  // 4. Manual encoder input (bench-test / parked convenience)
  if (rotationDirection != 0) {
    int8_t dir = rotationDirection;
    rotationDirection = 0;
    Serial.printf("[encoder] rotated, dir=%d\n", dir);
    advanceScreen(dir, "encoder");
  }

  // 5. Handle viewport repositioning + live data refresh for the active screen
  if (currentScreenID != lastRenderedScreenID) {
    int travelDirection = (currentScreenID > lastRenderedScreenID) ? 1 : -1;
    if (lastRenderedScreenID == 0 && currentScreenID == TOTAL_SCREENS - 1) travelDirection = -1;
    if (lastRenderedScreenID == TOTAL_SCREENS - 1 && currentScreenID == 0) travelDirection = 1;

    update_sliding_window(currentScreenID, travelDirection);
    lastRenderedScreenID = currentScreenID;
  }

  refresh_active_screen(currentScreenID);
}
