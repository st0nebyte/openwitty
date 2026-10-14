# Development

## Dependencies (no root required)
`tools/setup-sysroot.sh` downloads the needed Ubuntu `.deb`s and unpacks them
into `./.sysroot` (git-ignored), reusing system runtime libs. The script
prints the environment variables to export. Reverse-engineering tools
(Node 24, JDK 21, Ghidra, innoextract, REA) are bootstrapped by
`tools/setup-rootless.sh` into `./.tools`; see
`docs/REVERSE_ENGINEERING_TOOLS.md`.

With root, the equivalent system packages are:
Ubuntu/Debian `build-essential cmake ninja-build pkg-config libusb-1.0-0-dev libgtest-dev qt6-base-dev qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-window libasound2-dev`;
Arch/CachyOS `base-devel cmake ninja libusb gtest qt6-base qt6-declarative alsa-lib`.

## Build / test
```
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
build/src/gui/lewitt-control-center --mock     # simulated device
build/src/gui/lewitt-control-center --mock --screenshot out.png
build/lewitt-cli probe                          # dry run
```
Qt 6.4 note: QML cachegen is disabled (AOT singleton lookup looped on the
`Theme` singleton); revisit when requiring Qt ≥ 6.5.

## Clean-room rule
Document facts in PROTOCOL.md; write code from the facts; never copy
decompiled code. RE scripts in `tools/re` (capstone + LIEF, `python -I`).
