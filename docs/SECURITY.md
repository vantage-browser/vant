# Security boundary

Untrusted HTML, CSS and page JavaScript belong to WebKitGTK and its content
processes. The application chrome must not register a general page-to-native
message bridge. Internal `vantage:` resources are produced from fixed application
data and must never interpolate unescaped page-controlled content.

History, bookmark and download management actions use narrowly scoped internal
`vantage:` navigation targets. They are accepted only while the initiating tab
is already displaying trusted internal content; ordinary web pages cannot call
them. All stored titles, paths and URLs are escaped before entering internal
HTML. Favicons are cached while their page is visited so opening a management
page does not reconnect to every origin represented in the list.

Navigation policy accepts HTTP(S), fixed internal schemes and explicitly handed
off external protocols. `file:`, `javascript:`, `data:` and unknown schemes are
rejected at the application boundary. TLS errors fail closed in the first
implementation. Pop-ups become ordinary tabs subject to the same policy.

Private windows construct an ephemeral `WebKitNetworkSession`, so their cookies,
cache and other website data are not persisted. Vantage also suppresses history
and download-record writes from private windows. Downloaded files themselves are
still saved to the user's Downloads directory; private browsing cannot erase a
file the user explicitly downloaded.

The policy is wired to WebKit navigation decisions and TLS failures. The managed
Linux gate launches the real WebKit view under Xvfb. A manual hostile-navigation
pass on the target Wayland/Omarchy environment remains required before release.

## Trusted agents are not trusted webpages

Vantage's agent-native interface deliberately assumes that a local agent may be trusted with the user's normal machine authority. That agent may therefore receive broad application-side Vantage and WebKit control.

This does not grant application privileges to web content. Pages remain untrusted input inside WebKit's normal process, origin, TLS and permission boundaries. Page JavaScript must never receive Vantage's agent RPC endpoint as a privileged page API, arbitrary native process/filesystem authority, cross-site browser-global data, or privileged JS++ host objects merely because it is loaded in Vantage.

The authority direction is user/agent -> Vantage -> WebKit -> page. Never invert it into page -> privileged Vantage -> machine.
