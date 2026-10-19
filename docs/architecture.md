# Architecture

The runtime decision is recorded in [ADR 0002](adr/0002-firmware-slot-runtime.md).

## Components

- **Bootloader hook** (`firmware/launcher/bootloader_components/cyd_boot_hooks`) — on power-on or RST, erases `otadata` so the launcher starts, unless the user enabled "power-on starts last app".
- **Launcher** (`firmware/launcher`) — lists apps on the SD card, verifies and installs the selected app into `ota_0`, and boots it. Also holds platform settings and updates itself from GitHub releases over Wi-Fi ([ADR 0003](adr/0003-launcher-self-update.md)).
- **Board support** (`components/cyd_board`) — ILI9341 display drawn in 20-row stripes with two small DMA buffers, bit-banged XPT2046 touch, SD card on SPI3, BOOT button.
- **Platform API** (`components/cyd_platform`, `sdk/arduino`) — return to the launcher, read or change the power-on policy, and read the touch calibration.
- **Touch Setup** (`apps/touch-calibration`) — a system app that calibrates the touch screen and saves the result for all apps. Calibration lives outside the launcher so the launcher stays small and the procedure can be updated like any app.
- **Build tooling** (`tools/build.sh`, CI) — builds everything and writes `dist/` with the launcher image, the SD card folder, and checksums.

## Boot flow

```text
power-on / RST ──► bootloader hook ──► otadata erased ──► factory: launcher
                                   └─ keep-app enabled ─► ota_0: app

launcher ── open app ──► verify SHA-256 ──► flash ota_0 (if changed) ──► reboot ──► app
app ── cyd_platform_return_to_launcher() ──► select factory ──► reboot ──► launcher
```

Software restarts and crashes are not power-on resets, so they restart the app rather than the launcher.

Launcher self-update:

```text
launcher (factory) ── download cyd-launcher.bin ──► ota_0 ── verify ──► reboot
new launcher (ota_0) ── copy itself ──► factory ── verify ──► reboot ──► launcher (factory)
```

## Storage

```text
/sdcard/apps/<app-id>/
  manifest.json
  app.bin
```

The `cydcfg` partition holds the power-on policy (read by the bootloader hook) and the touch calibration, an affine map from raw XPT2046 readings to screen pixels (read by `cyd_touch_init()`).

The launcher records which app is in the slot (id, SHA-256, and slot address) in NVS namespace `cyd_launcher`, and Wi-Fi credentials imported from `/wifi.json` in namespace `cyd_wifi`. Apps share the `nvs` partition and must use their own namespace. A per-app data directory on the SD card (`/apps/<app-id>/data/`) is the planned convention for larger app data.

## App sources and trust

A repository can provide metadata and release artifacts, but it must not be able to declare itself officially approved. Official approval must come from trusted platform metadata. User-added repositories should be visibly marked as unreviewed. Packages are verified before installation, and a failed install never switches the boot partition.
