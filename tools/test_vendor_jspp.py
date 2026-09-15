#!/usr/bin/env python3
"""Focused regression tests for Vantage JS++ release-vendor consistency.

Each test builds a synthetic sibling JS++ git repository and a synthetic
vendored tree, then runs `vendor_jspp.check()` and asserts the expected
pass/fail outcome. The sibling checkout is always read-only.

Run with: python3 tools/test_vendor_jspp.py
"""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import vendor_jspp as V

FILES = V.FILES


def git(repo, *arguments):
    return subprocess.run(["git", "-C", str(repo), *arguments], check=True,
                          capture_output=True, text=True)


def git_head(repo):
    return subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                   text=True).strip()


def write_files(path, marker):
    for relative in FILES:
        target = path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f"{marker}-{relative}\n")


def commit_all(repo, message):
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", message)


def setup_source(tmp, commits):
    """commits: list of dicts {marker, tags=[...], annotated=bool}."""
    source = tmp / "source"
    source.mkdir()
    git(source, "init", "-q")
    git(source, "config", "user.email", "vendor@test.invalid")
    git(source, "config", "user.name", "vendor test")
    for index, entry in enumerate(commits):
        write_files(source, entry["marker"])
        commit_all(source, f"commit {index}")
        head = git_head(source)
        for tag in entry.get("tags", []):
            if entry.get("annotated"):
                git(source, "tag", "-a", tag, "-m", tag, head)
            else:
                git(source, "tag", tag, head)
    return source


def setup_vendored(tmp, name, version, commit, marker):
    vendored = tmp / name
    write_files(vendored, marker)
    manifest = {
        "schema_version": 1,
        "product": "JS++",
        "version": version,
        "repository": V.REPOSITORY,
        "commit": commit,
        "files": {relative: V.digest(vendored / relative) for relative in FILES},
    }
    (vendored / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return vendored


def run_check(source, vendored):
    try:
        V.check(source, vendored)
        return 0
    except SystemExit as error:
        return 1 if error.code else 0


CASES = []
def case(name):
    def register(fn):
        CASES.append((name, fn))
        return fn
    return register


@case("HEAD exactly at latest release tag -> PASS")
def case_1(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": ["v1.0.0"]}])
    vendored = setup_vendored(tmp, "v1", "1.0.0", git_head(source), "A")
    assert run_check(source, vendored) == 0


@case("HEAD ahead of latest release tag -> PASS")
def case_2(tmp):
    source = setup_source(tmp, [
        {"marker": "A", "tags": ["v1.0.0"]},
        {"marker": "B"},
    ])
    head = git_head(source)
    assert head != git(source, "rev-parse", "v1.0.0^{commit}").stdout.strip()
    vendored = setup_vendored(tmp, "v1", "1.0.0",
                              git(source, "rev-parse", "v1.0.0^{commit}").stdout.strip(), "A")
    assert run_check(source, vendored) == 0


@case("vendor pin older than latest release tag -> FAIL")
def case_3(tmp):
    source = setup_source(tmp, [
        {"marker": "A", "tags": ["v1.0.0"]},
        {"marker": "B", "tags": ["v1.1.0"]},
    ])
    old_pin = git(source, "rev-parse", "v1.0.0^{commit}").stdout.strip()
    vendored = setup_vendored(tmp, "v1", "1.1.0", old_pin, "A")
    assert run_check(source, vendored) == 1


@case("vendor pin newer/unreleased relative to latest release tag -> FAIL")
def case_4(tmp):
    source = setup_source(tmp, [
        {"marker": "A", "tags": ["v1.0.0"]},
        {"marker": "B"},
    ])
    head = git_head(source)
    vendored = setup_vendored(tmp, "v1", "1.0.0", head, "B")
    assert run_check(source, vendored) == 1


@case("annotated release tag correctly peeled -> PASS")
def case_5(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": ["v1.0.0"], "annotated": True}])
    peeled = git(source, "rev-parse", "v1.0.0^{commit}").stdout.strip()
    assert peeled == git_head(source)
    vendored = setup_vendored(tmp, "v1", "1.0.0", peeled, "A")
    assert run_check(source, vendored) == 0


@case("irrelevant/non-release tags ignored -> PASS")
def case_6(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": [
        "v1.0.0", "dev", "experimental", "foo", "v1.0", "v1.0.0-rc1",
    ]}])
    vendored = setup_vendored(tmp, "v1", "1.0.0", git_head(source), "A")
    assert run_check(source, vendored) == 0


@case("no applicable release tag -> explicit failure")
def case_7(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": ["dev", "unstable"]}])
    vendored = setup_vendored(tmp, "v1", "1.0.0", git_head(source), "A")
    assert run_check(source, vendored) == 1


@case("manifest version/tag disagreement -> explicit failure")
def case_8(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": ["v2.0.0"]}])
    vendored = setup_vendored(tmp, "v1", "1.0.0", git_head(source), "A")
    assert run_check(source, vendored) == 1


@case("manifest version not a stable release version -> explicit failure")
def case_9(tmp):
    source = setup_source(tmp, [{"marker": "A", "tags": ["v1.0.0"]}])
    vendored = setup_vendored(tmp, "v1", "0.0.0-dev", git_head(source), "A")
    assert run_check(source, vendored) == 1


@case("sibling checkout absent -> explicit failure")
def case_10(tmp):
    vendored = setup_vendored(tmp, "v1", "1.0.0", "0" * 40, "A")
    assert run_check(tmp / "missing", vendored) == 1


def main():
    failed = 0
    for name, fn in CASES:
        with tempfile.TemporaryDirectory(prefix="vant-vendor-test-") as temporary:
            tmp = Path(temporary)
            try:
                fn(tmp)
                print(f"ok: {name}")
            except AssertionError:
                failed += 1
                print(f"FAIL: {name}")
            except SystemExit as error:
                failed += 1
                print(f"FAIL: {name} (unexpected exit {error.code})")
            except Exception as error:  # pragma: no cover - diagnostic
                failed += 1
                print(f"FAIL: {name} ({type(error).__name__}: {error})")
    if failed:
        print(f"\n{failed} vendor-check regression test(s) failed")
        return 1
    print(f"\nall {len(CASES)} vendor-check regression tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())