# Publishing an app from your own repository

Any GitHub repository can offer an app to CYD App Platform. Users add it in the launcher's web page (**GET APPS** → open the address shown on the screen → *GitHub repositories*). Apps from user-added repositories are marked **unreviewed**.

## What the device looks for

The launcher reads the **latest release** of the repository and needs two assets:

1. **The app firmware**, for example `app.bin`: the application image built for the original ESP32 with 4 MB flash (for Arduino, the `.ino.bin`, not the merged image with bootloader). It must fit the 2.6 MB app slot and must not need a SPIFFS/LittleFS partition.
2. **`cyd-app.json`** describing it:

   ```json
   {
     "id": "plane-spotter",
     "name": "Plane Spotter",
     "version": "1.2.0",
     "description": "Shows aircraft overhead",
     "binary": "app.bin",
     "sha256": "<sha-256 of app.bin>",
     "size": 1350000
   }
   ```

   - `id`: lower-case letters, digits, `-` and `_`, at most 31 characters. It names the folder on the SD card (`/apps/<id>/`).
   - `name`: shown on the device; keep it short (about 15 characters fit).
   - `version`: any string; when it changes, the device offers an update.
   - `binary`: the asset name of the firmware in the same release.
   - `sha256` and `size`: of the firmware file. The device refuses a download that does not match.

## Returning to the launcher

Call `cydReturnToLauncher()` from [`sdk/arduino/CYDPlatform.h`](../sdk/arduino/CYDPlatform.h) (Arduino) or `cyd_platform_return_to_launcher()` (ESP-IDF) from your app's exit action. Pressing RST also returns to the launcher.

## Creating the release with GitHub Actions

After your build step produces the firmware, add something like:

```yaml
      - name: Describe the app for CYD App Platform
        run: |
          cp build/my_app.bin app.bin
          python3 - <<'PY'
          import hashlib, json, os
          data = open("app.bin", "rb").read()
          json.dump({
              "id": "my-app",
              "name": "My App",
              "version": os.environ["GITHUB_REF_NAME"].lstrip("v"),
              "description": "What it does",
              "binary": "app.bin",
              "sha256": hashlib.sha256(data).hexdigest(),
              "size": len(data),
          }, open("cyd-app.json", "w"), indent=2)
          PY

      - uses: softprops/action-gh-release@v2
        with:
          files: |
            app.bin
            cyd-app.json
```

The repository must be public, because the device downloads without signing in.

## Getting into the official catalog

The official catalog lists apps that were reviewed. To propose yours, open an issue in this repository with a link to your release.
