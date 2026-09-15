#!/usr/bin/env python3
"""Native black-box agent regressions. Requires a running `vant` instance
whose agent socket is reachable at the standard runtime path.

Run with: python3 tests/agent_native.py [SOCKET_PATH]
Exits 0 on success, 1 on any regression.
"""
import json
import os
import socket
import sys

SOCKET = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"vantage-agent-{os.getuid()}.sock")


def call(method, params, request_id="native-regression"):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(SOCKET)
        sock.sendall((json.dumps(
            {"version": 1, "id": request_id, "method": method, "params": params},
            separators=(",", ":")) + "\n").encode())
        out = b""
        while True:
            part = sock.recv(65536)
            if not part:
                break
            out += part
    return json.loads(out)


def require(condition, message):
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)
    print(f"ok: {message}")


def main():
    reply = call("browser.tab.new", {"window_id": 1, "uri": "about:blank"})
    require(reply.get("ok"), "browser.tab.new")
    tab = reply["result"]["id"]

    # describe must be a complete, name-honouring catalog
    catalog = call("describe", {})
    require(catalog.get("ok"), "describe")
    require(len(catalog["result"]) >= 40, f"describe catalog has >=40 methods ({len(catalog['result'])})")
    one = call("describe", {"name": "page.interact"})
    require(one.get("ok") and "page.interact" in one["result"] and len(one["result"]) == 1,
            "describe honors the name parameter")

    # setting.set rejects JSON type mismatches instead of silently coercing
    bad = call("webkit.setting.set", {"tab_id": tab, "name": "enable-javascript", "value": "notabool"})
    require(not bad.get("ok") and bad["error"]["code"] == "invalid_param_type",
            "webkit.setting.set rejects string for a boolean setting")

    # navigation to rejected schemes returns an explicit error
    rejected = call("browser.navigate", {"tab_id": tab, "uri": "file:///etc/passwd"})
    require(not rejected.get("ok") and rejected["error"]["code"] == "navigation_rejected",
            "browser.navigate rejects file:// with navigation_rejected")

    # event stream carries lifecycle events
    events = call("events.since", {"after": 0, "limit": 100})
    require(events.get("ok"), "events.since")
    types = {e["type"] for e in events["result"]["events"]}
    require("tab.created" in types, "event stream contains tab.created")
    require("window.created" in types, "event stream contains window.created")

    print("agent native regressions: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())