# Vantage Browser

Vantage Browser (`vant`) is an early, Linux-first browser built with GTK 4 and
WebKitGTK. Vantage owns the native browser interface and local browser model;
WebKitGTK renders untrusted web content. A pinned copy of JS++ provides a
separate, capability-limited automation runtime and is never used to execute
page JavaScript.

This is a development preview, not a daily-use or security-ready release. The
native shell currently provides tabs, navigation controls, persistent bookmarks,
history and downloads, private windows, printing, zoom controls and familiar
Chromium-style keyboard shortcuts. Permission and automation models are not all
connected to the native interface yet.

## Install development dependencies

### Ubuntu 24.04 or newer

```sh
sudo apt update
sudo apt install build-essential pkg-config python3 \
  libgtk-4-dev libwebkitgtk-6.0-dev libsqlite3-dev
```

Vantage currently targets WebKitGTK 6.0. Older Ubuntu releases that do not
provide `libwebkitgtk-6.0-dev` are not supported by this development baseline.

### Omarchy / Arch Linux

```sh
sudo pacman -S --needed base-devel pkgconf python gtk4 webkitgtk-6.0 sqlite
```

Confirm that the native packages are visible:

```sh
pkg-config --modversion gtk4 webkitgtk-6.0 sqlite3
```

The accepted development baseline used GTK 4.14.5, WebKitGTK 2.52.3 and SQLite
3.45.1. Newer compatible distribution packages are expected to work, but each
release will eventually pin and test its supported platform matrix.

## Build

```sh
make
```

The executable is written to `build/vant`.

## Run

Open the Vantage start page:

```sh
./build/vant
```

Or open a URL directly:

```sh
./build/vant https://nift.dev
```

On Wayland, GTK normally selects the correct backend automatically. To require
Wayland while diagnosing desktop integration:

```sh
GDK_BACKEND=wayland ./build/vant
```

## Test

```sh
make test
make test-sanitize
make vendor-check
make benchmark
```

`make test` runs the core suites and checks that the executable links against
the installed GTK and WebKitGTK runtime. `make test-sanitize` runs the portable
C++ and embedded-JS++ suites under ASan/UBSan. `make vendor-check` verifies that
the embedded JS++ files exactly match the recorded upstream commit; it expects
the sibling `js` repository to be present, or an explicit source can be supplied
directly to the checker:

```sh
python3 tools/vendor_jspp.py --check --source /path/to/js
```

Run the real native smoke test from an active graphical session:

```sh
make smoke-native
```

## Useful commands

```sh
./build/vant --version
./build/vant --native-probe
make clean
```

The architecture, security boundary, checkpoint status and release procedure
are maintained in [HANDOVER.md](HANDOVER.md). More focused notes live under
[`docs/`](docs/).

## Project status

The first eight implementation checkpoints have established:

- a native GTK/WebKitGTK shell and fail-closed navigation policy;
- widget-independent tabs and keyboard commands;
- SQLite session recovery and private in-memory profiles;
- bookmarks, per-origin permission decisions and safe download paths;
- an initial lifecycle benchmark and tab-discard policy;
- a deterministic vendored JS++ snapshot; and
- default-deny JS++ automation capabilities.

Real Wayland/Omarchy interaction testing, native tab binding, full daily-browser
features, security review and release packaging remain future work. See the
checkpoint evidence under [`docs/evidence/`](docs/evidence/) for exact limits.

## Licence

See [LICENSE](LICENSE). The vendored JS++ licence is retained separately under
`third_party/jspp/LICENSE`.
