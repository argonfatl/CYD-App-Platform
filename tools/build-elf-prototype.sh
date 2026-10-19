#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/apps/examples/hello-world"
FIRMWARE_DIR="$ROOT_DIR/firmware/launcher"
EMBEDDED_ELF="$FIRMWARE_DIR/main/hello_world.app.elf"

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

echo
echo "Build complete. Flash with:"
echo "  cd \"$FIRMWARE_DIR\" && idf.py -p PORT flash monitor"
echo "Replace PORT with the serial port for your CYD."
