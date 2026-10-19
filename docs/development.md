# Development setup

## Target

The initial target is the common 2.8-inch CYD with the original ESP32 and a 320x240 landscape UI. CYD boards have hardware variants; confirm the exact display driver and pin mapping before enabling display code.

## Toolchain

The platform prototype will use ESP-IDF because the leading app-loader candidate is an ESP-IDF component. Arduino libraries may be integrated later if needed, but app modules must not assume the full Arduino environment is available inside a dynamically loaded ELF.

Install ESP-IDF using Espressif's official setup instructions, then build the launcher from `firmware/launcher`:

```sh
idf.py set-target esp32
idf.py build
```

Flash and monitor commands will be documented after the first build is validated on the target board.

## First validation checklist

- [ ] Identify the exact CYD hardware/display variant.
- [ ] Build the launcher firmware for `esp32`.
- [ ] Flash and confirm the startup log over serial.
- [ ] Build and run the official ESP-ELFLoader example.
- [ ] Verify repeated load/unload and memory recovery.
- [ ] Add display and touch services only after the runtime test passes.
