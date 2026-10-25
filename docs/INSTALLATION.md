# Installation

> **Unofficial and experimental.** OpenWitty is not affiliated with or endorsed by LEWITT and is not a
> replacement for LEWITT CONTROL CENTER. It has been tested with **one CONNECT 2 on one system**
> (CachyOS, Linux 7.3, KDE Plasma 6 on Wayland, PipeWire/WirePlumber). What is validated and what is not:
> `README.md`, `FEATURES.md`, `docs/RELEASE_READINESS.md`. Licence: MIT (`LICENSE`); third-party notes:
> `docs/THIRD_PARTY.md`.

Building and running need no root. Administrator steps are always optional, explicit and done by you:
installing a package, the USB permission rule, and the kernel compatibility quirk. OpenWitty never
performs them.

## What gets installed

| Path (prefix `/usr`; `~/.local` for a user-local install) | Purpose |
|---|---|
| `bin/openwitty` | the application (Qt 6 / QML) |
| `bin/openwitty-cli` (+ `lewitt-cli` alias) | diagnostic command-line tool, read-only unless a write command is explicitly enabled |
| `bin/openwitty-volumed` | desktop volume service (see below) |
| `lib/systemd/user/openwitty-volumed.service` (packages) or `share/systemd/user/…` (user-local/default builds) | systemd **user** unit, installed **disabled** |
| `share/applications/io.github.st0nebyte.openwitty.desktop` | menu entry "OpenWitty" (+ "Start with simulated device") |
| `share/metainfo/…metainfo.xml`, `share/icons/hicolor/…` | AppStream data, own icons (no LEWITT artwork) |
| `share/man/man1/openwitty*.1.gz` | manual pages |
| `lib/udev/rules.d/70-openwitty.rules` | USB permission rule, **system packages only** |
| `share/doc/openwitty/` | documentation, licences, `examples/openwitty-connect2.conf` (kernel quirk example, not active) |

Run-time dependencies: Qt 6 (Core, Gui, Qml, Quick, Svg and the QtQuick QML modules), libusb-1.0 and
alsa-lib. The desktop integration was tested with PipeWire/WirePlumber; `systemd` (user session) is
needed only for the optional background service.

## Arch Linux / CachyOS

Build the package from this repository as a normal user (never as root):

```sh
packaging/arch/make-source-tarball.sh            # tarball of the committed tree
cd packaging/arch && makepkg -s                  # -s asks pacman for missing build dependencies
sudo pacman -U openwitty-0.1.0-1-x86_64.pkg.tar.zst
```

Remove: `sudo pacman -R openwitty` (see "Uninstall" for your own settings and the service).

## Debian / Ubuntu

```sh
sudo apt install build-essential cmake ninja-build pkg-config libgtest-dev libusb-1.0-0-dev libasound2-dev \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-shapes                                   # build dependencies
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEWITT_SYSTEMD_USER_UNIT_DIR=/usr/lib/systemd/user
cmake --build build && ctest --test-dir build --output-on-failure
(cd build && cpack -G DEB)                                       # -> build/openwitty_0.1.0-1_amd64.deb
sudo apt install ./build/openwitty_0.1.0-1_amd64.deb
```

Inspect the package first with `dpkg-deb -I` / `dpkg-deb -c`. Its postinst only reloads the udev rules for
LEWITT devices; it never touches drivers, the kernel quirk or the service. Remove: `sudo apt remove openwitty`.
The Debian package has not been built or installed on a Debian/Ubuntu system since the desktop integration
was added (see `docs/PACKAGING.md`).

## User-local install (no root)

```sh
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DLEWITT_INSTALL_UDEV_RULES=OFF
cmake --build build-local && ctest --test-dir build-local
cmake --install build-local
systemctl --user daemon-reload                    # makes the (disabled) user unit known
```

A user-local install cannot install the USB permission rule (udev reads only system directories); install it
once as described below. `~/.local/bin` must be on your `PATH` for the menu entry to start the application.

## USB permissions (needed for the real device)

The CONNECT 2's USB device node is not writable for ordinary users, and USB control requests need that. The
rule `70-openwitty.rules` gives the user logged in at the seat access to LEWITT devices (`uaccess`; no group,
no `0666`). Packages install it; otherwise:

```sh
sudo install -m 0644 packaging/udev/70-openwitty.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=29c2    # or replug the device
openwitty-cli diagnose                                                   # read-only check
```

