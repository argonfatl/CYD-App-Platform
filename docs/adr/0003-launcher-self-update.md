# ADR 0003: Launcher self-update from GitHub Releases

- **Status:** Accepted; hardware validation pending
- **Date:** 2026-10-19

## Context

Updating the launcher currently requires a USB cable and a flashing tool. The launcher should be able to update itself over Wi-Fi from this repository's GitHub releases.

The launcher runs from the `factory` partition and cannot overwrite itself while running. The partition table has only one other app partition, `ota_0`, which normally holds the installed app.

## Decision

The update is staged through `ota_0` in two steps:

1. The launcher downloads `cyd-launcher.json` from the latest release (`https://github.com/<repo>/releases/latest/download/cyd-launcher.json`). It contains `version`, `sha256`, and `size`. If `version` is newer than the running launcher, the user is offered the update.
2. On confirmation, the launcher downloads `cyd-launcher.bin` from that release into `ota_0`, checks its size and SHA-256, checks that it is a launcher image, selects `ota_0`, and restarts.
3. The new launcher starts from `ota_0`, sees where it is running, copies itself into `factory`, verifies the copy, selects `factory`, and restarts.

Using the stable `latest/download/<asset>` URL avoids the GitHub API and its rate limits. HTTPS uses the ESP-IDF certificate bundle (common CA roots).

The repository is a build option (`CONFIG_CYD_UPDATE_REPO`, default `argonfatl/CYD-App-Platform`), so forks can publish their own launchers.

Wi-Fi credentials come from `/wifi.json` on the SD card and are copied into NVS (namespace `cyd_wifi`). An on-screen Wi-Fi setup is future work.

## Versioning

Release builds are made from tags such as `v0.2.0`; ESP-IDF takes the firmware version from `git describe`. A launcher counts as older than a release when its version is a lower `X.Y.Z`, or when it is a development build (any version that is not exactly `vX.Y.Z`).

## Failure handling

- Download, size, hash, or image errors abort the update; `factory` is untouched and the launcher keeps working.
- Power loss while copying into `factory` leaves `factory` invalid. The bootloader then falls back to `ota_0`, which still holds the new launcher and repeats the copy. The bootloader hook erasing `otadata` on power-on does not prevent this, because the bootloader tries `ota_0` when `factory` fails validation.

## Consequences

- An update removes the installed app from `ota_0`; the launcher reinstalls it from the SD card the next time it is opened.
- The launcher now includes Wi-Fi, TLS, and an HTTP client, so it grew to about 850 KB. The `factory` partition was enlarged to 1.25 MB and the app slot reduced to 2.625 MB. Devices must be reflashed over USB once to get this layout; later launcher updates happen over Wi-Fi.
- Release images are trusted because they come from the repository over HTTPS and match the published SHA-256. They are not signed; signing is future work.
