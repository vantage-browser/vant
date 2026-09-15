#!/usr/bin/env python3
"""Vantage JS++ vendor verification.

Vantage vendors a reviewed snapshot of JS++ under `third_party/jspp`. Release-
vendor consistency is defined against the sibling JS++ repository's latest
applicable release tag, not against its development HEAD:

    Vantage vendored JS++ pin == commit of the latest applicable JS++ release tag

A sibling repository may legitimately contain unreleased development commits
beyond the version Vantage vendors. Such a HEAD is reported as informational
development state and is never treated as a mismatch.

The vendored manifest must additionally record a stable release version that
corresponds to the latest applicable release tag. This prevents an old vendor
pin from passing merely because it corresponds to some historical release.

`--check` fails explicitly (never by silently falling back to sibling HEAD)
whenever release-vendor consistency cannot be established: sibling absent, no
applicable release tags, a manifest version that is not a release version, a
manifest version that does not match the latest release tag, or a vendored pin
that differs from the latest release commit.

Only stable `v?N.N.N` tags participate and are compared with version-aware
ordering. Prerelease, dev and other tags are excluded. Annotated tags are
peeled to their commit. The sibling checkout is only ever read; no fetch or
update is performed.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FILES = ["LICENSE", "include/js.h", "src/Bytecode.cpp", "src/Bytecode.h",
         "src/Conversion.cpp", "src/Conversion.h", "src/Error.h",
         "src/Frontend.cpp", "src/Frontend.h", "src/Heap.cpp",
         "src/Intrinsics.cpp", "src/Intrinsics.h", "src/Property.h",
         "src/Runtime.cpp", "src/Runtime.h", "src/VM.cpp", "src/VM.h",
         "src/Version.h"]

REPOSITORY = "https://github.com/gantry-tools/js.git"
RELEASE_TAG = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")

root = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(repo: Path, *arguments: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *arguments], text=True).strip()


def release_tags(repo: Path):
    """Return [(tag, (major, minor, patch), commit)] for stable release tags.

    Only stable `v?N.N.N` tags participate; prerelease, dev and unrelated tags
    are ignored. Annotated tags are peeled to their commit.
    """
    try:
        listed = git(repo, "tag", "--list")
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"cannot list tags in sibling checkout {repo}: {error}")
    tags = []
    for tag in listed.splitlines():
        match = RELEASE_TAG.match(tag)
        if not match:
            continue
        version = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
        try:
            commit = git(repo, "rev-parse", f"{tag}^{{commit}}")
        except subprocess.CalledProcessError as error:
            raise SystemExit(f"malformed JS++ release tag {tag!r}: cannot resolve to a commit ({error})")
        tags.append((tag, version, commit))
    return tags


def latest_release(repo: Path):
    tags = release_tags(repo)
    if not tags:
        raise SystemExit(
            "vendored JS++ release consistency cannot be certified: "
            f"no applicable JS++ release tags (stable v?N.N.N) found in sibling checkout {repo}")
    return max(tags, key=lambda item: item[1])


def version_string(version) -> str:
    return ".".join(str(part) for part in version)


def blob_at(repo: Path, commit: str, relative: str) -> bytes:
    blob = git(repo, "rev-parse", f"{commit}:{relative}")
    return subprocess.check_output(["git", "-C", str(repo), "cat-file", "blob", blob])


def snapshot(repo: Path, commit: str, destination: Path, version: str):
    for relative in FILES:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob_at(repo, commit, relative))
    manifest = {
        "schema_version": 1,
        "product": "JS++",
        "version": version,
        "repository": REPOSITORY,
        "commit": commit,
        "files": {name: digest(destination / name) for name in FILES},
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def report(latest_tag, latest_version, latest_commit, manifest_version, pin, sibling_head):
    lines = [
        "vendored JS++:",
        f"  version: {manifest_version}",
        f"  commit:  {pin}",
        "sibling JS++ latest release:",
        f"  tag:     {latest_tag}",
        f"  version: {version_string(latest_version)}",
        f"  commit:  {latest_commit}",
        "sibling JS++ HEAD:",
        f"  {sibling_head} (development checkout; informational only)",
    ]
    return "\n".join(lines)


def check(source: Path, vendored: Path) -> int:
    if not source.is_dir():
        raise SystemExit(f"JS++ sibling checkout absent at {source}; cannot certify release-vendor consistency")
    manifest_path = vendored / "manifest.json"
    if not manifest_path.is_file():
        raise SystemExit(f"vendored JS++ manifest missing at {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    pin = manifest.get("commit", "")
    manifest_version = manifest.get("version", "")

    latest_tag, latest_version, latest_commit = latest_release(source)
    try:
        sibling_head = git(source, "rev-parse", "HEAD")
    except subprocess.CalledProcessError:
        sibling_head = "(no HEAD)"

    body = report(latest_tag, latest_version, latest_commit, manifest_version, pin, sibling_head)

    match = RELEASE_TAG.match(manifest_version)
    if not match:
        raise SystemExit(body + "\nvendor-check: FAIL\n"
                         f"  vendored JS++ manifest version {manifest_version!r} is not a stable release "
                         f"version; expected a release version such as {version_string(latest_version)}")
    manifest_version_tuple = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    if manifest_version_tuple != latest_version:
        raise SystemExit(body + "\nvendor-check: FAIL\n"
                         f"  vendored JS++ manifest version {manifest_version} does not match the latest "
                         f"applicable release tag {latest_tag} (version {version_string(latest_version)})")
    if pin != latest_commit:
        raise SystemExit(body + "\nvendor-check: FAIL\n"
                         f"  vendored JS++ pin {pin[:12]} does not match the latest applicable release tag "
                         f"{latest_tag} (commit {latest_commit[:12]})")

    with tempfile.TemporaryDirectory(prefix="vant-jspp-check-") as temporary:
        expected = Path(temporary) / "jspp"
        snapshot(source, latest_commit, expected, version_string(latest_version))
        for relative in FILES + ["manifest.json"]:
            vendored_file = vendored / relative
            expected_file = expected / relative
            if not vendored_file.is_file():
                raise SystemExit(f"vendored JS++ file missing: {relative}")
            if vendored_file.read_bytes() != expected_file.read_bytes():
                raise SystemExit(f"vendored JS++ drift: {relative}")

    print(body + "\nvendor-check: PASS")
    return 0


def regenerate(source: Path, vendored: Path) -> int:
    if not source.is_dir():
        raise SystemExit(f"JS++ sibling checkout absent at {source}; cannot vendor from it")
    latest_tag, latest_version, latest_commit = latest_release(source)
    if vendored.exists():
        shutil.rmtree(vendored)
    vendored.mkdir(parents=True)
    snapshot(source, latest_commit, vendored, version_string(latest_version))
    print(f"vendored JS++ {version_string(latest_version)} at {latest_commit} from release tag {latest_tag}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=root.parent.parent / "js")
    parser.add_argument("--vendored", type=Path, default=root / "third_party" / "jspp")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    vendored = args.vendored.resolve()
    if args.check:
        return check(source, vendored)
    return regenerate(source, vendored)


if __name__ == "__main__":
    sys.exit(main())