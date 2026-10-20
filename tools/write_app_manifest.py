#!/usr/bin/env python3
"""Write an app's SD card manifest: the source manifest plus binary name and SHA-256.

Usage: write_app_manifest.py <source manifest.json> <app.bin> <output manifest.json>
Build-only fields (platformio_env, upstream) are dropped.
"""
import hashlib
import json
import sys

src, binary, dst = sys.argv[1:]
manifest = json.load(open(src))
for key in ("platformio_env", "upstream"):
    manifest.pop(key, None)
manifest["binary"] = "app.bin"
manifest["sha256"] = hashlib.sha256(open(binary, "rb").read()).hexdigest()
with open(dst, "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
