# Changelog

All notable changes to OpenWitty. Unofficial project: not made, supported or endorsed by LEWITT.

## 0.1.0 — 2026-11-02

First public release. Tested with **one LEWITT CONNECT 2 on one system**: CachyOS, Linux 7.3, KDE Plasma 6.8
(Wayland), PipeWire 1.6 / WirePlumber 0.5. The Debian package was built, tested, installed and removed on
Debian 13 (trixie) in a clean container (no real device there).

### Device control

- Every setting that was verified on the real CONNECT 2 works from the window: input gain and mute, gain lock,
  low-cut, Clipguard, Clean/Warm/Vivid, Denoise (with noise-floor measurement), Comp, 48 V, monitoring source and
  mix, Dim, Mono, loopback mute, speaker and headphone volume and mute, high-volume speaker and high-impedance
  headphone modes, LED brightness and colours, custom button, input/playback mix to speakers, mobile streaming
  mode, volume-key mapping.
- Autosetup (gain measurement, optional 48 V, optional ambient-noise test for Denoise).
- Presets: save the current settings under a name and load them again (OpenWitty's own format; output levels only
  when chosen; every change listed before loading).
- Live view: the window follows changes made on the device (knob, touch wheel, buttons, Panic Mute) and shows level
  meters.
- Safety built in: only known settings bytes are written, each write is read back and never retried, large
  increases go in verified steps, and 48 V, the louder output modes and the routing changes ask once.

### Desktop integration (Settings ▸ Desktop integration)

- The desktop volume follows the CONNECT 2's volume knob (Linux cannot see the knob by itself).
- Follow active output: the volume keys control the output you can hear (speakers or headphones).
- Optional background service `openwitty-volumed` (systemd user unit): keeps the above working while the window is
  closed; the window and `openwitty-cli` then use the device through it.
- Optional tray icon `openwitty-tray`: shows whether the CONNECT 2 is connected and opens the window.
- Open the window at login; reopen the window where it was closed (KDE Plasma, or any X11 session).
- Readable names in the desktop and in audio apps: "CONNECT 2 Output", "CONNECT 2 Microphone / Instrument /
  Loopback" as separate inputs (optional), channel names in DAWs.
- Desktop microphone mute that really mutes (optional ALSA profile + WirePlumber rule).
- Setup check for the Linux 7.3 kernel fix (an optional module option that OpenWitty explains but never installs).

### Other

- Simulated device (`openwitty --mock`), always labelled as simulated.
- Debug mode (`openwitty --debug`): hardware check and experimental options.
- `openwitty-cli`: device listing, diagnosis and read-only reads.
- Packages: Arch Linux (PKGBUILD) and Debian 13 (`.deb`); udev rule for user access; man pages for all programs.

### Fixed since the private test builds

- After login the window could stay "not connected" when it started while the background service was still
  finding the CONNECT 2; it now connects by itself.

### Known limitations

- One device on one system tested. Other distributions and desktops are untested; the desktop integration was
  developed for KDE Plasma with PipeWire/WirePlumber. The tray icon needs a desktop with a system tray
  (GNOME needs an extension). "Reopen the window where it was closed" does not work on Wayland desktops other
  than KDE Plasma.
- Linux 7.3 needs the optional kernel fix for the desktop hardware volume (see `docs/INSTALLATION.md`); with it,
  the login screen's audio session can set the speaker level unless the optional login-screen rule is installed.
- Sample rates other than 48 kHz are experimental (debug mode): repeated rate changes can hang the CONNECT 2's audio
  until it is replugged.
- macOS streaming mode, firmware update and factory reset are not part of OpenWitty; use LEWITT's official app.
- The CONNECT 6 is not supported.

The development history before this release: `docs/dev/CHANGELOG_DEVELOPMENT.md`.
