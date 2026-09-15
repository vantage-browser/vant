# A17 native certification evidence

Date: 2026-09-15

## Environment

- Host: supported Ubuntu Vantage development machine (Wayland session, seat0).
- Compiler: c++ (GCC) 15.2.0-16ubuntu1.
- GTK: 4.22.4. WebKitGTK: 2.52.6. SQLite: 3.46.1.
- Vantage commit: A17 native certification (this commit).
- The implementation environment used for A0-A17 did not provide the
  GTK4/WebKitGTK development pkg-config files. All native gates in this
  document were executed on the real development host with the required
  packages installed.

## Clean build and test wall

- Clean build (`make clean && make`) passes with
  `-Wall -Wextra -Wpedantic -Werror` and no warnings.
- `make test` passes: unit tests, `desktop_entry.py`, `agent_security.py`
  source invariants, JS++ adapter/automation tests, `--headless-smoke` and
  `--native-probe` (reports GTK 4.22.4; WebKitGTK 2.52.6).
- `make smoke-native` passes on the real Wayland session.
- `make test-agent-native` (new live black-box agent regression suite) passes.
- `make test-sanitize` (ASan + UBSan) passes for every model/service unit.
- `make vendor-integrity` passes.
- `git diff --check` clean.

## Defects found and fixed during this campaign

The independent review surfaced defects that could not have been seen in the
UI-independent implementation environment:

1. **A7 screenshot API misuse (build-blocking).** `agent_snapshot_finished`
   treated `webkit_web_view_get_snapshot_finish` output as a
   `cairo_surface_t*`; on GTK4/WebKitGTK 6.0 it returns `GdkTexture*`. The
   code never compiled against real headers. Fixed with
   `gdk_texture_save_to_png`; viewport and full-document PNGs verified.
2. **`-Werror=misleading-indentation`** in `gobject_properties_json`. Fixed.
3. **Agent socket fd inheritance (security + CLI hang).** The agent Unix
   socket and its accepted connection fds were not `FD_CLOEXEC`. WebKitWebProcess
   and xdg-dbus-proxy children inherited them, so (a) the CLI's read-until-EOF
   hung on `browser.tab.new`/`browser.window.new`, and (b) untrusted web
   processes held fds to the RPC listener and in-flight connections. Fixed with
   `FD_CLOEXEC` on the listener and accepted fds; regression test in
   `tests/agent_rpc.cpp`.
4. **UTF-8 corruption in page JavaScript strings.** `javascript_string` used
   `g_strescape` octal escapes, mangling non-ASCII text (e.g. `héllo` became
   mojibake). Fixed to JSON/JS string escaping; Unicode round-trips verified.
5. **Hidden-element interaction.** `page.interact` silently acted on
   `display:none`/unrendered elements instead of returning the documented
   `hidden` error. Fixed with a `getClientRects()` check.
6. **`describe` self-discovery gap.** `describe` ignored its `name` parameter
   and documented only 5 methods. Replaced with a 42-method catalog that honors
   `name`.
7. **Rejected navigation silently succeeded.** `browser.navigate` to
   `file:`/`javascript:` returned `ok:true` while doing nothing. Now returns
   `navigation_rejected` (and `navigation_external` for mailto/tel).
8. **WebKit setting type coercion footgun.** `webkit.setting.set` silently
   coerced `"notabool"` to boolean `false`. Now rejects JSON type mismatches
   with `invalid_param_type`.
9. **Event stream breadth.** Only load/web-process/load-failed events were
   published. Added `tab.created`, `tab.closed`, `window.created`,
   `window.closed`, `download.started`, `download.finished`,
   `download.failed` to match the documented A11 contract.
10. **Closed-tab URI loss.** Closing a tab while still loading lost the URI in
    the restore path and `tab.closed` event; `pending_web_uri` is now included.

## A1-A3 live control

Real graphical Vantage launched on the Wayland session. Enumerate windows and
tabs, identify current tab, create/close/select tabs, create/close windows,
navigate/back/forward/reload/stop, focus/activate, URI/title/load inspection.
Stable IDs survive selection churn and window changes. Closed tab IDs return
`not_found` and are never rebound to replacement tabs.

## A4 arbitrary page JavaScript

