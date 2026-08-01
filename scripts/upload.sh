#!/bin/bash
# Compiles and flashes the firmware.
# Usage: ./scripts/upload.sh /dev/cu.usbmodemXXXXXX
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

if [ $# -lt 1 ]; then
    echo "usage: $0 <serial-port>" >&2
    echo "  e.g. $0 /dev/cu.usbmodem112401" >&2
    echo "  list candidate ports with: arduino-cli board list" >&2
    exit 1
fi
PORT="$1"

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "error: arduino-cli not found on PATH." >&2
    exit 1
fi

echo "Building $SKETCH_DIR"
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"

echo "Uploading to $PORT"
arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH_DIR"
