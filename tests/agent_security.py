#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
native=(root/'src/native_app.cpp').read_text()
rpc=(root/'src/agent_rpc.cpp').read_text()
assert 'chmod(impl_->path.c_str(),0600)' in rpc
assert 'request_too_large' in rpc
assert '1024*1024' in rpc
assert 'WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START' in native
assert 'window.__vantageAgentDiagnostics' in native
# Page instrumentation must remain observational, not a native host bridge.
for forbidden in ('window.vantage=', 'window.Vantage=', 'window.agentRpc=', 'window.__vantageRpc='):
    assert forbidden not in native
# A7 screenshots must use the GTK4/WebKitGTK 6.0 GdkTexture API, not the
# GTK3-era cairo_surface_t handling that cannot compile against real headers.
assert 'gdk_texture_save_to_png' in native
assert 'webkit_web_view_get_snapshot_finish' in native
assert 'cairo_surface_write_to_png' not in native
# Page/agent strings embedded into JavaScript must use JSON escaping so UTF-8
# round-trips instead of g_strescape's octal (mojibake) escaping.
assert 'return vantage::json_string(value);' in native
assert 'g_strescape' not in native
# The agent listening socket must be close-on-exec so WebKit web processes
# never inherit the RPC socket across spawn.
assert 'set_cloexec' in rpc
assert 'FD_CLOEXEC' in rpc
print('agent security source invariants: PASS')
