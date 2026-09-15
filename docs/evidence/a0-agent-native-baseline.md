# A0 agent-native baseline

Date: 2026-09-15

## Repository baseline

- Starting HEAD: `b7986bc Plan agent-native browser control`.
- Vantage version: 0.1.2 development line.
- Required native packages: GTK 4, WebKitGTK 6.0 and SQLite 3.
- Normal gates: `make test`, `make test-sanitize`, `make smoke-native`, `make vendor-check`, `make vendor-integrity`.
- This execution environment does not provide the GTK4/WebKitGTK development pkg-config files, so native build/runtime gates cannot execute here. Pure C++ model tests remain available and are used checkpoint-by-checkpoint.

## Existing ownership map

- `src/browser_model.*`: UI-independent tab model and shortcut semantics. It already assigns monotonic tab IDs, but the native GTK/WebKit application does not use those IDs.
- `src/native_app.*`: GTK windows, native tabs, WebKitWebView ownership, navigation, page actions, downloads, history/bookmarks UI, site data, permissions and network sessions. Most live browser operations are currently widget-coupled here.
- `src/user_data.*`: authoritative SQLite services for bookmarks, history, downloads, favicons and permission decisions.
- `src/automation.*` + `src/jspp_adapter.*`: private bounded JS++ automation runtime. It is intentionally not a WebKit page realm and remains separate from the trusted-agent RPC campaign.
- `docs/AUTOMATION.md`: documents that bounded JS++ surface; it is not the future full-trust agent API.

## Existing useful WebKit surface

The native application already owns/uses WebKitWebView navigation and state, JavaScript evaluation for find support, find controllers, network sessions/cookie managers, downloads, TLS/load policy handling, favicon/progress/title/URI state, context menus, printing and website-data/site-permission related UI. A0 treats these as foundations to expose through a model/service bridge rather than duplicating them in an automation-only browser.

## Refactoring pressure

Tab selection/close/move/create, active-view tracking, navigation, downloads and site-data operations are still primarily expressed as GTK callback functions over `WindowState`/`TabState`. A1-A9 should introduce stable IDs and a trusted application-side dispatch layer over these existing objects. The human UI and agent RPC must converge on the same operations rather than maintaining parallel browser state.
