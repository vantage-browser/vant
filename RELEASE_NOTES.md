# Vantage Browser v0.1.2

Vantage Browser v0.1.2 is the first release built around the completed
agent-native browser campaign (A0-A17). A deliberately trusted local agent can
drive the same live Vantage windows, tabs, profiles and authenticated sessions
the human is using, through a versioned local RPC and agent-oriented CLI.
Agent protocol 1 is **stable**.

The agent surface includes:

- stable live window and tab identities that are never silently retargeted;
- a versioned same-user Unix-socket RPC with bounded requests and a dependency-free reference client;
- live browser control: windows, tabs, navigation, back/forward, reload, stop, focus and waits;
- arbitrary page JavaScript with structured JSON-serialisable results;
- compact semantic page snapshots with `@e...` element references and strict stale-reference invalidation;
- semantic interaction (click, fill, type, select, check, scroll, key) and waits;
- viewport and full-document screenshots plus text/html/selection/metadata inspection;
- broad WebKit/Vantage introspection through `webkit.*` and a full `describe` catalog;
- browser-owned downloads, bookmarks, history, permissions, cookies and profile state;
- bounded page diagnostics (console, errors, unhandled rejections) and native diagnostics;
- a bounded cursor-based event stream covering lifecycle, navigation, downloads and web-process events;
- multi-controller correctness so humans and agents share one browser safely.

v0.1.2 also completes the native certification wall on the supported
Linux/Wayland development host: clean GTK 4.22.4/WebKitGTK 2.52.6 build, the
full test and sanitizer suites, hostile-page security-boundary testing,
concurrency, native performance measurements and five real shared
human+agent dogfooding workflows. The trusted-agent/untrusted-webpage trust
model is preserved: page content receives no Vantage RPC, filesystem, process
or privileged host access.

Known limitations carried into this release are documented in the repository
and website: WebKitGTK capability gaps such as arbitrary response-body capture
are not emulated, a pathological page evaluation can wedge its WebKit web
process (agent calls are bounded by the RPC timeout; there is no claimed
automatic recovery for that state), and Cortex/Warden-direct dogfooding plus
sustained multi-day Wayland endurance remain non-blocking ongoing evidence.

Vantage is Linux-first early software. It does not provide Chromium or Firefox
extensions, browser-account sync, or an independent rendering engine. WebKitGTK
provides web rendering and the core web security model.

Install from the immutable tagged source release with:

```sh
curl -fsSL https://vant.cx/install.sh | sh
```