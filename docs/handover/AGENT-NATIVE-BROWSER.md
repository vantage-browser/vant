# Vantage agent-native browser handover and game plan

## Objective and trust model

Make Vantage a browser that humans and trusted agents operate together. Agents should drive the same live windows, tabs, profiles and authenticated sessions the user is using, inspect page/browser state deeply, execute page JavaScript, and reach the useful WebKitGTK capabilities owned by Vantage.

This is deliberately **not** a sandboxed-agent project. A trusted agent already operating with the user's machine authority should not encounter an artificial browser capability boundary.

That does **not** mean giving websites application privileges. Preserve this authority direction:

```text
trusted user / trusted agent
            |
            v
      Vantage application
            |
            v
         WebKitGTK
            |
            v
   untrusted web content
```

Never invert it into `website -> privileged Vantage -> user's machine`. If page JavaScript inherited application privileges, merely visiting a hostile site could potentially let it invoke whatever native powers had been bridged: read/write user files, launch processes or shell commands, inspect browser-global/cross-site state, manipulate Vantage history/bookmarks/downloads outside normal web APIs, call privileged JS++ hosts, or invoke the agent RPC as the user. The agent gets broad authority because the user deliberately trusted the agent; a random webpage has received no such trust.

Preserve WebKit's web-process isolation, sandboxing, origin/CORS rules, TLS policy and page-permission boundaries while expanding the trusted Vantage application's control surface. Never bridge privileged JS++ Vantage host objects directly into page JavaScript.

## Product principles

- Human and agent are first-class controllers of the same live browser.
- Operate the user's real Vantage session, not a separate disposable automation browser.
- Full useful WebKit access is the goal, not a permanently narrow automation DSL.
- Keep semantic convenience commands because they are efficient for routine agent work.
- Always retain low-level escape hatches, especially arbitrary page JavaScript and broad WebKit/Vantage introspection.
- Make the installed API self-describing through `capabilities` and `describe` operations.
- Prefer a stable versioned Vantage RPC contract over trying to serialize arbitrary C/GObject calls.
- Agent operations use the same authoritative browser model/services as human UI actions.
- Preserve private/ephemeral semantics.
- Agent actions should affect the normal visible browser rather than a hidden parallel state.
- Agent support should have negligible idle cost when unused.

## Target architecture

```text
Human GTK UI --------------------+
                                 |
CLI / coding agents ----+        |
                        v        v
                  Vantage Agent RPC
                         |
       +-----------------+------------------+
       |                 |                  |
 browser model      Vantage services   WebKit bridge
 windows/tabs       history/downloads   WebKitWebView
 sessions           bookmarks/data      settings/context
 navigation         permissions          JS/DOM/page state
       +-----------------+------------------+
                         |
                     WebKitGTK
                         |
                  untrusted pages
```

Use a local Unix-domain socket under `$XDG_RUNTIME_DIR` by default with normal same-user ownership/permissions. `vant agent ...` is the first generic client. Cortex, Warden, OpenCode, Codex, Claude Code, shell scripts and future MCP adapters should all be able to use the same protocol without coupling Vantage to one agent product.

## Sequential checkpoint checklist

### A0 - baseline and existing automation
- [x] Record current Vantage/WebKitGTK/GTK versions, clean Git state and build/test commands.
- [x] Map browser-model, WebKit, JS++ automation and `docs/AUTOMATION.md` ownership.
- [x] Inventory useful WebKit objects/APIs already used by Vantage.
- [x] Identify widget-coupled operations that should move behind model/services.
- [x] Add the trusted-agent versus untrusted-page rule to security docs.
- [x] Freeze existing human behavior with regression tests.

### A1 - stable live-object identities
- [x] Give windows/tabs stable runtime IDs independent of GTK pointers and visible indices.
- [x] Define deterministic current-window/current-tab semantics.
- [x] Expose URI/title/load/private/audio/lifecycle metadata.
- [x] Prevent closed IDs from silently retargeting new objects.
- [x] Test create/reorder/move/close/restore/multi-window lifecycle.

### A2 - versioned local RPC
- [x] Add a Vantage-owned Unix-domain-socket RPC service.
- [x] Define protocol versions, request IDs, structured errors and bounded messages.
- [x] Bind socket lifecycle safely to the running Vantage instance.
- [x] Add `vant agent status`, `version`, `capabilities` and machine-readable discovery.
- [x] Keep malformed/slow clients from blocking GTK/WebKit.
- [x] Model asynchronous WebKit work asynchronously.

### A3 - window/tab/browser control
- [x] List/select/create/close windows and tabs.
- [x] Open/navigate/reload/stop/back/forward.
- [x] Focus/activate targets intentionally in the human-visible UI.
- [x] Add URI/title/loading and bounded wait primitives.
- [x] Route popups/new views through the normal Vantage model.
- [x] Verify human and agent actions interleave safely.

