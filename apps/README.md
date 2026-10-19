# Apps

This directory contains examples built as loadable ELF modules, not normal bootable ESP32 firmware images.

- **hello-world** — validates the basic module load/run/return lifecycle.
- **clock** — validates periodic updates and the return-to-launcher lifecycle.

The current build script builds both examples as regression checks and embeds Clock into the test launcher. App modules are embedded only for this early prototype; filesystem-based loading is a later milestone.

Do not place arbitrary Arduino firmware `.bin` files here: those are not dynamically loadable app modules.
