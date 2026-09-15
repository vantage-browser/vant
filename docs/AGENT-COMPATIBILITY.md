# Agent API compatibility

## Current status

The wire protocol number is `1` and the agent API is **stable v1** as of the
A17 native certification commit (2026-09-15), based on the completed native
certification wall in `docs/evidence/a17-native-certification.md`.

`capabilities` reports `stability: "stable"`.

Within stable protocol 1, integrations should call `capabilities` and
`describe` and tolerate additive methods/fields. Additive optional
fields/methods remain compatible; removals, semantic retargeting,
required-field changes and incompatible result changes require a new protocol
version or a documented deprecation window.

## Correction history during preview

The following behaviors were corrected while protocol 1 was still preview and
are now part of the frozen v1 contract:

- `page.interact` on a hidden (unrendered) element returns `{ok:false,
  error:"hidden"}` instead of silently acting.
- `browser.navigate` to a rejected scheme returns `navigation_rejected`, and
  to an external-protocol handoff returns `navigation_external`, instead of a
  silent no-op success.
- `webkit.setting.set` rejects JSON type mismatches with `invalid_param_type`
  instead of silently coercing values (for example a string into a boolean
  setting).
- `describe` returns a complete method catalog and honors the `name`
  parameter.
- The agent event sequence includes tab/window lifecycle and download events.
- The agent Unix socket is close-on-exec so WebKit web processes never inherit
  it.

## WebKit-dependent gaps

Vantage exposes what WebKitGTK can support cleanly. It does not emulate
Chromium CDP merely for surface parity. In particular, arbitrary response-body
capture is not currently claimed. Property availability under `webkit.*`
depends on the installed WebKitGTK/GObject version and must be
runtime-discovered.

## Known limitations that are not protocol blockers

- A pathological page evaluation (for example an out-of-memory allocation)
  can wedge a WebKit web process; agent calls to a wedged tab time out after
  30 seconds with an error that echoes the request id and method, and other
  tabs remain responsive. WebKitGTK's responsiveness probe does not flag
  wedged evaluations, so Vantage does not expose a terminate operation; a
  genuinely dead web process is reported through `page.native_diagnostics`
  and recovered by `browser.reload`.
- Sustained multi-day dogfooding and Cortex/Warden-direct dogfooding remain
  ongoing-evidence work and are recorded as such in the certification
  evidence.