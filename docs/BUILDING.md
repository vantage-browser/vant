# Building Vantage

Vantage requires a C++20 compiler, GNU Make, Python 3, GTK 4 and WebKitGTK 6.0. Build and
run its evidence gates with:

```sh
make clean
make test
make test-sanitize
make evidence
```

The retained managed-Linux baseline uses GTK 4.14.5 and WebKitGTK 2.52.3. The
`make smoke` gate verifies the linked GTK/WebKit versions without requiring a
display. `make smoke-native` launches the real GTK window and WebKit content
process when a display is available. This managed container forbids the local
display socket needed by Xvfb, so the Wayland/Omarchy launch remains a separate
target-platform gate.
