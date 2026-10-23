# Phase 8A.13 — DELAY_INIT single cold-boot test

**Status: PREPARED — NOT YET EXECUTED** (checkpoint written 2026-10-23 ~20:30 CEST, before shutdown)

This file is the recovery checkpoint. If the chat session is gone after the test boot, a new
session should read this file, `PHASE8A12_USB_CORE_DELAY_INIT_QUIRK_ASSESSMENT.md`, and then
continue at "Resume after boot". Do not start a second boot because a session was interrupted.

## Authorized scope (user, 2026-10-23)
- ONE genuine OS shutdown, ONE manual power-on (not `reboot`, suspend, hibernate, USB reset).
- ONE non-persistent Limine menu-time edit appending exactly: `usbcore.quirks=29c2:0004:g`
- Read-only pre/post diagnostics; at most one GET-only OpenWitty snapshot before shutdown
  (done, below) and one after boot.
- No reconnect, no second experimental boot, no SET/volume/mute writes, no PipeWire/WirePlumber
  or ALSA restore/policy changes, no Limine/initramfs/sysfs changes, no tracing.
- Not self-renewing: no automatic repeat.

## Mechanism recap (see Phase 8A.12)
`g` = `USB_QUIRK_DELAY_INIT`. Expected extra enumeration delay ~2.2 s total: `msleep(2000)` in
`hub_port_connect()` after `hub_port_init()` and before `usb_new_device()`, and `msleep(200)` per
configuration in `usb_get_configuration()` (device has 1 configuration), which runs between
`New USB device found` and `New USB device strings`. No existing built-in quirk for `29c2:0004`;
no existing `usbcore.quirks` override. `/proc/cmdline` visibility ≠ proof the delay ran.

## Preflight baseline (2026-10-23 20:25 CEST, read-only)
- Repo `st0nebyte/openwitty`, branch `phase7-hardware-integration`, HEAD
  `1e92e3720eca75a78c0a6e87d5367dc9203e727b` = origin; `main` = `ef6143363cdd69c25e3baa8f278068d838a6ac72`;
  worktree clean at start.
- Other agent processes seen: a separate `codex` CLI session (cwd = home dir, not the repo) and the
  background Codex daemon; no file in the repo was modified in the previous 20 min. No
  `openwitty`/tracing process running.
- Kernel `7.2.10-1-cachyos`, up ~7.5 h. `/proc/cmdline` (root UUID redacted):
  `quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=<redacted>`.
  `/sys/module/usbcore/parameters/quirks` empty. No `usbcore.quirks` present.
- `usbcore` is built in (`modinfo`: builtin).
- Bootloader: Limine 12.9.3. `/boot` is root-only, so `limine.conf` could NOT be read; the entry
  structure, `editor_enabled`, and whether a config hash is enrolled are unverified.
  `/etc/default/limine` has `KERNEL_CMDLINE[default]` identical to the command line above.
  SecureBoot EFI var = 0 (inactive); `ENABLE_ENROLL_LIMINE_CONFIG` unset (default no).
- USB: CONNECT 2 is `3-2` (`29c2:0004`, 1 configuration); interfaces 0–2 bound to
  `snd-usb-audio`, 3 and 4 unbound. ALSA card `C2`.
- **Important baseline caveat:** the current mixer is working only because the device was
  reconnected ~74 minutes after this session's boot (a pre-existing, separate event). This boot
  (boot 0, 12:58) itself showed the usual failure: FU2 sticky warning at 11.414 s (device
  identified at 2.069 s), FU5 sticky warning at 11.418 s, AudioStreaming `usb_set_interface failed
  (-110)` at 17.491 s. Prior 4 boots (Oct 20–23) show the same pattern; all followed genuine
  shutdown → later power-on.
- ALSA (control names only, `amixer -c C2 controls`): `PCM Playback Volume` and `PCM Playback
  Switch` present *now* (post-reconnect). Saved `/var/lib/alsa/asound.state` (written 07:44) holds
  C2 `PCM Playback Volume` = 112 (-4.00 dB) and Switch = true. `alsa-restore.service` is
  `static` (triggered on sound-card appearance) and runs `alsactl restore` at boot; this is the
  system's normal behavior and was also active during the five baseline boots. At shutdown
  `alsactl store` runs.
