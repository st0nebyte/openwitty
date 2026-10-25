# Phase 8A.13 — DELAY_INIT single cold-boot test

**Status: EXECUTED — classification: NEGATIVE** (checkpoint written 2026-10-23 ~20:30 CEST before shutdown; results appended below)

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
  `c98d886b56162ea2f77b4c6395dbc1c8384beda3` = origin; `main` = `ef6143363cdd69c25e3baa8f278068d838a6ac72`;
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
| A | FU2 sticky warning | **Present** (11.433 s) |
| B | ALSA PCM Playback Volume | **Missing** (Switch controls only) |
| C | PipeWire `route.hw-volume` | **False** (softVolumes 0.614) |
| D | one stable stereo sink | **Exactly one** ("Analoges Stereo") |
| E | AudioStreaming timeout | **Present** (17.508 s) |

Positive requires cmdline confirmed, normal enumeration, A absent, B available, C true, D ok
(E may persist). One positive boot ≠ reliable fix. Absent warning alone ≠ working mixer.

## Resume after boot
Read this file and Phase 8A.12, verify `git status`/branch, run the passive collection above,
fill in the table and the sections listed in the Phase 8A.13 brief (authorization record,
procedure, editor availability, cmdline confirmation, timing, A–E, physical state, audible
report, classification, uncertainty, recommendation), run the USB call-site audit with an
explicit exit status, `git diff --check`, privacy review (no serial, root UUID, raw logs), then
commit and push to `origin/phase7-hardware-integration`.

---

# Results (recorded 2026-10-23 ~20:32 CEST, after the single test boot)

## Procedure and authorization
User confirmed all six safety conditions (playback/recording stopped, external audio equipment
protected, headphones isolated, no reliance on software/hardware mute, instructions retained,
one shutdown→power-on accepted) and then did one normal shutdown (20:28) and one manual power-on
(20:30). Boot 0 is a genuine shutdown→power-on boot (`last -x`: shutdown 20:28, boot 20:30).
The user later reported the boot as uneventful (see "User-reported behavior").
Whether the Limine editor was available and used as instructed is evidenced only indirectly: the
kernel command line contains exactly the baseline arguments plus the one new token.

## Parameter verification
- `/proc/cmdline`: baseline arguments unchanged, plus `usbcore.quirks=29c2:0004:g` (root UUID
  redacted here). Kernel log `Kernel command line:` agrees.
- `/sys/module/usbcore/parameters/quirks` reads `29c2:0004:g` — the sysfs parameter *does* mirror
  the boot argument (this corrects the Phase 8A.12 expectation that it would stay empty).
- Visible argument proves it was accepted by `usbcore`; it is not itself proof that the delay ran.

## Timing markers (monotonic kernel log, CONNECT 2 = `3-2`)
| Marker | Baseline boot (Oct 23 12:58) | This boot |
|---|---|---|
| `new high-speed USB device number 2` | 2.069219 s | 2.041779 s |
| `New USB device found` | 2.069273 s (+0.05 ms) | 2.771194 s (**+0.73 s**) |
| `New USB device strings` | 2.069371 s (+0.1 ms) | 2.981 s (**+0.21 s** after `found`) |
| FU2 sticky warning | 11.414 s | 11.433 s |
| FU5 sticky warning | 11.418 s | 11.437 s |
| AudioStreaming `usb_set_interface failed (-110)` | 17.491 s | 17.508 s |

The `found`→`strings` gap of ~0.21 s matches the expected `msleep(200)` in `usb_get_configuration()`
and was ~0.1 ms at baseline, so that site very likely executed. The `new`→`found` gap was
~0.73 s, **not** the ~2.0 s predicted for `msleep(2000)`; this is unexplained (possible causes not
investigated: timestamp ordering of the `new` line, different code path/kernel behaviour than the
disassembly suggested). The 2000 ms site is therefore **not confirmed**; neither the 2.0 s nor the
exact total can be asserted. Everything after enumeration (sticky warnings, timeout) landed at
the same times as baseline (+~20 ms), so the added enumeration delay had no visible effect on the
mixer-probe moment.

