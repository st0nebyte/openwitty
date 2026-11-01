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
| `bin/openwitty-tray` | optional system tray icon (see below); never started by the installation |
| `lib/systemd/user/openwitty-volumed.service` (packages) or `share/systemd/user/…` (user-local/default builds) | systemd **user** unit, installed **disabled** |
| `share/applications/io.github.st0nebyte.openwitty.desktop` | menu entry "OpenWitty" (+ "Start with simulated device") |
| `share/metainfo/…metainfo.xml`, `share/icons/hicolor/…` | AppStream data, own icons (no LEWITT artwork) |
| `share/man/man1/openwitty*.1.gz` | manual pages |
| `lib/udev/rules.d/70-openwitty.rules` | USB permission rule, **system packages only** |
| `share/doc/openwitty/` | documentation, licences, `examples/openwitty-connect2.conf` (kernel quirk example, not active) |

Run-time dependencies: Qt 6 (Core, Gui, Widgets, Network, DBus, Qml, Quick, Svg and the QtQuick QML modules),
libusb-1.0 and alsa-lib. The desktop integration was tested with PipeWire/WirePlumber; `systemd` (user session) is
needed only for the optional background service.

## Arch Linux / CachyOS

Build the package from this repository as a normal user (never as root):

```sh
packaging/arch/make-source-tarball.sh            # tarball of the committed tree
cd packaging/arch && makepkg -s                  # -s asks pacman for missing build dependencies
sudo pacman -U openwitty-0.1.0alpha1-1-x86_64.pkg.tar.zst
```

Remove: `sudo pacman -R openwitty` (see "Uninstall" for your own settings and the service).

## Debian / Ubuntu

```sh
sudo apt install build-essential cmake ninja-build pkg-config libgtest-dev libusb-1.0-0-dev libasound2-dev \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-shapes                                   # build dependencies
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEWITT_SYSTEMD_USER_UNIT_DIR=/usr/lib/systemd/user
cmake --build build && ctest --test-dir build --output-on-failure
(cd build && cpack -G DEB)                                       # -> build/openwitty_0.1.0~alpha1-1_amd64.deb
sudo apt install ./build/openwitty_0.1.0~alpha1-1_amd64.deb
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

> **Warning — speaker level after a logout.** With the quirk, the CONNECT 2's speaker level can be set by any
> audio session, including the login screen's own one. **Observed** on the tested system: after a logout and login
> the speaker was at −23.5 dB instead of the knob position — this can be **louder or quieter** than before. The
> **explanation** (the login screen's WirePlumber applies its default volume) is strongly supported but has not
> been traced. Turn down or switch off your speakers/headphones before logging out if a sudden level change would
> be a problem. With autologin there is no login screen at boot; it appears after a logout. Details:
> `docs/RELEASE_READINESS.md` ("Speaker level at the login screen").

#### Optional: keep the login screen away from the CONNECT 2

An administrator step, never done by OpenWitty or its packages; **verified on Plasma Login** (CachyOS, 2026-11-01:
the login screen's WirePlumber logs the CONNECT 2 as disabled), SDDM and GDM untested. A
WirePlumber rule in the *login screen user's* configuration makes that WirePlumber ignore the CONNECT 2
(`device.disabled`): it can then no longer set its level, and the login screen plays no sound through it. No other
device is affected and your own session never reads that file.

1. The rule file — use the copy that exists on your system:

   | OpenWitty installed as | File |
   |---|---|
   | system package | `/usr/share/doc/openwitty/examples/51-openwitty-connect2-login-screen.conf` |
   | user-local CMake install | `~/.local/share/doc/openwitty/examples/51-openwitty-connect2-login-screen.conf` (installs of `1156aa4` or later) |
   | source checkout | `packaging/wireplumber/51-openwitty-connect2-login-screen.conf` |

   Check with `ls -l <file>`; if it is missing, your installation predates the rule — use the source checkout's copy.

2. Find the login screen's account and home (read-only):

   ```sh
   systemctl show display-manager -p Id      # plasmalogin.service, sddm.service, gdm.service, ...
   getent passwd plasmalogin                 # or sddm / gdm: the 6th field is the home directory
   journalctl -b _UID=$(id -u plasmalogin) | grep -E 'wireplumber|pipewire' | head   # it runs its own audio stack
   ```

   On the tested CachyOS system: `plasmalogin.service`, user `plasmalogin` (UID 958), home `/var/lib/plasmalogin`
   (`plasmalogin:plasmalogin`, mode 0750), its own `pipewire`/`wireplumber` processes in the journal. Other
   distributions and login screens use other users and homes (SDDM: usually `sddm`, `/var/lib/sddm`; GDM: `gdm`,
   `/var/lib/gdm`); only Plasma Login has been looked at.

3. Install it into that user's `$HOME/.config/wireplumber/wireplumber.conf.d/`, creating missing directories **as
   that user** (a root-owned `~/.config` could break the login screen's own settings):

   ```sh
   RULE=/usr/share/doc/openwitty/examples/51-openwitty-connect2-login-screen.conf   # from step 1
   GREETER=plasmalogin                                                               # from step 2
   GHOME=$(getent passwd "$GREETER" | cut -d: -f6)
   sudo -u "$GREETER" mkdir -p "$GHOME/.config/wireplumber/wireplumber.conf.d"
   sudo install -m 0644 -o "$GREETER" -g "$GREETER" "$RULE" "$GHOME/.config/wireplumber/wireplumber.conf.d/"
   sudo ls -l "$GHOME/.config/wireplumber/wireplumber.conf.d/"
   ```

   It takes effect the next time the login screen starts (after the next logout). Never put the file into your
   own `~/.config/wireplumber`, `/etc/wireplumber` or `/usr/share/wireplumber` — the CONNECT 2 would vanish from
   your own desktop.

4. Undo: `sudo rm "$GHOME/.config/wireplumber/wireplumber.conf.d/51-openwitty-connect2-login-screen.conf"`; the
   next login screen manages the CONNECT 2 again. (Directories created in step 3 can stay; they are empty.)

### Desktop microphone mute that really mutes (recommended)

Verified on hardware (2026-10-28): without this, **muting the CONNECT 2 input in the desktop does not silence it** —
the device ignores the standard USB mute for its audio, reports "muted" by itself, and apps keep hearing the
microphone and instrument whenever they are unmuted on the device. The fix gives the CONNECT 2 its own small ALSA
profile in which PipeWire mutes and scales the input in software (output, speaker volume and knob sync unchanged;
other devices unaffected). It also stops the desktop from sending its saved input volume to the device.

```sh
ex=~/.local/share/doc/openwitty/examples     # package install: /usr/share/doc/openwitty/examples
mkdir -p ~/.config/alsa-card-profile/mixer/profile-sets ~/.config/alsa-card-profile/mixer/paths \
         ~/.config/wireplumber/wireplumber.conf.d
