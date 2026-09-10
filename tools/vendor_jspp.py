#!/usr/bin/env python3
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path

PIN = "8fbad86a49123d973e5f61fbaf09fe5e92abd991"
VERSION = "0.0.0-dev"
FILES = ["LICENSE", "include/js.h", "src/Bytecode.cpp", "src/Bytecode.h", "src/Conversion.cpp", "src/Conversion.h", "src/Error.h", "src/Frontend.cpp", "src/Frontend.h", "src/Heap.cpp", "src/Intrinsics.cpp", "src/Intrinsics.h", "src/Property.h", "src/Runtime.cpp", "src/Runtime.h", "src/VM.cpp", "src/VM.h", "src/Version.h"]
root = Path(__file__).resolve().parents[1]

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def snapshot(source, destination):
    for relative in FILES:
        target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/relative,target)
    manifest={"schema_version":1,"product":"JS++","version":VERSION,"repository":"https://github.com/gantry-tools/js.git","commit":PIN,"files":{name:digest(destination/name) for name in FILES}}
    (destination/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")

parser=argparse.ArgumentParser();parser.add_argument("--source",type=Path,default=root.parent.parent/"js");parser.add_argument("--check",action="store_true");args=parser.parse_args()
source=args.source.resolve();actual=subprocess.check_output(["git","-C",str(source),"rev-parse","HEAD"],text=True).strip()
if actual!=PIN: raise SystemExit(f"JS++ source is {actual}; expected {PIN}")
destination=root/"third_party/jspp"
if args.check:
    with tempfile.TemporaryDirectory(prefix="vant-jspp-check-") as temporary:
        expected=Path(temporary)/"jspp";snapshot(source,expected)
        for relative in FILES+["manifest.json"]:
            if not (destination/relative).is_file() or (destination/relative).read_bytes()!=(expected/relative).read_bytes(): raise SystemExit(f"vendored JS++ drift: {relative}")
    print(f"vendored JS++ verified at {PIN}")
else:
    if destination.exists(): shutil.rmtree(destination)
    snapshot(source,destination);print(f"vendored JS++ {PIN}")
