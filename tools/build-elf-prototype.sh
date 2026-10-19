#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/apps/examples/hello-world"
FIRMWARE_DIR="$ROOT_DIR/firmware/launcher"
EMBEDDED_ELF="$FIRMWARE_DIR/main/hello_world.app.elf"
MERGED_BIN="$FIRMWARE_DIR/build/cyd-app-platform-full.bin"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py was not found. Install ESP-IDF and export its environment first." >&2
    exit 1
fi

echo "==> Building the Hello World ELF app"
cd "$APP_DIR"
idf.py set-target esp32
idf.py build

if [ ! -f "$APP_DIR/build/cyd_hello_world.app.elf" ]; then
    echo "ERROR: expected ELF output was not produced: $APP_DIR/build/cyd_hello_world.app.elf" >&2
    exit 1
fi

cp "$APP_DIR/build/cyd_hello_world.app.elf" "$EMBEDDED_ELF"

echo "==> Building the launcher firmware with the embedded test module"
cd "$FIRMWARE_DIR"
idf.py set-target esp32
idf.py build

echo "==> Merging bootloader, partition table, and app into one flashable binary"
idf.py merge-bin -o "$MERGED_BIN"

if [ ! -s "$MERGED_BIN" ]; then
    echo "ERROR: merged firmware binary was not produced: $MERGED_BIN" >&2
    exit 1
fi

echo
echo "Build complete."
echo "Merged firmware binary: $MERGED_BIN"
echo "Flash it with esptool using offset 0x0, or use the generated flash_project_args."
echo "The merged binary includes the bootloader, partition table, and launcher app."
