// ============================================================================
// ble_obd.ino — BLE connection to the Veepeak OBD Check BLE adapter, and a
// simple synchronous ELM327/UDS request-response engine on top of it.
//
// Protocol reverse-engineered from a real capture (see broadcaster repo,
// docs/ble-capture/). The Veepeak tunnels plaintext ELM327 AT-commands over
// a BLE GATT characteristic pair (one write, one notify) - no encryption at
// that layer. We skip the ELM327 auto-protocol-detect dance the phone app
// does (ATSP 1..6 trial and error) since we already know this car answers on
// protocol 6 (ISO 15765-4 CAN, 11-bit ID, 500kbps).
//
// Confirmed signals (see docs/findings.md in the broadcaster repo for the
// full writeup and confidence levels):
//   0x7E0 / DID 1310  -> engine oil temp,  (raw/100)-40 degC        [very high confidence]
//   0x7DF / PID 05 (Mode 01, standard)  -> coolant temp, A-40 degC  [high confidence, standard PID]
//   0x726 / DID D926  -> tire temp, front-left,  raw-50 degC        [high confidence]
//   0x726 / DID D927  -> tire temp, front-right, raw-50 degC        [high confidence]
//   0x726 / DID D928  -> tire temp, candidate rear corner, raw-50   [UNCONFIRMED corner]
//   0x726 / DID D929  -> tire temp, candidate rear corner, raw-50   [UNCONFIRMED corner]
//   0x720 / DID DB63  -> cruise cancel switch, nonzero = pressed    [high confidence]
//   0x760 / DID 2B0B  -> yaw rate (exists+varies; exact scale TBD)  [medium confidence]
//   0x760 / DID 2B11  -> g-force axis candidate A (scale TBD)       [medium confidence]
//   0x760 / DID 4175  -> g-force axis candidate B (scale TBD)       [medium confidence]
// ============================================================================

#include <NimBLEDevice.h>
#include "shared_state.h"

// ---- tunables ----
// Substring candidates checked (case-insensitive) against the advertised
// name. Veepeak's exact advertised name wasn't captured in our BLE sniff (we
// only captured GATT traffic, not the advertisement packet) - "VEEPEAK" is
// the best guess from the product's own documentation, "OBD" as a fallback.
// Debug scan logging is left ON by default (costs nothing but a few Serial
// lines) specifically so that if neither guess matches, you can read the
// actual advertised name straight off the Serial monitor and fix this list
// without needing to flip a flag and reflash first.
static const char* OBD_BLE_NAME_CANDIDATES[] = { "veepeak", "obd" };
// Confirmed via a real Android BLE HCI snoop capture (advertises as
// "VEEPEAK", but only rarely - it appears to stop general advertising once
// something else is connected to it, e.g. the phone app). Matched as a
// fallback alongside the name check so a missed/absent advertised name
// doesn't stop us from finding it.
static const char* OBD_KNOWN_MAC = "66:1e:87:05:cf:86";
#define OBD_DEBUG_SCAN         1
#define OBD_SCAN_SECONDS      5
#define OBD_MAX_CONNECT_RETRIES 3
#define OBD_REQUEST_TIMEOUT_MS 400

// ---- shared state other .ino files read (struct defined in shared_state.h) ----
OBDState obdState;

// Edge-triggered "cancel switch was just pressed" event, auto-clears once read.
static bool cancelSwPressedEvent = false;
bool obd_consume_cancel_press_event() {
  if (cancelSwPressedEvent) {
    cancelSwPressedEvent = false;
    return true;
  }
  return false;
}

// ---- BLE plumbing ----
static NimBLEClient* bleClient = nullptr;
static NimBLERemoteCharacteristic* writeChar = nullptr;
static NimBLERemoteCharacteristic* notifyChar = nullptr;

static volatile bool rxLineReady = false;
static String rxBuffer = "";

static void notifyCallback(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') {
      rxLineReady = true;
    } else if (c != '\r' && c != '\n') {
      rxBuffer += c;
    }
  }
}

