# Vantage Browser

Vantage Browser (`vant`) is an early, Linux-first browser built with GTK 4 and
WebKitGTK. Vantage owns the native browser interface and local browser model;
WebKitGTK renders untrusted web content. A pinned copy of JS++ provides a
separate, capability-limited automation runtime and is never used to execute
page JavaScript.

Version 0.1.6 is the current release. It adds persistent normal-profile cookies
so website logins survive browser restarts, while private windows remain
ephemeral. It also includes the v0.1.5 tab-strip overflow fixes and the earlier
tab sizing and agent robustness improvements. The
native shell provides tabs, navigation
controls, persistent bookmarks, searchable history and downloads, private
windows, printing, zoom controls and familiar Chromium-style keyboard
shortcuts. Bookmarks can be added, edited and bulk-removed; history supports
bulk removal; downloads expose live byte progress, recent activity, link
copying and file-manager actions. A trusted local agent can drive the same
live Vantage windows and tabs through the stable v1 agent RPC, including direct
HTTP/API requests with `vant agent fetch`: arbitrary page
JavaScript, semantic snapshots and interaction, screenshots and content
inspection, WebKit/Vantage introspection, browser data/services, diagnostics
and event streaming.

## Using Vantage with AI agents

`vant agent` is the semantic browser-control interface. Start Vantage (under
Xvfb on a headless machine), then operate a page through its semantic
snapshot/interact loop — no coordinate automation:

```sh
vant agent open http://127.0.0.1:7335
vant agent snapshot                 # semantic inventory with @e references
vant agent fill  @e4_17 user@example.com
vant agent click @e4_19
vant agent screenshot /tmp/page.png
vant agent diagnostics              # console/JS-error ring
```

Prefer `page.interact`/`page.snapshot` for actually using websites;
`page.javascript` is inspection/support, and `net.fetch` is HTTP/API tooling —
not a substitute for rendered-browser dogfooding.

Start here: [docs/AGENT-GUIDE.md](docs/AGENT-GUIDE.md). Full reference:
[docs/AGENT-REFERENCE.md](docs/AGENT-REFERENCE.md).

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

Install the normal GStreamer media set for broad audio and video support:

```sh
sudo pacman -S --needed gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-plugins-ugly gst-libav
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

Open normally in fullscreen, or open one site as a standalone application with
no tabs, address bar, downloads button or settings menu:

```sh
./build/vant --fullscreen https://nift.dev
./build/vant --private
./build/vant --app https://nift.dev
./build/vant --app=https://nift.dev --fullscreen
```

Press `F11` to enter or leave fullscreen. Application windows retain ordinary
page actions such as reload, find and print through their keyboard shortcuts,
while navigation remains inside the launched site window.

The installed desktop launcher also exposes **New Window** and **New Private
Window** when right-clicked in a compatible application menu or dock.

On Wayland, GTK normally selects the correct backend automatically. To require
Wayland while diagnosing desktop integration:

```sh
GDK_BACKEND=wayland ./build/vant
```

Vantage uses compatibility video rendering by default. It avoids blank or
flickering video observed with some graphics drivers and virtual machines. Try
accelerated rendering from the internal Settings page, or configure it from the
command line:

```sh
./build/vant settings video-rendering compatibility
./build/vant settings video-rendering accelerated
```

The saved preference applies on the next launch. For one launch only, use
`--compatibility-video-rendering` (also available as `--fix-broken-video`) or
`--accelerated-video-rendering`.

## Test

```sh
make test
make test-sanitize
make vendor-check
make benchmark
```

`make test` runs the core suites and checks that the executable links against
the installed GTK and WebKitGTK runtime. `make test-sanitize` runs the portable
C++ and embedded-JS++ suites under ASan/UBSan. `make vendor-check` verifies
release-vendor consistency: Vantage's vendored JS++ pin and manifest version
must match the sibling JS++ repository's latest applicable release tag (stable
`v?N.N.N`, compared version-aware and peeled through annotated tags). An
unreleased sibling `HEAD` is informational and never a mismatch; the check
fails explicitly when no applicable release tag exists or the manifest version
does not correspond to the latest release. It expects the sibling `js`
repository to be present, or an explicit source can be supplied directly to
the checker:

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
./build/vant settings video-rendering compatibility
make clean
```

Desktop integration files are retained under `packaging/`. Public installation
and user documentation live at [vant.cx](https://vant.cx).

Tagged releases publish an immutable source archive and checksums. The public
installer verifies that archive and builds Vantage against the target system's
GTK, WebKitGTK and multimedia libraries; Vantage does not claim that one generic
Linux binary is portable across distributions.

The architecture, security boundary, checkpoint status and release procedure
are maintained in [HANDOVER.md](HANDOVER.md). More focused notes live under
[`docs/`](docs/).

## Project status

Vantage v0.1.6 is a released development-line browser. The native GTK/WebKitGTK
shell, fail-closed navigation policy, widget-independent tabs, SQLite session
recovery, private in-memory profiles, bookmarks/permissions/downloads, the
deterministic vendored JS++ snapshot and default-deny JS++ automation are
implemented and tested. The agent-native browser campaign (A0-A17) is complete:
a trusted local agent can operate the user's live browser through stable agent
protocol 1, and the hostile-page security boundary, concurrency, performance
and native WebKitGTK certification evidence are retained under
[`docs/evidence/`](docs/evidence/).

Remaining honest caveats: Vantage remains early Linux-first software rather
than a drop-in Chromium replacement, WebKitGTK capability gaps (for example
CDP-style response-body capture) are documented rather than emulated, and
sustained multi-day Wayland dogfooding plus Cortex/Warden-direct dogfooding
remain ongoing-evidence items recorded in the certification evidence.

## Licence

See [LICENSE](LICENSE). The vendored JS++ licence is retained separately under
`third_party/jspp/LICENSE`.