### A4 - arbitrary page JavaScript
- [x] Evaluate arbitrary JavaScript against an explicit tab/frame.
- [x] Return structured JSON-safe results where possible.
- [x] Accept script files/stdin for large programs.
- [x] Surface useful JS exception information.
- [x] Define main-frame/subframe targeting.
- [x] Investigate/document page-world versus isolated-world execution.
- [x] Keep privileged Vantage JS++ hosts out of page realms.

### A5 - semantic snapshots and element references
- [x] Produce compact agent-oriented page snapshots.
- [x] Prefer accessibility/semantic information; supplement through page JS where needed.
- [x] Include role/name/text/state and compact refs such as `@e17`.
- [x] Strictly invalidate refs after navigation/DOM/frame replacement rather than retargeting them.
- [x] Offer compact defaults plus opt-in detail.
- [x] Test SPAs, shadow DOM, iframes, duplicate labels and mutating DOMs.

### A6 - semantic interaction
- [x] Add click/focus/fill/type/clear/select/check/scroll/key operations.
- [x] Accept semantic refs and explicit selectors where useful.
- [x] Add waits for element/text/state.
- [x] Preserve normal browser event semantics for user-like interaction.
- [x] Return actionable stale/hidden/disabled/detached errors.
- [x] Keep arbitrary JS as the unrestricted escape hatch.

### A7 - rendered/content inspection
- [ ] Add viewport/full-page screenshots where WebKitGTK permits reliable capture.
- [ ] Add page text, selection, HTML/source and metadata extraction.
- [ ] Expose viewport, scroll, zoom and find state.
- [ ] Handle binary/file output without huge JSON blobs.
- [ ] Support snapshot + screenshot verification workflows.

### A8 - broad WebKit/Vantage introspection
- [ ] Inventory useful WebKitGTK API families rather than exposing a brittle generic GObject ABI.
- [ ] Add discoverable namespaces for web view, settings, context, website data, navigation, permissions, media and related facilities.
- [ ] Expose useful properties and operations systematically.
- [ ] Add `describe`/schema introspection for args, returns and version availability.
- [ ] Mark unsupported/version-specific WebKit functionality explicitly.
- [ ] Keep semantic aliases for common operations.

### A9 - browser-owned services and data
- [ ] Expose downloads: list/inspect/wait/cancel/open/show-path and existing operations.
- [ ] Expose bookmarks/history through authoritative Vantage services.
- [ ] Expose cookies, website data/storage and permissions where supported.
- [ ] Expose profile/session/private metadata without violating private-mode non-persistence.
- [ ] Expose relevant Vantage/WebKit settings/capabilities.
- [ ] Keep destructive operations explicit and machine-readable without fake agent-only confirmation barriers.

### A10 - developer diagnostics
- [ ] Capture/expose bounded console events where WebKitGTK supports it.
- [ ] Surface page JS errors and web-process termination.
- [ ] Inventory request/response/resource observability and web-extension options.
- [ ] Expose failed resources and useful network metadata where available.
- [ ] Investigate response-body capture only if WebKitGTK supports it cleanly; document gaps instead of badly emulating CDP.
- [ ] Add clear/reset/follow semantics.

### A11 - event streaming
- [ ] Add subscriptions/streaming for navigation, lifecycle, load, console, downloads, permissions and useful events.
- [ ] Bound queues and define overflow/backpressure.
- [ ] Define disconnect/reconnect behavior.
- [ ] Add `vant agent events`/`watch`.
- [ ] Ensure slow clients cannot stall Vantage/WebKit.

### A12 - multi-controller correctness
- [ ] Stress simultaneous human/agent operation of one tab.
- [ ] Stress multiple agent clients across same/different tabs.
- [ ] Define close/navigation/focus races during outstanding async work.
- [ ] Fail stale references/cancelled operations cleanly.
- [ ] Test disconnects, crashes, restart and session restore with RPC enabled.

### A13 - agent-oriented CLI quality
- [ ] Make common operations one-shot shell commands with stable exit codes.
- [ ] Provide global JSON output plus concise terminal output.
- [ ] Support stdin/script-file input.
- [ ] Keep commands composable/discoverable.
- [ ] Add examples for coding workflows, forms, inspection, downloads and debugging.
- [ ] Ensure `--help`, `capabilities` and `describe` let an unfamiliar agent teach itself the API.

### A14 - direct integrations without coupling
- [ ] Document RPC for non-CLI clients.
- [ ] Add a reference client only if useful; RPC remains authoritative.
- [ ] Evaluate MCP as an optional adapter over the same RPC.
- [ ] Validate Cortex/Warden while keeping Vantage independent of Gantry.
- [ ] Validate at least one unrelated coding-agent workflow.

