# Agent API compatibility

## Current status

The wire protocol number is `1`, but the agent API is **preview**, not frozen stable v1. `capabilities` reports `stability: "preview"`. The A17 rule is intentionally that the API must not be declared stable until native WebKitGTK/Wayland dogfooding exercises real workflows on a supported development host.

Within preview protocol 1, integrations should call `capabilities` and `describe` and tolerate additive methods/fields. Once v1 is frozen, additive optional fields/methods remain compatible; removals, semantic retargeting, required-field changes and incompatible result changes require a new protocol version or a documented deprecation window.

## WebKit-dependent gaps

Vantage exposes what WebKitGTK can support cleanly. It does not emulate Chromium CDP merely for surface parity. In particular, arbitrary response-body capture is not currently claimed. Property availability under `webkit.*` depends on the installed WebKitGTK/GObject version and must be runtime-discovered.

## Release gate still required

Before changing `stability` to `stable`, run the complete native build/test/sanitizer wall on a supported Linux/Wayland host with GTK4 and WebKitGTK 6.0 development packages, then dogfood shared human+agent browsing, diagnostics, events, multiple controllers, private mode, browser data and repeated development loops. The packaging runner used for A10-A17 could not complete this native gate because installing those development packages timed out.
