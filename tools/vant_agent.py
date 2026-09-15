#!/usr/bin/env python3
"""Minimal dependency-free reference client for Vantage Agent RPC v1."""
import argparse, json, os, socket, sys

def socket_path():
    return os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"vantage-agent-{os.getuid()}.sock")

def call(method, params, request_id="python-reference"):
    request={"version":1,"id":request_id,"method":method,"params":params}
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(socket_path()); s.sendall((json.dumps(request,separators=(",",":"))+"\n").encode())
        chunks=[]
        while True:
            part=s.recv(65536)
            if not part: break
            chunks.append(part)
    return json.loads(b"".join(chunks))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("method"); ap.add_argument("params",nargs="?",default="{}")
    ns=ap.parse_args(); reply=call(ns.method,json.loads(ns.params)); print(json.dumps(reply,indent=2)); return 0 if reply.get("ok") else 1
if __name__=="__main__": raise SystemExit(main())