### A15 - security-boundary certification
- [ ] Verify WebKit web-process sandboxing remains enabled.
- [ ] Verify page JS cannot invoke Vantage RPC merely by being loaded.
- [ ] Verify page content receives no privileged JS++ Vantage host objects.
- [ ] Verify cross-origin/TLS/permission security is not disabled for automation convenience.
- [ ] Test hostile pages, navigation races, malformed RPC, oversized payloads, stale handles and malicious JS results.
- [ ] Run sanitizers and lifecycle/leak tests over repeated agent sessions.
- [ ] Document clearly: this boundary protects the user from websites, not from their deliberately trusted local agent.

### A16 - performance and sustained use
- [ ] Measure RPC overhead, snapshot latency/size and interaction latency.
- [ ] Test large DOMs, many tabs, long event streams and repeated snapshots.
- [ ] Require negligible idle cost when agent functionality is unused.
- [ ] Bound retained diagnostic/event data.
- [ ] Test long human browsing with the service idle and repeated agent-driven development loops.

### A17 - release/compatibility contract
- [ ] Freeze agent API v1 only after real workflows exercise it.
- [ ] Document compatibility/deprecation rules.
- [ ] Publish machine-readable capability/version information.
- [ ] Record WebKitGTK-version-dependent gaps.
- [ ] Test packaging/socket cleanup.
- [ ] Complete human + agent dogfooding on supported Linux/Wayland.

## Documentation and website work throughout

Do not run this as a large code campaign followed by a documentation catch-up. Every checkpoint that changes behavior must update repository docs and the website in the same checkpoint, or explicitly record why no user-facing/public change is warranted.

### Human documentation

Human pages should be detailed and example-rich but quickly digestible. A person should be able to understand the idea, start using it, complete common workflows and troubleshoot likely problems without reading an API textbook. Prefer short conceptual sections, copyable examples, tables and links to deeper references.

Suggested pages: agent-use overview; driving your current browser; CLI quick start; common workflows; simultaneous human + agent control; trust/privacy/security; troubleshooting.

### Agent documentation

Agent-oriented pages may and should be exhaustive. They are intended to be fetched/read by coding agents, so completeness and exactness matter more than brevity. Include every CLI command, RPC method/schema, lifecycle rule, error code, stale-handle rule, event type, WebKit-version caveat and extensive executable examples.

Suggested pages: agent bootstrap/start-here; complete CLI reference; complete RPC/schema reference; capability discovery; semantic snapshot/reference contract; JavaScript/script worlds; full WebKit/Vantage surface; events; error/recovery model; browser data/services; diagnostics/network/console; end-to-end recipes; compatibility matrix.

Make agent-reference pages deterministic and individually retrievable. Do not require an agent to scrape human prose to discover exact protocol behavior.

### Website checklist for every checkpoint

- [ ] Update `vantage-browser.github.io/content/`; never hand-edit generated `public/`.
- [ ] Build with the site's normal Nift workflow and inspect generated output.
- [ ] Keep implemented functionality distinct from roadmap/planned functionality.
- [ ] Add/update human docs as soon as a feature becomes usable.
- [ ] Add/update agent reference docs in lockstep with CLI/RPC/schema changes.
- [ ] Keep examples executable against the version they document.
- [ ] Keep architecture/security pages aligned with the trusted-agent/untrusted-page distinction.
- [ ] Update homepage/features positioning only when accepted evidence supports it.
- [ ] Keep known limitations honest, especially WebKitGTK-version gaps.
- [ ] Preserve the site's established visual language/accessibility requirements.
- [ ] Validate navigation between human docs and deeper agent docs.
- [ ] Commit generated deployment output first, then outer website source as required by its handover.

## Initial positioning

Do not lead with generic “AI browser” language. The differentiator is shared control of a real lightweight browser:

> Vantage is a browser built for humans and agents to use together.

Once implemented and evidenced, supporting ideas are: the agent drives the Vantage session already in use; human and agent share windows/tabs/logins/browser state; semantic controls make common work cheap while deep WebKit/Vantage access remains available; Vantage remains a normal privacy-focused WebKitGTK browser when no agent is connected; trusted agents get broad browser authority while websites do not inherit it.

## Definition of done

A trusted generic shell-capable agent can attach to the user's running Vantage instance, discover capabilities, select the exact human-visible tab, inspect it semantically and visually, interact with it, execute arbitrary page JavaScript, inspect useful WebKit/Vantage state and diagnostics, operate browser-owned services, observe asynchronous events, and coexist reliably with human input—while ordinary web content remains confined to WebKit's normal web security/process boundary.
