# Phase 8E – Does a vendor settings SET make the CONNECT 2 honour Feature Unit 2 again?

Status: **prepared and tested offline; not executed.** Needs one ordinary reboot by the owner and a separate fresh
audio-safety confirmation before the active test.

## Why
After an ordinary boot the device ignores UAC SET_CUR on Feature Unit 2 – the ALSA `PCM Playback Volume` that
PipeWire/KDE drive – so desktop volume has no effect (Phase 8C). In the session of Phase 8D the same device instance
first ignored FU2 (directly after a port reset, 21:16) and later honoured it (21:5x); in between several vendor settings
writes were made. If a vendor settings SET is the trigger, one guarded device-initialization write would leave normal
KDE/PipeWire volume in charge and make the permanent bridge unnecessary.

## Competing explanations and how the design separates them
| Explanation | Observable |
|---|---|
| FU2 is simply healthy in this boot | P0 follows → stop (nothing manufactured) |
| FU2 recovers with time (or the ALSA probe SET itself heals it) | P1 follows after the time-control wait with no vendor write |
| **any** vendor settings SET heals it | P1 fails, intervention A (audio-neutral LED SET), P2 seconds later follows |
| only a speaker-field vendor write heals it | P2 fails, intervention B (speaker 1 dB down), P3 follows |
| recovery is temporary | P4 (after `--persist`) fails |
| something else / not reproducible | NOT_RECOVERED, or a different result on the next boot |

A no-op ("same level") vendor write is **not** used: `DeviceSession::commit()` and every caller drop unchanged
settings, so nothing would be transmitted. The guards are not weakened. Intervention A is a real settings SET that
cannot change audio: the settings message always carries the whole 128-byte struct, so changing LED brightness by one
step retransmits every speaker/headphone byte unchanged. Intervention B is a small **decrease**.

Measurement: there is no read-only way to see whether FU2 is honoured (the kernel serves ALSA reads from its cache).
A probe therefore sets the ALSA control **1 dB below the lower of the ALSA and device levels**, waits 1.5 s and reads the
device with the vendor GET; "followed" = the device reached the target and had to move to get there.

## Tool
`tools/diagnostics/fu2_recovery_probe.py` (offline tests: `test_fu2_recovery_probe.py`, CTest `fu2_recovery_probe_offline`):
* `plan --cli build-release/openwitty-cli`: read-only (vendor GET + ALSA read): current levels, first probe target.
* `run --cli … --confirm-audio-safe [--wait 600] [--persist 120]`: the fixed sequence P0 → wait → P1 → A → P2 → (B → P3) →
  (wait → P4), stopping as soon as the question is answered; LED brightness restored at the end (also after an abort);
  JSON-lines log with wall-clock timestamps in `~/.local/state/openwitty/fu2-recovery/` (0600, outside Git).
* Never raises a level: every ALSA probe is a 1 dB decrease on the 0.5 dB grid (rounded down), B is a 1 dB decrease;
  worst case −5 dB on the speaker. Only `LEWITT_ALLOW_HARDWARE_WRITE=1` (+ `LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1` for B)
  per write; no retries; aborts on any failed read or unverified write.
* Offline-tested against a simulated device for every explanation above, plus authorization exactness, never-louder
  and LED restore after an abort.

## Procedure
1. **Safety checkpoint 1 (reboot):** owner confirms the audio safety; owner performs one ordinary reboot (default entry).
   After login: do **not** start the OpenWitty GUI, `openwitty-volumed` or any CLI write; no playback; leave the volume alone.
2. Read-only, by the agent: boot id, kernel FU2 messages of this boot, PipeWire route state, `fu2_recovery_probe.py plan`.
3. **Safety checkpoint 2 (active test):** fresh owner confirmation (playback/recording stopped, speakers/amplifiers
   physically protected, headphones/listeners protected, no reliance on software mute). The owner leaves knob and volume
   alone for ~15 minutes.
4. `fu2_recovery_probe.py run --confirm-audio-safe --wait 600 --persist 120`, foreground, stop at the first unexpected value.
5. Record the verdict, timestamps and uptime; nothing is automated from one result.

## What one result can and cannot show
* RECOVERED_AFTER_VENDOR_SETTINGS_SET with P1 failing ten minutes after boot and P2 following seconds after A is strong
  evidence for the trigger in **this** boot. It does not prove it for every boot, firmware or machine: before any automatic
  device-initialization behaviour is shipped, the result must be reproduced on at least one more ordinary boot, and the
  first probe after A must keep working across a longer period (P4) and a later desktop change.
* A time-based recovery slower than `--wait` remains a confound; repeat with a longer wait if the result is ambiguous.
* If FU2 is healthy at boot (P0 follows) the run stops; no failure is manufactured with resets, reloads or replugs.

## Product consequence
* Supported → evaluate a minimal guarded device-initialization step (one audio-neutral vendor settings SET when the
  CONNECT 2 appears) that leaves KDE/PipeWire in charge, keeping the native bridge as fallback until reproduced.
* Not supported → keep the native bridge (`openwitty-volumed`) and validate its vendor write path in the FU2-broken state.
