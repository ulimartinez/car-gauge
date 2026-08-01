#!/bin/bash
# Compiles the firmware with arduino-cli. Requires: arduino-cli installed,
# the esp32 board package installed (arduino-cli core install esp32:esp32),
# and LovyanGFX, lvgl (8.3.11) + lv_conf.h, NimBLE-Arduino installed under
# your Arduino user libraries directory. See README.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "error: arduino-cli not found on PATH. Install it: https://arduino.github.io/arduino-cli/latest/installation/" >&2
    exit 1
fi

echo "Building $SKETCH_DIR"
echo "FQBN: $FQBN"
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"
