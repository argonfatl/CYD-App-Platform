# ADR 0002: Use firmware slots for the v1 app runtime

- **Status:** Accepted; hardware validation in progress
- **Date:** 2026-10-19
- **Supersedes:** [ADR 0001](0001-runtime-evaluation.md)

## Context

ADR 0001 evaluated Espressif ELF Loader as the app runtime. The prototype built and ran, but it exposed problems that matter on the common CYD (original ESP32, 4 MB flash, no PSRAM):

- Module code must live in scarce executable RAM, so apps would have to be thin wrappers around services the platform provides.
- Every service (display, touch, Wi-Fi, HTTP, TLS, JSON, storage) needs a stable ABI before any real app can be written.
- Existing Arduino projects, including CYD28PlaneSpotter, cannot run as modules without a rewrite.
- ELF modules are not a sandbox, so they give no security benefit over full firmware.

## Decision

Every app is an ordinary, complete ESP32 firmware image. The platform switches between apps by flashing and rebooting:

1. The **launcher** lives permanently in the `factory` partition.
2. Apps are stored on the SD card as `/apps/<app-id>/app.bin` with a `manifest.json`.
3. To run an app, the launcher verifies its SHA-256, writes it into the single `ota_0` slot (skipped if that exact image is already there), selects `ota_0` as the boot partition, and restarts.
4. To return, the app calls `cyd_platform_return_to_launcher()`, which selects `factory` and restarts.
5. Apps that know nothing about the platform still get a way back: a bootloader hook returns to the launcher on every power-on or **RST** button press, unless the user enabled "keep app on power-on" in the launcher. Software restarts and crashes stay in the app.

## Partition layout (4 MB flash)

| Name      | Type / subtype   | Offset     | Size     | Purpose                                    |
|-----------|------------------|------------|----------|--------------------------------------------|
| `nvs`     | data / nvs       | `0x9000`   | 20 KB    | Shared NVS (Wi-Fi credentials, settings)   |
| `otadata` | data / ota       | `0xE000`   | 8 KB     | Selects `factory` or `ota_0`               |
| `cydcfg`  | data / `0x40`    | `0x10000`  | 4 KB     | Boot policy and touch calibration          |
| `factory` | app / factory    | `0x20000`  | 1.25 MB  | Launcher                                   |
| `ota_0`   | app / ota_0      | `0x160000` | 2.625 MB | The currently installed app                |

The factory partition was enlarged from 896 KB to 1.25 MB when the launcher gained Wi-Fi and HTTPS for self-updates ([ADR 0003](0003-launcher-self-update.md)); the app slot shrank from 3 MB accordingly.

There is no SPIFFS/LittleFS partition. Apps keep their data in NVS (under their own namespace) or on the SD card.

## Consequences

**Benefits**

- Any Arduino or ESP-IDF firmware runs unmodified, with all RAM and the full app slot.
- No ABI to design or freeze before useful apps exist.
- CI already produces exactly the artifact an app needs: a `.bin`.
- Switching back to the same app is fast because flashing is skipped when the slot already holds it.

**Costs**

- Switching to a different app rewrites the slot: seconds, not milliseconds, plus flash wear (rated around 100,000 erase cycles per sector).
- Apps are trusted native code with full device access. Trust and integrity checks must happen before flashing.
- Apps share one NVS partition and must use their own namespace.
- Apps must be built for the original ESP32 with a 4 MB flash layout and must not depend on a filesystem partition.

## Validation needed on hardware

Confirmed so far: the launcher boots from `factory`, mounts the SD card, lists apps, installs an app into `ota_0` and boots it, and apps return to the launcher. Still to confirm:

- Power-on and RST both report `RESET_REASON_CHIP_POWER_ON` on the CYD, so the bootloader hook returns to the launcher.
- Time to flash a 1.5 MB app from SD.
- CYD28PlaneSpotter runs from `ota_0` with this partition table.

## Future work

A lighter runtime (Lua/Berry scripts or WebAssembly) may be added later for small apps that should switch instantly. It would sit alongside firmware slots rather than replace them.
