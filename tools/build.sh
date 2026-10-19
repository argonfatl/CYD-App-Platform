#!/usr/bin/env bash
# Build the launcher and example apps, and assemble a ready-to-use dist/ folder:
#   dist/cyd-launcher-full.bin   flash once at offset 0x0
#   dist/cyd-launcher.bin/.json  launcher image and metadata for Wi-Fi self-update
#   dist/sd/apps/<id>/           copy to the SD card root
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

# Package one app: build it, copy app.bin, and write manifest.json with its SHA-256.
package_app() {
    local app_dir="$1"
    local app_id
    app_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$app_dir/manifest.json")"

    echo "==> Building app '$app_id'"
    (cd "$app_dir" && idf.py build)

    local app_bin out
    app_bin="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["app_bin"])' \
        "$app_dir/build/project_description.json")"
    out="$DIST/sd/apps/$app_id"
    mkdir -p "$out"
    cp "$app_dir/build/$app_bin" "$out/app.bin"

    python3 - "$app_dir/manifest.json" "$out/app.bin" "$out/manifest.json" <<'PY'
import hashlib, json, sys
src, binary, dst = sys.argv[1:]
manifest = json.load(open(src))
manifest["binary"] = "app.bin"
manifest["sha256"] = hashlib.sha256(open(binary, "rb").read()).hexdigest()
json.dump(manifest, open(dst, "w"), indent=2)
open(dst, "a").write("\n")
PY
}

for app_dir in "$ROOT_DIR"/apps/examples/*/; do
    if [ -f "$app_dir/manifest.json" ]; then
        package_app "${app_dir%/}"
    fi
done

(cd "$DIST" && find . -type f ! -name SHA256SUMS | sort | xargs sha256sum > SHA256SUMS)

echo
echo "Build complete:"
echo "  Launcher (flash at 0x0): $DIST/cyd-launcher-full.bin"
echo "  SD card contents:        $DIST/sd/"
