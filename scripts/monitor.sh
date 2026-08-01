#!/bin/bash
# Opens a serial monitor at the firmware's baud rate (115200).
# Usage: ./scripts/monitor.sh /dev/cu.usbmodemXXXXXX
set -euo pipefail

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

arduino-cli monitor -p "$PORT" -c baudrate=115200
