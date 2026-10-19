# CYD App Platform

**One device. Many apps.**

CYD App Platform is an open-source application platform for the ESP32 Cheap Yellow Display (CYD). The goal is to provide a launcher, a standardized app SDK, and a web-based manager for installing, organizing, and updating compatible applications from GitHub.

The platform is designed to make one CYD device useful for many projects without requiring users to manually flash a different firmware for every experiment.

## Project status

🚧 **Early design and prototype stage.** The runtime and app-loading approach are not yet finalized. The first milestone is to validate the app lifecycle on real CYD hardware before building the full app catalog and web manager.

## Goals

- Provide a launcher for selecting and starting installed apps.
- Define a common app manifest and SDK.
- Keep each app's metadata, assets, and user data organized separately.
- Allow users to add an app by entering its GitHub repository URL.
- Check for app releases and download compatible, prebuilt packages.
- Distinguish community-added apps from apps reviewed and approved for the official catalog.
- Support a standard exit flow back to the launcher, with device restart as a fallback.
- Make updates robust, validate downloaded packages, and preserve user data.
- Target the common 2.8-inch CYD class (320×240 landscape UI), while documenting hardware variants explicitly.

## Planned architecture

- **Core** — shared hardware services and platform lifecycle.
- **Launcher** — app list, app launch/exit, platform settings.
- **App SDK** — documented entry points and services available to apps.
- **App Library** — installed app registry, manifests, versions, and trust status.
- **Web Manager** — add repositories, inspect app metadata, install, update, and remove apps.
- **Package tooling** — validation and packaging of compatible, prebuilt app builds.
- **Examples** — minimal Hello World and Clock apps to validate the SDK.

## Important design principle

An app stored in a filesystem folder is not automatically executable on ESP32. The platform must first choose and validate a runtime model. A dynamically loaded module could enable app exit without rebooting, but it requires a compatible app ABI and careful resource cleanup. Separate firmware images are a fallback option, but switching between them requires deliberate flash-partition and boot-selection management.

The first prototype will compare these constraints on actual hardware. The app manifest and user-facing app lifecycle should be designed to remain stable as implementation details evolve.

## Initial milestones

1. Document the target hardware and runtime constraints.
2. Build a minimal launcher skeleton.
3. Validate two small apps: Hello World and Clock.
4. Implement and test app start, exit, cleanup, and return to launcher (with reboot fallback).
5. Define the app manifest and SDK contract.
6. Add a local app registry and per-app data directories.
7. Add GitHub repository registration and package updates.
8. Adapt an existing project, starting with [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter), if the selected runtime supports the required features.

## App trust and security

Apps added from arbitrary repositories must be treated as unreviewed code. A manifest from a repository cannot declare itself officially approved. Approval should be represented by trusted platform metadata. Package downloads should be checked for integrity before installation, and the platform should preserve a known-good state when an update fails.

## Contributing

The runtime, SDK, manifest format, and packaging conventions are still being designed. Please open an issue to discuss significant architectural changes before investing in an app port.

## License

License not selected yet.
