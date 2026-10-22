# Apps

Apps are complete ESP32 firmware images that the launcher installs into the `ota_0` slot.

New here? Start with the [app creation guide](../docs/creating-apps.md), then use the Hello example as a template.

- **touch-calibration** (Touch Setup) — calibrates the touch screen for all apps.
- **sat-tracker** (Satellites) — satellites above your location and their next passes, computed on the device with SGP4.
- **plane-spotter** — live aircraft overhead; an adapted copy of CYD28PlaneSpotter built with PlatformIO.
- **examples/hello-app** — shows a counter and returns to the launcher on EXIT or a long BOOT press.

Each app folder contains a `manifest.json`. ESP-IDF apps are built by `tools/build.sh`; apps with a `platformio.ini` are Arduino apps built by `tools/build-pio-app.sh` with the PlatformIO env named in the manifest (`platformio_env`). Both write `dist/sd/apps/<id>/` with `app.bin` and a manifest that includes the SHA-256, and `tools/package.py` adds them to the release catalog.

When you change an app, bump `version` in its `manifest.json`: the launcher offers an update only when the catalog version differs from the installed one. After the change is merged, the **Publish apps** workflow publishes it to devices without a platform release. The Release workflow warns about apps that changed without a version bump (`tools/check_app_versions.py`).
