# Packaging

Native packages are the supported distribution route. Everything below is reproducible without
root and nothing is installed on the build host.

## Artifacts

| Format | Source | Status |
| ------ | ------ | ------ |
| `.deb` (Ubuntu 24.04 / Debian) | `packaging/CPackConfig.cmake` (`cpack -G DEB`) | Built and inspected here (dpkg-deb, desktop-file-validate, appstreamcli, xmllint); **not installed**, not lintian-checked (lintian unavailable) |
| Arch / CachyOS | `packaging/arch/PKGBUILD` + `make-source-tarball.sh` | Validated by `bash -n` and by `tools/packaging/emulate_pkgbuild.sh` (fake srcdir/pkgdir). **Not run through a real makepkg/namcap** (unavailable on the Ubuntu host) |
| AppImage | – | **Not built**, see decision below |

## Design rules

* Binaries carry no RPATH/RUNPATH and no build/source paths (`-ffile-prefix-map`,
  `LEWITT_REPRODUCIBLE`); no timestamps are embedded (`lewitt/version.hpp` is generated
  from `src/version.hpp.in`). `.sysroot/`, `.tools/`, `.venv/` and `research/` are
  development-only, git-ignored and never packaged.
* The `.deb` Depends come from `dpkg-shlibdeps` plus `qml6-module-qtquick`,
  `qml6-module-qtquick-window` and `qt6-qpa-plugins` (QML modules are loaded at runtime, so
  shlibdeps cannot see them).
* Identity is explicitly unofficial: package name `lewitt-connect2-unofficial`, desktop name
  "CONNECT 2 Control Center (unofficial)", own icon (no LEWITT artwork).
* Mock-only builds (`-DLEWITT_ENABLE_USB=OFF`) have no libusb dependency and an
  identically-behaving write guard in the stub transport.
* The udev rule is shipped in system-prefix packages only and is never installed by a
  plain `cmake --install` into `~/.local`.

## Verification tooling

```sh
tools/packaging/check_staging.sh build          # also a ctest entry: packaging_staging
tools/packaging/emulate_pkgbuild.sh             # PKGBUILD build()/check()/package() emulation
```

`check_staging.sh` installs into a temp DESTDIR and checks: file layout, only `/usr`
populated, no installers/captures, no RPATH/RUNPATH, NEEDED libs are system sonames, no
`.sysroot`/`.tools`/`/home` strings, `--version`, a mock read, a headless `--mock
--screenshot` start/exit, desktop/appstream/XML validity, icon/Exec resolution, udev rule
properties (uaccess, vendor-scoped, no 0666).

## AppImage decision: not worthwhile (for now)

* Requires third-party tooling (linuxdeploy + Qt plugin) downloaded as binaries — against the
  project's no-unvetted-binaries rule and unavailable offline.
* Bundles Qt (~60–100 MB) for a 280 kB application.
* The decisive point: it cannot install the udev rule, so USB access would still need an
  administrative step and would fail with EACCES; the kernel-driver question (EBUSY) is
  unaffected. An AppImage would therefore only be useful for the simulated device.
* Revisit when a license is chosen and hardware access works. A Flatpak would have the same
  udev limitation and also needs device portal permissions.

## Open items for the project owner

* **Choose a licence.** Metadata uses `LicenseRef-proprietary` (appstream) /
  `LicenseRef-custom` (PKGBUILD) and `copyright` says "all rights reserved until chosen".
* Replace the placeholder homepage `https://example.invalid/…` and maintainer address.
* Run `makepkg`, `namcap` and `lintian` on a real Arch / Debian host.
* Install and smoke-test the packages in a clean VM/container (we did not install anything).
