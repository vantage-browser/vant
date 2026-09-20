#!/usr/bin/env python3
"""Regression test for the browser-tab sizing model.

Vantage tabs must report a small GTK MINIMUM width while keeping a natural
(preferred) width of 184px. Before the fix, each tab's current width was set as
its size request, so GTK derived a minimum window width of roughly
tabs * 184 + chrome and the window manager refused to shrink the window below
it. This test drives a live window via `--tab-sizing-probe` and asserts the
fixed contract:

  * the window's measured minimum is substantially below tabs * 184;
  * with room to spare every tab is exactly 184px (never stretched beyond);
  * below tabs * 184 the tabs compress uniformly as the window shrinks;
  * compact tabs never shrink below the favicon-safe per-tab minimum;
  * creating/closing tabs while compressed follows the same uniform rule.

Requires a display and the native build. Run:
    python3 tests/tab_sizing.py [VANT_BINARY]
Exits 0 on success, 1 on any regression.
"""
import os
import subprocess
import sys

K_TAB_MIN = 44
K_TAB_NATURAL = 184
PROBE_TABS = 6
TIMEOUT = 90


def require(condition, message):
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)
    print(f"ok: {message}")


def run_probe(binary):
    env = dict(os.environ)
    env["WEBKIT_DISABLE_COMPOSITING_MODE"] = "1"
    proc = subprocess.run(
        [binary, "--tab-sizing-probe"],
        env=env,
        capture_output=True,
        text=True,
        timeout=TIMEOUT,
    )
    output = (proc.stdout or "") + "\n" + (proc.stderr or "")
    return proc.returncode, output


