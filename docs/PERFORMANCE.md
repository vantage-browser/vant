# Performance evidence

Run `make benchmark` on an otherwise idle machine. The retained V5 baseline
measures only the in-process browser model and SQLite save/restore path. It does
not include GTK, WebKit content/network/GPU processes, live pages, cold disk
caches or network traffic and therefore cannot support comparisons with another
browser.

A release-facing comparison must pin pages, browser versions, profile state,
hardware, power mode, network capture, cache state and sampling method. It must
measure cold and warm startup, idle and total process-tree RSS, CPU wakeups,
1/10/50 live tabs, discard/restore fidelity and prolonged churn.
