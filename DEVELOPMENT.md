# Development

## Dependencies
Ubuntu/Debian:
`sudo apt install build-essential cmake ninja-build pkg-config libusb-1.0-0-dev libgtest-dev qt6-base-dev qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-window libasound2-dev`
Arch/CachyOS: `sudo pacman -S base-devel cmake ninja libusb gtest qt6-base qt6-declarative alsa-lib`

Without root (what this repo's author did): `apt-get download` the packages
and `dpkg -x` them into `./.sysroot` (git-ignored), then set
`PKG_CONFIG_PATH`, `PKG_CONFIG_SYSROOT_DIR`, `CMAKE_PREFIX_PATH`,
`LD_LIBRARY_PATH`, `QML_IMPORT_PATH` accordingly.

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
