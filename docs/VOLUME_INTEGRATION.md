# Desktop volume integration for the LEWITT CONNECT 2

Current reference for users and maintainers (supersedes the design parts of the Phase 8C/8D/8E notes, which remain as
history). Verified on one machine (CachyOS, Linux 7.3.0-5, KDE Plasma, PipeWire 1.6 / WirePlumber 0.5); statements marked
*observed once* still need reproduction before they are presented as general.

## What the device does (evidence)
| Fact | Evidence |
|---|---|
| The speaker volume/mute of the vendor settings message (interface 3) is honoured in every device state seen | Phase 8C/8D, verified by read-back |
| Feature Unit 2 **GET_CUR is broken**: it returns the current level and never changes, so the kernel cannot read the level back and never learns about hardware-knob changes | every boot since 8A; kernel `broken mixer GET_CUR` / `sticky mixer values` |
| Feature Unit 2 **SET_CUR is honoured after an ordinary boot** – desktop volume reaches the speaker through the kernel | *observed once* (Phase 8E) |
| SET_CUR is **ignored** directly after a software re-enumeration of a running device (`authorized` toggle, `USBDEVFS_RESET`) | Phase 8C, measured twice |
| On Linux 7.3 the always-broken microphone control (unit 5, channels 3/4) makes every ALSA mixer load fail with ENXIO → WirePlumber "Unable to load mixer" → no hardware-volume route at all | Phase 8C; fixed by the quirk below, confirmed across a real boot (Phase 8E) |

## Architecture
* **KDE/PipeWire are authoritative for explicit desktop volume changes.** When the kernel's hardware-volume route exists,
  the slider reaches the device through ALSA → Feature Unit 2. OpenWitty does not sit in that path.
* **`openwitty-volumed` (user service, optional) keeps the desktop in step with the device.** Default *mirror* mode opens the
  CONNECT 2 read-only (vendor GET), and when the hardware knob or mute button changes the device it updates the ALSA control
  so KDE shows the real level. An ALSA control write is forwarded to the device by the kernel, so every such update re-reads
  the device first, writes only the level the device already has, rounds down to the control grid and never unmutes. A
  desktop change the device does not follow is reported, never corrected.
* **Vendor fallback (explicit opt-in).** `--vendor-fallback` additionally applies a desktop change that the device did not
  follow through the vendor settings message: speaker bytes only, fresh snapshot, one SET, read-back verification, no retries,
  increases limited to 10 dB per step, never at startup. Intended for the verified "SET ignored" state.
* **GUI.** Shows the real speaker/headphone state (vendor GET). Speaker/headphone controls write the device only with the
  `OPENWITTY_OUTPUT_WRITES=1` opt-in (scoped policy). While the GUI owns the device it can host the same mirror
  (`OPENWITTY_DESKTOP_SYNC=1`, native mode only); the daemon yields the device to the GUI and takes it back afterwards.
* **One writer.** All OpenWitty processes share one per-user control lease; the GUI asks the daemon to hand over with a yield
  request. Policy and write path are the same code everywhere (`core/VolumeSync`, `desktop/DesktopVolumeLink`).
* **Startup is read-only towards the device** in every mode; a 5 s hold keeps WirePlumber's login route restore from being
  treated as a user change.

