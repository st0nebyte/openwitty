# Phase 8A.24 – Deferred `snd-usb-audio` probe: result

**Classification: NEGATIVE.** Loading `snd-usb-audio` deliberately, ≈5 min 17 s after enumeration on a settled desktop,
reproduced the known failure. One experimental boot, one guarded load, no retry. Raw run files stay outside Git
(`~/.local/state/openwitty/trace-8a24/run-20261024-130022/`, mode 0600).

## Preconditions (verified before the boot)
Branch `phase7-hardware-integration` at `affc054` (hardened tooling), clean worktree, `main` = `ef614336`; no Limine
entry, no `/var/lib/openwitty-8a24`, no run directory; normal entry and `default_entry: 2` untouched; kernel
7.2.10-1-cachyos; CONNECT 2 on port `3-2`, xHCI `0000:0d:00.4`. UEFI S5 setting unchanged (user-reported; not
software-verifiable). Installed entry: exactly one marked block, only `modprobe.blacklist=snd_usb_audio` added (no
`module_blacklist`, quirks, tracing, dyndbg); original prefix byte-untouched; backup + sha256 stored.

## Chronology (CEST, 2026-10-24)
| Time | Event |
|---|---|
| 12:51 | entry installed and inspected; user confirmed safety checkpoint 1; user performed ONE manual restart, selected the experimental entry (16 s to Limine, 3 s selection, 13 s to desktop — user-reported) |
| 12:58:08.107 | CONNECT 2 enumerated (`New USB device found`, device number 2, port 3-2) |
| 12:58:11–12 | HDA/HDMI sound drivers load; **no** `snd-usb-audio` activity |
| 13:00:22 | `prepare` + `check` after ≥ 2 min idle desktop: **gate OK** (see below) |
| 13:00:28 | GET-only snapshot before load: Speaker −23.50 dB unmuted, Headphone −32.40 dB muted |
| ≈13:03 | user confirmed safety checkpoint 2 |
| **13:03:25.63** | guarded `load` starts (`modprobe snd-usb-audio`), root-owned single-load marker created first |
| 13:03:31.25 | kernel: unit **2:0 sticky** (`-15360/0/128 => -6016`) and unit 5:0 sticky (`0/18432/256 => 6912`) |
| 13:03:31.27 | `usbcore: registered new interface driver snd-usb-audio`; `modprobe` returned 0 at 13:03:31.27 (**5.64 s** after it started) |
| 13:03:36.82 | `usb 3-2: 2:0: usb_set_interface failed (-110)` (≈5.5 s after registration; same message as in boots, where it came from the audio-stack side — the issuing task is not identified here without tracing) |
| ≈13:05 | second classification pass; entry removed and verified |
Enumeration → deliberate load start: **317.5 s** (5 min 17.5 s).

## Gate (all passed, `checkpoint.json`)
Experimental argument active; `snd_usb_audio` not loaded and not in `/sys/module`; no interface of CONNECT 2 bound;
exactly one `29c2:0004` on port `3-2` behind `0000:0d:00.4`; kernel journal verified (1577 lines incl. boot start and
the enumeration line, no snd-usb-audio registration); `modprobe -c` reported the blacklist; checkpoint of this boot id.
Autoload suppression **succeeded**: the module stayed absent for 5 min while the device was enumerated; exactly **one**
registration line exists in the boot journal and the marker file shows exactly one guarded load.

## Observed after the load
| | Result |
|---|---|
| FU2 (unit 2) sticky | **yes** (`-6016`, i.e. −23.5 dB — the real speaker level, as before) |
| Unit 5 sticky | yes (separate message, not used for the verdict) |
| `usb_set_interface failed (-110)` | yes, 13:03:36.82 |
| ALSA `PCM Playback Volume` | **absent** (controls: Playback/Capture Switch, channel maps, clock validity) |
| PipeWire `route.hw-volume`, analog-output | **false** (also analog-input-mic false) |
| Software sink volume | 35 % (unchanged by us); CONNECT 2 stereo sink present |
| GET-only snapshot after load | Speaker −23.50 dB unmuted; Headphone −32.40 dB muted; mic/instrument muted (**identical to before**) |
| Journal after load | `OK` (11 lines in the final pass) |
`modprobe`'s 5.64 s duration is a wall-clock measurement of the module load; no tracing was enabled, so which request
stalled inside it is NOT established (it is consistent with a ≈5 s control-request timeout in the probe, as in earlier
traces) and no request latency is inferred.

Note on the two classification passes: the first pass (`classification_first_early.json`, run seconds after the load)
saw no `-110` yet and no PipeWire device, and therefore reported INCONCLUSIVE (`journal_after_load` OK but the events
had not happened). Re-running the passive `classify` after the audio stack settled gave NEGATIVE. This is a
timing artefact of when the read-only collection ran, not a second experiment; the module was loaded once.

## User observations
Nothing audible around the load (no clicks, crackles or level changes heard). The user did not check the interface
display for changes. Absence of audible artefacts does not prove that no transient occurred. The hardware speaker
level and the muted headphone state were read back unchanged (−23.50 dB / −32.40 dB muted).

## Interpretation
- Supported: a quiet, late probe (5 min after enumeration, desktop settled, no other boot-time activity, `modprobe`
  context instead of `udev-worker`) fails exactly like a boot-time probe. **Delaying the probe alone does not avoid
  the FU2 sticky verdict.** Combined with 8A.23/8A.23B (power state before boot irrelevant), the boot-time *host
  context/timing of the probe* is no longer a sufficient explanation.
- Still consistent: the device/enumeration state produced at boot (device enumerated once at host start and left
  idle) differs from the state after a live replug on a running host (8A.21), where the probe followed
  enumeration within ≈0.15 s. Candidate factors not separated here: elapsed idle time of an enumerated-but-unconfigured
  device; the initial enumeration sequence/port reset done by the boot-time xHCI vs by the running hub driver;
  the device needing to be configured/probed promptly after enumeration; or firmware state set during first
  configuration.
- Not shown: any cause inside the device; no firmware-defect claim.

## Rollback verification
`entry remove` → `entry verify`: `/boot/limine.conf` byte-identical to the pre-install backup (sha256
`8017afbb…` matches), no `OPENWITTY` marker, `default_entry: 2`, `timeout: 5` unchanged. No additional reboot was
performed; the running system still booted with the experimental argument but no further module loads occur; the next
boot uses the normal entry. `/var/lib/openwitty-8a24/` (backup, marker) is kept until the user wants it removed.

## Uncertainty
One boot, one load; no instrumentation of request latencies; the S5 setting and the user's reported timings are not
independently verified; unit-2 verdict was identical to earlier boots and so cannot discriminate between idle-time and
enumeration-path explanations.

## Recommended next discriminator
Test whether the *enumeration path/age* matters rather than the probe time: boot the same deferred entry again, then do
**one user-performed physical replug** of CONNECT 2 (the 8A.21 action, with all audio-safety gates) while `snd-usb-audio`
is still unloaded, and only then run the guarded load right after the re-enumeration. Healthy → the boot-time
enumeration state is the cause (not the probe timing, not power); failing → the 8A.21 success depended on the driver
already being bound when the device enumerated. It needs separate design and approval and is not started. Independently,
the read-only OpenWitty health diagnostic (8A.22 §8) already detects the failing state.
