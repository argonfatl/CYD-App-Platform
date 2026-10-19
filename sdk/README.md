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

## Rules for apps

- Use your own NVS namespace; NVS is shared.
- Keep large data on the SD card under `/apps/<app-id>/data/`.
- Provide a way to exit; RST always works as a fallback.
