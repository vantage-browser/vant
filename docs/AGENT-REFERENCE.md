# Vantage agent reference

Vantage exposes a versioned local agent RPC over a same-user Unix-domain socket. The first client is `vant agent`.

```sh
vant agent status
vant agent version
vant agent capabilities
vant agent call METHOD '{"parameter":"value"}'
```

Requests and responses are newline-delimited JSON. Protocol v1 requests contain `version`, `id`, `method`, and `params`. Messages are bounded to 1 MiB and individual CLI calls have a bounded wait. The socket is created under `$XDG_RUNTIME_DIR` when available and mode 0600. This is process-local IPC for an already trusted user/agent, not a webpage capability.

The API is self-describing through `capabilities`; later checkpoints extend namespaces without changing the transport.

## Live browser control

The browser namespace targets stable numeric `window_id` and `tab_id` handles. Closing a tab invalidates its handle; IDs are never reused during a Vantage process lifetime.

Methods: `browser.windows`, `browser.tabs`, `browser.window.new`, `browser.tab.new`, `browser.tab.select`, `browser.tab.close`, `browser.navigate`, `browser.reload`, `browser.stop`, `browser.back`, `browser.forward`. Omit `tab_id` to target the current live tab. Selecting a tab also selects/presents it in the normal human-visible Vantage UI.

## Arbitrary page JavaScript

`page.javascript` evaluates arbitrary JavaScript in the selected live WebKit view. `vant agent js 'document.title'`, `vant agent js --file script.js`, and piping a script to `vant agent js -` are convenience forms. JSON-serialisable values are returned structurally; `undefined` maps to JSON `null`. JavaScript exceptions become structured `javascript_error` responses.

This execution is initiated by the trusted Vantage application against the page. It does not inject Vantage RPC, filesystem/process APIs or privileged JS++ host objects into the page. Main-frame evaluation is implemented first; explicit subframe/world targeting remains capability-versioned where WebKitGTK exposes a suitable stable application API.

## Semantic snapshots

`page.snapshot` returns a compact semantic inventory of up to 500 interactive elements with role, accessible-ish name/text, visibility/state and references such as `@e4_17`. The first number is the page's semantic generation. A MutationObserver invalidates all references whenever the DOM mutates; a new snapshot creates fresh refs. Vantage never silently retargets a stale ref to a different element.

The initial semantic extractor covers native interactive controls, ARIA-role elements, contenteditable and tabindex targets. Shadow DOM and cross-origin iframe depth are reported as current limitations rather than bypassing WebKit's page-origin rules.

## Semantic interaction and waits

`page.interact` accepts `action` plus either a semantic `ref` or CSS `selector`. Actions are `click`, `focus`, `fill`, `type`, `clear`, `select`, `check`, `uncheck`, `scroll`, and `key`. Ref lookup never falls back to a different element after invalidation. `page.wait` polls without blocking GTK/WebKit for a selector, current semantic ref, or visible body text, with `timeout_ms` bounded to 30 seconds.

## Rendered and content inspection

`page.screenshot` writes a PNG directly to a caller-selected filesystem path. `full_page:false` captures the visible WebKit region; `full_page:true` requests the full document through WebKit's snapshot API. Binary image data is never embedded in JSON.

`page.inspect` supports `kind` values `text`, `html`, `selection`, and `metadata`. Metadata includes URL/title, viewport size, scroll position and document dimensions. Agents can combine semantic snapshots with rendered screenshots for verification.

## WebKit introspection

`webkit.webview`, `webkit.settings`, and `webkit.network` expose the readable GObject properties of the selected Vantage-owned WebKit objects. This deliberately provides broad, version-sensitive introspection without pretending arbitrary C pointers can be serialised safely across RPC. `webkit.setting.set` can set writable boolean/string/integer WebKitSettings properties. `describe` documents stable Vantage methods; `capabilities` identifies available namespaces.

Unknown properties and unsupported property types fail explicitly. Agents should discover properties on the installed WebKitGTK rather than assuming every distribution exposes the same version.

## Browser-owned data and services

