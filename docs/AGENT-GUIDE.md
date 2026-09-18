# Using Vantage with AI agents

Vantage exposes a **stable, semantic browser-control API** over a same-user
Unix-domain socket. The command-line client is `vant agent`. It lets an agent
operate a real live Vantage window the way a user would — no coordinate
automation, no accessibility hacks.

Prefer this over GUI-coordinate automation: Vantage resolves controls
semantically (role/name), reports stale or hidden targets explicitly, and
exposes screenshots, page diagnostics and the network view as first-class
operations.

## 1. Start Vantage (headless example)

On a headless Linux VM (Ubuntu 24.04 shown):

```sh
# dependencies
sudo apt-get install -y build-essential pkg-config libgtk-4-dev libwebkitgtk-6.0-dev libsqlite3-dev xvfb

# build
make

# run under a virtual display
Xvfb :99 -screen 0 1280x900x24 &
export DISPLAY=:99
./build/vant http://127.0.0.1:7335 &
```

While Vantage runs, the agent socket is created at
`$XDG_RUNTIME_DIR/vantage-agent-<uid>.sock` (mode `0600`, same user only).

## 2. Drive a page semantically

```sh
vant agent status                     # is the socket reachable?
vant agent open http://127.0.0.1:7335 # navigate (applies the navigation policy)
vant agent snapshot                   # semantic inventory with @e references
```

`page.snapshot` returns a semantic inventory of interactive elements with
references such as `@e4_17`. Use those references (or CSS selectors) to
interact:

```sh
vant agent fill  @e4_17 user@example.com
vant agent fill  @e4_18 'correct horse battery'
vant agent click @e4_19
vant agent snapshot
vant agent screenshot /tmp/after-login.png
vant agent diagnostics               # console/JS-error ring
```

`page.interact` actions: `click`, `focus`, `fill`, `type`, `clear`, `select`,
`check`, `uncheck`, `scroll`, `key`. It accepts a semantic `ref` **or** a CSS
`selector`. It never retargets a stale reference to a different element; a
missing/disabled/hidden target returns `{ok:false,error:...}`.

`vant agent open` / `snapshot` / `click` / `fill` / `screenshot` /
`diagnostics` are convenience forms. Every method is also available through
the universal `vant agent call METHOD '{"params":...}'`.

## 3. Which operation to use

- `page.interact` / `page.snapshot` — **semantic browser operation**: actually
  using a website the way a user does. Preferred for dogfooding and UI tests.
- `page.javascript` — inspection/supporting operation (e.g. `document.title`),
  **not** a substitute for filling forms or clicking buttons.
- `net.fetch` — HTTP/API tooling, **not** a substitute for rendered-browser
  dogfooding.

## 4. Inspect failures

- `page.diagnostics` returns the bounded console/error ring (uncaught
  exceptions, failed fetches, CSP/mixed-content errors, asset failures).
- `page.screenshot` writes a PNG to a path for visual evidence.
- `browser.navigate` rejects disallowed schemes explicitly
  (`navigation_rejected`) instead of silently doing nothing.

## 5. Full reference

The complete method catalog, the `@e` reference model, socket details and the
frozen protocol contract live in:

- [docs/AGENT-REFERENCE.md](AGENT-REFERENCE.md)
- [docs/AGENT-COMPATIBILITY.md](AGENT-COMPATIBILITY.md)

Run `vant agent capabilities` and `vant agent call describe` on the installed
build for the authoritative runtime catalog.