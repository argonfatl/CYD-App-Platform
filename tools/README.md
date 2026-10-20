# Developer tools

- `build.sh` — builds the launcher and all apps and assembles `dist/` (launcher image, SD card folder and zip, catalog, checksums). PlatformIO apps are built when `pio` is installed.
- `build-pio-app.sh` — builds one PlatformIO (Arduino) app into `dist/sd/apps/<id>/`.
- `package.py` — writes `catalog.json`, `app-<id>.bin`, `cyd-sd-card.zip`, and `SHA256SUMS` for a built `dist/`.
- `write_app_manifest.py` — writes an app's SD card manifest with its SHA-256.

Planned:

- validate an app manifest;
- package third-party app releases;
- inspect target and partition compatibility of an app image.
