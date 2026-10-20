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

## Getting apps over Wi-Fi

Select **GET APPS** at the bottom of the launcher (Wi-Fi from `wifi.json` is required). It lists the apps from the official catalog and from any GitHub repositories you added, marked **NEW**, **UPDATE x.y.z**, or **INSTALLED**. Open one to download it to the SD card; it then appears in the main list.

While GET APPS is open, the screen shows a web address such as `192.168.1.50`. Open it in a browser on the same network to add or remove GitHub repositories and to start downloads from your phone or computer. Apps from your own repositories are marked **GITHUB** / **unreviewed**: they run with full access to the device. [docs/app-publishing.md](docs/app-publishing.md) explains how to publish an app so the launcher can find it.

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

Open **Actions → Release → Run workflow**, pick which part of the version to increase (`patch`, `minor`, or `major`), and run it. The workflow tags the current `main` (for example `v0.2.0` → `v0.3.0`), builds the firmware with that version, checks that the built launcher reports it, and publishes a release with `cyd-launcher-full.bin` (USB flashing), `cyd-launcher.bin` and `cyd-launcher.json` (Wi-Fi self-update), `catalog.json` and `app-<id>.bin` (GET APPS), `cyd-sd-card.zip`, and `SHA256SUMS`. You can also enter an exact version instead.

From a terminal: `gh workflow run release.yml -f bump=minor`.

Pushing a `v*` tag by hand still works and publishes a release the same way.

## Repository layout

- `firmware/launcher/` — the launcher, its partition table, and the bootloader hook.
- `components/cyd_board/` — display (stripe renderer, no full framebuffer), touch, SD, and button support. Pins, panel orientation, color inversion, and default touch calibration are constants in `include/cyd_board.h`.
- `components/cyd_platform/` — the small API apps use to return to the launcher.
- `sdk/arduino/` — a single header for Arduino-ESP32 apps.
- `apps/touch-calibration/` — Touch Setup, the system app that calibrates the touch screen.
- `apps/plane-spotter/` — Plane Spotter, an adapted copy of [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) built with PlatformIO.
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
3. ~~Run [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) from the app slot.~~ Plane Spotter is in the catalog; awaiting hardware test.
4. Wi-Fi setup screen and a shared settings store.
5. ~~Install and update apps from GitHub releases, with a trusted catalog of reviewed apps.~~ Done; awaiting hardware test.
6. Optional lightweight runtime (scripts or WebAssembly) for small apps that should switch instantly.

## App trust and security

Apps are native firmware with full access to the device. Apps from arbitrary repositories must be treated as unreviewed code. A repository cannot declare itself officially approved in its own manifest; approval must come from trusted platform metadata. The launcher refuses to install an app whose SHA-256 does not match its manifest.

## Contributing

The manifest format and platform API are still being designed. Please open an issue to discuss significant changes before investing in an app port.

## License

[MIT](LICENSE). Third-party apps installed through the platform keep their own licenses; `apps/plane-spotter` is MIT-licensed code from CYD28PlaneSpotter.
