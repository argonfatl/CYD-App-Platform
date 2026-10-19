# CYD App Platform

**One device. Many apps.**

CYD App Platform is an open-source launcher and app platform for the ESP32 Cheap Yellow Display (CYD). It aims to let you keep several apps on an SD card, pick one from a touch menu, and later install and update apps from GitHub.

## Project status

🚧 **Early prototype.** The firmware-slot launcher builds in CI and boots on a physical ESP32-2432S028R: it reads the SD card and lists apps from their manifests. Display, touch, the BOOT button, installing an app from SD, and returning to the launcher work on hardware. Launcher self-update over Wi-Fi is new and still being tested. See [ADR 0002](docs/adr/0002-firmware-slot-runtime.md) for the design and [docs/development.md](docs/development.md) for the hardware checklist.

## How it works

Every app is an ordinary, complete ESP32 firmware image (Arduino or ESP-IDF).

1. The **launcher** is flashed once into the `factory` partition.
2. Apps live on the SD card in `/apps/<app-id>/` as `app.bin` plus `manifest.json`.
3. When you open an app, the launcher checks its SHA-256, writes it into the `ota_0` slot (skipped if that exact app is already installed), and reboots into it.
4. An app returns to the launcher by calling `cyd_platform_return_to_launcher()` (ESP-IDF) or `cydReturnToLauncher()` (Arduino).
5. Apps that know nothing about the platform still get a way back: pressing **RST** or power-cycling returns to the launcher. This can be switched off in the launcher if you want the last app to start on power-on.

The launcher works with touch or with the **BOOT** button alone: short press moves to the next item, long press opens it.

If touches land in the wrong place, open the **Touch Setup** app from the launcher. It calibrates the screen once for the launcher and all apps. See [apps/touch-calibration](apps/touch-calibration/README.md).

## Updating the launcher over Wi-Fi

1. Put a `wifi.json` file in the root of the SD card:

   ```json
   { "ssid": "MyNetwork", "password": "my-password" }
   ```

   The launcher copies it into its settings at startup.
2. Select **CHECK UPDATE** at the bottom of the launcher. It connects to Wi-Fi and checks the latest release of this repository.
3. If a newer version is found, the button changes to **INSTALL vX.Y.Z**. Open it again to download, verify, and install the update. The device restarts twice.

The launcher version is shown in the top-right corner: **cyan** for a release (`v0.2.0`), **amber** for a development build (`v0.2.0+5` means five commits after v0.2.0).

The installed app is removed during an update and is reinstalled from the SD card the next time you open it. See [ADR 0003](docs/adr/0003-launcher-self-update.md) for details.

## Quick start

1. Download `cyd-launcher-full.bin` and `cyd-sd-card.zip` from a CI run or release.
2. Flash the launcher once over USB (later updates can be installed over Wi-Fi), for example with `esptool.py write_flash 0x0 cyd-launcher-full.bin` or the [ESP Web Flasher](https://espressif.github.io/esptool-js/).
3. Unzip `cyd-sd-card.zip` to the root of a FAT32 microSD card and insert it.

To build locally, activate ESP-IDF v5.4 and run `./tools/build.sh`. See [docs/development.md](docs/development.md).

## Making a release

Push a tag such as `v0.2.0`. CI builds the firmware with that version and publishes `cyd-launcher-full.bin` (USB flashing), `cyd-launcher.bin` and `cyd-launcher.json` (Wi-Fi self-update), `cyd-sd-card.zip`, and `SHA256SUMS`.

## Repository layout

- `firmware/launcher/` — the launcher, its partition table, and the bootloader hook.
- `components/cyd_board/` — display (stripe renderer, no full framebuffer), touch, SD, and button support. Pins, panel orientation, color inversion, and default touch calibration are constants in `include/cyd_board.h`.
- `components/cyd_platform/` — the small API apps use to return to the launcher.
- `sdk/arduino/` — a single header for Arduino-ESP32 apps.
- `apps/touch-calibration/` — Touch Setup, the system app that calibrates the touch screen.
- `apps/examples/hello-app/` — a minimal example app.
- `docs/` — architecture notes and decision records.

## Making an app

Any firmware built for the original ESP32 with 4 MB flash can run, as long as it:

- fits in the 2.6 MB app slot;
- does not need its own SPIFFS/LittleFS partition (use NVS or the SD card for data);
- uses its own NVS namespace, because NVS is shared with the launcher.

Add a `manifest.json` next to `app.bin`:

```json
{
  "id": "hello",
  "name": "Hello",
  "version": "0.1.0",
  "sdk": 1,
  "binary": "app.bin",
  "sha256": "<sha-256 of app.bin, optional but recommended>"
}
```

## Roadmap

1. ~~Validate the launcher on a physical CYD.~~ Done.
2. ~~Launcher self-update from GitHub releases over Wi-Fi.~~ Done; awaiting hardware test.
3. Run [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) from the app slot.
4. Wi-Fi setup screen and a shared settings store.
5. Install and update apps from GitHub releases, with a trusted catalog of reviewed apps.
6. Optional lightweight runtime (scripts or WebAssembly) for small apps that should switch instantly.

## App trust and security

Apps are native firmware with full access to the device. Apps from arbitrary repositories must be treated as unreviewed code. A repository cannot declare itself officially approved in its own manifest; approval must come from trusted platform metadata. The launcher refuses to install an app whose SHA-256 does not match its manifest.

## Contributing

The manifest format and platform API are still being designed. Please open an issue to discuss significant changes before investing in an app port.

## License

[MIT](LICENSE). Third-party apps installed through the platform keep their own licenses.
