# CYD App Platform

**One device. Many apps.**

CYD App Platform is an open-source application platform for the ESP32 Cheap Yellow Display (CYD). It aims to provide a launcher, a standardized app SDK, and a web-based manager for installing, organizing, and updating compatible applications from GitHub.

## Project status

🚧 **Early prototype.** The Hello World ELF lifecycle has been confirmed on a physical CYD. The current Clock prototype adds a 320x240 landscape UI on the common ILI9341-based CYD while a Clock ELF module runs.

## Goals

- Provide a launcher for selecting and starting installed apps.
- Define a common app manifest and SDK.
- Keep each app's metadata, assets, and user data organized separately.
- Allow users to add an app by entering its GitHub repository URL.
- Check for app releases and download compatible, prebuilt packages.
- Distinguish community-added apps from apps reviewed and approved for the official catalog.
- Support a standard exit flow back to the launcher, with device restart as a fallback.
- Validate downloaded packages and preserve user data during updates.
- Target the common 2.8-inch CYD class (320x240 landscape UI), while documenting hardware variants explicitly.

## Current prototype

The prototype uses ESP-IDF and Espressif's ELF Loader as a candidate runtime. Hello World is kept as a build regression check; Clock is embedded in the launcher.

The launcher initializes the ILI9341 display using the common 2.8-inch CYD pin mapping and renders a digital elapsed-time screen while the Clock ELF module runs for 30 seconds. When the module returns, the screen status changes to `APP FINISHED`. The displayed counter is elapsed time since boot, not wall-clock time; Wi-Fi/NTP and an app display SDK are future steps.

To build locally from a shell with ESP-IDF activated:

```sh
./tools/build-elf-prototype.sh
```

GitHub Actions builds the Hello World and Clock ELF modules, the launcher firmware, and the merged flashable binary. Successful runs publish a `cyd-app-platform-elf-prototype` artifact.

This is a runtime experiment, not yet a finished launcher. It does not yet include filesystem-based app installation, a full app SDK, or GitHub manager.

## Planned architecture

- **Core** — shared hardware services and platform lifecycle.
- **Launcher** — app list, app launch/exit, platform settings.
- **App SDK** — documented API for compatible apps.
- **App Library** — installed app metadata, versions, source repository, and trust status.
- **Web Manager** — add repositories, inspect metadata, install, update, and remove apps.
- **Package tooling** — manifest validation and target-specific packages.
- **Examples** — Hello World and Clock modules to validate the SDK.

## Important design principle

An ordinary firmware `.bin` stored in a folder is not directly executable by ESP32. ELF modules may enable switching without rebooting, but they need a compatible toolchain, defined APIs, and careful resource cleanup. The runtime remains provisional until it has been tested on the target CYD.

## Initial milestones

1. Build and flash the ESP-IDF launcher skeleton.
2. Load and unload the Hello World ELF module.
3. Test repeated load/unload cycles and memory recovery.
4. Load modules from a filesystem instead of embedding them.
5. Define the app lifecycle and SDK contract.
6. Add a local app registry and per-app data directories.
7. Add GitHub repository registration and package updates.
8. Adapt [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) if its requirements fit the selected runtime.

## App trust and security

Apps from arbitrary repositories must be treated as unreviewed code. A repository cannot declare itself officially approved in its own manifest; approval must come from trusted platform metadata. Package integrity must be checked before installation. ELF loading is not a security sandbox, so third-party apps execute with platform privileges.

## Contributing

The runtime, SDK, manifest format, and packaging conventions are still being designed. Please open an issue to discuss significant architectural changes before investing in an app port.

## License

License not selected yet.
