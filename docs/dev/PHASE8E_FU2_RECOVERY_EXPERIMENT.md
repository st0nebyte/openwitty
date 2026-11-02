# Phase 8E – FU2 SET_CUR on an ordinary boot (and the vendor-SET recovery hypothesis)

Status: **executed 2026-10-25 on one ordinary boot. Result: FU2 honoured SET_CUR at boot (`FU2_HEALTHY_AT_BOOT`)**, so the
vendor-SET hypothesis could not be tested and is not needed for the ordinary boot path. See "Result" at the end; the design
below is kept because it is the procedure for repeating the measurement (another boot, suspend/resume).

## Why (as of the preparation)
Phase 8C assumed that after an ordinary boot the device ignores UAC SET_CUR on Feature Unit 2 – an inference from the kernel's
`broken mixer GET_CUR` classification that this experiment showed to be wrong for the boot measured. What had actually been
measured: the same device instance first ignored FU2 directly after a software port reset (21:16) and later honoured it (21:5x),
with several vendor settings writes in between. If a vendor settings SET had been the trigger, one guarded device-initialization
write would have left normal KDE/PipeWire volume in charge.

## Competing explanations and how the design separates them
| Explanation | Observable |
|---|---|
| FU2 is simply healthy in this boot | P0 follows → stop (nothing manufactured) |
| FU2 recovers with time (or the ALSA probe SET itself heals it) | P1 follows after the time-control wait with no vendor write |
| **any** vendor settings SET heals it | P1 fails, intervention A (settings SET intended to change only the LED), P2 seconds later follows |
| only a speaker-field vendor write heals it | P2 fails, intervention B (speaker 1 dB down), P3 follows |
| recovery is temporary | P4 (after `--persist`) fails |
| something else / not reproducible | NOT_RECOVERED, or a different result on the next boot |

A no-op ("same level") vendor write is **not** used: `DeviceSession::commit()` and every caller drop unchanged
settings, so nothing would be transmitted. The guards are not weakened. Intervention A is a real settings SET whose
*intended* edit is LED brightness only (one step). The settings message always carries the whole 128-byte struct, so
every speaker/headphone byte is retransmitted with its current value. That the device does nothing audible when it
re-applies those unchanged bytes is **not established**; what can be checked – and is checked – is the full settings
read-back afterwards (only byte 37 may change). Intervention B is a small **decrease**.

Measurement: there is no read-only way to see whether FU2 is honoured (the kernel serves ALSA reads from its cache).
A probe therefore sets the ALSA control **1 dB below the device level**, waits 1.5 s and reads the complete device
settings with the vendor GET: *followed* = the speaker reached the target; *not followed* = nothing changed; anything else
aborts. The target is relative to the device, not to ALSA: after unfollowed probes ALSA has drifted down, and a later
recovery must not make the device jump several steps. The kernel sends no USB request when an ALSA write repeats the
cached value, so when ALSA already sits 1 dB down the target is half a step lower (1.5 dB) – every probe really transmits.

## Safety behaviour (revised after the preflight review)
* **Start conditions:** ALSA and device agree within 0.5 dB, speaker and ALSA control unmuted, level ≥ −56 dB; otherwise the
  tool refuses before any write.
* **Bounds (offline-verified for every branch):** from a start level L the device can only go down, by at most 1.5 dB per
  change, never below L − 3 dB; the lowest ALSA target is L − 3 dB. Nothing ever gets louder than at start.
* **Identity:** before every ALSA or vendor write the single CONNECT 2 (USB bus/device number) and its ALSA card (re-found
  by USB id) must be unchanged; otherwise abort before writing.
* **Read-back after every write and between steps:** the full 128-byte settings may differ only in the bytes the step may
  change, and only to the expected value (probe: speaker level to the target or not at all; A: byte 37 to the requested
  value; B: speaker level to the requested value). Louder levels, mute changes, partial moves, side effects on other bytes,
  and changes made outside the experiment (knob, other tools) all abort.
* **Inconclusive probes** (no measurable decrease possible, e.g. at the floor) abort before writing; they are never counted
  as "FU2 not following".
* **After an abort nothing more is written.** No LED restore: the log records `pending_led_restore` (original, last
  confirmed, last sent value) to be handled later under an explicitly verified state. Only a normal completion restores
  the LED, through the same guarded vendor-write path and read-back check.

