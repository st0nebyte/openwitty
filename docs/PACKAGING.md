# Packaging

Native packages are the supported distribution route. Everything below is reproducible without
root and nothing is installed on the build host.

## Artifacts

| Format | Source | Validation performed |
| ------ | ------ | ------ |
| Arch / CachyOS | `packaging/arch/PKGBUILD` + `make-source-tarball.sh` | **Real rootless `makepkg` on CachyOS** (2026-10-25, Phase 9A; makepkg with the host's toolchain, build/package/source directories in a temporary location): `build()`, `check()` (281/281 CTests; `packaging_staging` excluded by design) and `package()` succeeded. Package inspected: all files `root:root`, binaries stripped without RPATH or development paths, every linked library owned by a declared dependency or a base package (`qt6-svg` is a declared run-time plugin), user unit at `/usr/lib/systemd/user` with `ExecStart=/usr/bin/openwitty-volumed` and not enabled, udev rule at `/usr/lib/udev/rules.d`, licences in `/usr/share/licenses/openwitty`, desktop entry and AppStream valid. **`namcap` was not run** (not installed; not installed for this audit). Not built in a clean chroot |
| `.deb` (Debian / Ubuntu) | `packaging/CPackConfig.cmake` (`cpack -G DEB`) | Earlier (Phase 5, Ubuntu 24.04 host, before the desktop integration): `lintian --pedantic`, `dpkg-deb -I/-c`, rootless install/upgrade/remove/purge simulation (`tools/packaging/check_deb_lifecycle.sh`). **Phase 9A, on CachyOS without Debian tools:** only a CPack build with `CPACK_DEBIAN_PACKAGE_SHLIBDEPS=OFF` to inspect the contents (control file, maintainer scripts, all files `root:root`, unit and udev rule locations, `ExecStart`). Shared-library dependencies, `lintian` and the lifecycle simulation have **not** been re-run since the desktop integration was added |
| AppImage | – | **Not built**, see decision below |

## Design rules

* Binaries carry no RPATH/RUNPATH and no build/source paths (`-ffile-prefix-map`,
  `LEWITT_REPRODUCIBLE`); no timestamps are embedded (`lewitt/version.hpp` is generated
  from `src/version.hpp.in`). `.sysroot/`, `.tools/`, `.venv/` and `research/` are
  development-only, git-ignored and never packaged.
* The `.deb` Depends come from `dpkg-shlibdeps` (Qt, libusb, alsa-lib) plus `qml6-module-qtquick`,
  `qml6-module-qtquick-window`, `qml6-module-qtquick-shapes`, `qt6-qpa-plugins` and `libqt6svg6` (QML modules and
  the SVG image plugin are loaded at run time, so shlibdeps cannot see them).
* Run-time dependencies on Arch: `qt6-base qt6-declarative qt6-svg libusb alsa-lib hicolor-icon-theme`.
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
* Run `namcap` on the Arch package (and ideally build it in a clean chroot, `extra-x86_64-build`).
* Rebuild the `.deb` on Debian/Ubuntu with `dpkg-shlibdeps` and `lintian`, re-run
  `tools/packaging/check_deb_lifecycle.sh`, and confirm `libqt6svg6` is the right package for the SVG plugin
  on each targeted release.
* Install and smoke-test both packages in a clean VM/container (nothing was installed on the development host;
  only the user-local install was used on the device).
