# Development setup

## Target

The initial target is the common 2.8-inch CYD (ESP32-2432S028R): original ESP32, 4 MB flash, ILI9341 display, XPT2046 touch, microSD. Pins and panel settings are in `components/cyd_board/include/cyd_board.h`; other variants need changes there.

If the image is mirrored or the colors look inverted (dark backgrounds shown as white), change `CYD_LCD_MIRROR_X`, `CYD_LCD_MIRROR_Y`, or `CYD_LCD_INVERT_COLORS`. If touches land in the wrong place, run the Touch Setup app; the `CYD_TOUCH_*` constants are only the defaults used before calibration.

## Toolchain

Install ESP-IDF v5.4 using Espressif's official instructions and activate its environment in your shell.

## Build

From the repository root:

```sh
./tools/build.sh
```

This builds the launcher and every app under `apps/examples/`, then writes:

- `dist/cyd-launcher-full.bin` — bootloader, partition table, and launcher; flash at `0x0`;
- `dist/sd/apps/<id>/` — `app.bin` and `manifest.json` with the computed SHA-256; copy to the SD card;
- `dist/SHA256SUMS`.

## Flash and monitor the launcher

```sh
cd firmware/launcher
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port (for example a `/dev/cu.*` device on macOS).

## Test an app without the SD card

You can flash an app straight into the slot and select it as the boot partition:

```sh
cd apps/examples/hello-app
idf.py build
esptool.py --chip esp32 -p PORT write_flash 0x160000 build/cyd_hello_app.bin
```

Then use `otatool.py switch_ota_partition --slot 0` from ESP-IDF, or open the app once from the launcher.

## Hardware validation checklist

- [x] Launcher boots, mounts the SD card, and lists apps from their manifests.
- [x] Image is not mirrored and colors are correct.
- [ ] Footer buttons (power-on setting, launcher update) and status line do not overlap.
- [x] Touch selects the right row.
- [ ] Touch Setup: targets accept presses, the test screen follows the stylus, SAVE persists across reboots and is used by the launcher and Hello.
- [ ] Touch Setup: REDO restarts, holding BOOT leaves without saving, a sloppy calibration is rejected.
- [x] BOOT short press moves, long press opens.
- [x] Installing Hello from SD works.
- [ ] Opening the same app again skips flashing.
- [x] EXIT and long BOOT in Hello return to the launcher.
- [ ] RST returns to the launcher; with "power-on starts last app" it returns to the app.
- [ ] A corrupted `app.bin` or wrong `sha256` is rejected and the previous app still boots.
- [ ] `/wifi.json` is imported and CHECK UPDATE connects to Wi-Fi.
- [ ] A newer release is offered and installs; the device returns to the launcher with the new version.
- [ ] Power loss while "UPDATING LAUNCHER" is shown recovers on the next boot.
- [ ] Battery indicator: the percentage roughly matches a multimeter reading on BAT+ (see the `Battery ... mV` log line), shows `BAT --` without a battery, and updates while discharging.
- [ ] Web page: searching a town and pressing Use saves the location; Satellites then logs `Location from LAUNCHER`.
- [ ] Satellites: SKY shows satellites, the ISS panel has elevation or its next pass, and PASSES fills in after start-up (note the `took N s` log line).
- [ ] Satellites: holding the screen for 2 seconds returns to the launcher on the start-up screens and on errors.
- [ ] Satellites: restarting within 2 hours of a download logs `Last download attempt ... using the saved copy` and does not contact CelesTrak.
- [ ] Satellites: start-up notices appear in the header and disappear after 15 seconds; N and S labels are not covered.
- [ ] GET APPS lists the catalog with NEW / UPDATE / INSTALLED and downloads an app to the SD card.
- [ ] The web page at the shown address loads, adds and removes a repository, and starts a download.
- [ ] An app from a user repository shows the GITHUB badge in the main list and runs.
