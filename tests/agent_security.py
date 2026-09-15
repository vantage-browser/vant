#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
native=(root/'src/native_app.cpp').read_text()
rpc=(root/'src/agent_rpc.cpp').read_text()
assert 'chmod(impl_->path.c_str(),0600)' in rpc
assert 'request_too_large' in rpc
assert '1024*1024' in rpc
assert 'vantage-agent://diagnostics' in native
assert 'window.__vantageAgentDiagnostics' in native
# Page instrumentation must remain observational, not a native host bridge.
for forbidden in ('window.vantage=', 'window.Vantage=', 'window.agentRpc=', 'window.__vantageRpc='):
    assert forbidden not in native
print('agent security source invariants: PASS')
