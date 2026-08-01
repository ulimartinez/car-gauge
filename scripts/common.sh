#!/bin/bash
# Shared config for build.sh / upload.sh / monitor.sh. Not meant to be run
# directly -- sourced by the other scripts.

# Verified empirically on real ESP32-S3R8 hardware (CrowPanel 1.28" round
# rotary display, same board as espNOW-car-gauge): PSRAM=opi is required
# (this chip has 8MB embedded OCTAL PSRAM -- the "QSPI PSRAM" option is for
# external quad-PSRAM parts and leaves ESP.getFreePsram()==0). USBMode=hwcdc
# /CDCOnBoot=cdc matches this board's native USB-Serial/JTAG peripheral (no
# external USB-UART bridge chip). Do not change without re-verifying
# ESP.getFreePsram() > 0 on hardware after flashing.
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default"

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
