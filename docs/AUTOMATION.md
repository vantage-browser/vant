# Vantage automation API 0.1

Vantage scripts run in a private JS++ runtime. They do not run in a WebKit page
realm and receive no DOM, page-JavaScript, filesystem, network or clipboard
objects. Capabilities are denied unless granted before runtime construction.

| Function | Capability | Effect |
|---|---|---|
| `vant.tabCount()` | `tabs.read` | Return the number of model tabs. |
| `vant.openTab(uri)` | `tabs.write` | Open an allowed HTTP(S) or internal URI. |
| `vant.bookmark(uri, title)` | `bookmarks.write` | Store an HTTP(S) bookmark. |

Each runtime has fixed instruction, stack and allocation limits. Cancellation is
checked at every native host call; pure JS++ loops terminate through the
instruction budget. Dynamic revocation, asynchronous jobs, filesystem, direct
network access, clipboard, page access, DOM handles and package loading are
unsupported. Grant persistence and user-facing consent are future work.
