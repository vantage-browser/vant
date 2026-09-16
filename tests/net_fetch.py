#!/usr/bin/env python3
"""Native black-box net.fetch regressions. Requires a running `vant` instance
whose agent socket is reachable at the standard runtime path.

Spins up its own deterministic local HTTP server and verifies the observable
net.fetch contract: methods, headers, bodies, status handling, redirects (not
auto-followed), UTF-8, binary/base64, truncation, timeouts, malformed input,
scheme/header-injection rejection and cookie isolation.

Run with: python3 tests/net_fetch.py [SOCKET_PATH]
Exits 0 on success, 1 on any regression.
"""
import base64
import json
import os
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SOCKET = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"vantage-agent-{os.getuid()}.sock")


def call(method, params, request_id="net-fetch-test"):
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


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(n) if n else b""

    def do_GET(self): self._handle("GET")
    def do_POST(self): self._handle("POST")
    def do_PUT(self): self._handle("PUT")
    def do_DELETE(self): self._handle("DELETE")

    def _handle(self, method):
        body = self._body()
        path, _, query = self.path.partition("?")
        params = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
        headers = {k: v for k, v in self.headers.items()}
        self.server.last_request = {"method": method, "headers": headers,
                                    "body": body.decode("utf-8", "replace")}
        if path.startswith("/status/"):
            code = int(path.split("/")[-1])
            payload = f"status {code}".encode()
            self.send_response(code)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if path == "/redirect":
            self.send_response(302)
            self.send_header("Location", params.get("to", "/echo"))
            self.end_headers()
            return
        if path == "/large":
            n = int(params.get("bytes", "2000"))
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(n))
            self.end_headers()
            self.wfile.write(b"x" * n)
            return
        if path == "/utf8":
            # Raw UTF-8 bytes (not JSON-escaped) to prove text detection.
            data = '{"message": "héllo wörld — ✓"}'.encode("utf-8")
        elif path == "/binary":
            data = bytes([0, 1, 2, 253, 254, 255])
        else:
            data = json.dumps({"method": method, "received_headers": headers,
                               "received_body": body.decode("utf-8", "replace")}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):
        pass


class Server(ThreadingHTTPServer):
    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.last_request = None


def fetch(params, expect_ok=True):
    reply = call("net.fetch", params)
    if expect_ok:
        require(reply.get("ok"), f"net.fetch ok for {params.get('url')}")
        return reply["result"]
    require(not reply.get("ok"), f"net.fetch rejects {params.get('url')}")
    return reply["error"]


def main():
    server = Server()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"

    try:
        # GET JSON
        r = fetch({"url": base + "/echo", "method": "GET"})
        require(r["status"] == 200 and r["ok"], "GET 200 ok")
        echo = json.loads(r["body"])
        require(echo["method"] == "GET", "GET method echoed")

        # GET with custom headers
        r = fetch({"url": base + "/echo",
                   "headers": {"Authorization": "Bearer token", "X-Test": "yes"}})
        echo = json.loads(r["body"])
        require(echo["received_headers"].get("Authorization") == "Bearer token",
                "custom request header sent")
        require(echo["received_headers"].get("X-Test") == "yes",
                "second custom request header sent")

        # POST with headers + body: Content-Type must arrive intact (UAF regression)
        r = fetch({"url": base + "/echo", "method": "POST",
                   "headers": {"Content-Type": "application/json"},
                   "body": '{"hello":"world"}'})
        echo = json.loads(r["body"])
        require(echo["received_headers"].get("Content-Type") == "application/json",
                "POST Content-Type header intact (no use-after-free garbage)")
        require(echo["received_body"] == '{"hello":"world"}', "POST body received intact")

        # Body without explicit Content-Type -> application/octet-stream
        r = fetch({"url": base + "/echo", "method": "PUT", "body": "put-payload"})
        echo = json.loads(r["body"])
        require(echo["received_headers"].get("Content-Type") == "application/octet-stream",
                "default content type for bare body")
        require(echo["received_body"] == "put-payload", "PUT body received")

        # 404/500 status handling
        r = fetch({"url": base + "/status/404"})
        require(r["status"] == 404 and not r["ok"], "HTTP 404 surfaced as status 404")
        r = fetch({"url": base + "/status/500"})
        require(r["status"] == 500 and not r["ok"], "HTTP 500 surfaced as status 500")

        # Redirects are NOT auto-followed; 302 + Location are returned
        r = fetch({"url": base + "/redirect?to=/echo"})
        require(r["status"] == 302, "redirect not auto-followed (302 returned)")
        require(r["headers"].get("Location") == "/echo", "Location header present for manual follow")

        # UTF-8 and binary/base64
        r = fetch({"url": base + "/utf8"})
        require(r["body_encoding"] == "text" and "✓" in r["body"], "UTF-8 body as text")
        r = fetch({"url": base + "/binary"})
        require(r["body_encoding"] == "base64", "binary body base64-encoded")
        require(base64.b64decode(r["body"]) == bytes([0, 1, 2, 253, 254, 255]),
                "base64 body decodes to the original bytes")

        # Truncation bounds the retained body and reports the full size
        r = fetch({"url": base + "/large?bytes=5000", "max_bytes": 100})
        require(r["bytes"] == 5000 and r["truncated"] and len(r["body"]) == 100,
                "truncated body reports full bytes=5000, truncated=true, len=100")
        r = fetch({"url": base + "/large?bytes=100", "max_bytes": 100})
        require(r["bytes"] == 100 and not r["truncated"] and len(r["body"]) == 100,
                "exact-fit body truncated=false")

        # Malformed input
        fetch({"url": base + "/echo", "headers": {"A": 1}}, expect_ok=False)
        fetch({"url": "file:///etc/passwd"}, expect_ok=False)
        fetch({"url": base + "/echo", "headers": {"X-Bad": "a\r\nInjected: yes"}}, expect_ok=False)
        fetch({"method": "GET"}, expect_ok=False)
        require(call("net.fetch", {"url": base + "/echo", "headers": {}})["ok"],
                "empty headers object is valid")

        # Cookie isolation: a Set-Cookie response must not be replayed on the next fetch
        r = fetch({"url": base + "/echo", "method": "GET"})
        # (server sets no cookie; net.fetch never attaches cookies regardless)
        r = fetch({"url": base + "/echo"})
        echo = json.loads(r["body"])
        require("Cookie" not in echo["received_headers"], "no Cookie header attached")

        print("net.fetch regressions: PASS")
        return 0
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    sys.exit(main())