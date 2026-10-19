# Development setup

## Target

The initial target is the common 2.8-inch CYD with the original ESP32 and a 320×240 landscape UI. CYD boards have hardware variants; confirm the exact display driver and pin mapping before enabling display code.

## Toolchain

The prototype uses ESP-IDF because the leading app-loader candidate is an ESP-IDF component. Arduino libraries may be integrated later if needed, but app modules must not assume the full Arduino environment is available inside a dynamically loaded ELF.

Install ESP-IDF using Espressif's official setup instructions and activate its environment in your shell.

## Build the runtime prototype

From the repository root:

```sh
./tools/build-elf-prototype.sh
```

The script builds the Hello World ELF app, embeds the generated module into the launcher firmware, and builds the firmware. The generated embedded ELF is intentionally ignored by Git.

To flash and monitor, use the serial port for your board:

```sh
cd firmware/launcher
idf.py -p PORT flash monitor
```

Replace `PORT` with the correct serial port (for example, a device under `/dev/cu.*` on macOS).

## First validation checklist

- [ ] Confirm the exact CYD hardware variant.
- [ ] Build the Hello World ELF module.
- [ ] Build and flash the launcher firmware.
- [ ] Confirm the module prints its message and returns to the launcher core.
- [ ] Verify repeated load/unload cycles and memory recovery.
- [ ] Load a module from a filesystem instead of embedding it.
- [ ] Add display and touch services only after runtime behavior is understood.
