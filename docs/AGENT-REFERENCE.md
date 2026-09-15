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
