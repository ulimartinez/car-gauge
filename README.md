# car-gauge
ESP32 code for a car gauge that displays CAN data

## Hardware

CrowPanel 1.28" round rotary display: ESP32-S3R8 (8MB embedded octal PSRAM, 16MB flash), 240x240 GC9A01 IPS panel, rotary encoder + press switch. Same board family as the `espNOW-car-gauge` display.

## Arduino CLI installation

```bash
brew install arduino-cli   # or see https://arduino.github.io/arduino-cli/latest/installation/
arduino-cli core install esp32:esp32
```

## Required libraries

Install into your Arduino **user** libraries directory (`~/Documents/Arduino/libraries/` on macOS/Linux):

| Library | Notes |
| --- | --- |
| LovyanGFX | Display driver (GC9A01 panel, DMA push) |
| lvgl (8.3.11) | UI framework -- also requires an `lv_conf.h` placed as a sibling of the `lvgl` folder (directly in `libraries/`, not inside `libraries/lvgl/`) |
| NimBLE-Arduino | BLE client for the Veepeak OBD adapter |

## Build, upload, monitor

```bash
./scripts/build.sh                              # compile only
./scripts/upload.sh /dev/cu.usbmodemXXXXXX      # compile + flash; find your port with: arduino-cli board list
./scripts/monitor.sh /dev/cu.usbmodemXXXXXX     # open a Serial monitor at 115200 baud
```

The board FQBN and its options (`PSRAM=opi`, `FlashSize=16M`, `PartitionScheme=app3M_fat9M_16MB`, `USBMode=hwcdc`, `CDCOnBoot=cdc`) are centralized in [`scripts/common.sh`](scripts/common.sh) -- verified empirically on real hardware (`ESP.getFreePsram() > 0`, full display/encoder operation confirmed over Serial). Don't change them without re-verifying on hardware.

Note: `sim/build_lvgl.sh` and `sim/render.sh` are a separate, unrelated tool -- a desktop LVGL simulator that renders screens to `.bmp` files for fast iteration without hardware. The scripts above are for the real Arduino/ESP32 build.