## Scoring
| | Observation | Result |
|---|---|---|
| A | FU2 sticky warning | **Present** — `2:0: sticky mixer values (-15360/0/128 => -1024), disabling` at 11.433 s; identical values to the earlier baseline boot |
| B | ALSA `PCM Playback Volume` | **Missing** (`amixer -c C2 controls`, names only: Switch controls, Clock Source Validity, channel maps) |
| C | PipeWire `route.hw-volume` | **False** (output route `analog-output`; softVolumes 0.614138) |
| D | Stereo playback sink | **Exactly one** (`CONNECT 2 Analoges Stereo`, 0.85) |
| E | AudioStreaming timeout | **Present** — `usb_set_interface failed (-110)` at 17.508 s |

## Physical hardware state (GET-only, `openwitty-cli read settings`, interface 3)
Post-boot snapshot 2026-10-23 20:31:40 CEST (a post-userspace snapshot, not a post-probe
measurement), compared with the pre-shutdown snapshot at 20:25:58:
| | Before | After |
|---|---|---|
| Speaker | -4.00 dB, unmuted | -4.00 dB, unmuted |
| Headphone | -32.40 dB, muted | -32.40 dB, muted |
| Mic / Instrument mute | on / on | on / on |
No hardware writes by OpenWitty. No change in these values was observed. Note the kernel's own
mixer probe happened between the snapshots; it either did not alter these values or restored them.

## User-reported behavior (subjective, not instrumented)
1. **The boot experiment itself was uneventful from the user's perspective:** no clicks,
   speaker-on sounds, unexpected volume changes, or USB instability were noticed.
2. **Separate, later event:** after the experiment the user accidentally moved the Linux/KDE
   volume slider from 85% down to 40%. This was a manual, accidental UI adjustment and is not an
   effect of the DELAY_INIT quirk. No hardware measurement was taken after that adjustment; the
   last measured physical Speaker state remains -4.00 dB, unmuted (20:31:40 CEST). Because
   `route.hw-volume=false` on this boot, the Linux slider is a software attenuation; it must not
   be assumed that the physical Speaker level followed it (or did not).

## Classification: NEGATIVE
(Qualified by Phase 8A.14: the measured added enumeration delay was ~0.94 s — +729 ms before `found`, +210 ms after — not the ~2.2 s expected; see `PHASE8A14_DELAY_INIT_TIMING_POSTMORTEM.md`. The classification itself is unchanged.)

The parameter was accepted by the kernel, CONNECT 2 enumerated normally, and the Feature Unit 2
sticky rejection persisted exactly as in the five baseline boots, with no ALSA hardware volume and
`route.hw-volume=false`. This is one boot with one parameter, shutdown→power-on method matched.

## Remaining uncertainty
- Whether the 2000 ms site executed (observed 0.73 s, not ~2.0 s); the 200 ms site very likely did.
- Whether USB standby power actually disappears at shutdown (unmeasured); a power state that
  survives shutdown could mean the device never cold-starts in either condition.
- A single negative boot cannot exclude a probabilistic effect, but 5 baseline boots all failed
  identically, and the failure moment/values were unchanged.
- The delay was applied *before* the audio driver; if the firmware needs time relative to a later
  event (e.g. first audio-class traffic) the quirk would not help.

## Recommendation for the next phase
Do not repeat this boot. Treat `DELAY_INIT` as negative evidence (not a fix). Candidates, each
needing separate authorization: investigate why the 2000 ms gap was ~0.73 s (offline source/binary
analysis only), then resume the passive observation line (Phase 8A.11 USB-core boot observer
remains offline, still NO-GO, and is still the only path to seeing the actual boot-time FU2
traffic), or evaluate a power-state hypothesis (YKUSH-style VBUS control or BIOS USB standby
power) as a distinct, non-software experiment.
