# Security boundary

Untrusted HTML, CSS and page JavaScript belong to WebKitGTK and its content
processes. The application chrome must not register a general page-to-native
message bridge. Internal `vantage:` resources are produced from fixed application
data and must never interpolate unescaped page-controlled content.

Navigation policy accepts HTTP(S), fixed internal schemes and explicitly handed
off external protocols. `file:`, `javascript:`, `data:` and unknown schemes are
rejected at the application boundary. TLS errors fail closed in the first
implementation. Pop-ups become ordinary tabs subject to the same policy.

The policy is wired to WebKit navigation decisions and TLS failures. The managed
Linux gate launches the real WebKit view under Xvfb. A manual hostile-navigation
pass on the target Wayland/Omarchy environment remains required before release.