## Validation status (real device)
| Behaviour | Status |
|---|---|
| Ordinary boot: quirk active, mixer loads, hardware-volume route present, FU2 SET reaches the device | observed once (Phase 8E, 2026-10-25) |
| Mirror mode, foreground (2026-10-25, same boot): read-only start; KDE/PipeWire volume decrease (sink 0.27 → 0.25) reached the device through the kernel (−34 → −36 dB) with no "did not follow" notice and no vendor SET (read-only transport); speaker volume knob turned up by the owner (−36 → −27 dB) mirrored to the desktop step by step; speaker mute via the Mute button (short press, speaker output selected) mirrored to the desktop; after the physical unmute the desktop was left muted with the notice, as designed; no log activity for 10 s afterwards; final state after stopping: vendor GET −27.00 dB unmuted (headphone −32.40 dB muted, unchanged), ALSA −27.00 dB unmuted, sink 0.35 unmuted – consistent | passed |
| What finally unmuted the desktop control in that test (OpenWitty's alignment cannot) – the kernel reporting the device's mute state itself, or WirePlumber | not determined: the log had no timestamps. Fixed since (millisecond timestamps outside the journal, notice clearing logged, hardware notes carry the level). A short repeat – one Mute short press on/off **without** the daemon running – would show whether the kernel already tracks the speaker mute natively |
| Vendor fallback in the "SET ignored" state; GUI ↔ daemon hand-over; suspend/resume; second ordinary boot | not yet tested on the device |

## Kernel compatibility quirk
`options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error` (example: `packaging/modprobe/openwitty-connect2.conf`).
* **What it does:** for this one USB id, failed mixer control reads/writes return 0 instead of an error, so the ALSA mixer
  loads despite the broken microphone control and PipeWire gets its hardware-volume route.
* **Limitations:** it hides genuine control errors of this device from ALSA clients (they read 0); it affects nothing else;
  kernels without the string form of `quirk_flags` ignore it; a kernel that handles the broken control itself makes it
  unnecessary. It is a device-specific compatibility workaround, not a fix of the device.
* **Policy:** OpenWitty never installs it silently. `openwitty-volumed --check` reports whether the
  mixer loads and whether the quirk is configured/active, and explains the finding; installing it is a documented manual step
  (or a packager decision with the same explanation). It takes effect at the next replug or boot; removing the file undoes it.
* **Upstream:** the right long-term fix is in the kernel (keep a per-channel control readable when only some channels stall,
  or a device quirk for 29c2:0004); worth reporting to linux-sound with the Phase 8C analysis.

## Using it
Modes are mutually exclusive: no flag = mirror, or exactly one of `--vendor-fallback`, `--dry-run`, `--check`; conflicting or
repeated mode flags, `--max-increase-db` outside the fallback, and options with `--check` are rejected before anything runs.
`--check` makes no OpenWitty vendor USB access and no intentional hardware SET; it does open the CONNECT 2's ALSA mixer, which
can make the kernel issue USB control *reads* to the device.
```
openwitty-volumed --check                                   # setup diagnosis
systemctl --user enable --now openwitty-volumed.service     # mirror mode (device read-only)
journalctl --user -u openwitty-volumed -f                   # what it does
systemctl --user disable --now openwitty-volumed.service    # undo
```
Vendor fallback: `systemctl --user edit openwitty-volumed.service` (the unit file contains the exact drop-in).

## User-facing configuration (design for the release; not implemented yet)
Ordinary users should not need environment variables. Planned:
* one settings page in the GUI, "Desktop integration": *Show hardware-knob changes in the desktop volume* (= enable the user
  service in mirror mode), *Apply desktop volume through OpenWitty if the device does not respond* (= vendor fallback, with an
  explanation), and a status line from `--check` with a guided explanation when the kernel quirk is needed;
* persisted in `$XDG_CONFIG_HOME/openwitty/openwitty.conf` (`desktop_volume = off|mirror|fallback`), read by both the GUI and
  the daemon; the systemd unit stays the single way the background part starts, enabled/disabled through
  `systemctl --user` by the GUI on the user's request;
* the GUI's output-control opt-in becomes a visible setting with the same explanation instead of `OPENWITTY_OUTPUT_WRITES`.

## Packaging
`cmake --install` installs `openwitty`, `openwitty-cli`, `openwitty-volumed`, the udev rule and the user unit
(`lib/systemd/user`, **never enabled**), plus the quirk file only as a documentation example. Distribution packages should
keep the unit disabled and the quirk opt-in, and show the `--check` hint in their post-install message.

## Prepared hardware checkpoints (optional, each needs the owner's go-ahead)
1. **Reproduce the ordinary-boot result.** Owner reboots normally; nothing OpenWitty-related runs before the test; then
   `tools/diagnostics/fu2_recovery_probe.py run --cli build-release/openwitty-cli --confirm-audio-safe` – stops after P0 if FU2
   is honoured (≈1 s, speaker −1 dB). Two consistent boots are the minimum before calling the kernel path "the" behaviour.
2. **Suspend/resume.** Owner suspends and resumes the machine once; then the same tool run. `FU2_HEALTHY_AT_BOOT` (P0 followed)
   means suspend/resume keeps the kernel path working; anything else is the first evidence that ordinary users can reach the
   "SET ignored" state, which would justify offering the vendor fallback more prominently.
Neither checkpoint involves reboots, resets, reloads or replugs by OpenWitty.
