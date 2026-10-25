# Changelog

All notable changes to OpenWitty. Unofficial project, not affiliated with LEWITT.

## Unreleased — first public alpha (draft, not tagged)

Proposed version: `0.1.0-alpha.1` (see `docs/RELEASE_READINESS.md`, "Versioning"). The source tree still
advertises `0.1.0`; nothing has been tagged or released.

Tested with **one LEWITT CONNECT 2 on one system**: CachyOS, Linux 7.3.0, KDE Plasma 6 (Wayland),
PipeWire 1.6 / WirePlumber 0.5. Everything else is untested.

### Added

- Qt 6 / QML application for the CONNECT 2 with a simulated device (`openwitty --mock`, always labelled).
- Live, read-only view of the real device: input gain and mute, speaker/headphone volume and mute, monitoring
  mix, selected channel, Panic Mute, level meters (16 Hz settings / 48 Hz status), hot-plug and reconnect.
- Desktop volume integration (Settings → Desktop integration): the KDE/PipeWire volume follows the speaker
  volume knob, which Linux cannot see by itself. Never writes the device; only sets the desktop to the level
  the device already has (re-read first, rounded down, never unmuting).
- `openwitty-volumed` systemd user service (installed disabled, switched on from the Settings page): keeps the
  desktop volume in step while the window is closed, hands the device over to the window and back, starts with
  the desktop session. Validated at boot/login and across suspend/resume.
- Setup check for the desktop hardware volume and the kernel compatibility quirk (`openwitty-volumed --check`,
  Settings page), with an example module option for Linux 7.3 (`examples/openwitty-connect2.conf`, never
  installed automatically).
- Optional, confirmed *Speaker/headphone control*: OpenWitty's faders write speaker and headphone volume/mute
  only (≤ 10 dB louder per step, read back, never retried). The vendor write path is verified on hardware; the
  GUI option has not been exercised on hardware.
- Optional *vendor fallback* for a device that ignores the desktop volume — **experimental, not validated on
  hardware**.
- `openwitty-cli`: device listing, diagnosis, read-only reads, and four opt-in single-setting write commands
  (LED brightness and speaker volume/mute verified on hardware; instrument mute prepared, never executed).
- Optional system tray icon `openwitty-tray` (Qt's Status Notifier tray, no KDE libraries): status of the knob
  display and the background service, Open OpenWitty, Desktop integration settings, Quit tray icon. Never uses
  the device or changes the service; started at login only when the user turns it on.
- Single window per session: a second start brings the open window to the front (Wayland activation token
  forwarded) instead of opening a second device session. Launcher action "Desktop integration settings".
- One-writer control lease shared by all OpenWitty processes; USB call-site audit; man pages for all four
  programs; Arch PKGBUILD and Debian packaging; udev `uaccess` rule.

### Known limitations

See `docs/RELEASE_READINESS.md`. In short: one device and one distribution tested; most controls change the
simulated device only; vendor fallback unvalidated; CONNECT 6 unsupported; firmware update and factory reset
deliberately not implemented; not a replacement for LEWITT CONTROL CENTER.
