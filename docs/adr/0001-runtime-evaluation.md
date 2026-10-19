# ADR 0001: Evaluate an ELF-based app runtime

- **Status:** Proposed; must be validated on hardware
- **Date:** 2026-10-19

## Context

The platform should let users install multiple apps independently and return to the launcher with an Exit command. Rebooting is acceptable as a fallback, but a no-reboot transition is preferred.

A normal ESP32 firmware `.bin` is a bootloader image, not a program that can simply be executed from a filesystem folder. The standard ESP-IDF bootloader selects an application partition in flash and transfers control to that application.

## Candidate: Espressif ELF Loader

Espressif publishes the `espressif/elf_loader` ESP-IDF component. It can load and execute ELF applications on the original ESP32, among other supported chips.

This is the leading candidate for the first prototype because it could allow the platform firmware to remain installed while app modules are stored separately.

## Constraints and risks

- ELF modules must be built for the exact target architecture and a compatible toolchain/runtime.
- Modules cannot assume arbitrary Arduino libraries or symbols are available to them.
- The platform needs a small, stable SDK/ABI for display, input, networking, storage, and lifecycle requests.
- App exit must stop app-owned tasks and release resources; unloading code alone is not sufficient.
- A module is not a security sandbox. Third-party modules should be treated as trusted code with access to the firmware's privileges.
- RAM, flash, and task-stack limits must be measured on the actual CYD board.

## Prototype plan

1. Build and flash a minimal ESP-IDF launcher firmware for the original ESP32 target.
2. Build the official ELF Loader example and confirm that a module can be loaded and executed.
3. Run two minimal modules in sequence and confirm that the loader can be deinitialized and reused safely.
4. Define a minimal app lifecycle contract: start, update/run, exit request, and cleanup.
5. Measure free heap and stack before and after repeated load/unload cycles.
6. Only then decide whether ELF modules are the v1 runtime or whether a reboot-based firmware-slot approach is the safer first release.

## Decision gate

Do not port CYD28PlaneSpotter until the prototype confirms the loader's lifecycle, symbol/API requirements, memory use, and compatibility with the required display and networking services.
