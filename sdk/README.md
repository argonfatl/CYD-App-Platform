# CYD App SDK

Apps are ordinary firmware, so the SDK is intentionally tiny. SDK version: **1**.

## ESP-IDF

Add the repository's `components/` directory to `EXTRA_COMPONENT_DIRS` and depend on `cyd_platform` (and optionally `cyd_board` for display, touch, SD, and button helpers).

```c
#include "cyd_platform.h"

cyd_platform_return_to_launcher(); // reboots into the launcher
```

## Arduino-ESP32

Copy [`arduino/CYDPlatform.h`](arduino/CYDPlatform.h) into your sketch folder.

```cpp
#include "CYDPlatform.h"

void onExitPressed() {
    cydReturnToLauncher();
}
```

Build for ESP32 Dev Module with 4 MB flash. The partition scheme chosen in the IDE does not matter at run time because the device keeps the platform's partition table, but the app must fit in the 2.6 MB slot and must not need a SPIFFS/LittleFS partition.

### Touch calibration in Arduino apps

The same header reads the calibration saved by the Touch Setup app:

```cpp
CYDTouchCalibration cal;
if (cydGetTouchCalibration(cal)) {
    int x, y;
    cydMapTouch(cal, rawX, rawY, x, y); // rawX = XPT2046 cmd 0xD0, rawY = cmd 0x90
}
```

ESP-IDF apps that use `cyd_board` get it automatically from `cyd_touch_init()`.

## Time

If the launcher could reach an NTP server, the clock is already set when your app starts (it survives the restart into the app). Check `time(NULL) > 1700000000` before relying on it, and sync yourself if it is not set, because a power-on or RST clears it.

## Location

`cyd_platform_get_location(&loc)` returns the location set in the launcher's web page (GET APPS). Arduino apps can read the same values with `Preferences` from namespace `cyd_location`: `lat_u` and `lon_u` (int32, microdegrees), `alt_m`, `utc_off_min`, and `name`.

## Rules for apps

- Use your own NVS namespace; NVS is shared.
- Keep large data on the SD card under `/apps/<app-id>/data/`.
- Provide a way to exit; RST always works as a fallback.
