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

## REA MCP integration (development only)
`rea` is registered for Claude Code user-wide (`mcpServers.rea` in
`~/.claude.json`, backup in `~/.claude/backups/pre-rea/`), pointing at the
project-local Node 24 + Ghidra 12.1.4 + JDK 21 (`.tools/`). Restart Claude Code
to load its tools; verify with `claude mcp list` (`rea … ✔ Connected`). The
REA skill lives in `~/.claude/skills/reverse-engineer-anything`. Remove with
`rea uninstall` or `claude mcp remove --scope user rea`.

Fast scripted analysis without MCP (no re-import): a persistent Ghidra project
(`research/ghidra_proj`, git-ignored) plus the scripts in `tools/re/ghidra/`:
```
source .tools/rea-env.sh
analyzeHeadless research/ghidra_proj lcc -import lcc_win.exe        # once
analyzeHeadless research/ghidra_proj lcc -process lcc_win.exe -noanalysis -readOnly \
    -scriptPath tools/re/ghidra -postScript StringXrefs.java TUSBAUDIO_ 0   # ~4 s
```
Scripts: `StringXrefs`, `DecompileAt`, `CallersOf`, `IndirectCalls`, `VtableDump`.

## Sanitizers, benchmarks, diagnostics
```
# ASan+UBSan (core + GUI)
cmake -S . -B build-asan -G Ninja -DCMAKE_PREFIX_PATH=$PWD/.sysroot/usr -DLEWITT_SANITIZE=address,undefined -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan && ctest --test-dir build-asan
# TSan (core only)
cmake -S . -B build-tsan -G Ninja -DCMAKE_PREFIX_PATH=$PWD/.sysroot/usr -DLEWITT_SANITIZE=thread -DLEWITT_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan; setarch $(uname -m) -R build-tsan/lewitt_tests
# benchmarks
cmake --build build --target lewitt_bench lewitt_bench_gui && build/lewitt_bench && QT_QPA_PLATFORM=offscreen build/src/gui/lewitt_bench_gui
# diagnostics (read-only)
build/lewitt-cli devices | diagnose | read api [--method interface|device-recipient] [--request uac2|uac1] | probe [--execute]
```

## Presets

`cmake --preset release` (also `debug`, `mock-only`, `core-only`, `asan`, `tsan`, `warnings`),
then `cmake --build --preset <name>` and `ctest --preset <name>`. Build trees are
`build-<preset>/`. Export `PKG_CONFIG_PATH`, `LD_LIBRARY_PATH`, `QML_IMPORT_PATH` and
`CMAKE_PREFIX_PATH` for the rootless sysroot first (`tools/setup-sysroot.sh` prints them); the
build tree carries no RPATH, so `LD_LIBRARY_PATH` is required to run the binaries. TSan needs
`setarch -R` (also for the build, because of gtest test discovery).

## Packaging builds

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel
(cd build-rel && cpack -G DEB)                     # rootless .deb, not installed
cmake -S . -B build-mock -DLEWITT_ENABLE_USB=OFF   # mock-only, no libusb
tools/packaging/check_staging.sh build-rel         # isolated DESTDIR install checks
tools/packaging/emulate_pkgbuild.sh                # PKGBUILD emulation (no makepkg here)
```

The configure step bakes `QML_IMPORT_PATH` into the GUI test environment; set it (and the
sysroot variables) *before* running `cmake`, or reconfigure. Details: docs/PACKAGING.md.
