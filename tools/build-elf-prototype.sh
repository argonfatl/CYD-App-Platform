#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELLO_DIR="$ROOT_DIR/apps/examples/hello-world"
CLOCK_DIR="$ROOT_DIR/apps/examples/clock"
FIRMWARE_DIR="$ROOT_DIR/firmware/launcher"
EMBEDDED_ELF="$FIRMWARE_DIR/main/clock.app.elf"
MERGED_BIN="$FIRMWARE_DIR/build/cyd-app-platform-full.bin"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py was not found. Install ESP-IDF and export its environment first." >&2
    exit 1
fi

echo "==> Building the Hello World regression example"
cd "$HELLO_DIR"
idf.py set-target esp32
idf.py build

if [ ! -f "$HELLO_DIR/build/cyd_hello_world.app.elf" ]; then
    echo "ERROR: expected Hello World ELF was not produced." >&2
    exit 1
fi

echo "==> Building the Clock ELF app"
cd "$CLOCK_DIR"
idf.py set-target esp32
idf.py build

if [ ! -f "$CLOCK_DIR/build/cyd_clock.app.elf" ]; then
    echo "ERROR: expected Clock ELF was not produced." >&2
    exit 1
fi

cp "$CLOCK_DIR/build/cyd_clock.app.elf" "$EMBEDDED_ELF"

echo "==> Building the launcher firmware with the embedded Clock module"
cd "$FIRMWARE_DIR"
idf.py set-target esp32
idf.py build

echo "==> Merging bootloader, partition table, and launcher into one flashable binary"
idf.py merge-bin -o "$MERGED_BIN"

if [ ! -s "$MERGED_BIN" ]; then
    echo "ERROR: merged firmware binary was not produced: $MERGED_BIN" >&2
    exit 1
fi

echo
echo "Build complete."
echo "Merged firmware binary: $MERGED_BIN"
echo "Clock ELF: $CLOCK_DIR/build/cyd_clock.app.elf"
echo "The merged binary includes the bootloader, partition table, and launcher app."
