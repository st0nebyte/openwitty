# Phase 8A.26 – Loaded-driver reconnect on kernel 7.3.0-5: result

Classification: **INCONCLUSIVE** by the pre-registered rules (kernel/USB evidence healthy, PipeWire evidence not). One physical
reconnect, performed by the user, in the same boot as Phase 8A.25. No reboot, module operation, Limine/UEFI change, USB reset, hardware
SET or playback occurred. Raw run data stays outside Git (`~/.local/state/openwitty/trace-8a26/`, mode 0700).

## Question
On kernel 7.3.0-5-cachyos, does ONE physical reconnect of CONNECT 2 while `snd-usb-audio` is **already loaded and bound** reproduce the
healthy reconnect of Phase 8A.21 (kernel 7.2.10), starting from the failing state left by Phase 8A.25?

## Safety
Fresh user confirmations at checkpoint 1 (before the pre-check) and checkpoint 2 (before arming the observer): playback and recording
stopped; speakers/amplifiers off, disconnected or independently protected; headphones/listeners protected; KDE software volume (30 %) and
the CONNECT 2 mute not relied upon; understanding that the new enumeration can change the physical output level or mute state.

## Sequence (2026-10-24; boot id prefix `d396d14b`, the 8A.25 boot)
| Time | Event |
|---|---|
| 20:03:20 | pre-check (hardened gate) OK: kernel `7.3.0-5-cachyos`; one CONNECT 2 on port `3-2` / controller `0000:0d:00.4`, device number 3 = journal's last enumeration (sequence [2, 3]); `snd_usb_audio` loaded and registered once; `1.0`, `1.1`, `1.2` bound; FU2 failure present in the boot log; PipeWire `DEVICE_NO_ROUTES`; GET-only Speaker −23.50 dB unmuted, Headphone −32.40 dB muted |
| 20:07:13 | user unplugs (journal: `USB disconnect, device number 3`) |
| 20:07:18 | user replugs into the same port; new enumeration, **device number 4**; unit-5 `broken mixer GET_CUR (0/18432/256 => 6912)` |
| 20:07:18 | WirePlumber (user journal): `spa.alsa: Unable to load mixer: no such device or address` ×3 |
| 20:07:27 | post gate OK: exactly one new disconnect, one new enumeration and identity line (sequence [2, 3, 4]); journal number = observer number = sysfs number = 4; module loaded, registration count unchanged (1); `1.0`–`1.2` bound; one device; journal verified |
| 20:08:49 | first passive classify (≈91 s after the reconnect): INCONCLUSIVE |
| 20:48:50 | second passive classify (≈41 min after): identical, INCONCLUSIVE |

## Timing (sampling bounds, not request latency)
The observer was a single sysfs sampling loop at 0.1 s: the device node reappeared within (20:07:18.49, 20:07:18.59]; all three audio
interfaces were bound within (…, 20:07:18.79]. Derived **reconnect → audio-interfaces-bound lies between 0.10 s and 0.30 s**. This is an
interval between sysfs observations; it is not USB request latency, and says nothing about when the first mixer request was issued. The
journal's enumeration timestamp is 20:07:18.53. (Phase 8A.25: 120.5 s from reconnect to load; 8A.21: ≈0.15 s from reconnect to probe,
measured with tracing.)

## Evidence after the reconnect
| Item | Before (failing state) | After the reconnect |
|---|---|---|
| FU2 (unit 2) kernel message | `broken mixer GET_CUR (… => -6016)` | **none** |
| unit 5 | `broken mixer GET_CUR (… => 6912)` | same message (independent; not an FU2 failure) |
| `usb_set_interface failed (-110)` | present | **none** |
| driver registration | once | unchanged (still once) |
| ALSA `PCM Playback Volume` | present | present (functionality untested) |
| audio interfaces `1.0`/`1.1`/`1.2` | bound | bound again (0.10–0.30 s bound) |
| PipeWire CONNECT 2 | device + sink + source, 0 Route / 0 EnumRoute | device + **new** sink/source, still **0 Route / 0 EnumRoute** (`DEVICE_NO_ROUTES`) |
| `route.hw-volume` | none | none |
| WirePlumber | `Unable to load mixer` ×2 (at the 8A.25 load) | `Unable to load mixer` ×3 at the re-enumeration |
| GET-only Speaker | −23.50 dB unmuted | **−20.00 dB unmuted** |
| GET-only Headphone | −32.40 dB muted | −32.40 dB muted (unchanged) |

