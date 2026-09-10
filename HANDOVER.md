# Vantage Browser development handover

Vantage Browser (short name and executable: `vant`) is a minimalist,
keyboard-first desktop web browser for Linux. Its first supported environment is
Ubuntu/Omarchy on Wayland. The product goal is a genuinely useful daily browser
with a smaller operational footprint and a calmer interface than a general
purpose Chromium distribution.

This repository is pre-implementation. Nothing described below is shipped,
supported, secure, standards-compatible or release-ready until its checkpoint is
implemented and backed by retained evidence.

## Product identity

- Display name: Vantage Browser.
- Short name: Vant.
- Executable: `vant`.
- Initial implementation language: C++17 or later where dependencies require it.
- Initial rendering backend: WebKitGTK.
- Initial desktop target: Linux/Wayland, with Omarchy as the primary experience.
- License: inspect `LICENSE`; do not change it casually.

Use `Vantage` for the visible product and `vant` for commands, paths and concise
technical identifiers. Do not rename the repository, executable or application
ID without a dedicated compatibility checkpoint.

## Settled product boundary

Vantage owns browser chrome, tabs, workspaces, profiles, session recovery,
navigation policy, permissions, downloads, history, bookmarks, request controls,
developer-facing automation and integration with the Linux desktop.

WebKitGTK initially owns untrusted web content: HTML parsing, CSS, layout,
painting, page JavaScript, media, accessibility and the web security model.
Vantage must not claim to be an independent browser engine while this remains
true.

JS++ is embedded as a privileged, explicitly bounded scripting engine for Vantage
automation, commands, internal tools and later extensions. Arbitrary page
JavaScript must continue to run in WebKit. Do not expose JS++ directly to page
content, DOM objects or unsanitised WebKit callbacks.

## Vendored JS++ policy

Follow the same broad integration model used when Nift embeds Minify++, Markup++
and Jsonic++: Vantage builds a reviewed copy of the required JS++ source rather
than depending on a mutable sibling checkout at build or runtime.

The first vendoring checkpoint must establish:

- a dedicated path such as `third_party/jspp/`;
- an upstream repository URL, exact commit and JS++ semantic version;
- retained licence and notices;
- an allowlist of copied public headers and implementation files;
- a deterministic update/check script;
- a machine-readable manifest containing file hashes;
- a CI drift check that recreates the snapshot and rejects differences; and
- Vantage-owned adapter code outside the vendored directory.

Never edit vendored JS++ files to solve a Vantage problem. Fix JS++ upstream,
accept its tests and embedding evidence, then update the pinned snapshot in a
separate reviewable commit. A snapshot update must not silently change Vantage's
public command or automation API.

## Architecture direction

```text
GTK application and Linux integration
  -> Vantage browser model (windows, workspaces, tabs and profiles)
  -> WebKitGTK views and per-view policy boundary
  -> Vantage services (history, downloads, permissions and recovery)
  -> JS++ host adapter (privileged commands and automation only)
  -> vendored, pinned JS++ source
```

Prefer one clear browser model independent of widgets. UI objects must not become
the source of truth for tabs, windows or sessions. Long-lived services need
explicit ownership and deterministic teardown. Persistent data requires schema
versions, migrations, corruption handling and bounded retention.

## Non-negotiable principles

- Security updates to WebKitGTK and other runtime dependencies are release
  work, not optional maintenance.
- Do not advertise memory or speed advantages without reproducible comparisons
  on the same machine, pages, network conditions and cold/warm state.
- No telemetry, account or cloud dependency by default.
- No blue product palette. The UI should remain neutral, dark and restrained.
- Keyboard operation is first-class, but every essential action also remains
  discoverable and accessible without memorised shortcuts.
- Restore safely after a crash; never trade user data integrity for startup
  speed.
- Use native platform facilities for secrets. Do not store credentials in the
  history/session database.
- Page content, downloaded files and imported profile data are hostile input.
- Every checkpoint ends with tests, documentation, retained evidence and clean
  repository state.

## Checkpoint game plan

Checkpoints are sequential acceptance gates. A later checkpoint may be refined
from earlier evidence, but it must not erase a failed or unsupported result.

### V0 - architecture and reproducible shell

- Freeze supported Linux distribution/compiler/WebKitGTK versions.
- Establish the build, formatting, unit-test and sanitizer commands.
- Create one GTK window with an empty browser model and deterministic teardown.
- Record dependency versions and clean-build instructions.

Exit evidence: clean and sanitizer builds, launch/close smoke test, dependency
manifest and no generated residue.

### V1 - one safe browsing surface

- Add one WebKit view, address input, back, forward, reload and stop.
- Validate and normalise user-entered URLs and search queries.
- Handle TLS errors, navigation failures, pop-ups and external protocols
  deliberately.
- Keep WebKit page JavaScript isolated from application privileges.

Exit evidence: navigation integration tests and a manual hostile-navigation
checklist with exact runtime identity.

### V2 - browser model, tabs and keyboard control

- Introduce window/workspace/tab models independent of GTK widgets.
- Add tab creation, closing, ordering, duplication and recently closed recovery.
- Define stable keyboard shortcuts and a command palette.
- Restore focus predictably between chrome and page content.

Exit evidence: model tests, repeated open/close stress, accessibility pass and
documented shortcuts.

### V3 - durable profiles and crash recovery

- Add versioned local storage for sessions, history and settings.
- Use atomic writes or transactions and explicit migrations.
- Add profiles and private windows with a documented non-persistence contract.
- Recover interrupted sessions without repeatedly reopening a crashing page.

Exit evidence: migration, corruption, crash-interruption and private-mode tests.

### V4 - daily browsing essentials

