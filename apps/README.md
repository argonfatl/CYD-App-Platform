# Apps

Apps are complete ESP32 firmware images that the launcher installs into the `ota_0` slot.

- **hello-app** — shows a counter and returns to the launcher on EXIT or a long BOOT press.

Each app folder contains a `manifest.json`. `tools/build.sh` builds every app here and writes `dist/sd/apps/<id>/` with `app.bin` and a manifest that includes the SHA-256.
