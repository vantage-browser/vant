# Vantage agent reference

Vantage exposes a versioned local agent RPC over a same-user Unix-domain socket. The first client is `vant agent`.

```sh
vant agent status
vant agent version
vant agent capabilities
vant agent call METHOD '{"parameter":"value"}'
```

Requests and responses are newline-delimited JSON. Protocol v1 requests contain `version`, `id`, `method`, and `params`. Messages are bounded to 1 MiB and individual CLI calls have a bounded wait. The socket is created under `$XDG_RUNTIME_DIR` when available and mode 0600. This is process-local IPC for an already trusted user/agent, not a webpage capability.

The API is self-describing through `capabilities`; later checkpoints extend namespaces without changing the transport.
