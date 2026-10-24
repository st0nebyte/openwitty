# Phase 8A.26B – Ordinary boot failure, then one live reconnect (kernel 7.3.0-5): result

Classification: **INCONCLUSIVE** by the pre-registered rules, same pattern as the first 8A.26 run (kernel/USB side healthy after the
reconnect, PipeWire route absent). One user-operated reconnect with `snd-usb-audio` loaded. No reboot by the agent, module operation,
Limine/UEFI change, USB reset, hardware SET or playback. Raw data stays outside Git (`~/.local/state/openwitty/trace-8a26/`, 0700).

## Why this run
The first 8A.26 run (`PHASE8A26_LOADED_DRIVER_RECONNECT_RESULT.md`) started from a failure that had been created by the deliberate late load
of 8A.25. This run starts from an **ordinary autoload boot** of the default entry (no experimental argument) after a user-performed
shutdown/power-on, to test whether a live reconnect heals a normal boot-time failure.

## Safety
Fresh user confirmations before the reboot/shutdown and at checkpoint 2 before arming the observer (playback/recording stopped; speakers,
amplifiers and headphones/listeners protected; KDE volume and CONNECT 2 mute not relied upon; understanding of possible level changes).

## Sequence (2026-10-24; kernel `7.3.0-5-cachyos`; boot id prefix `d60887d7`)
| Time | Event |
|---|---|
| 21:04:31 | new boot, ordinary default entry, no `modprobe.blacklist` argument |
| 21:04:34 | CONNECT 2 enumerated, port `3-2`, device number 2 |
| 21:04:43 | `snd-usb-audio` autoloaded: unit-2 `broken mixer GET_CUR (… => -5120)` (= −20.0 dB, the speaker level), unit-5 `broken mixer GET_CUR`, driver registered (≈9 s after enumeration) |
| 21:04:44–50 | WirePlumber `Unable to load mixer` ×3; 21:04:50 `usb_set_interface failed (-110)` (unit 2) |
| 21:05:48 | pre-check gate OK (failing state reproduced; PipeWire `DEVICE_NO_ROUTES`; GET-only Speaker −20.00 dB unmuted, Headphone −32.40 dB muted) |
| 21:06:19 | user unplugs (journal: `USB disconnect, device number 2`) |
| 21:06:23 | user replugs into the same port: **device number 3**; only the unit-5 `broken mixer GET_CUR` follows; WirePlumber `Unable to load mixer` ×3 |
| 21:06:28 | post gate OK: one new disconnect and enumeration (sequence [2, 3]); journal = observer = sysfs device number; registration count unchanged; `1.0`–`1.2` bound |
| 21:06:49 / 21:07:48 | passive classify at ≈26 s and ≈85 s: INCONCLUSIVE both times |

## Timing (sysfs sampling bounds, not request latency)
Single 0.1 s sampling loop: device node reappeared in (21:06:23.20, 21:06:23.30]; all three audio interfaces bound by 21:06:23.50. Derived
**reconnect → audio-interfaces-bound: 0.10–0.30 s** (identical to the first 8A.26 run). Boot-time comparison: enumeration → driver
registration ≈ 9 s (21:04:34 → 21:04:43), from journal timestamps. Not USB request latency.

## Evidence
| Item | Before the reconnect (ordinary boot) | After the reconnect |
|---|---|---|
| FU2 (unit 2) kernel message | `broken mixer GET_CUR (… => -5120)` | **none** |
| unit 5 | `broken mixer GET_CUR (… => 6912)` | same message (independent; not an FU2 failure) |
| `usb_set_interface failed (-110)` | present (21:04:50) | **none** |
| ALSA `PCM Playback Volume` | present | present (functionality untested) |
| audio interfaces `1.0`–`1.2` | bound | bound (0.10–0.30 s) |
| PipeWire CONNECT 2 | 0 Route / 0 EnumRoute | new device (id 81), new sink/source, **still 0 Route / 0 EnumRoute** |
| WirePlumber | `Unable to load mixer` ×3 | `Unable to load mixer` ×3 at the re-enumeration |
| GET-only Speaker / Headphone | −20.00 dB unmuted / −32.40 dB muted | unchanged (−20.00 / −32.40 muted) |

## User observations
* Audible: nothing (no pop, click or level change).
* Device: after the replug the LEDs flash for about 4–5 s, then the device shows its volume (user report, not measured). So CONNECT 2
  itself initialises for a few seconds after enumeration, while the already-bound driver reached the bound state within 0.3 s and the
  kernel mixer probe found no FU2 failure.

## Reading
* Together with the first 8A.26 run this is the second observation that a reconnect with the audio driver already loaded gives **no FU2 failure
  and no −110**, starting from two different failing states (late-load failure and an ordinary boot failure). The failure on 7.3.0-5 is therefore
  not "sticky": reconnecting once clears the kernel-level symptom. N = 2; one machine, one kernel; no tracing, so no request-level claim.
* The speaker level (−20.00 dB) is the same across the shutdown and the reconnect, consistent with the earlier observation that the device keeps
  its speaker level and the sticky FU2 readback equals that level.
* PipeWire is the remaining defect: WirePlumber cannot open the ALSA mixer when the card appears ("no such device or address", every time, also
  after the healthy reconnects) and CONNECT 2 never gets routes, hence no `route.hw-volume`. This is an observation of the audio stack, independent
  of the FU2 verdict. Whether the mixer-open failure is a race with the kernel's mixer creation is **untested**.
* Not shown: why the boot-time probe fails; what the device is doing during the 4–5 s LED phase; causation.

## Rollback
Nothing to roll back: CONNECT 2 is enumerated (device 3) and bound; `/boot/limine.conf` and UEFI untouched.

## Next steps (each needs approval)
1. Read-only: capture WirePlumber's ALSA mixer-open attempt (log level / timing) at a hotplug to see whether the mixer controls exist at that
   moment.
2. After the next reconnect, a **user-session** restart of WirePlumber (no USB, no module action) to test whether routes then appear — this
   separates "kernel healthy" from "PipeWire never reloads the mixer". Needs explicit approval and the usual audio-safety confirmation because routes
   may change the applied hardware volume.
3. OpenWitty diagnostic wording: report "kernel mixer healthy, PipeWire mixer not loaded" as its own state; do not suggest reconnecting while
   speakers/headphones are active.
