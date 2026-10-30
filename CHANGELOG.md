# Changelog

All notable changes to OpenWitty. Unofficial project, not affiliated with LEWITT.

## 0.1.0-alpha.1 — unreleased (prepared 2026-10-25, not tagged)

Version metadata set in the tree (`--version`: `0.1.0-alpha.1`; Debian `0.1.0~alpha1`; Arch `0.1.0alpha1`;
AppStream `0.1.0~alpha1`, type development). No Git tag and no release yet.

Tested with **one LEWITT CONNECT 2 on one system**: CachyOS, Linux 7.3.0, KDE Plasma 6 (Wayland),
PipeWire 1.6 / WirePlumber 0.5. Everything else is untested.

### Added
- Input meters: a blue line marks the noise floor from the last Denoise measurement (bytes 8/17, hover for the value).
- Settings ▸ Audio: "Input/playback mix applies to speakers" and "Mobile streaming mode" under a new *Routing*
  permission (each switch-on asks first); the Audio section scrolls.
- `openwitty-cli read settings` lists the bytes the device maintains itself.
- Denoise: "Measure" runs the device's noise measurement; the device then switches Denoise on by itself (verified
  on the hardware).
- Output faders: dB readout; Shift + wheel/drag for 0.1 dB steps. Autogain asks before every measurement and closes
  a measurement that does not finish within 45 s.
- Optional separate desktop inputs "CONNECT 2 Microphone / Instrument / Loopback" (PipeWire loopback module, written
  for the user's device from Settings ▸ Desktop integration) and a descriptive name for the 4-channel input (Phase 11A.4).
- Headless Ardour DAW test and audio analysis tools in `tools/audio/` (DAW-1…4 passed with the ALSA backend).
- Desktop input mute that really mutes: optional per-device ALSA profile + WirePlumber rule (software input
  mute/volume for the CONNECT 2), detected in Settings ▸ Desktop integration (Phase 11C, hardware-verified).
- Hardware-verified (Phase 11B): all low-risk settings, outputs, loopback and input channel identity via the GUI.

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
- Settings → Device control: separate, separately confirmed permissions for every supported setting group —
  speaker, headphone, LED brightness, input gain/mute, input processing, monitoring, LED colours and custom button,
  48 V, output modes, autogain measurements — each scoped to its own bytes (volume ≤ 10 dB, gain ≤ 6 dB louder per
  write; every write read back, never retried). The LED brightness permission passed its GUI hardware test (HW-1);
  the other groups await their hardware sessions. Mix-to-speakers and the streaming modes stay display-only.
- Device sample rate shown from the kernel's stream state (read-only; "unknown" while no stream is open); the
  monitoring-mix knob also shows the raw device value, because the device scale is not calibrated.
- GUI preferences (`gui.conf`): the first-run introduction is shown once, the tooltip setting is remembered.
- `openwitty --hw-check`: read-only, in-window settings comparison for owner-approved hardware tests (baseline from
  the first successful read, also after a hand-over; never replaced silently; no device write without it).
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

### Changed

- The combined `output_control` setting was replaced by `speaker_control`, `headphone_control` and
  `led_brightness_control`. An existing `output_control = true` grants nothing (fail-closed); OpenWitty asks
  you to reconfirm the controls you want.

### Fixed
- Settings: each category opens directly under its header (accordion); long sections scroll instead of overlapping.

- After a successful write the GUI write status stayed "prepared"; it now reads "verified".
- Triggers (e.g. Autogain) could have been sent under the speaker/headphone permission; triggers now need their
  own authorization, which no control has.
- The LED brightness slider sent a full settings write on every pointer movement. On the real device it now sends
  at most one write per release and shows sending / verified / not confirmed.
- Real-device GUI writes now require two identical settings reads right before the write and stop all further
  writes in that session after a write that was not confirmed; unconfirmed edits are no longer kept.
- Reopening the device after a permission change could let the background service take the device in between.
- Volume faders, gain knobs and the mix knob wrote on every pointer movement or wheel notch on the real device; they
  now send one write per release (wheel: after it rests) and stay disabled until the result is known.
- The monitoring mix used a −1…+1 scale in the simulator and the setter; it now uses the device scale −100…0.

### Known limitations

See `docs/RELEASE_READINESS.md`. In short: one device and one distribution tested; with the kernel quirk a
login screen resets the speaker to its default level (optional login-screen rule provided as an example); most controls change the
simulated device only; vendor fallback unvalidated; CONNECT 6 unsupported; firmware update and factory reset
deliberately not implemented; not a replacement for LEWITT CONTROL CENTER.