Details, verification and removal: `docs/USB_PERMISSIONS.md`. OpenWitty talks to the device's own vendor
control interface; it never detaches the kernel audio driver, and audio keeps working while it runs.

## First start

```sh
openwitty --mock          # simulated device: every control, always labelled "simulated"
openwitty                 # the real CONNECT 2 (permissions above)
```

With a real device the window shows the live state read-only. Nothing on the device changes unless you turn
on one of the options that say "writes the device" in Settings and confirm it.

## Desktop volume (KDE/PipeWire)

**Settings → Desktop integration** shows whether the desktop has a hardware volume for the CONNECT 2 and
whether the kernel quirk is needed. Command-line equivalent: `openwitty-volumed --check`.

### The kernel compatibility quirk (optional, device-specific)

On Linux 7.3 a broken microphone control of the CONNECT 2 makes the kernel's mixer fail to load, and the
desktop then has no hardware volume for the device. If the page (or `--check`) reports that, this module
option fixes it for this device only (`29c2:0004`), by telling the kernel to ignore control errors of the
CONNECT 2:

```sh
sudo install -m 0644 /usr/share/doc/openwitty/examples/openwitty-connect2.conf /etc/modprobe.d/
#   (user-local install: ~/.local/share/doc/openwitty/examples/openwitty-connect2.conf)
# takes effect after a reboot (or replugging the CONNECT 2)
```

It is never installed silently, by packages or by OpenWitty. Not needed if the page reports "Desktop hardware
volume: available" without it; other kernels may not need it. Remove: `sudo rm
/etc/modprobe.d/openwitty-connect2.conf`, then reboot.

### Show the volume knob in the desktop volume

Linux cannot read back the CONNECT 2's speaker volume, so turning the **speaker volume knob** does not move
the desktop volume. Turn on **"Show the volume knob in the desktop volume"**: while the window is open it keeps
the desktop in step. It only sets the desktop's volume to the level the device already has (never louder,
never unmuting) and never writes the device. The desktop slider and the device's Mute button work without it.

### Background service (optional)

**"Keep it running when OpenWitty is closed"** enables the `openwitty-volumed` user service
(`systemctl --user enable --now openwitty-volumed.service`). It starts with your desktop session, mirrors the
knob while the window is closed and hands the device to the window when you open it. Turning the switch off
disables and stops it. Command line:

```sh
systemctl --user status openwitty-volumed.service
journalctl --user -u openwitty-volumed
systemctl --user disable --now openwitty-volumed.service
```

**"Reset desktop integration"** turns everything off again and disables the service. The settings live in
`~/.config/openwitty/openwitty.conf`; deleting that file restores the defaults.

The other options on that page write the device and need confirmation: *Speaker/headphone control* (OpenWitty's
own faders) and the *vendor fallback* (experimental, not validated on hardware; only for a device that ignores
the desktop volume).

## Uninstall

1. In OpenWitty: Settings → Desktop integration → **Reset desktop integration** (or
   `systemctl --user disable --now openwitty-volumed.service`).
2. Remove the program:
   * package: `sudo pacman -R openwitty` / `sudo apt remove openwitty`;
   * user-local: `xargs -d '\n' rm -f < build-local/install_manifest.txt` and `systemctl --user daemon-reload`
     (the manifest lists exactly the installed files, including the compatibility links).
3. Optional: `rm -r ~/.config/openwitty` (your settings) and `~/.local/state/openwitty` (only if you created
   logs there).
4. Only if you installed them by hand: `/etc/udev/rules.d/70-openwitty.rules` and
   `/etc/modprobe.d/openwitty-connect2.conf` (see above). Nothing else is touched by OpenWitty.

## Build variants

| Goal | Option |
|---|---|
| Release (default) | `-DCMAKE_BUILD_TYPE=Release` |
| Mock-only (no libusb, no ALSA, no service) | `-DLEWITT_ENABLE_USB=OFF` |
| Without the desktop volume service | `-DLEWITT_ENABLE_ALSA=OFF` |
| No GUI (CLI/core) | `-DLEWITT_BUILD_GUI=OFF` |
| No tests | `-DLEWITT_BUILD_TESTS=OFF` |
| Unit directory | `-DLEWITT_SYSTEMD_USER_UNIT_DIR=/usr/lib/systemd/user` (absolute, as packages want it) |
| Sanitizers | `-DLEWITT_SANITIZE=address,undefined` |
