# Packaging

Native packages are the supported distribution route. Everything below is reproducible without
root and nothing is installed on the build host.

## Artifacts

| Format | Source | Validation performed (host: Ubuntu 24.04, rootless) |
| ------ | ------ | ------ |
| `.deb` (Ubuntu 24.04 / Debian) | `packaging/CPackConfig.cmake` (`cpack -G DEB`) | `lintian --pedantic` 2.117 (rootless copy): clean except `initial-upload-closes-no-bugs`, which only applies to Debian-archive uploads; `dpkg-deb -I/-c`; desktop/appstream/XML validators; **rootless install/upgrade/remove/purge simulation** into a private root with a stubbed `udevadm` (`tools/packaging/check_deb_lifecycle.sh`). Never installed on the host |
| Arch / CachyOS | `packaging/arch/PKGBUILD` + `make-source-tarball.sh` | The genuine `makepkg` 7.1.0 script (from Arch's `pacman` package, sha256 checked against the mirror db) built a `.pkg.tar.zst` rootless with `--nodeps` and ran `check()` (`tools/packaging/run_makepkg_rootless.sh`). Built with the host (Ubuntu) toolchain, not an Arch chroot; `namcap` could **not** be run (needs libalpm/pyalpm). A `makepkg -s` + `namcap` run on Arch/CachyOS is still outstanding. `tools/packaging/emulate_pkgbuild.sh` remains as a faster approximation and is *not* equivalent |
| AppImage | – | **Not built**, see decision below |

## Design rules

* Binaries carry no RPATH/RUNPATH and no build/source paths (`-ffile-prefix-map`,
  `LEWITT_REPRODUCIBLE`); no timestamps are embedded (`lewitt/version.hpp` is generated
  from `src/version.hpp.in`). `.sysroot/`, `.tools/`, `.venv/` and `research/` are
  development-only, git-ignored and never packaged.
* The `.deb` Depends come from `dpkg-shlibdeps` plus `qml6-module-qtquick`,
  `qml6-module-qtquick-window`, `qml6-module-qtquick-shapes` and `qt6-qpa-plugins` (QML modules are loaded at runtime, so
  shlibdeps cannot see them).
* Qt SVG is required for the bundled Lucide UI pictograms (`qt6-svg` on Arch;
  Ubuntu 24.04 ships its SVG image plugin in `libqt6svg6`). The upstream Lucide
  ISC/Feather MIT notice is installed as `LICENSE-LUCIDE` beside the project license.
* Identity is explicitly unofficial: package name `openwitty`, desktop name
  "OpenWitty", own icon (no LEWITT artwork).
* Mock-only builds (`-DLEWITT_ENABLE_USB=OFF`) have no libusb dependency and an
  identically-behaving write guard in the stub transport.
* The udev rule is shipped in system-prefix packages only and is never installed by a
  plain `cmake --install` into `~/.local`.

## Reproducibility

Two builds from different source and build directories give a byte-identical package payload
when `SOURCE_DATE_EPOCH` is set (makepkg and dpkg-buildpackage set it automatically; Qt's
`rcc` embeds file mtimes otherwise). Verified with a fresh `git clone` against the working
tree. `LEWITT_REPRODUCIBLE=ON` (default) adds `-ffile-prefix-map` and drops the build-tree RPATH,
so sysroot-based development builds need `LD_LIBRARY_PATH` (as already documented).
The `.deb` container itself (ar/tar mtimes, owner) is not normalised by CPack.

## Verification tooling

```sh
tools/packaging/check_staging.sh build          # also a ctest entry: packaging_staging
tools/packaging/emulate_pkgbuild.sh             # PKGBUILD build()/check()/package() emulation
tools/packaging/run_makepkg_rootless.sh         # genuine makepkg, rootless, --nodeps (downloads pacman pkg)
tools/packaging/check_deb_lifecycle.sh <deb>    # install/upgrade/remove/purge in a private root
tools/audit/check_usb_call_sites.sh             # static guard (also ctest audit_usb_call_sites)
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
* Revisit when hardware access works. A Flatpak would have the same
  udev limitation and also needs device portal permissions.

## Open items for the project owner

* Maintainer/contact e-mail in the packages is the GitHub no-reply placeholder; replace if desired.
* Run `makepkg -s` and `namcap` on a real Arch/CachyOS host (namcap has not been run at all).
* Install and smoke-test the packages in a clean VM/container (nothing was installed on the
  development host; install/remove was simulated into a private root).
