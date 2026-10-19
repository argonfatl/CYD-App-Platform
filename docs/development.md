# Development setup

## Target

The initial target is the common 2.8-inch CYD (ESP32-2432S028R): original ESP32, 4 MB flash, ILI9341 display, XPT2046 touch, microSD. Pins and panel settings are in `components/cyd_board/include/cyd_board.h`; other variants need changes there.

If the image is mirrored or the colors look inverted (dark backgrounds shown as white), change `CYD_LCD_MIRROR_X`, `CYD_LCD_MIRROR_Y`, or `CYD_LCD_INVERT_COLORS`. If touches land in the wrong place, adjust the `CYD_TOUCH_*` constants.

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
esptool.py --chip esp32 -p PORT write_flash 0x100000 build/cyd_hello_app.bin
```

Then use `otatool.py switch_ota_partition --slot 0` from ESP-IDF, or open the app once from the launcher.

## Hardware validation checklist

- [x] Launcher boots, mounts the SD card, and lists apps from their manifests.
- [ ] Image is not mirrored and colors are correct (fixed after the first test; re-check).
- [ ] Settings row and status line do not overlap (fixed after the first test; re-check).
- [ ] Touch selects the right row; adjust `CYD_TOUCH_*` constants if not.
- [ ] BOOT short press moves, long press opens.
- [ ] Installing Hello from SD works; time to flash is acceptable.
- [ ] Opening the same app again skips flashing.
- [ ] EXIT and long BOOT in Hello return to the launcher.
- [ ] RST returns to the launcher; with "power-on starts last app" it returns to the app.
- [ ] A corrupted `app.bin` or wrong `sha256` is rejected and the previous app still boots.