// Finds the write/notify characteristic pair on whatever service the adapter
// exposes. We don't have a confirmed service/characteristic UUID from the
// capture (only raw ATT handles, which aren't portable across reconnects on
// every BLE stack), so we discover by capability instead: the OBD adapter is
// a simple UART-bridge-style device with exactly one write-capable and one
// notify-capable characteristic.
static bool discoverObdCharacteristics() {
  std::vector<NimBLERemoteService*>* services = bleClient->getServices(true);
  for (auto* service : *services) {
    std::vector<NimBLERemoteCharacteristic*>* chars = service->getCharacteristics(true);
    for (auto* c : *chars) {
      if (c->canWrite() || c->canWriteNoResponse()) {
        if (writeChar == nullptr) writeChar = c;
      }
      if (c->canNotify()) {
        if (notifyChar == nullptr) notifyChar = c;
      }
    }
    if (writeChar && notifyChar) return true;
  }
  return false;
}

static bool sendRawAndWait(const String& cmd, uint32_t timeoutMs) {
  if (!writeChar) return false;
  rxBuffer = "";
  rxLineReady = false;
  String withCR = cmd + "\r";
  if (!writeChar->writeValue((uint8_t*)withCR.c_str(), withCR.length(), writeChar->canWrite())) {
    return false;
  }
  uint32_t start = millis();
  while (!rxLineReady) {
    if (millis() - start > timeoutMs) return false;
    delay(2);
  }
  return true;
}

static bool obd_init_sequence() {
  const char* initCmds[] = { "ATZ", "ATE0", "ATL0", "ATS0", "ATH1", "ATSP6" };
  for (const char* cmd : initCmds) {
    if (!sendRawAndWait(cmd, 1500)) {
      Serial.printf("[OBD] init command '%s' timed out\n", cmd);
      return false;
    }
  }
  Serial.println("[OBD] init sequence complete (protocol forced to ISO 15765-4 CAN 500k)");
  return true;
}

static bool nameMatchesCandidate(std::string name) {
  for (auto &c : name) c = tolower(c);
  for (const char* candidate : OBD_BLE_NAME_CANDIDATES) {
    if (name.find(candidate) != std::string::npos) return true;
  }
  return false;
}

static bool addressMatchesKnown(std::string addr) {
  for (auto &c : addr) c = tolower(c);
  std::string known = OBD_KNOWN_MAC;
  for (auto &c : known) c = tolower(c);
  return addr == known;
}

static bool obd_scan_and_connect() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->start(OBD_SCAN_SECONDS, false);

  bool found = false;
  NimBLEAddress targetAddress;
  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice d = results.getDevice(i);
#if OBD_DEBUG_SCAN
    Serial.printf("[OBD][scan] seen: '%s' (%s)\n", d.getName().c_str(), d.getAddress().toString().c_str());
#endif
    if (nameMatchesCandidate(d.getName()) || addressMatchesKnown(d.getAddress().toString())) {
      targetAddress = d.getAddress();
      found = true;
      break;
    }
  }
  if (!found) {
    Serial.println("[OBD] no matching device found this scan");
    return false;
  }

  bleClient = NimBLEDevice::createClient();
  if (!bleClient->connect(targetAddress)) {
    Serial.println("[OBD] connect() failed");
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
    return false;
  }

  writeChar = nullptr;
  notifyChar = nullptr;
  if (!discoverObdCharacteristics()) {
    Serial.println("[OBD] could not find write+notify characteristics");
    bleClient->disconnect();
    return false;
  }
  if (notifyChar->canNotify()) {
    notifyChar->subscribe(true, notifyCallback);
  }
  return true;
}

// Retries up to OBD_MAX_CONNECT_RETRIES times, then gives up quietly - never
// hangs or crashes if the adapter isn't found (e.g. not plugged in, or the
// display is just being bench-tested away from the car). Screens fall back
// to showing stale/placeholder values when disconnected.
bool obd_begin() {
  NimBLEDevice::init("HUD");
  for (uint8_t attempt = 1; attempt <= OBD_MAX_CONNECT_RETRIES; attempt++) {
    Serial.printf("[OBD] connect attempt %d/%d...\n", attempt, OBD_MAX_CONNECT_RETRIES);
    if (obd_scan_and_connect() && obd_init_sequence()) {
      obdState.connected = true;
      Serial.println("[OBD] connected and ready");
      return true;
    }
    delay(500);
  }
  Serial.println("[OBD] giving up after max retries - running with no live data");
  obdState.connected = false;
  return false;
}