- Add bookmarks, downloads, find-in-page, zoom and per-site permissions.
- Integrate the desktop portal/file chooser and native secret store where needed.
- Provide useful empty, failure and interrupted-download states.
- Add import/export only for formats covered by fixtures and round-trip tests.

Exit evidence: end-to-end daily-use scenarios and download/path traversal tests.

### V5 - footprint and lifecycle baseline

- Measure cold/warm startup, idle RSS, tab RSS, CPU wakeups and session restore.
- Exercise 1, 10 and 50 representative tabs under a pinned workload.
- Add discard/suspend policy without losing form or navigation state silently.
- Profile leaks, process cleanup and long-running tab churn.

Exit evidence: reproducible benchmark harness, raw retained results and explicit
comparison limitations. No performance claim is permitted before this gate.

### V6 - vendored JS++ foundation

- Import an exact accepted JS++ snapshot under the vendoring policy above.
- Build it as a private Vantage component with hidden symbols where practical.
- Add a narrow adapter owning runtime creation, values, errors and teardown.
- Prove that removing or disabling JS++ does not change ordinary page execution.

Exit evidence: deterministic snapshot check, upstream revision identity, licence
audit, C/C++ boundary tests, sanitizers and repeated runtime lifecycle tests.

### V7 - privileged commands and automation

- Define a small, versioned capability API for reading browser state and issuing
  commands.
- Expose allowlisted operations such as tabs, navigation and bookmarks to JS++.
- Require explicit grants for network, filesystem, clipboard and page access.
- Add time, memory, cancellation and recursion limits at the host boundary.

Exit evidence: capability-denial tests, malformed-script corpus, cancellation and
teardown stress, plus API documentation with unsupported operations.

### V8 - privacy, content controls and isolation review

- Add clear data deletion and per-site storage/permission controls.
- Implement content blocking from a pinned, attributable rules source.
- Review WebKit process isolation, URI schemes, internal pages and IPC surfaces.
- Threat-model downloads, automation packages and privileged host objects.

Exit evidence: written threat model, security regression suite and an independent
review of all privileged JS++ entry points.

### V9 - Omarchy-quality desktop integration

- Package and test the application on the pinned Omarchy/Arch environment.
- Add correct desktop metadata, icon, MIME/URL handling and Wayland behaviour.
- Verify fractional scaling, clipboard, drag/drop, notifications and portals.
- Provide supported installation, update and uninstall paths.

Exit evidence: clean-machine installation, upgrade and uninstall rehearsals plus
retained package identities.

### V10 - compatibility and sustained-use trial

- Run a pinned representative site corpus covering login, video, documents,
  complex apps, downloads and accessibility.
- Classify failures as Vantage, WebKit, site, codec/package or environment issues.
- Conduct a multi-day dogfood trial with crash and resource records.
- Resolve release-blocking regressions; document known limitations honestly.

Exit evidence: corpus results, dogfood report, crash-free session evidence and a
release-candidate issue ledger.

### V11 - release candidate and public preview

- Freeze scope, version, dependency revisions and supported platforms.
- Complete security, licence, accessibility and privacy reviews.
- Build reproducible signed/checksummed artifacts and a source archive.
- Rehearse installation, upgrade, rollback and uninstall from published-shaped
  local artifacts.
- Update the website only from accepted evidence.

Exit evidence: a release manifest tying source commit, vendored JS++ commit,
WebKit/runtime identity, packages, checksums and test results together.

## Testing and evidence

Each retained result should identify at least the Vantage commit, configuration,
compiler, OS/kernel, GTK and complete WebKitGTK version, vendored JS++ commit,
test corpus revision and relevant hardware. Keep unit tests near implementation;
put black-box/browser compatibility harnesses in a separate repository if they
become large or independently versioned.

At minimum, CI should cover build and unit tests, ASan/UBSan, vendored-source
drift, application lifecycle, persistent-data migrations and package consumers.
GUI tests may use a controlled compositor, but must not replace real Wayland and
Omarchy evidence.

## Release procedure

No agent may push, tag, create a GitHub release, publish packages, deploy the
website or make public compatibility/performance claims without explicit owner
approval.

For an authorised release:

1. Freeze the release candidate and verify every promised V11 gate is complete.
2. Confirm the worktree and subcomponents are clean and all dependencies are
   pinned; regenerate and verify the JS++ snapshot manifest.
3. Run the full local test, sanitizer, compatibility, migration, packaging and
   performance suites from a clean committed-file archive.
4. Complete the supported-platform CI matrix and retain exact run URLs/IDs and
   artifacts.
5. Build source and binary packages in the documented release environment.
6. Test fresh install, upgrade from the previous release, rollback where
   supported, URL-handler registration and uninstall on clean systems.
7. Generate checksums and signatures; inspect package contents, licences,
   desktop metadata, symbols and version output.
8. Reconcile `HANDOVER.md`, release notes, known limitations, security/privacy
   statements and the public website against the retained evidence.
9. Tag the exact accepted commit with an annotated, signed tag only after owner
   approval. Publish immutable artifacts and verify their downloaded checksums.
10. Publish the website generated from its accepted source commit, verify live
    pages/assets, then record release and website commit identities.
11. Increment the development version in a separate post-release commit and
    open the next milestone without rewriting the retained release evidence.

If any rehearsal differs from the intended public path, the release is not
rehearsed. A failed gate stops the release; do not weaken or silently skip it.

## Working rules

- Inspect this file, the current checkpoint evidence and repository status before
  substantial work.
- Keep each checkpoint reviewable and commit it independently when authorised.
- Do not combine dependency upgrades, vendored JS++ updates and browser feature
  work in one commit.
- Treat generated files, caches, profiles, downloads and benchmark output as
  disposable unless a documented evidence path explicitly retains them.
- Consolidate durable decisions here instead of appending an unstructured diary.

