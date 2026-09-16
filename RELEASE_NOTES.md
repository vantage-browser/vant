# Vantage Browser v0.1.3

Vantage Browser v0.1.3 builds on the v0.1.2 release with a direct agent HTTP
surface and a re-vendored, release-provenanced JS++ dependency. Agent protocol
1 remains **stable** and unchanged.

## Agent HTTP (net.fetch)

Trusted local agents can now make direct HTTP/API requests through
`vant agent fetch`, application-side and separate from page work:

```sh
vant agent fetch https://api.example.com/status
vant agent fetch https://api.example.com/items '{"method":"POST","headers":{"Content-Type":"application/json"},"body":"{\"name\":\"demo\"}"}'
```

`net.fetch` accepts `url` (http/https only), `method`, arbitrary `headers`, an
exact `body`, a read/idle `timeout_ms`, and a `max_bytes` retained-body cap. It
returns `status`, `ok`, `url`, `headers`, `body`, `body_encoding`
(`text`/`base64`), `truncated` and `bytes`. Redirects are not auto-followed
(3xx responses are returned with their `Location` header), browser cookies and
session state are never attached, and TLS verification is unchanged. The
schema is discoverable through `vant agent call describe '{"name":"net.fetch"}'`.

This surface was independently reviewed after the initial implementation. The
review found and fixed genuine defects: a native `-Werror` build failure, a
request `Content-Type` lifetime/use-after-free that corrupted the header on
the wire, silent loss of malformed header objects, ineffective response-memory
bounding, incomplete JSON `\uXXXX` decoding, and ambiguous `bytes` reporting
for unknown-length responses. A deterministic local-HTTP regression suite
(`tests/net_fetch.py`) now covers methods, headers, bodies, the
`Content-Type` regression, 404/500 handling, redirects, UTF-8, binary/base64,
bounded truncation (including chunked bodies), timeout behavior, malformed
input, scheme/header-injection rejection and cookie isolation.

## Vendored JS++ provenance

The vendored JS++ dependency was updated to the released **JS++ v0.0.1**
(`8885f56`, tag `v0.0.1`) via the repository's vendoring mechanism, and
`vendor-check` now certifies release-vendor consistency against the sibling's
latest applicable release tag rather than a development `HEAD`.
`vendor-integrity` remains the independent content/checksum verification.

## Documentation

The repository reference and the public website were updated for `net.fetch`
and the corrected review findings, including the precise `bytes` semantics for
unknown-length/chunked responses, the read/idle timeout meaning, redirect
behavior, the absence of a default `User-Agent`, and cookie/session isolation.
Website documentation that advertised un-implemented functionality (for
example a developer-tools shortcut) was corrected.

## Compatibility

- Agent protocol **1** stays **stable**; additive optional fields/methods
  remain compatible.
- WebKitGTK 6.0 / GTK 4 remain the rendering stack; page content stays inside
  WebKit's normal web security/process boundary.
- Vantage remains Linux-first early software. Known limitations carried from
  v0.1.2 (WebKitGTK capability gaps such as arbitrary response-body capture,
  bounded-timeout containment for pathological page evaluations, and the
  non-blocking ongoing dogfooding evidence items) still apply.

Install from the immutable tagged source release with:

```sh
curl -fsSL https://vant.cx/install.sh | sh
```