The Speaker level changed by +3.5 dB (louder). OpenWitty issued only GET reads; which component wrote −20.00 dB is **not established**
(kernel mixer initialization after the new enumeration, or the audio stack, are candidates). 8A.21 showed a similar level change after a
healthy reconnect (−4.00 → −23.50 dB).

## Classification and reading
The rule (`classify_evidence`): no FU2 failure requires, for POSITIVE, an ALSA control **and** a CONNECT 2 route with `route.hw-volume=true`;
no FU2 failure with `DEVICE_NO_ROUTES` is INCONCLUSIVE. The result therefore stays **INCONCLUSIVE**; it was not relabelled, and nothing was
done to WirePlumber, PipeWire or the driver to obtain a route.

What the data supports, with their limits:
* At the kernel/USB level the loaded-driver reconnect did **not** reproduce the failure that every earlier 7.3.0-5 probe (normal boot,
  8A.25 late load) showed: no FU2 message, no −110, driver bound within 0.3 s. This resembles the 8A.21 kernel-level outcome on 7.2.10, from the
  same failing starting state. It is one observation.
* The PipeWire layer did not come up healthy: WirePlumber could not open the ALSA mixer at the moment of enumeration ("no such device or
  address") and CONNECT 2 has no routes, even 41 minutes later. A plausible but **untested** reading is a race between WirePlumber/udev
  and the kernel's mixer creation; that would be a separate problem from the FU2 failure and may also explain part of the earlier "no route"
  states. Not shown: that ALSA volume control works, or that a restart of WirePlumber would create the routes.
* It does not isolate whether driver registration, immediate probing, device power/enumeration state or their combination is decisive.
  Kernel 7.3.0-5 differs from 8A.21's 7.2.10, no USB tracing was enabled, and no hardware SET was validated.

## User audio observation
Nothing audible at the unplug or replug (no pop, click or level change noticed), although the GET-only Speaker reading rose from −23.50 to
−20.00 dB. Silence does not exclude electrical transients.

## State afterwards
Nothing to roll back: CONNECT 2 is enumerated (device number 4) and bound; the boot still carries the temporary
`modprobe.blacklist=snd_usb_audio` until the next normal boot (no reboot performed); `/boot/limine.conf` was not touched in this phase.

## Implications for the next investigation (each needs its own approval)
1. The decisive difference between "healthy at the kernel level" (8A.21 reconnect, this reconnect) and "FU2 failure" (cold boot, 8A.24,
   8A.25 late load) is now reproduced on two kernels: the failure appears when the audio driver probes a device that was enumerated at
   boot or by an unbound reconnect, and not when an already-bound driver meets a freshly enumerated device. Still correlational, N = 1 per
   condition.
2. A separate, read-only investigation of the PipeWire "Unable to load mixer" / no-route state (WirePlumber log level, ALSA mixer open timing
   at hotplug) is warranted before concluding anything about hardware volume; the OpenWitty health diagnostic should report "kernel healthy,
   PipeWire mixer not loaded" as its own state, not as a hardware-volume failure.
3. Candidate experiments: a clean *normal boot, then one reconnect* run on 7.3.0-5 (this run started from a failure created by the
   deliberate late load of 8A.25, not by an ordinary autoload boot; it is the closest practical test of "does a reconnect heal a boot-time
   failure"); a Windows startup-timing comparison (`PHASE8A22_WINDOWS_CODEX_HANDOFF.md`); the `SKIP_IFACE_SETUP` single-flag trial still needs a
   design review.
