#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
vendor = root / "third_party" / "jspp"
manifest = json.loads((vendor / "manifest.json").read_text())

for relative, expected in manifest["files"].items():
    path = vendor / relative
    if not path.is_file():
        raise SystemExit(f"vendored JS++ file missing: {relative}")
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f"vendored JS++ checksum mismatch: {relative}")

print(f"vendored JS++ integrity verified at {manifest['commit']}")