- PipeWire 1.6.9: one CONNECT 2 sink "Analoges Stereo" (`vol 0.85`, cubic; channelVolumes
  0.614138), output route `analog-output` `route.hw-volume=true`, `softVolumes=[1,1]`,
  `mute=false`. Source "Analog Surround 4.0" muted, `route.hw-volume=false`. WirePlumber saved
  state agrees (output channelVolumes 0.614138, mute false; profile `analog-stereo + analog-surround-40`).
- GET-only OpenWitty snapshot (interface 3, `read settings`, 2026-10-23 20:25:58 CEST; a first
  attempt with default interface selection failed with `LIBUSB_ERROR_IO`, no side effects):
  - Speaker: **-4.00 dB, not muted**
  - Headphone: **-32.40 dB, muted**
  - Microphone: gain 27.00 dB, mute on; Instrument: gain 14.00 dB, mute on
  - Phantom power flag: **on** (not changed; mention to user because it is unrelated to the test)
  - No hardware writes were performed.

## Audio safety conditions (must be confirmed by user immediately before shutdown)
Stopped playback/recording; external speakers/amps disconnected, off or independently safe;
headphones isolated; no reliance on KDE mute or CONNECT 2 hardware mute; instructions retained
offline; user accepts one shutdown→power-on and that the test may change nothing. The Speaker
output is currently **unmuted at -4 dB**; a sticky-failure probe has previously been associated
with an attempt at 0 dB and an unmute.

## Exact intended edit (Limine menu)
1. Pick the usual CachyOS entry, press `e` if the editor is available.
2. Find the `cmdline:` line; keep every existing argument unchanged; append one space +
   `usbcore.quirks=29c2:0004:g`; add nothing else; boot with the editor's documented boot action.
3. Abort (boot the normal entry unmodified, no other method) if: editor disabled/unavailable,
   entry or `cmdline` unclear/truncated, unclear whether the edit is temporary, or any
   unexpected behavior.
4. Do not repeat; recovery boots use the unmodified entry.

## Evidence to collect after boot (passive only)
1. `/proc/cmdline` contains `usbcore.quirks=29c2:0004:g`.
2. `journalctl -k -b 0 -o short-monotonic`, CONNECT 2 context only: `new … USB device number`,
   `New USB device found`, `New USB device strings`, FU2 and FU5 sticky warnings, `usb_set_interface
   failed (-110)`, other errors. Optional markers: ~2.0 s before `found`, ~0.2 s between `found`
   and `strings`. Distinguish Feature Unit 2 from AudioStreaming interface 2.
3. `amixer -c C2 controls` (names only): `PCM Playback Volume` present/missing. No `contents`/`cget`.
4. PipeWire: exactly one CONNECT 2 stereo sink, route, `route.hw-volume`, channelVolumes,
   `softVolumes`, mute (read-only `wpctl`/`pw-dump`). No restarts.
5. At most one post-boot `openwitty-cli read settings --method interface --interface 3` (no
   competing OpenWitty process); record Speaker/Headphone volume+mute and timestamp; label it a
   post-userspace snapshot, not a post-probe measurement.
6. User-reported audible behavior (clicks, level change, instability).

## Scoring table (fill after boot)
| | Observation | Result |
|---|---|---|
| A | FU2 sticky warning | TBD |
| B | ALSA PCM Playback Volume | TBD |
| C | PipeWire `route.hw-volume` | TBD |
| D | one stable stereo sink | TBD |
| E | AudioStreaming timeout | TBD |

Positive requires cmdline confirmed, normal enumeration, A absent, B available, C true, D ok
(E may persist). One positive boot ≠ reliable fix. Absent warning alone ≠ working mixer.

## Resume after boot
Read this file and Phase 8A.12, verify `git status`/branch, run the passive collection above,
fill in the table and the sections listed in the Phase 8A.13 brief (authorization record,
procedure, editor availability, cmdline confirmation, timing, A–E, physical state, audible
report, classification, uncertainty, recommendation), run the USB call-site audit with an
explicit exit status, `git diff --check`, privacy review (no serial, root UUID, raw logs), then
commit and push to `origin/phase7-hardware-integration`.