def parse_probe(output):
    rows = []
    for line in output.splitlines():
        line = line.strip()
        if not line.startswith("tab_probe"):
            continue
        fields = line.split()
        record = {}
        for field in fields[1:]:
            if "=" in field:
                key, value = field.split("=", 1)
                record[key] = value
        tabs = []
        for token in fields[1:]:
            if token.isdigit():
                tabs.append(int(token))
        if "tabs" in record:
            rows.append({
                "kind": "measure",
                "tabs": int(record["tabs"]),
                "window_min": int(record["window_min"]),
                "window_natural": int(record["window_natural"]),
                "strip_min": int(record["strip_min"]),
                "strip_natural": int(record["strip_natural"]),
            })
        elif fields[1] == "positions":
            rows.append({
                "kind": "positions",
                "x": [int(t) for t in fields[2:]],
            })
        elif fields[1].startswith("webview_bg"):
            rows.append({
                "kind": "webview_bg",
                "hex": fields[1].split("=", 1)[1],
            })
        else:
            rows.append({
                "kind": "resize" if fields[1] == "resize" else
                        "after_add" if fields[1] == "after_add" else "after_close",
                "window_w": int(record["window_w"]),
                "tabs": tabs,
            })
    return rows


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "vant")
    returncode, output = run_probe(binary)
    require(returncode == 0, f"--tab-sizing-probe exits 0 (got {returncode})")
    rows = parse_probe(output)
    require(rows, "probe produced measurement output")

    measure = [r for r in rows if r["kind"] == "measure"]
    resizes = [r for r in rows if r["kind"] == "resize"]
    require(len(measure) == 1, "probe reports exactly one measurement row")
    require(len(resizes) >= 5, f"probe reports resize rows ({len(resizes)})")

    m = measure[0]
    require(m["tabs"] == PROBE_TABS, f"probe opened {PROBE_TABS} tabs (got {m['tabs']})")

    # THE core regression: the window minimum must stay substantially below
    # tabs * natural width. The old model made the minimum tabs * 184 + chrome,
    # which is exactly the value this assertion rejects.
    require(m["window_min"] < PROBE_TABS * K_TAB_NATURAL,
            f"window minimum {m['window_min']} is below {PROBE_TABS}*{K_TAB_NATURAL} = "
            f"{PROBE_TABS * K_TAB_NATURAL} (old model locked the window at this size)")
    require(m["window_min"] <= PROBE_TABS * K_TAB_MIN + 220,
            f"window minimum {m['window_min']} is a sensible small value "
            f"(<= tabs*min + chrome ~= {PROBE_TABS * K_TAB_MIN + 220})")
    require(m["window_natural"] >= PROBE_TABS * K_TAB_NATURAL,
            f"window natural {m['window_natural']} still accommodates tabs at {K_TAB_NATURAL}px")

    # Every tab must occupy its own slot: positions must be present, valid and
    # strictly increasing. A regression here stacked every tab at x=0 (the
    # custom layout allocated size but never translated each child), which made
    # all but the first tab invisible and unclickable while still reserving
    # their space.
    positions = [r for r in rows if r["kind"] == "positions"]
    require(len(positions) == 1 and len(positions[0]["x"]) == PROBE_TABS,
            f"probe reports per-tab x positions for all {PROBE_TABS} tabs")
    xs = positions[0]["x"]
    require(all(x >= 0 for x in xs), f"all tabs have a non-negative x ({xs})")
    require(all(b > a for a, b in zip(xs, xs[1:])),
            f"tab x positions strictly increase so tabs are side by side, "
            f"not stacked ({xs})")

    # A freshly created tab's webview background must be dark. WebKitGTK
    # defaults it to opaque white, which flashes before the (dark) page HTML
    # renders when a tab is opened.
    bg = [r for r in rows if r["kind"] == "webview_bg"]
    require(len(bg) == 1, "probe reports the webview background color")
    require(len(bg[0]["hex"]) == 6, f"webview background is a hex color ({bg[0]['hex']})")
    channels = [int(bg[0]["hex"][i:i + 2], 16) for i in (0, 2, 4)]
    require(max(channels) < 128,
            f"webview background is dark, not white (rgb={channels})")

    # The widest row (>= natural) must hold every tab at exactly 184px.
    widest = max(resizes, key=lambda r: r["window_w"])
    require(widest["window_w"] >= m["window_natural"] - 8,
            f"widest probe resize reaches natural width ({widest['window_w']} vs {m['window_natural']})")
    require(all(w == K_TAB_NATURAL for w in widest["tabs"]),
            f"tabs stay at {K_TAB_NATURAL}px when there is room, never stretched "
            f"(got {widest['tabs']})")

    # A mid resize (between min and natural) must actually shrink the window and
    # compress the tabs uniformly below 184.
    mid = [r for r in resizes if m["window_min"] + 40 < r["window_w"] < m["window_natural"] - 40]
    require(bool(mid), "there is a mid-range resize to observe compression")
    for row in mid:
        require(row["window_w"] < m["window_natural"] - 8,
                f"window shrank to {row['window_w']} (below natural {m['window_natural']})")
        require(len(set(row["tabs"])) == 1,
                f"tabs compress uniformly ({row['tabs']})")
        require(0 < row["tabs"][0] < K_TAB_NATURAL,
                f"compressed tab width {row['tabs'][0]} is below {K_TAB_NATURAL}")

    # At the floor tabs must never shrink below the favicon-safe minimum.
    floor = [r for r in resizes if r["window_w"] <= m["window_min"] + 8]
    require(bool(floor), "probe reached the window minimum")
    for row in floor:
        require(len(set(row["tabs"])) == 1 and row["tabs"][0] == K_TAB_MIN,
                f"tabs floor at {K_TAB_MIN}px when the window is at its minimum ({row['tabs']})")
        require(row["window_w"] >= m["window_min"] - 8,
                f"window clamps at its minimum, never below ({row['window_w']} vs {m['window_min']})")

    # Tab creation/closing while compressed must keep the uniform distribution.
    after_add = [r for r in rows if r["kind"] == "after_add"]
    after_close = [r for r in rows if r["kind"] == "after_close"]
    require(len(after_add) == 1 and len(after_close) == 1,
            "probe exercises tab add/close while compressed")
    for row in after_add + after_close:
        require(len(set(row["tabs"])) == 1,
                f"tabs stay uniform after {'add' if row['kind'] == 'after_add' else 'close'} ({row['tabs']})")
        require(all(0 < w <= K_TAB_NATURAL for w in row["tabs"]),
                f"tab widths stay within (0, {K_TAB_NATURAL}] after "
                f"{'add' if row['kind'] == 'after_add' else 'close'} ({row['tabs']})")

    print("tab sizing regressions: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())