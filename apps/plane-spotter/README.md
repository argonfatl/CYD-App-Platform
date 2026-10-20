# Plane Spotter (CYD App Platform build)

Live ADS-B aircraft overhead, airline and route details, weather, and a radar view, for the 2.8-inch CYD.

This folder is an **adapted copy** of [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) (MIT), taken at commit [`ed43e94`](https://github.com/argonfatl/CYD28PlaneSpotter/tree/ed43e94f1a0b26ff6e72d71c11a9c8c87c5cdd0d). The original project stays in its own repository and keeps working as standalone firmware. Changes from upstream are behind `CYD_PLATFORM` in `src/main.cpp`.

## Changes for the platform

- The settings button **Updates** becomes **Launcher**: tap twice to return to the CYD App Platform launcher. The web page's *Firmware* card offers **Exit to launcher** instead of upload and GitHub update.
- The firmware's own update paths are off: no GitHub release checks, no browser upload, no ArduinoOTA. The launcher installs and updates the app (GET APPS).
- The platform has no LittleFS partition, so the aircraft type collection is not saved between restarts. Everything else (WiFi, location, calibration, settings) is stored in NVS as before.

## Board variant

The display init differs between CYD boards. `manifest.json` names the PlatformIO env the catalog ships (`platformio_env`, currently `cyd-alt`, the ILI9341 board with a USB-C port). If the screen stays white or black, build another env (`cyd` or `cyd2usb`) and copy its `firmware.bin` to `/apps/plane-spotter/app.bin` on the SD card.

## Building

The platform CI builds it with PlatformIO. Locally:

```sh
cd apps/plane-spotter
pio run -e cyd-alt
# .pio/build/cyd-alt/firmware.bin is the app image
```

`config.env` (copied from `config.env.example`) is optional; WiFi and location can be set up on the device.

## Updating from upstream

Copy `src/`, `lib/`, and `platformio.ini` from a newer upstream commit, then re-apply the `CYD_PLATFORM` blocks (search for `CYD_PLATFORM` in this folder's `src/main.cpp`) and bump `version` in `manifest.json`.
