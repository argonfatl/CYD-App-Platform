#!/usr/bin/env python3
"""Finish a dist/ folder after the launcher and apps are built.

Writes, next to dist/cyd-launcher.json (which gives the release version):
  catalog.json and app-<id>.bin  for the launcher's GET APPS screen (ADR 0004)
  cyd-sd-card.zip                the sd/ folder, ready to unzip onto a card
  SHA256SUMS                     checksums of everything above

Usage: package.py <dist> [owner/repo]
"""
import glob
import hashlib
import json
import os
import shutil
import sys
import zipfile

dist = sys.argv[1]
repo = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("GITHUB_REPOSITORY", "argonfatl/CYD-App-Platform")
release = json.load(open(os.path.join(dist, "cyd-launcher.json")))["version"]

apps = []
for manifest_path in sorted(glob.glob(os.path.join(dist, "sd", "apps", "*", "manifest.json"))):
    manifest = json.load(open(manifest_path))
    binary = os.path.join(os.path.dirname(manifest_path), manifest["binary"])
    asset = "app-%s.bin" % manifest["id"]
    shutil.copyfile(binary, os.path.join(dist, asset))
    entry = {key: manifest[key] for key in ("id", "name", "version", "description") if key in manifest}
    entry["url"] = "https://github.com/%s/releases/download/%s/%s" % (repo, release, asset)
    entry["sha256"] = manifest["sha256"]
    entry["size"] = os.path.getsize(binary)
    apps.append(entry)
    print("catalog: %-20s %-8s %8d bytes" % (entry["id"], entry["version"], entry["size"]))

with open(os.path.join(dist, "catalog.json"), "w") as f:
    json.dump({"version": release, "apps": apps}, f, indent=2)
    f.write("\n")

with zipfile.ZipFile(os.path.join(dist, "cyd-sd-card.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for path in sorted(glob.glob(os.path.join(dist, "sd", "**", "*"), recursive=True)):
        if os.path.isfile(path):
            z.write(path, os.path.relpath(path, os.path.join(dist, "sd")))

lines = []
for path in sorted(glob.glob(os.path.join(dist, "**", "*"), recursive=True)):
    if os.path.isfile(path) and os.path.basename(path) != "SHA256SUMS":
        digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
        lines.append("%s  ./%s\n" % (digest, os.path.relpath(path, dist)))
with open(os.path.join(dist, "SHA256SUMS"), "w") as f:
    f.writelines(lines)