cp $ex/alsa-card-profile/profile-sets/openwitty-connect2.conf ~/.config/alsa-card-profile/mixer/profile-sets/
cp $ex/alsa-card-profile/paths/openwitty-connect2-input.conf  ~/.config/alsa-card-profile/mixer/paths/
cp $ex/52-openwitty-connect2-input.conf ~/.config/wireplumber/wireplumber.conf.d/
systemctl --user restart wireplumber
```

Afterwards the desktop shows the input as a normal, unmuted microphone at 100 %; desktop mute and the input slider
act on what apps receive (all four channels, loopback included). The device's own MUTE stays separate and still
silences the input at the source. OpenWitty's Settings ▸ Desktop integration shows whether it is installed.
Undo: delete the three files and restart WirePlumber.

### Separate inputs for apps (optional)

The CONNECT 2 delivers one 4-channel input (1 microphone, 2 instrument, 3/4 loopback of the computer audio; order
verified on hardware). Voice-chat and streaming apps usually want a single microphone. OpenWitty can add three extra
inputs — **CONNECT 2 Microphone** (mono), **CONNECT 2 Instrument** (mono) and **CONNECT 2 Loopback** (stereo) — with
PipeWire's loopback module: Settings ▸ Desktop integration ▸ *Separate inputs for apps*, then *Restart desktop audio*
(all sound stops for a moment). OpenWitty writes `~/.config/pipewire/pipewire.conf.d/53-openwitty-connect2-split.conf`
for your device (the file names the device's PipeWire node, which contains its serial number, so it is generated on
your machine). Each extra input is active only while an app records from it; the 4-channel input stays available for
DAWs. Switching it off removes the file. With the software input mute above installed, muting the main input in the
desktop also silences the extra inputs.

### Allow 44.1 and 96 kHz for apps (optional)

By default PipeWire runs at 48 kHz only (`pw-metadata -n settings 0 clock.allowed-rates` shows `[ 48000 ]`), so the
CONNECT 2 always runs at 48 kHz and other rates are converted. *Settings ▸ Desktop integration ▸ Allow 44.1 and 96 kHz
for apps* writes `~/.config/pipewire/pipewire.conf.d/54-openwitty-sample-rates.conf` with
`default.clock.allowed-rates = [ 44100 48000 96000 ]`; after *Restart desktop audio*, PipeWire runs the device at the
rate a DAW or player asks for. It applies to all sound cards. **Experimental:** in a test with repeated rate changes
(96 → 44.1 → 48 kHz, one every few seconds) the CONNECT 2's audio stopped responding (`usb_set_interface failed
(-110)` in `dmesg`) until the device was unplugged and plugged in again; single changes with the device idle worked. At 96 kHz the CONNECT 2 offers no Denoise and Comp.
Undo: switch it off (OpenWitty removes only its own file) or delete the file, then restart desktop audio.

### Using a DAW

The CONNECT 2 is a class-compliant USB audio device; DAWs need no OpenWitty code.

* **Channels:** capture 1 = microphone, 2 = instrument, 3/4 = loopback (computer playback) L/R; playback 1/2 = the
  stereo output that feeds speakers and headphones.
  With "Name the CONNECT 2 channels in audio apps" (Desktop integration, on by default) DAWs show these names
  instead of capture_FL/FR/RL/RR; the background service keeps them set.
* **Backends:** Ardour's ALSA backend opens the device directly (desktop audio on the CONNECT 2 pauses meanwhile).
  With `pipewire-jack` instead of `jack2`, JACK applications share the device with the desktop and also see the
  separate inputs.
* **Latency** (round trip through the loopback, pipewire-jack, 48 kHz, measured 2026-10-30): 64 frames 8.7 ms,
  128 frames about 12 ms, 256 frames 23 ms; a 30-minute run at 128 frames had no dropouts.
* **Ardour with JACK/PipeWire:** "JACK/Pipewire is already running, Ardour will connect to it" is normal; the
  sample rate comes from PipeWire. Pick the buffer size in Ardour's Audio/MIDI setup (it may start at PipeWire's
  1024 samples): at 128 samples Ardour reported 2.7 ms buffer and 8.7 ms input/output latency, DSP 2 % (2026-11-01).
* **Monitoring twice:** the CONNECT 2 already plays the inputs directly (Monitoring: source + Input/playback mix). If
  the DAW's track monitoring is on as well, you hear every input twice, the second time delayed — use either the
  device's direct monitoring (mix knob towards "Input") or the DAW's monitoring, not both.
* **96 kHz:** possible (see "Allow 44.1 and 96 kHz"), but Denoise and Comp are not available at that rate.

### Show the volume knob in the desktop volume

Linux cannot read back the CONNECT 2's speaker volume, so turning the **speaker volume knob** does not move
the desktop volume. Turn on **"Show the volume knob in the desktop volume"**: while the window is open it keeps
the desktop in step. It only sets the desktop's volume to the level the device already has (never louder,
never unmuting) and never writes the device. The desktop slider and the device's Mute button work without it.

### Background service (optional)

**"Keep it running when OpenWitty is closed"** enables the `openwitty-volumed` user service
(`systemctl --user enable --now openwitty-volumed.service`). It starts with your desktop session, mirrors the
knob while the window is closed and hands the device to the window when you open it. Turning the switch off
disables and stops it.

While it runs, the service is the **only program that talks to the CONNECT 2**: the window and `openwitty-cli` reach
the device through its private socket (`$XDG_RUNTIME_DIR/openwitty/device.sock`, this user only). The window takes
control there (the service pauses its own work until the window closes); `openwitty-cli read …` works at the same
time. The service checks every write again: only the controlling client may write, and only the settings OpenWitty's
window may change. Without the service everything opens the device directly, as
before. Command line:

```sh
systemctl --user status openwitty-volumed.service
journalctl --user -u openwitty-volumed
systemctl --user disable --now openwitty-volumed.service
```

### System tray icon (optional)

**"Show OpenWitty in the system tray"** (Settings → Desktop integration) starts `openwitty-tray` now and at every
login (an XDG autostart entry in `~/.config/autostart/`); turning it off removes that entry and the icon. Its
tooltip and menu show whether the volume knob is shown in the desktop volume and whether the background service
runs; the menu has **Open OpenWitty**, **Desktop integration settings…** and **Quit tray icon**, and a left
click opens OpenWitty. The icon only reads the settings file and `systemctl --user show`: it never uses the
device and never starts or stops the service. On a desktop without a system tray it exits quietly; OpenWitty
works the same without it. Tested offscreen; on KDE Plasma it uses the standard Status Notifier tray.

### What closing and quitting do

| Action | Effect |
|---|---|
| Close the OpenWitty window | the window ends and releases the device; a running background service takes it back |
| "Quit tray icon" | only the icon goes away (until the next login, if the tray setting is on) |
| "Keep it running…" off | the background service is disabled and stopped |
| Open OpenWitty while it is open (launcher, tray) | the open window comes to the front; no second window, no second device session |

**"Reset desktop integration"** turns everything off again, disables the service and removes the tray login entry. The settings live in
`~/.config/openwitty/openwitty.conf`; deleting that file restores the defaults.

OpenWitty's own controls need no permission switches: every setting that was verified on the real CONNECT 2 can be
changed from the window, every change is read back, and switching on 48 V, the louder output modes, mix to speakers,
mobile streaming or headphone volume keys asks once. Two experimental options are shown only in **debug mode**
(`openwitty --debug`): the *vendor fallback* (writes the device; only for a device that ignores the desktop volume;
not validated on hardware) and "Allow 44.1 and 96 kHz for apps". Permission keys from older OpenWitty versions in
`openwitty.conf` are ignored.

## Uninstall

1. In OpenWitty: Settings → Desktop integration → **Reset desktop integration** (or
   `systemctl --user disable --now openwitty-volumed.service`, `openwitty-tray --quit` and
   `rm ~/.config/autostart/io.github.st0nebyte.openwitty-tray.desktop`).
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
