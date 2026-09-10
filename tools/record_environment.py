#!/usr/bin/env python3
import argparse
import json
import platform
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True)
args = parser.parse_args()

def command(*parts):
    try:
        return subprocess.check_output(parts, text=True, stderr=subprocess.STDOUT).splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        return None

payload = {
    "schema_version": 1,
    "platform": platform.platform(),
    "machine": platform.machine(),
    "compiler": command("c++", "--version"),
    "python": platform.python_version(),
    "dependencies": {
        "gtk4": command("pkg-config", "--modversion", "gtk4") if shutil.which("pkg-config") else None,
        "webkitgtk_6": command("pkg-config", "--modversion", "webkitgtk-6.0") if shutil.which("pkg-config") else None,
    },
    "notes": ["Null dependency values mean this environment cannot build the native GTK/WebKit frontend."],
}
Path(args.output).write_text(json.dumps(payload, indent=2) + "\n")
