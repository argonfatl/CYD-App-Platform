#!/usr/bin/env bash
# Build the launcher and example apps, and assemble a ready-to-use dist/ folder:
#   dist/cyd-launcher-full.bin   flash once at offset 0x0
#   dist/cyd-launcher.bin/.json  launcher image and metadata for Wi-Fi self-update
#   dist/catalog.json, app-*.bin app catalog for the launcher's GET APPS screen
#   dist/sd/apps/<id>/, cyd-sd-card.zip  copy to the SD card root
#
# Set CYD_SKIP_PIO=1 to skip PlatformIO apps, CYD_SKIP_PACKAGE=1 to skip the final packaging step.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "$0")/.." && pwd)"
DIST="$ROOT_DIR/dist"
LAUNCHER_DIR="$ROOT_DIR/firmware/launcher"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py was not found. Install ESP-IDF and export its environment first." >&2
    exit 1
fi

rm -rf "$DIST"
mkdir -p "$DIST/sd/apps"

echo "==> Building the launcher"
(cd "$LAUNCHER_DIR" && idf.py build && idf.py merge-bin -o "$DIST/cyd-launcher-full.bin")

# ESP-IDF only warns when an app is larger than some app partitions; the launcher must fit factory.
python3 "$IDF_PATH/components/partition_table/check_sizes.py" partition --type app --subtype factory \
    "$LAUNCHER_DIR/build/partition_table/partition-table.bin" "$LAUNCHER_DIR/build/cyd_launcher.bin"

# Launcher app image and metadata used for over-the-air self-updates.
cp "$LAUNCHER_DIR/build/cyd_launcher.bin" "$DIST/cyd-launcher.bin"
python3 - "$LAUNCHER_DIR/build/project_description.json" "$DIST/cyd-launcher.bin" "$DIST/cyd-launcher.json" <<'PY'
import hashlib, json, os, sys
desc, binary, dst = sys.argv[1:]
data = open(binary, "rb").read()
meta = {
    "version": json.load(open(desc))["project_version"],
    "sha256": hashlib.sha256(data).hexdigest(),
    "size": len(data),
}
json.dump(meta, open(dst, "w"), indent=2)
open(dst, "a").write("\n")
PY

# Build one ESP-IDF app and copy app.bin plus its manifest (with SHA-256) to the SD folder.
build_idf_app() {
    local app_dir="$1" app_id app_bin out
    app_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$app_dir/manifest.json")"

    echo "==> Building app '$app_id'"
    (cd "$app_dir" && idf.py build)

    app_bin="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["app_bin"])' \
        "$app_dir/build/project_description.json")"
    out="$DIST/sd/apps/$app_id"
    mkdir -p "$out"
    cp "$app_dir/build/$app_bin" "$out/app.bin"
    python3 "$ROOT_DIR/tools/write_app_manifest.py" "$app_dir/manifest.json" "$out/app.bin" "$out/manifest.json"
}

# System apps live in apps/<name>/, examples in apps/examples/<name>/.
# Apps with a platformio.ini are Arduino apps built with PlatformIO.
for manifest in "$ROOT_DIR"/apps/*/manifest.json "$ROOT_DIR"/apps/examples/*/manifest.json; do
    [ -f "$manifest" ] || continue
    app_dir="$(dirname "$manifest")"
    if [ -f "$app_dir/platformio.ini" ]; then
        if [ "${CYD_SKIP_PIO:-0}" = "1" ]; then
            echo "==> Skipping PlatformIO app in $app_dir (CYD_SKIP_PIO=1)"
        elif command -v pio >/dev/null 2>&1; then
            "$ROOT_DIR/tools/build-pio-app.sh" "$app_dir" "$DIST"
        else
            echo "WARNING: pio not found; skipping PlatformIO app in $app_dir" >&2
        fi
    else
        build_idf_app "$app_dir"
    fi
done

# Catalog, SD card zip, and checksums. CI runs this in a later job, after the
# PlatformIO apps (built separately) are added to dist/.
if [ "${CYD_SKIP_PACKAGE:-0}" != "1" ]; then
    python3 "$ROOT_DIR/tools/package.py" "$DIST"
fi

echo
echo "Build complete:"
echo "  Launcher (flash at 0x0): $DIST/cyd-launcher-full.bin"
echo "  SD card contents:        $DIST/sd/"
