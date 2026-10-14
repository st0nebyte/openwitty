# Installation

> **Unofficial and experimental.** This project is not affiliated with or endorsed by LEWITT.
> The control protocol was derived by static analysis and has **not been validated on real
> hardware**; hardware writes are disabled, and a simulated device is built in.
> Licence: MIT (see `LICENSE`); third-party and asset notes: `docs/THIRD_PARTY.md`.

Nothing here needs root **to build or run**. Only two optional steps are administrative and
are never performed by the build system or by us: installing a distribution package, and
installing the udev rule (see `docs/USB_PERMISSIONS.md`).

## What gets installed

| Path (prefix `/usr`)                                   | Purpose |
| ------------------------------------------------------ | ------- |
| `bin/openwitty`                            | Qt 6 / QML application |
| `bin/openwitty-cli`                                       | Read-only diagnostic CLI |
| `share/applications/io.github.st0nebyte.openwitty.desktop`     | Menu entry "OpenWitty" (+ "Simulated device" action) |
| `share/metainfo/…metainfo.xml`, `share/icons/hicolor/…`| AppStream data, own icon artwork (no LEWITT artwork) |
| `lib/udev/rules.d/70-openwitty.rules`            | `uaccess` rule, **package builds only** |
| `share/doc/openwitty/`                | Docs, copyright |

Runtime dependencies are only Qt 6 (Core/Gui/Qml/Quick), the Qt Quick QML modules, a Qt
platform plugin and libusb-1.0. No Python, Node, Wine, Ghidra or REA tooling is needed.

## Ubuntu / Debian (.deb)

Build the package as a normal user (no `sudo`):

```sh
sudo apt install build-essential cmake ninja-build pkg-config libgtest-dev \
    qt6-base-dev qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-controls qml6-module-qtquick-layouts libusb-1.0-0-dev   # build deps (admin)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
(cd build && cpack -G DEB)          # -> build/openwitty_0.1.0-1_amd64.deb
```

Inspect it without installing: `dpkg-deb -I`, `dpkg-deb -c`. Installing (`sudo apt install
./build/openwitty_*.deb`) is your decision; the package's postinst only
reloads udev rules (`udevadm control --reload`, `udevadm trigger --subsystem-match=usb
--attr-match=idVendor=29c2`) and never touches drivers or devices.

## Arch Linux / CachyOS

```sh
packaging/arch/make-source-tarball.sh     # git archive of the committed tree
cd packaging/arch && makepkg -s           # as a normal user, NEVER as root
sudo pacman -U openwitty-0.1.0-1-x86_64.pkg.tar.zst   # your decision
```

The PKGBUILD was validated by emulation (see `docs/PACKAGING.md`), not by a real `makepkg`
run, because `makepkg` is unavailable on the development host.

## Without root: user-local install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEWITT_INSTALL_UDEV_RULES=OFF
cmake --build build
cmake --install build --prefix ~/.local
~/.local/bin/openwitty --mock      # simulated device
```

User-local installs cannot install the udev rule; see `docs/USB_PERMISSIONS.md` if you
need USB access. Uninstall: `xargs rm < build/install_manifest.txt`.

## Build variants

| Goal                | Command |
| ------------------- | ------- |
| Release (default)   | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| Debug               | `-DCMAKE_BUILD_TYPE=Debug` |
| Mock-only (no libusb) | `-DLEWITT_ENABLE_USB=OFF` — hardware transport reports "not supported" |
| No GUI (CLI/core)   | `-DLEWITT_BUILD_GUI=OFF` |
| No tests            | `-DLEWITT_BUILD_TESTS=OFF` |
| Sanitizers          | `-DLEWITT_SANITIZE=address,undefined` |

## First run

```sh
openwitty --mock      # simulated device, always safe
openwitty-cli diagnose               # read-only USB/permission diagnosis
openwitty-cli read settings          # read-only; needs permissions, see USB_PERMISSIONS.md
```
