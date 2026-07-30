#pragma once
#include <math.h>

// Shared OBD telemetry state, written by ble_obd.ino, read by the screen
// modules. One definition, included everywhere it's needed.
struct OBDState {
  bool connected = false;
  float oilTempC = NAN;
  float vehicleSpeedKmh = NAN;  // standard Mode 01 PID 0x0D, for the g-force screen's speed readout
  float tireTempFL_C = NAN;
  float tireTempFR_C = NAN;
  float tireTempRL_C = NAN;
  float tireTempRR_C = NAN;
  float gForceX = 0.0f;
  float gForceY = 0.0f;
  float yawRateRaw = 0.0f;
};

extern OBDState obdState;
