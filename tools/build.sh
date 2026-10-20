#!/usr/bin/env bash
# Build the launcher and example apps, and assemble a ready-to-use dist/ folder:
#   dist/cyd-launcher-full.bin   flash once at offset 0x0
#   dist/cyd-launcher.bin/.json  launcher image and metadata for Wi-Fi self-update
#   dist/catalog.json, app-*.bin app catalog for the launcher's GET APPS screen
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

    local app_bin out
    out="$DIST/sd/apps/$app_id"
    mkdir -p "$out"

    if [ -f "$app_dir/platformio.ini" ]; then
        echo "==> Building Arduino/PlatformIO app '$app_id'"
        if ! command -v pio >/dev/null 2>&1; then
            echo "ERROR: PlatformIO CLI (pio) is required to build $app_id" >&2
            exit 1
        fi
        (cd "$app_dir" && pio run -e cyd)
        app_bin=".pio/build/cyd/firmware.bin"
        if [ ! -f "$app_dir/$app_bin" ]; then
            echo "ERROR: PlatformIO did not produce $app_bin" >&2
            exit 1
        fi
        cp "$app_dir/$app_bin" "$out/app.bin"
        # Enforce the app slot limit before packaging.
        python3 - "$out/app.bin" <<'PY_SIZE'
import os, sys
size = os.path.getsize(sys.argv[1])
limit = 0x2A0000
print("Plane Spotter image: %d bytes (slot limit %d bytes)" % (size, limit))
if size > limit:
    raise SystemExit("ERROR: app image exceeds ota_0 partition")
PY_SIZE
    else
        echo "==> Building ESP-IDF app '$app_id'"
        (cd "$app_dir" && idf.py build)
        app_bin="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["app_bin"])' \
            "$app_dir/build/project_description.json")"
        cp "$app_dir/build/$app_bin" "$out/app.bin"
    fi

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

# System apps live in apps/<name>/, examples in apps/examples/<name>/.
for manifest in "$ROOT_DIR"/apps/*/manifest.json "$ROOT_DIR"/apps/examples/*/manifest.json; do
    if [ -f "$manifest" ]; then
        package_app "$(dirname "$manifest")"
    fi
done

# Catalog for the launcher's GET APPS screen: one app-<id>.bin per app, with
# download URLs pointing at the release this build becomes (see ADR 0004).
python3 - "$DIST" "$LAUNCHER_DIR/build/project_description.json" "${GITHUB_REPOSITORY:-argonfatl/CYD-App-Platform}" <<'PY'
import glob, json, os, shutil, sys
dist, desc, repo = sys.argv[1:]
release = json.load(open(desc))["project_version"]
apps = []
for manifest_path in sorted(glob.glob(os.path.join(dist, "sd", "apps", "*", "manifest.json"))):
    manifest = json.load(open(manifest_path))
    asset = "app-%s.bin" % manifest["id"]
    binary = os.path.join(os.path.dirname(manifest_path), manifest["binary"])
    shutil.copyfile(binary, os.path.join(dist, asset))
    entry = {key: manifest[key] for key in ("id", "name", "version", "description") if key in manifest}
    entry["url"] = "https://github.com/%s/releases/download/%s/%s" % (repo, release, asset)
    entry["sha256"] = manifest["sha256"]
    entry["size"] = os.path.getsize(binary)
    apps.append(entry)
with open(os.path.join(dist, "catalog.json"), "w") as f:
    json.dump({"version": release, "apps": apps}, f, indent=2)
    f.write("\n")
PY

(cd "$DIST" && find . -type f ! -name SHA256SUMS | sort | xargs sha256sum > SHA256SUMS)

echo
echo "Build complete:"
echo "  Launcher (flash at 0x0): $DIST/cyd-launcher-full.bin"
echo "  SD card contents:        $DIST/sd/"
