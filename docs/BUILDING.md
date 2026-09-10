# Building Vantage

The portable core requires a C++20 compiler, GNU Make and Python 3. Build and
run its evidence gates with:

```sh
make clean
make test
make test-sanitize
make evidence
```

The native UI target will require GTK 4 and WebKitGTK 6.0. This checkpoint's
managed Linux image does not provide those packages, so V0 retains only verified
portable application lifecycle evidence. A GTK window and real WebKit process
remain required before V0 can be declared complete on the target platform.
