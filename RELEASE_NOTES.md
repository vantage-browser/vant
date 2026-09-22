# Vantage Browser v0.1.9

Vantage Browser v0.1.9 fixes normal-profile login persistence and includes the
v0.1.5 tab-strip overflow improvements. Agent protocol 1 remains **stable** and
unchanged.

## Login persistence

Normal Vantage sessions now configure WebKitGTK's cookie manager with a
persistent SQLite cookie store under the existing `vantage-browser/webkit`
profile directory. Persistent website logins therefore survive a complete
browser restart as expected. Private windows continue to use an ephemeral
network session and do not persist their cookies.

## Developer tools

WebKit DevTools are restored and explicitly enabled for every web view. `F12` and
`Ctrl+Shift+I` open the inspector for the active tab, and the page context menu
again exposes **Inspect Element**. DevTools are available in both normal and
private windows.

## Tab strip

The v0.1.5 changes cap the tab strip minimum and hide tabs that overflow the
available window width, preserving the favicon at the minimum visible tab width
and avoiding the previous ellipsis-only overflow state.

## Version

- Vantage Browser: 0.1.9
- Agent protocol: 1 (unchanged)

Vantage Browser v0.1.4 builds on the v0.1.3 release with a corrected tab
sizing model, fixes for newly opened tabs, a dark new-tab background, and two
agent robustness fixes. Agent protocol 1 remains **stable** and unchanged.

## Tab sizing

Newly opened tabs now report a small GTK minimum width (96px) and a natural
(preferred) width of 184px. Previously each tab's current width was set as a
GTK minimum-size request, so GTK derived a minimum window width of roughly
`tabs * 184 + chrome` and the window manager refused to shrink the window
below it — the browser could not be resized narrower once enough tabs were
open. With the corrected measurement model the window can shrink down to
`tabs * 96 + chrome`: while there is room every tab stays at 184px, and as the
strip runs out of space the tabs compress together uniformly. Adding tabs and
narrowing the window follow the same layout path, the `+` button and the
window controls are never compressed as though they were tabs, and tab titles
continue to ellipsize normally.

Also fixed a related layout bug where the custom tab layout allocated width to
every tab but positioned them all at the same origin, stacking them on top of
each other: space was reserved but only the first tab was visible or
clickable. Tabs are now laid out side by side.

## New-tab flash

Opening a new tab no longer flashes white. WebKitWebView paints an opaque
white background by default, so the page area flashed white in the instant
before the dark new-tab page rendered. Tab webviews are now painted with the
window's dark background, which the page's own background replaces once the
document draws.

A live `--tab-sizing-probe` and its regression suite (`tests/tab_sizing.py`)
verify the tab measurement/compression contract, the per-tab positions, and
the dark webview background. The probe drives a real window and asserts the
window minimum stays below `tabs * 184`, tabs hold 184px when there is room,
compress uniformly as the window shrinks, floor at 96px at the minimum, stay
side by side (never stacked), and keep a dark background.

## Agent robustness

- Page snapshots, inspections, interactions and diagnostics now run in a
  dedicated isolated world, so pages that set a strict `script-src` Content
  Security Policy remain drivable.
- JavaScript `alert()`, `confirm()` and `prompt()` dialogs are auto-dismissed
  (the standard headless-browser behaviour), so a page that calls them
  synchronously can no longer hang every later semantic operation.

## Documentation

The repository reference and the public website were updated to make the agent
interface discoverable for AI agents.

## Compatibility

- Agent protocol **1** stays **stable**; additive optional fields/methods
  remain compatible.
- WebKitGTK 6.0 / GTK 4 remain the rendering stack; page content stays inside
  WebKit's normal web security/process boundary.
- Vantage remains Linux-first early software. Known limitations carried from
  v0.1.3 (WebKitGTK capability gaps such as arbitrary response-body capture,
  bounded-timeout containment for pathological page evaluations, and the
  non-blocking ongoing dogfooding evidence items) still apply.

Install from the immutable tagged source release with:

```sh
curl -fsSL https://vant.cx/install.sh | sh
```