// ---- request/response helpers ----
static uint16_t currentHeader = 0xFFFF;  // force first ATSH to actually send

static bool setHeader(uint16_t header) {
  if (header == currentHeader) return true;
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "ATSH %04X", header);
  if (!sendRawAndWait(cmd, 500)) return false;
  currentHeader = header;
  return true;
}

// Sends "22<did>" under the given header, parses a positive Mode 22 response,
// returns the raw data bytes (up to 4) and how many were found. Returns false
// on timeout / negative response / malformed reply.
static bool pollDID(uint16_t header, const char* didHex, uint8_t* outData, uint8_t* outLen) {
  if (!obdState.connected) return false;
  if (!setHeader(header)) { obdState.connected = false; return false; }

  char cmd[8];
  snprintf(cmd, sizeof(cmd), "22%s", didHex);
  if (!sendRawAndWait(cmd, OBD_REQUEST_TIMEOUT_MS)) return false;

  // rxBuffer now holds something like "7280562DB630000" (header + PCI + 62 + DID + data)
  if (rxBuffer.length() < 3 + 2) return false;
  String body = rxBuffer.substring(3);  // strip the 3-hex-char response CAN ID
  if (body.length() < 2) return false;

  uint8_t pci = strtoul(body.substring(0, 2).c_str(), nullptr, 16);
  String payload = body.substring(2, 2 + pci * 2);
  if (payload.length() < 6) return false;               // need at least SID+DID
  if (payload.substring(0, 2) != "62") return false;     // not a positive Mode 22 response

  String dataHex = payload.substring(6);  // skip SID(2) + DID(4)
  uint8_t n = dataHex.length() / 2;
  if (n > 4) n = 4;
  for (uint8_t i = 0; i < n; i++) {
    outData[i] = strtoul(dataHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }
  *outLen = n;
  return true;
}

// Standard Mode 01 PID request (positive response SID 0x41, vs Mode 22's 0x62).
// Used for coolant temp (PID 0x05) - a normal SAE J1979 PID, no manufacturer
// DID needed, confirmed supported on ND3.
static bool pollPID01(uint16_t header, const char* pidHex, uint8_t* outData, uint8_t* outLen) {
  if (!obdState.connected) return false;
  if (!setHeader(header)) { obdState.connected = false; return false; }

  char cmd[8];
  snprintf(cmd, sizeof(cmd), "01%s", pidHex);
  if (!sendRawAndWait(cmd, OBD_REQUEST_TIMEOUT_MS)) return false;

  if (rxBuffer.length() < 3 + 2) return false;
  String body = rxBuffer.substring(3);
  if (body.length() < 2) return false;

  uint8_t pci = strtoul(body.substring(0, 2).c_str(), nullptr, 16);
  String payload = body.substring(2, 2 + pci * 2);
  if (payload.length() < 4) return false;
  if (payload.substring(0, 2) != "41") return false;  // not a positive Mode 01 response

  String dataHex = payload.substring(4);  // skip SID(2) + PID(2)
  uint8_t n = dataHex.length() / 2;
  if (n > 4) n = 4;
  for (uint8_t i = 0; i < n; i++) {
    outData[i] = strtoul(dataHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }
  *outLen = n;
  return true;
}

static int16_t asS16(uint8_t* d, uint8_t len) {
  if (len < 2) return 0;
  return (int16_t)((d[0] << 8) | d[1]);
}
static uint16_t asU16(uint8_t* d, uint8_t len) {
  if (len < 2) return 0;
  return (uint16_t)((d[0] << 8) | d[1]);
}
static uint8_t asU8(uint8_t* d, uint8_t len) {
  if (len < 1) return 0;
  return d[0];
}

// ---- per-signal poll functions ----
static void pollCancelSwitch() {
  uint8_t data[4], len;
  if (!pollDID(0x0720, "DB63", data, &len)) return;
  uint16_t raw = asU16(data, len);

  static bool waitingForRelease = false;
  bool pressed = (raw != 0);
  if (pressed && !waitingForRelease) {
    cancelSwPressedEvent = true;
    waitingForRelease = true;
  } else if (!pressed) {
    waitingForRelease = false;
  }
}

static void pollOilTemp() {
  uint8_t data[4], len;
  if (!pollDID(0x07E0, "1310", data, &len)) return;
  uint16_t raw = asU16(data, len);
  obdState.oilTempC = (raw / 100.0f) - 40.0f;
}

static void pollTireFL() {
  uint8_t data[4], len;
  if (!pollDID(0x0726, "D926", data, &len)) return;
  obdState.tireTempFL_C = (float)asU8(data, len) - 50.0f;
}
static void pollTireFR() {
  uint8_t data[4], len;
  if (!pollDID(0x0726, "D927", data, &len)) return;
  obdState.tireTempFR_C = (float)asU8(data, len) - 50.0f;
}
static void pollTireRL() {
  // DID corrected to D928 (was D922 - wrong DID entirely, found via a real
  // drive capture cross-referenced against a comprehensive diagnostic tool's
  // own PID list). Sequential with FL=D926/FR=D927 so RL/RR assignment here
  // is a reasonable guess, but which physical corner is still UNCONFIRMED -
  // see docs/findings.md.
  uint8_t data[4], len;
  if (!pollDID(0x0726, "D928", data, &len)) return;
  obdState.tireTempRL_C = (float)asU8(data, len) - 50.0f;
}
static void pollTireRR() {
  // DID corrected to D929 (was D923) - see pollTireRL() comment above.
  uint8_t data[4], len;
  if (!pollDID(0x0726, "D929", data, &len)) return;
  obdState.tireTempRR_C = (float)asU8(data, len) - 50.0f;
}

// TODO: axis assignment (which DID is lateral vs longitudinal) and the scale
// factor here are BOTH unconfirmed - see docs/ble-capture/README.md "drive 2"
// section. /100 is a placeholder guess matching the scaling convention seen
// on several other DIDs in this investigation (e.g. oil temp). Recalibrate
// once a maneuver-isolated test drive pins these down, then update here.
#define GFORCE_SCALE 0.01f
static void pollGForceX() {
  uint8_t data[4], len;
  if (!pollDID(0x0760, "2B11", data, &len)) return;
  obdState.gForceX = asS16(data, len) * GFORCE_SCALE;
}
static void pollGForceY() {
  uint8_t data[4], len;
  if (!pollDID(0x0760, "4175", data, &len)) return;
  obdState.gForceY = asS16(data, len) * GFORCE_SCALE;
}
static void pollYawRate() {
  uint8_t data[4], len;
  if (!pollDID(0x0760, "2B0B", data, &len)) return;
  obdState.yawRateRaw = asS16(data, len);
}

static void pollVehicleSpeed() {
  uint8_t data[4], len;
  if (!pollPID01(0x07DF, "0D", data, &len)) return;  // standard PID, km/h directly
  obdState.vehicleSpeedKmh = (float)asU8(data, len);
}

// ---- scheduler: only poll what the active screen needs, cancel switch always ----
// SCREEN_TIRE=0, SCREEN_OIL=1, SCREEN_GFORCE=2 (matches display.ino)
void obd_poll_tick(int activeScreenID) {
  static uint32_t lastTick = 0;
  static bool doCancelThisTick = true;
  static uint8_t screenTargetIndex = 0;

  uint32_t now = millis();
  if (now - lastTick < 120) return;  // pace requests; the adapter is a single synchronous link
  lastTick = now;

  if (!obdState.connected) return;

  if (doCancelThisTick) {
    pollCancelSwitch();
  } else {
    switch (activeScreenID) {
      case 0: {  // tire temps
        void (*targets[])() = { pollTireFL, pollTireFR, pollTireRL, pollTireRR };
        targets[screenTargetIndex % 4]();
        screenTargetIndex++;
        break;
      }
      case 1: {  // oil temp
        pollOilTemp();
        break;
      }
      case 2: {  // g-force
        void (*targets[])() = { pollGForceX, pollGForceY, pollYawRate, pollVehicleSpeed };
        targets[screenTargetIndex % 4]();
        screenTargetIndex++;
        break;
      }
    }
  }
  doCancelThisTick = !doCancelThisTick;  // cancel switch gets ~half of all poll slots
}
