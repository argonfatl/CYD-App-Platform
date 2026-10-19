# Hello World ELF app

This is the first runtime probe. It is built as an ELF app, not as a normal bootable ESP32 firmware image.

From the repository root, after installing ESP-IDF and exporting its environment, run:

```sh
./tools/build-elf-prototype.sh
```

The build script compiles this module as a regression check, then builds the Clock module and embeds Clock in the launcher firmware.

The first prototype embeds a module in the firmware to keep the test deterministic. Once app load/unload works reliably, the next step is to load modules from a filesystem instead.