`document.title`, `location.href`, selector counts, numbers, booleans, null,
`undefined`, Unicode, nested structures, arrays, big-but-bounded results,
script files and stdin. Syntax errors, thrown exceptions and reference errors
return structured `javascript_error` results with useful source locations.
Page JavaScript receives no `window.vantage`, RPC, process, filesystem or
privileged host objects (`typeof` probes all undefined). A tab closed during
an outstanding evaluation fails cleanly rather than retargeting.

## A5-A6 semantic snapshots and interaction

Form, SPA, long-page and large-DOM fixtures. Snapshots return role/name/
visibility/disabled/checked plus `@e<gen>_<n>` refs, capped at 500 elements
and compact enough for LLM use. Interaction covers click/focus/fill/type/
clear/select/check/uncheck/scroll/key plus selector targeting. Stale refs
after DOM mutation, SPA rerender and navigation always fail with
`stale_or_missing`; `page.wait` for text/selector/ref behaves correctly and is
bounded to 30s. Hidden and disabled elements return the documented errors.

## A7 rendered/content inspection

Viewport screenshots and full-document screenshots are valid PNGs (verified by
decode; full page 1100x19414). `page.inspect` text/html/selection/metadata
work. Binary image data is written to caller paths, never embedded in JSON.

## A8-A9 WebKit introspection and browser services

`webkit.webview` (62 properties), `webkit.settings` (58), `webkit.network`,
`webkit.setting.set` with strict type checking, and `describe`/`capabilities`
are exercised against WebKitGTK 2.52.6. Unknown properties fail explicitly.
Bookmarks, history, permissions, downloads (list/cancel/remove), cookies
(scoped by URI) and profile/private metadata operate through Vantage's
authoritative services. Private windows report `private:true,
persistent:false` and use the ephemeral network session.

## A10 diagnostics

Bounded page console (log/info/warn/error), JavaScript errors and unhandled
rejections are captured with timestamps. `page.native_diagnostics` reports
top-level load errors (connection refused verified) and web-process
termination. The recorder exposes no privileged object to the page.
CDP-equivalent response-body capture remains honestly unclaimed; the Resource
Timing API through arbitrary page JavaScript covers failed-resource discovery.

## A11-A12 events and multiple controllers

Cursor-based `events.since` with `latest`/`dropped`, bounded ring (2048),
`events.clear`, `vant agent events`, and lifecycle/download/navigation events
verified. Ten concurrent clients, a slow consumer that never reads its reply,
navigation-plus-close races and disconnect mid-async all leave the main loop
responsive. No client or race stalls the GTK/WebKit main thread.

## A13-A14 CLI and generic reference client

Exit classes 0/1/2 verified; concise vs `--json` output; stdin/script-file JS;
`vant agent --help` and `capabilities` self-describe. The dependency-free
`tools/vant_agent.py` reference client drives the same live browser state as
the CLI (tab created via one client is visible via the other).

## A15 hostile-page boundary

Hostile fixture page cannot discover Vantage RPC, native objects, GTK/GObject,
process/filesystem APIs; `file://` fetch/XHR blocked; `javascript:` and
`file:` navigation rejected by policy; cross-origin cookies isolated; page
content stays ordinary WebKit content. Malformed RPC, oversized (>1 MiB),
invalid methods, excessive nesting and mid-request disconnects all fail
explicitly and leave the server healthy. WebKitWebProcess no longer holds any
agent socket fd after the CLOEXEC fix.

## A16 performance and endurance

Median round-trip latencies on the real host: status 0.30 ms, browser.tabs
0.16 ms, page.javascript 0.56 ms, page.snapshot (11 elements) 0.99 ms,
page.snapshot (500-element cap) 4.8 ms, page.interact 1.25 ms, events.since
0.18 ms. Snapshot sizes: 1.1 KB (form), 43 KB (500-element cap). Idle CPU with
the agent service unused: 0.03% of one core over 40 s. RSS after 200 snapshots
plus 60 tab create/close cycles grew modestly (184 -> 190 MB), consistent with
WebKit process pooling rather than a Vantage-side leak. Event ring and
diagnostics stay bounded.

## Dogfooding

Five real workflows completed through the documented `vant agent` CLI against
the live, user-visible Vantage instance:

1. **Ordinary browsing**: find instance, list tabs, open example.com, snapshot,
   follow a link, back, new tab, interact with a form, screenshot, sensible end
   state.
2. **Shared control**: agent discovers the exact existing tabs without
   launching another browser; a page-driven live counter changed state while
   the agent observed it through authoritative browser state; agent scroll
   acted on the visible window.
3. **Authenticated session**: agent filled/submitted a local login form,
   creating an HttpOnly session cookie in the browser; navigating to the
   account page showed authenticated content with no credential export and no
   separate profile.
4. **Frontend development**: open localhost dev app, snapshot, interact,
   diagnose a `ReferenceError` via `page.diagnostics`, fix the source, reload,
   verify corrected behavior.
5. **Unrestricted escape hatch**: Resource Timing API via arbitrary JavaScript
   exposed a failed stylesheet that the semantic DSL does not cover;
   `webkit.webview` introspection and `webkit.setting.set` reached surface
   beyond the convenience commands.

Cortex/Warden direct dogfooding was not completed because the local Cortex
server's API is authentication-gated and no token was available. The fallback
required by the campaign was performed instead: another generic coding agent
(OpenCode/DeepSeek) drove Vantage through the documented interface for all
workflows. Vantage itself contains no Cortex/Warden dependency.

## Known limitations recorded

- An out-of-memory page allocation wedged the WebKit web process; Vantage
  handled it with a bounded 30 s timeout and other tabs stayed responsive, but
  the wedged tab requires WebKit process recycling (WebKit behavior, not a
  Vantage interface defect).
- CDP-equivalent response-body capture remains unsupported and unclaimed.
- `webkit.*` property availability is runtime-dependent on the installed
  WebKitGTK/GObject version.
- Sustained multi-day dogfooding is ongoing-evidence work rather than a
  completed release gate.

## Web-process wedge investigation (post-stable follow-up)

WebKitGTK 6.0 exposes two official recovery primitives since 2.34:
`webkit_web_view_get_is_web_process_responsive` and
`webkit_web_view_terminate_web_process`. Both were investigated empirically
against wedged processes; `page.native_diagnostics` now reports a
`responsive` field derived from the former, and RPC timeout errors now echo
the request id and method so an agent can correlate a bounded timeout to its
own operation.

Findings:

1. **Responsiveness detection does not cover wedged JavaScript.** A tab whose
   web process is stuck in a pathological evaluation (`while(true){}` or a
   2 GiB `Uint8Array`) reports `responsive:true` continuously (polled for
   40+ seconds). `get_is_web_process_responsive` reflects IPC/main-loop health,
   not whether a single evaluation will ever return. The common agent accident
   (stuck page JavaScript) is therefore not detectable through this API.
2. **Reload alone does not recover a wedged evaluation.** `browser.reload`
   posts a navigation to the same wedged process; the navigation queues behind
   the stuck evaluation and the tab remains unusable.
3. **Terminate-based recovery is not a clean supported path on this build.**
   `webkit_web_view_terminate_web_process` followed by a reload crashed the
   whole Vantage process once during testing (heap corruption,
   `munmap_chunk(): invalid pointer`) in a memory-deadlocked process state,
   and its triggering condition (`responsive == false`) is not reachable for
   the common wedge anyway. Vantage therefore does not expose a terminate
   operation to agents: it is unsafe, and the primitive is of little value
   when detection cannot identify the affected tabs.
4. **Genuine web-process crash recovery works through the existing API.** When
   a web process actually dies (verified by `kill -9`), Vantage records the
   termination in `page.native_diagnostics`, publishes
   `page.web_process_terminated`, and a subsequent `browser.reload` respawns
   the process and restores an interactive tab with its stable `tab_id`.

Conclusion: the bounded 30-second RPC timeout remains Vantage's containment
for wedged evaluations, and the clean supported recovery path for genuinely
dead processes is termination detection plus `browser.reload`. No unsafe
generic process-killing machinery was added.

## A17 decision

All three promotion criteria were met: native WebKitGTK behavior is proven,
live human/agent shared control is reliable, and hostile-page boundary testing
passes. Protocol 1 is promoted from **preview** to **stable** in this commit.
The corrections listed above are part of freezing the v1 contract; they are
additive (new event types, expanded `describe`, additional error codes that
replace silent no-ops).