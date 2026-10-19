# Hello app

A minimal CYD App Platform app. It is a normal ESP-IDF firmware that uses the shared
`cyd_board` and `cyd_platform` components, shows a running counter, and returns to the
launcher when you tap **EXIT** or hold the **BOOT** button.

Install it by copying `app.bin` and `manifest.json` to `/apps/hello/` on the SD card.
`tools/build.sh` produces that folder in `dist/sd/apps/hello/`.