## Tool
`tools/diagnostics/fu2_recovery_probe.py` (offline tests: `test_fu2_recovery_probe.py`, CTest `fu2_recovery_probe_offline`):
* `plan --cli build-release/openwitty-cli`: read-only (vendor GET + ALSA read): identity, levels, start problems, bounds.
* `run --cli … --confirm-audio-safe [--wait 600] [--persist 120]`: the fixed sequence P0 → wait → P1 → A → P2 → (B → P3) →
  (wait → P4), stopping as soon as the question is answered; LED brightness restored only on normal completion;
  JSON-lines log with wall-clock timestamps in `~/.local/state/openwitty/fu2-recovery/` (0600, outside Git).
* Safety behaviour as above; only `LEWITT_ALLOW_HARDWARE_WRITE=1` (+ `LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1` for B) per
  write; no retries.
* Offline-tested (23 cases) against a simulated device: every explanation above; bounds for every branch incl. temporary
  and late recovery; refusal on disagreement/mute/floor; abort without any further write (and with the pending LED
  report) after a failed read-back, a knob change during the wait, an unexpected increase, a vendor write with side
  effects, a card or USB identity change, and an inconclusive probe at the floor; exact authorization per field.

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
* Supported → evaluate a minimal guarded device-initialization step (one vendor settings SET with an LED-only intended edit, read-back checked, when the
  CONNECT 2 appears) that leaves KDE/PipeWire in charge, keeping the native bridge as fallback until reproduced.
* Not supported → keep the native bridge (`openwitty-volumed`) and validate its vendor write path in the FU2-broken state.

## Result (2026-10-25, ordinary boot `064c941b…`, kernel 7.3.0-5-cachyos, quirk active from boot)
Owner confirmed the audio safety before the reboot step and the active test. Boot 07:32:00; kernel at 07:32:10:
`2:0: broken mixer GET_CUR (-15360/0/128 => -8448)` (−33.0 dB), separate unit-5 warning, `usb_set_interface failed (-110)` at
07:32:17. WirePlumber logged **no** "Unable to load mixer"; the CONNECT 2 had `analog-output` with `route.hw-volume=true`.
No OpenWitty process or usbfs access before the run.

| Time | Step | ALSA | Device (vendor GET, full 128 bytes) |
|---|---|---|---|
| 07:33:56 | start (checks passed, worst case −36 dB) | −33.0 dB unmuted | −33.0 dB unmuted, LED 100 |
| 07:33:57 | P0: ALSA → −34.0 dB | −34.0 dB | **−34.0 dB – followed**, no other byte changed |

**Verdict: `FU2_HEALTHY_AT_BOOT`.** The run stopped after P0 as designed: no vendor write, LED untouched, no failure
manufactured. Afterwards PipeWire showed the new level (sink 0.27), kernel log quiet apart from the CLI's usbfs notices.

### What this shows (one ordinary boot)
* "broken mixer GET_CUR" means exactly that: the device's FU2 **read-back** is constant (it reports the current level, so
  the kernel's sanity test sees no change). It did **not** mean that FU2 ignores SET_CUR: on this ordinary boot the
  kernel/ALSA volume path reached the device directly.
* The state in which the device ignored FU2 SETs was observed in this project only right after software re-enumeration of
  an already-running device (`authorized` toggle, `USBDEVFS_RESET`, Phase 8C). The Phase 8C table's "normal boot: SET
  ignored" had been an inference and is corrected.
* With the per-device quirk (`ignore_ctl_error`) the complete standard path worked after a real boot: mixer loads,
  PipeWire hardware-volume route, KDE/ALSA volume reaches the speaker.
* Because GET_CUR is broken, the kernel never learns about knob changes: the desktop shows a stale level after the knob is
  turned (the Phase 8D live test showed `openwitty-volumed` mirroring the knob to KDE).
* The vendor-SET hypothesis was **not** tested (FU2 was already healthy) and is not needed for the ordinary boot path.

### Consequences
1. The kernel quirk is the primary fix for desktop volume after boot (to be reproduced on at least one more ordinary boot,
   and ideally on another machine, before it is presented as such).
2. `openwitty-volumed` remains useful for knob → desktop mirroring (read-only towards the device, ALSA alignment under the
   Phase 8D safety rules) and as the vendor-write fallback for a device that ignores FU2.
3. Open question for ordinary users: can suspend/resume (USB reset-resume) produce the FU2-ignored state? Test once, with
   the owner performing the suspend, reading the result with one ALSA probe (decrease) + vendor GET.
