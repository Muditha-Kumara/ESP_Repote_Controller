# ESP32-S3 Xbox 360 Controller USB Host

This workspace contains an ESP-IDF project that reads Xbox 360 controller input over USB OTG (host mode) and logs joystick/button state.

Requirements:
- ESP-IDF v6.x installed and sourced
- Target: esp32s3
- USB OTG on the ESP32-S3 wired to a USB-A receptacle that the controller plugs into

Build & flash (from this folder):

```bash
# set up ESP-IDF environment first, e.g.:
. $HOME/.espressif/path/to/esp-idf/export.sh

# set target and build
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Notes:
- If CMake reports a missing component named `usb`, this project uses the `esp_usb` component available in IDF v6. If your IDF version lacks USB host support, update ESP-IDF to v6.
- You may need to enable USB OTG in menuconfig depending on your board.
