# Hello World ELF app

This is the first runtime probe. It is built as an ELF app, not as a normal bootable ESP32 firmware image.

From the repository root, after installing ESP-IDF and exporting its environment, run:

```sh
./tools/build-elf-prototype.sh
```

The script builds this module, copies the generated `cyd_hello_world.app.elf` into the launcher build as an embedded test asset, then builds the launcher firmware.

The first prototype embeds the module in the firmware to keep the test small and deterministic. Once load/unload works, the next step is to load modules from a filesystem instead.
