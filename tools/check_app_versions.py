#!/usr/bin/env python3
"""Warn when an app changed since the last release but its manifest version did not.

The launcher offers an app update only when the version in the catalog differs from the
installed one, so a forgotten bump means devices keep the old build.

Usage: check_app_versions.py [base ref]   (default: the latest vX.Y.Z tag)
Prints GitHub Actions ::warning lines and exits 0; with --strict, exits 1 on findings.
"""
import glob
import json
import os
import subprocess
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
strict = "--strict" in sys.argv


def git(*cmd):
    return subprocess.run(["git", *cmd], capture_output=True, text=True).stdout.strip()


base = args[0] if args else git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
if not base:
    print("No release tag yet; nothing to compare.")
    sys.exit(0)

findings = 0
for manifest in sorted(glob.glob("apps/*/manifest.json") + glob.glob("apps/examples/*/manifest.json")):
    app_dir = os.path.dirname(manifest)
    changed = git("diff", "--name-only", base, "HEAD", "--", app_dir).splitlines()
    changed = [f for f in changed if not f.endswith(("README.md", "manifest.json")) and "/test/" not in f]
    if not changed:
        continue
    old = git("show", "%s:%s" % (base, manifest))
    new_version = json.load(open(manifest))["version"]
    old_version = json.loads(old)["version"] if old else None
    if old_version == new_version:
        findings += 1
        print("::warning file=%s::%s changed since %s (%d files) but version is still %s; "
              "devices will not be offered the update." % (manifest, app_dir, base, len(changed), new_version))
    else:
        print("%s: %s -> %s" % (app_dir, old_version, new_version))

sys.exit(1 if strict and findings else 0)
