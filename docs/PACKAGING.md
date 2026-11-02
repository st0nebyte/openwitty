# Packaging

Native packages are the supported distribution route. Everything below is reproducible without
root and nothing is installed on the build host.

## Artifacts

| Format | Source | Validation performed |
| ------ | ------ | ------ |
| Arch / CachyOS | `packaging/arch/PKGBUILD` + `make-source-tarball.sh` | **Real rootless `makepkg` on CachyOS** (Phase 9A–9D; temporary build/package/source directories): `build()`, `check()` (292/292; `packaging_staging` excluded by design), `package()`. **`namcap`** 3.6.0 (Phase 9D): PKGBUILD clean; package only `Dependency included, but may not be needed ('qt6-svg')` — a false positive, Qt's SVG image plugin is loaded at run time (Lucide icons). Package inspected: `root:root`, stripped, no RPATH/dev paths, unit at `/usr/lib/systemd/user` (disabled), udev rule, licences, examples only. Not built in a clean chroot |
| `.deb` (Debian / Ubuntu) | `packaging/CPackConfig.cmake` (`cpack -G DEB`) | **0.1.0 (2026-11-02), same disposable container, updated Debian 13 (Qt 6.8.2):** build, 361 CTests (one timing race in a GUI test found and fixed), `cpack`, `lintian --pedantic` (only `initial-upload-closes-no-bugs`), lifecycle PASS, real install / use / remove / purge, nothing left. Earlier: **Disposable Debian 13.7 (trixie) container** (Phase 9D: `debootstrap` + `systemd-nspawn`, no USB devices inside, built and tested as an unprivileged user): 292/292 CTests (Qt 6.8.2); `cpack` with `dpkg-shlibdeps`; `lintian --pedantic`: only `initial-upload-closes-no-bugs` (archive uploads only); `check_deb_lifecycle.sh`: PASS; real `apt install` → binaries, GUI `--mock --screenshot`, tray without a tray host, `systemd-analyze --user verify` clean, unit disabled, no autostart/modprobe/WirePlumber file outside the examples → `apt remove` + `purge`: nothing left. A package built on Debian 13 needs Qt ≥ 6.8 (shlibdeps); build per target distribution. **Ubuntu 24.04 / Debian 12 not tested** |
| AppImage | – | **Not built**, see decision below |

## Design rules

* Binaries carry no RPATH/RUNPATH and no build/source paths (`-ffile-prefix-map`,
  `LEWITT_REPRODUCIBLE`); no timestamps are embedded (`lewitt/version.hpp` is generated
  from `src/version.hpp.in`). `.sysroot/`, `.tools/`, `.venv/` and `research/` are
  development-only, git-ignored and never packaged.
* The `.deb` Depends come from `dpkg-shlibdeps` (Qt, libusb, alsa-lib) plus `qml6-module-qtquick`,
  `qml6-module-qtquick-window`, `qml6-module-qtquick-shapes`, `qt6-qpa-plugins` and
  `qt6-svg-plugins | libqt6svg6 (<< 6.5~)` (QML modules and the SVG image plugin are loaded at run time, so
  shlibdeps cannot see them). The SVG plugin moved to `qt6-svg-plugins` with Qt 6.8 (Debian 13; without it the
  GUI's icons fail to load — seen in the container); Debian 12 and Ubuntu 24.04 (Qt 6.4.2) ship it in `libqt6svg6`.
* The upstream changelog is installed as `share/doc/openwitty/changelog.gz` (Debian/FHS name).
* Run-time dependencies on Arch: `qt6-base qt6-declarative qt6-svg libusb alsa-lib hicolor-icon-theme` (`qt6-base`
  also provides Qt Widgets, Network and D-Bus for the tray icon and the single-instance channel).
* `openwitty-tray` is installed but never autostarted by a package: there is no `/etc/xdg/autostart` entry; the
  login entry is written into the user's `~/.config/autostart` only by the Settings switch. The tray links no
  libusb and no ALSA (checked by `packaging_staging` and `desktop_single_instance_and_tray`).
* The systemd user unit is installed **disabled**; packages put it into the distribution's user unit directory
  by passing an absolute `-DLEWITT_SYSTEMD_USER_UNIT_DIR=/usr/lib/systemd/user` (the default
  `<prefix>/share/systemd/user` is also on systemd's search path and is right for `~/.local`). Note: the exact
  *relative* value `lib/systemd/user` is migrated to the default for old build trees; use the absolute path.
* The kernel compatibility quirk is shipped only as `share/doc/openwitty/examples/openwitty-connect2.conf`;
  nothing is installed into `modprobe.d`, and maintainer scripts never touch drivers, modules or the service.
* Developer and hardware-test notes (`HARDWARE_TEST_PLAN.md`, `HANDOFF_HARDWARE_TESTING.md`, phase notes) are
  not installed.
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
tools/packaging/run_makepkg_rootless.sh         # non-Arch hosts only: genuine makepkg, rootless, --nodeps
# on Arch/CachyOS: BUILDDIR=… PKGDEST=… SRCDEST=… makepkg -f   (from a copy of packaging/arch + the tarball)
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
* A sandboxed format would also have to reach the host's ALSA control and the systemd user session for the
  desktop integration. A Flatpak would have the same udev limitation and needs device permissions. Native
  packages remain the supported route.

## Open items for the project owner

* Maintainer/contact e-mail in the packages is the GitHub no-reply placeholder; replace if desired.
* Optionally build the Arch package in a clean chroot (`extra-x86_64-build`).
* Build and install-test the `.deb` on Ubuntu 24.04 and Debian 12 (only Debian 13 was tested).
* Install and smoke-test both packages in a clean VM/container (nothing was installed on the development host;
  only the user-local install was used on the device).
