# Changelog

All notable changes to OpenWitty. Unofficial project, not affiliated with LEWITT.

## 0.1.0-alpha.1 — unreleased (prepared 2026-10-25, not tagged)

Version metadata set in the tree (`--version`: `0.1.0-alpha.1`; Debian `0.1.0~alpha1`; Arch `0.1.0alpha1`;
AppStream `0.1.0~alpha1`, type development). No Git tag and no release yet.

Tested with **one LEWITT CONNECT 2 on one system**: CachyOS, Linux 7.3.0, KDE Plasma 6 (Wayland),
PipeWire 1.6 / WirePlumber 0.5. Everything else is untested.

### Added
- Desktop names (2026-11-01, owner wish): the output is "CONNECT 2 Output" (was "Analog Output"; device name and
  KDE's port name, via the profile's own playback path `openwitty-connect2-output.conf`), the 4-channel
  input "CONNECT 2 All channels (for DAWs)". The separate Microphone input is preferred as the default input, so
  an app that records "the default" gets only the microphone, never mic + instrument + computer audio mixed.
- **Product mode:** no more permission switches. Every setting verified on the real CONNECT 2 works from the window
  right away; Settings ▸ Device control is gone (its keys in `openwitty.conf` are ignored). Safety stays built in:
  only known settings bytes are written, every write is read back, large increases go in verified steps, and 48 V,
  the louder output modes and the routing changes ask once. A stopped write shows in the status bar with a
  **Reconnect** button.
- **Debug mode** (`openwitty --debug`, also implied by `--hw-check`): Settings ▸ Debug (hardware check, last device
  change) and the experimental options (vendor fallback, 44.1/96 kHz); the developer start options appear in
  `--help` only together with `--debug`. Removed the disabled placeholders (firmware update, factory reset buttons).
- One program owns the CONNECT 2: while the background service runs, the window and `openwitty-cli` use the device
  through the service's private socket (no more hand-over; CLI reads work while the window is open). The service
  checks every write against the window's policy. Without the service, clients open the device directly.
- Settings ▸ Desktop integration: "Name the CONNECT 2 channels in audio apps" (on by default): DAWs show
  1 Microphone, 2 Instrument, 3/4 Loopback L/R and Output L/R instead of capture_FL … (JACK pretty names in
  PipeWire's metadata, display only; the background service sets them again after a replug).
- Settings ▸ Presets: save the current settings under a name and load them again (OpenWitty's own `.owpreset` JSON
  in `~/.local/share/openwitty/presets`; output levels only when chosen). Loading lists every change first, marks
  louder ones, skips settings OpenWitty cannot change, and sends them one by one (mutes first, unmutes last, level
  increases in verified steps).
- Settings: the CONNECT 2 firmware version is shown as its USB revision (e.g. 1.21), read from sysfs.
- Denoise: switching on reuses the stored noise measurement (dialog only when none exists); right-click measures again; the meter line shows only while Denoise is on.
- Custom button: "toggle mute (+ mic) / speakers + follow volume" extended modes, remembered per device (anonymous key); "Follow active output" is then on and greyed out with a note.
- Follow active output also moves the touch-wheel focus to that output (can be switched off).
- Settings ▸ Audio: "Follow active output" - the system volume keys control the output you can hear; choices for "both on" and "both muted" (Phase 12C, also in the background service).
- Settings ▸ Audio: "Map native volume controls to: None / Speakers / Headphones" (byte 59; Headphones asks first).
- Settings ▸ Desktop integration: "Allow 44.1 and 96 kHz for apps" (PipeWire allowed rates; shows the live value).
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
- Autosetup: a measurement no longer counts as finished on the previous measurement's progress (the voice step ended
  at once and the noise step stuck at 100 %); verified on the hardware.
- Autosetup: advances after a successful measurement on the real device; switches 48 V before the gain measurement; new ambient-noise step when Denoise is chosen.
- Greyed-out buttons (Denoise/Comp at 96 kHz) show why on hover; the noise-floor tooltip is no longer covered by the next strip.
- Separate inputs: no false "set up for another CONNECT 2" warning right after restarting desktop audio.
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
- Sample rates other than 48 kHz are experimental: repeated rate changes can hang the CONNECT 2's audio until it is replugged.

See `docs/RELEASE_READINESS.md`. In short: one device and one distribution tested; with the kernel quirk a
login screen resets the speaker to its default level (optional login-screen rule provided as an example); most controls change the
simulated device only; vendor fallback unvalidated; CONNECT 6 unsupported; firmware update and factory reset
deliberately not implemented; not a replacement for LEWITT CONTROL CENTER.