The trusted agent API uses Vantage's authoritative services rather than shadow copies: `downloads.list/cancel/remove`, `bookmarks.list/add/remove`, `history.list/clear`, `permissions.list/set`, `browser.cookies`, and `browser.profile`. Cookie access is scoped by a requested/current URI through WebKit's cookie manager; private profiles report non-persistence and continue to use Vantage's ephemeral network session.

Destructive operations are explicit RPC methods and return structured success/errors. There is intentionally no extra agent-only confirmation ceremony: the caller is already a trusted same-user agent.

## A10 diagnostics

`page.diagnostics` returns the bounded page-world console/error/unhandled-rejection ring for a tab. `page.diagnostics.clear` returns then clears it. `page.native_diagnostics` reports the last top-level load error and WebKit web-process termination observed by Vantage. The page recorder is injected by Vantage but deliberately exposes no native Vantage object to page JavaScript.

```sh
vant agent call page.diagnostics '{"tab_id":1}'
vant agent call page.diagnostics.clear '{"tab_id":1}'
vant agent call page.native_diagnostics '{"tab_id":1}'
```

Vantage does not claim CDP-equivalent arbitrary response-body capture. Resource/network coverage remains capability-described and can expand when WebKitGTK offers a clean application-side primitive.

## A11 event stream

Vantage maintains a bounded application-side event sequence. `events.since` is cursor-based and reconnect-safe: pass the last sequence you consumed as `after`. The ring is bounded; `dropped` reports overflow. The CLI aliases `vant agent events` and `vant agent watch` to an immediate event read; long-running clients should loop `events.since` themselves so a slow reader never holds the GTK/WebKit thread open.

```sh
vant agent call events.since '{"after":0,"limit":256}'
vant agent events
vant agent call events.clear '{}'
```

## A12 multiple controllers and races

The RPC acceptor services clients independently, so one slow request does not serialize unrelated agent clients. WebKit mutations are still marshalled onto GTK's main context. Window/tab IDs and semantic refs fail explicitly after closure/invalidation; they are never rebound to a replacement object. Outstanding page operations can therefore complete, fail stale/not-found, or time out without retargeting another tab.

## A13 CLI contract

The CLI has stable exit classes: 0 success, 1 runtime/RPC failure, 2 invalid CLI usage. Default output is the concise RPC result; `--json` emits the complete protocol envelope. Common one-shot aliases avoid hand-writing RPC JSON.

```sh
vant agent tabs
vant agent open https://example.com 1
vant agent snapshot 1
vant agent click @e3_4 1
vant agent fill @e3_7 hello 1
vant agent diagnostics 1
vant agent --json capabilities
vant agent call page.content '{"tab_id":1,"kind":"text"}'
cat script.js | vant agent js -
vant agent js --file script.js
```

`call` remains the universal escape hatch. `capabilities` and `describe` are authoritative for runtime discovery.

## A14 direct clients and adapters

The Unix RPC is authoritative; the CLI is only one client. `tools/vant_agent.py` is a dependency-free reference implementation showing the complete framing needed by any agent/runtime. Vantage contains no Cortex/Warden dependency: Gantry agents can use the shell CLI or RPC exactly like any unrelated coding agent.

```sh
python3 tools/vant_agent.py browser.tabs '{}'
python3 tools/vant_agent.py page.snapshot '{"tab_id":1}'
```

An MCP adapter is intentionally optional and should translate MCP calls onto this same RPC rather than becoming a second browser-control implementation. This keeps Vantage equally usable from Cortex, Warden, Codex, Claude Code, OpenCode, shell scripts and custom agents.

## A15 security boundary

RPC input is bounded to 1 MiB and the local socket is mode 0600. Vantage does not expose the RPC or native/JS++ host objects into web pages. Agent-requested page JavaScript remains page JavaScript; it does not become native application code. WebKit's origin, TLS, permission, web-process and sandbox boundaries remain intact.

## A16 performance bounds

Agent state is deliberately bounded: semantic snapshots return at most 500 interactive elements, page diagnostics retain 500 entries, the application event sequence retains 2,048 entries, and a single event read returns at most 1,024. The RPC acceptor sleeps while idle. See `docs/evidence/a16-agent-performance.md` for benchmark evidence and native-host certification caveats.
