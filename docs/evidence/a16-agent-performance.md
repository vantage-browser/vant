# A16 agent performance evidence

The UI-independent event-path benchmark on the packaging runner measured approximately 19.8 ns per bounded event publication and 2.35 us per cursor poll in the synthetic workload. The 2,048-event application ring retained exactly 2,048 entries after 200,000 publications and reported 197,952 dropped entries, demonstrating bounded retention rather than unbounded growth.

Semantic page snapshots are capped at 500 interactive elements per snapshot and page diagnostics at 500 entries. The application event ring is capped at 2,048 entries and per-poll output at 1,024 events. The RPC listener is idle-blocked on `accept()` and does no polling work when unused.

Native WebKitGTK latency, many-tab rendering and long interactive Wayland browsing could not be re-measured on this packaging runner because the GTK4/WebKitGTK development packages are unavailable. A package installation attempt timed out. These native measurements remain part of A17 dogfooding/release evidence and must be run on a supported Vantage development host before claiming release certification.
