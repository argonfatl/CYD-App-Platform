# ADR 0004: Download apps to the SD card from a catalog and GitHub repositories

- **Status:** Accepted; hardware validation pending
- **Date:** 2026-10-19

## Context

Apps are installed by copying `app.bin` and `manifest.json` to the SD card by hand. The launcher already has Wi-Fi and HTTPS for its own updates, so it can fetch apps too. Users should be able to get the platform's own apps and also add apps from any GitHub repository, while unreviewed apps stay clearly marked.

## Decision

### Sources

1. **Official catalog.** Every platform release publishes `catalog.json` and one `app-<id>.bin` per app. The launcher reads `https://github.com/<repo>/releases/latest/download/catalog.json`:

   ```json
   {
     "version": "v0.4.0",
     "apps": [
       {
         "id": "hello",
         "name": "Hello",
         "version": "0.1.0",
         "description": "...",
         "url": "https://github.com/argonfatl/CYD-App-Platform/releases/download/v0.4.0/app-hello.bin",
         "sha256": "...",
         "size": 249040
       }
     ]
   }
   ```

   Entries are marked **official**. Later the catalog can list reviewed third-party apps the same way, with URLs into their repositories.

2. **User repositories.** The user adds `owner/name` of a GitHub repository. Its latest release must contain `cyd-app.json` and the app binary:

   ```json
   { "id": "plane-spotter", "name": "Plane Spotter", "version": "1.2.0",
     "binary": "app.bin", "sha256": "...", "size": 1350000, "description": "..." }
   ```

   The binary is fetched from `releases/latest/download/<binary>`. These apps are marked **unreviewed** on the device and in the web page. A repository cannot mark itself official.

User repositories are stored in NVS (namespace `cyd_store`), at most 12.

### Device screen

**GET APPS** in the launcher connects to Wi-Fi, loads the catalog and every user repository, and lists the apps with their state: **NEW**, **UPDATE x.y.z**, or **INSTALLED**. Opening an entry downloads it to the SD card. Installing to the SD card does not touch the app slot; the app is flashed the next time it is opened, as before.

### Web interface

While GET APPS is open, the launcher serves a small page on port 80 and shows its address on screen. The page lists the available apps and the user repositories and lets the user add or remove repositories. The server runs only while that screen is open, so changes need someone at the device; it has no other authentication.

### Installing to the SD card

- The app id must match `[a-z0-9_-]{1,31}`; anything else is rejected, so an id cannot escape `/apps/`.
- The binary is downloaded to `/apps/<id>/app.bin.part`, its size and SHA-256 are checked, then it replaces `app.bin`.
- `manifest.json` is written with the verified SHA-256 and a `source` field (`official` or `github:owner/name`). The launcher's app list shows unreviewed apps with a **GITHUB** badge.

## Consequences

- Apps can be installed and updated without removing the SD card.
- Each user repository costs one HTTPS request (two TLS handshakes because of GitHub's redirect) when the list loads.
- Unreviewed apps run with full device access once opened; the badge and web page say so, but nothing prevents it.
- App authors publish two release assets. `docs/app-publishing.md` shows how.
