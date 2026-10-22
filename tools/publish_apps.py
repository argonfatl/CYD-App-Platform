#!/usr/bin/env python3
"""Publish changed apps to the rolling "apps" release without a platform release.

Reads the apps built into <dist>/sd/apps/<id>/ (app.bin + manifest.json), compares them
with the catalog already published in the "apps" release, uploads app-<id>-<version>.bin
for every app whose version is new, and replaces catalog.json. The launcher reads
https://github.com/<repo>/releases/download/apps/catalog.json (see ADR 0004).

The "apps" release is a prerelease, so GitHub's "latest release" (used for launcher
self-updates) still points at the newest platform release. Its tag sits on the root
commit, out of the way of version tags.

Usage: publish_apps.py <dist> [--force]     (needs the gh CLI and GH_TOKEN)
"""
import datetime
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

TAG = "apps"
dist = sys.argv[1]
force = "--force" in sys.argv
repo = os.environ["GITHUB_REPOSITORY"]


def gh(*args, check=True):
    result = subprocess.run(["gh", *args], capture_output=True, text=True)
    if check and result.returncode != 0:
        sys.exit("gh %s failed: %s" % (" ".join(args), result.stderr.strip()))
    return result


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout.strip()


# Make sure the release exists.
if gh("release", "view", TAG, "--repo", repo, check=False).returncode != 0:
    root = git("rev-list", "--max-parents=0", "HEAD").splitlines()[0]
    gh("release", "create", TAG, "--repo", repo, "--prerelease", "--target", root, "--title", "Apps",
       "--notes", "Current builds of the CYD App Platform apps. The launcher's GET APPS screen reads "
                  "`catalog.json` from here. Updated automatically when an app's version changes on main.")
    print("Created the '%s' release" % TAG)

# Current catalog and assets.
assets = {a["name"] for a in json.loads(gh("release", "view", TAG, "--repo", repo, "--json", "assets").stdout)["assets"]}
old = {"apps": []}
if "catalog.json" in assets:
    with tempfile.TemporaryDirectory() as tmp:
        gh("release", "download", TAG, "--repo", repo, "--pattern", "catalog.json", "--dir", tmp)
        old = json.load(open(os.path.join(tmp, "catalog.json")))
old_by_id = {a["id"]: a for a in old.get("apps", [])}

apps, uploads = [], []
for manifest_path in sorted(glob.glob(os.path.join(dist, "sd", "apps", "*", "manifest.json"))):
    manifest = json.load(open(manifest_path))
    app_id, version = manifest["id"], manifest["version"]
    binary = os.path.join(os.path.dirname(manifest_path), manifest["binary"])
    previous = old_by_id.get(app_id)
    if previous and previous["version"] == version and not force:
        apps.append(previous)  # unchanged: keep the published build
        continue
    asset = "app-%s-%s.bin" % (app_id, version)
    entry = {key: manifest[key] for key in ("id", "name", "version", "description") if key in manifest}
    entry["url"] = "https://github.com/%s/releases/download/%s/%s" % (repo, TAG, asset)
    entry["sha256"] = manifest["sha256"]
    entry["size"] = os.path.getsize(binary)
    apps.append(entry)
    uploads.append((binary, asset))
    print("%s: %s -> %s" % (app_id, previous["version"] if previous else "new", version))

if not uploads and len(apps) == len(old.get("apps", [])):
    print("No app versions changed; nothing to publish.")
    sys.exit(0)

# Upload binaries first, then the catalog that points at them.
with tempfile.TemporaryDirectory() as tmp:
    for binary, asset in uploads:
        target = os.path.join(tmp, asset)
        shutil.copyfile(binary, target)
        gh("release", "upload", TAG, target, "--repo", repo, "--clobber")
    catalog = {
        "version": "apps-" + datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M"),
        "apps": apps,
    }
    path = os.path.join(tmp, "catalog.json")
    with open(path, "w") as f:
        json.dump(catalog, f, indent=2)
        f.write("\n")
    gh("release", "upload", TAG, path, "--repo", repo, "--clobber")
print("Published catalog with %d apps" % len(apps))

# Remove binaries that neither the new nor the previous catalog references; devices that
# loaded the previous catalog moments ago can still download from it.
keep = {a["url"].rsplit("/", 1)[1] for a in apps + old.get("apps", [])} | {"catalog.json"}
for name in sorted(assets - keep):
    if name.startswith("app-"):
        gh("release", "delete-asset", TAG, name, "--repo", repo, "--yes")
        print("Removed old %s" % name)
