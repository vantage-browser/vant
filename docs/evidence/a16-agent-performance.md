# A16 agent performance evidence

The UI-independent event-path benchmark on the packaging runner measured approximately 19.8 ns per bounded event publication and 2.35 us per cursor poll in the synthetic workload. The 2,048-event application ring retained exactly 2,048 entries after 200,000 publications and reported 197,952 dropped entries, demonstrating bounded retention rather than unbounded growth.

Semantic page snapshots are capped at 500 interactive elements per snapshot and page diagnostics at 500 entries. The application event ring is capped at 2,048 entries and per-poll output at 1,024 events. The RPC listener is idle-blocked on `accept()` and does no polling work when unused.

## Native measurements (supported Vantage development host, 2026-09-15)

Real graphical WebKitGTK 2.52.6 on the Wayland session, median of five calls:

| Measurement | Latency |
|---|---|
| `status` | 0.30 ms |
| `version` | 0.19 ms |
| `capabilities` | 0.16 ms |
| `browser.tabs` | 0.16 ms |
| `browser.windows` | 0.11 ms |
| `page.javascript` (small) | 0.56 ms |
| `page.snapshot` (11 elements) | 0.99 ms |
| `page.snapshot` (500-element cap, large DOM) | 4.8 ms |
| `page.interact` | 1.25 ms |
| `page.inspect` metadata | 0.79 ms |
| `page.inspect` text | 0.40 ms |
| `events.since` (empty) | 0.18 ms |

Snapshot sizes: approximately 1.1 KB for the 11-element form fixture and 43 KB
at the 500-element cap. Idle CPU with the agent service unused measured 0.03%
of one core over 40 seconds. RSS grew from about 184 MB to 190 MB after 200
snapshots plus 60 tab create/close cycles (peak 191 MB), consistent with
WebKit web-process pooling rather than a Vantage-side leak.

See `docs/evidence/a17-native-certification.md` for the full native
certification wall including the A17 protocol-1 stability decision.
