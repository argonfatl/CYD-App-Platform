# Creating apps for CYD App Platform

This guide explains how to build an ESP-IDF or Arduino-ESP32 app that the CYD App Platform launcher can install and run. Apps are full firmware images installed from the SD card into the launcher's `ota_0` app slot; they are not plugins loaded into the launcher.

## Requirements

- Target the original ESP32 with 4 MB flash, used by the common 320×240 CYD (ESP32-2432S028R). Other CYD variants may need different display and touch drivers.
- Keep the app image within the 2.625 MB `ota_0` partition (about 2.6 MB in practice).
- Do not depend on a SPIFFS or LittleFS partition. The platform partition table has none.
- Include an on-screen way to return to the launcher. A software restart or crash normally boots the app again; the app must select the launcher partition explicitly.
- Make all essential actions available by touch. BOOT can be an optional shortcut, but may be inaccessible in an enclosure.

The launcher keeps the partition table and installs only the app image. Do not distribute a merged image containing a bootloader or partition table as the app binary.

## Start from the Hello example

The quickest route is to copy [`apps/examples/hello-app`](../apps/examples/hello-app/) to a new directory under `apps/`, then update the project name, manifest, and source files. The repository build script discovers apps in `apps/*/` and `apps/examples/*/` by their `manifest.json` files.

An app folder needs a manifest beside its build files:

```json
{
  "id": "my-app",
  "name": "My App",
  "version": "0.1.0",
  "sdk": 1,
  "binary": "app.bin",
  "description": "A short description shown by the launcher."
}
```

- `id` must be 1–31 characters using lowercase letters, digits, `-`, or `_`. The launcher uses it as the SD folder name, `/apps/<id>/`.
- `name` is the short label shown in the launcher.
- `version` should change whenever you publish a new build; GET APPS offers an update when it differs from the installed version.
- `sdk` is currently `1` and records the platform API generation.
- `binary` is `app.bin` in the package produced by this repository's build tools.
- `description` briefly describes the app.

The local manifest can omit `sha256`; `tools/build.sh` adds it to the packaged manifest. Always include a SHA-256 in release metadata for GET APPS (see [Publishing an app](app-publishing.md)).

## Build with ESP-IDF

Install and activate ESP-IDF v5.4. Use the Hello example as a starting point. Its top-level `CMakeLists.txt` adds this repository's `components/` directory, and its component CMake file declares dependencies on `cyd_board` and `cyd_platform`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_cyd_app)
```

For an app inside `apps/<folder>/`, adjust the relative path to `components/`. A minimal component declaration is:

```cmake
idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES cyd_board cyd_platform)
```

Use `sdkconfig.defaults` to select the original ESP32 and 4 MB flash, as in the Hello example. To build only your app:

```sh
cd apps/my-app
idf.py build
```

To build the launcher, bundled apps, and SD-card package together, run `./tools/build.sh` from the repository root. The resulting app package is `dist/sd/apps/<id>/`.

## Build with Arduino-ESP32

Build for `esp32dev` / ESP32 Dev Module with 4 MB flash. Copy [`sdk/arduino/CYDPlatform.h`](../sdk/arduino/CYDPlatform.h) into your sketch or add that folder to the include path. The header supplies `cydReturnToLauncher()`, saved touch-calibration helpers, and `cydMapTouch()`.

Apps with a `platformio.ini` are built by `tools/build-pio-app.sh`; set `platformio_env` in the source `manifest.json` to the environment to build. See [`apps/plane-spotter`](../apps/plane-spotter/) for a working example. The device uses the platform's partition table regardless of the Arduino IDE's partition scheme, but your image must still fit the app slot and must not rely on a filesystem partition.

## Use the platform APIs

### Return to the launcher

For ESP-IDF, include `cyd_platform.h` and call:

```c
#include "cyd_platform.h"

cyd_platform_return_to_launcher();
```

For Arduino, call `cydReturnToLauncher()` from the app's Exit action. Both APIs select the factory launcher and restart. They return only if switching fails, so show an error if the call returns.

The bundled apps also watch for a long touch or long BOOT press so a user can exit during startup or recover if a screen has no normal controls. RST and power-on normally return to the launcher, unless the user enabled “power-on starts last app”; software restarts and crashes stay in the app.

### Display, touch, and board helpers

ESP-IDF apps can use `cyd_board.h` for display, touch, BOOT, battery, and SD-card helpers. Drawing uses a callback and a small stripe canvas, not a full framebuffer:

```c
static void draw(cyd_canvas_t *canvas, void *ctx)
{
    (void)ctx;
    cyd_fill(canvas, CYD_DARK);
    cyd_draw_text(canvas, "MY APP", 12, 12, 2, CYD_WHITE);
}

// After cyd_display_init():
cyd_display_render(draw, NULL);
```

Initialize each helper your app uses. For touch calibration, ESP-IDF `cyd_touch_init()` loads the shared calibration automatically. Arduino apps can read and map it with `cydGetTouchCalibration()` and `cydMapTouch()`.

See [`sdk/README.md`](../sdk/README.md) for the full API, shared location settings, and clock behavior.

### Storage and shared settings

- NVS is shared by all apps and the launcher. Give your app a private namespace (ESP-IDF NVS namespaces are at most 15 characters); do not reuse platform namespaces such as `cyd_launcher`, `cyd_wifi`, or `cyd_location`.
- Put larger files under `/apps/<id>/data/` on the SD card. Create the directory if needed and handle a missing or read-only card gracefully.
- Read the shared location through `cyd_platform_get_location()` (ESP-IDF) or the documented `cyd_location` preferences (Arduino). Do not modify that namespace directly.
- The launcher usually sets the system clock before starting the app. Check that the time is valid before relying on it; power-on or RST may clear it.

## Install and test from an SD card

After `./tools/build.sh` completes, copy `dist/sd/apps/<id>/` to the SD card so the card contains:

```text
/apps/<id>/manifest.json
/apps/<id>/app.bin
```

Insert the card, start the launcher, and select the app. The launcher verifies a provided SHA-256 before flashing. Check normal startup, touch response, Exit/recovery, missing Wi-Fi or SD behavior as applicable, and reboot behavior. For the platform's hardware checklist, see [`docs/development.md`](development.md).

## Publish through GET APPS

To offer the app from a GitHub repository, create a public GitHub Release with the app binary and a `cyd-app.json` metadata asset. The launcher downloads the latest release, validates the metadata, binary size, and SHA-256, then writes the app to the SD card. Follow [Publishing an app from your own repository](app-publishing.md) for the exact release metadata and GitHub Actions example.

To propose the app for the official catalog, open an issue in this repository with a link to the release. User-added repositories are marked unreviewed on the device.
