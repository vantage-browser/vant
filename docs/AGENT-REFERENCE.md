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
