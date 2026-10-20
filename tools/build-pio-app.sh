#!/usr/bin/env bash
# Build one PlatformIO (Arduino) app and place it in <dist>/sd/apps/<id>/.
# The app's manifest.json names the PlatformIO env to build ("platformio_env").
# Usage: build-pio-app.sh <app dir> <dist dir>
set -euo pipefail

APP_DIR="$(cd -- "$1" && pwd)"
DIST="$2"
TOOLS="$(cd -- "$(dirname -- "$0")" && pwd)"

read -r APP_ID APP_ENV APP_VERSION < <(python3 -c '
import json, sys
m = json.load(open(sys.argv[1]))
print(m["id"], m["platformio_env"], m["version"])' "$APP_DIR/manifest.json")

echo "==> Building PlatformIO app '$APP_ID' (env $APP_ENV, version $APP_VERSION)"
(cd "$APP_DIR" && PLATFORMIO_BUILD_FLAGS="-DFW_VERSION=\\\"$APP_VERSION\\\"" pio run -e "$APP_ENV")

OUT="$DIST/sd/apps/$APP_ID"
mkdir -p "$OUT"
cp "$APP_DIR/.pio/build/$APP_ENV/firmware.bin" "$OUT/app.bin"
python3 "$TOOLS/write_app_manifest.py" "$APP_DIR/manifest.json" "$OUT/app.bin" "$OUT/manifest.json"
