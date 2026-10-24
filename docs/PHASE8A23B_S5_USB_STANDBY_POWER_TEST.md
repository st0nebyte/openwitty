# Phase 8A.23B – S5 USB standby power disabled: post-boot verification

Passive analysis of already-completed boots. No reboot, shutdown, reconnect, USB reset, driver reload, UEFI change,
hardware write or playback was performed in this phase. Raw logs and `post_boot.json` are kept outside Git.

## Baseline and what is new
- Cold boots with CONNECT 2 continuously powered: FU2 sticky, no `PCM Playback Volume`, `route.hw-volume=false`,
  5 s `SET_INTERFACE(1,0)`/interface-2 timeout (8A.19–8A.20). Live replug with Linux running: healthy (8A.21).
- **8A.23**: power off the PC, unplug CONNECT 2 (display dark), **replug while the PC was still off**, power on →
  NEGATIVE. The device was already powered again *before the host started*.
- **8A.23B (this)**: the missing condition — the device stays unpowered through S5 and receives power only when
  the PC starts, using a UEFI option that (as reported) disables USB standby power in S5. The option's name was not
  recorded (UNKNOWN); the user reported changing a BIOS/UEFI setting "intended to disable USB standby power".

## Boots identified (journal)
| Boot | ID prefix | Start → end (CEST) | Role |
|---|---|---|---|
| −1 | `86c58e1c` | 11:15:54 → 11:21:54 | the 8A.23 boot; ended with **`poweroff requested … plasma-shutdown`** (OS-initiated full power-off) |
| 0 | `9505a013` | 11:29:26 → (running) | the only Linux boot after the UEFI setting change |
Kernel for both: 7.2.10-1-cachyos, same normal entry, no trace/quirk parameters on `/proc/cmdline`.

Important correction to the premise: the journal retains **one** Linux boot after the setting change, not two. The
"restart" the user performed did not produce a Linux boot (user confirmed it went through the UEFI session; the PC was
then fully switched off and manually powered on, and only then Linux started at 11:29:26). So boot 0 follows a
UEFI-session + manual power-off/power-on, **not** an OS-initiated shutdown right before it (the OS poweroff at 11:21:54
preceded the UEFI visit). Whether the final power-off was a firmware-menu power-off or hardware-button power-off was
not recorded.

## Physical S5 condition
- UEFI option name: UNKNOWN (not recorded). CONNECT 2 remained connected to the same port: user report.
- Display during full shutdown: **dark until the PC power button was pressed** (user observation).
- Powered up when the PC started: implied by enumeration at kernel start; the exact moment of power return is not
  observed.
- Status: **S5 POWER CONDITION: CONFIRMED BY USER OBSERVATION (visual only).** A dark display is supporting evidence
  and not an electrical VBUS measurement; the UEFI option may also alter host-controller power behaviour.

## Passive measurements (boot 0, current)
| | Result |
|---|---|
| USB | 29c2:0004, bcdDevice 1.21, bus 3 port 2 (`3-2`), xHCI `0d:00.4`, device number 2, enumerated 21 ms after kernel start |
| ALSA card | C2; **`PCM Playback Volume` absent** (controls: Playback Switch, Capture Switch, channel maps) |
| PipeWire | `route.hw-volume`: analog-output **false**, analog-input-mic false |
| FU2 (unit 2) sticky | **present** (`-15360/0/128 => -6016`) at 11:29:35.42 |
| Unit 5 sticky | present (`0/18432/256 => 6912`) — separate message, not used as evidence |
| `usb_set_interface failed (-110)` | present, 11:29:41.49 (interface 2 alt 0), ≈6.1 s after the sticky lines |
| `snd-usb-audio` registered | 11:29:35.43 (≈9.3 s after enumeration) |
| Software sink volume | 35 % (changed by the user; not by us) |
| GET-only snapshot | Speaker −23.50 dB; Headphone −32.40 dB muted; mic/instrument muted; phantom on |
Boot −1 values come from the retained journal and the 8A.23 record (same failure: FU2 sticky `-6016`, unit 5 sticky,
`-110` at 11:16:09.53, registered 11:16:03.50). ALSA/PipeWire state of boot −1 is only known from the 8A.23 snapshot;
the present state is attributed to boot 0 only. No dynamic debug was enabled, so no early "timed out" line is
expected and no request latency is inferred.

## Classification
**SOFTWARE RESULT: FAILED. S5 POWER CONDITION: CONFIRMED BY USER OBSERVATION (visual; not an electrical measurement).**
→ **NEGATIVE.** FU2 sticky, `PCM Playback Volume` absent, hardware volume unavailable, same -110 timeout.

## Observation worth keeping (derived from numbers already in the logs)
The sticky "readback" is the device's real current speaker level: −6016/256 = **−23.5 dB** in both of the last two
boots, matching the GET-only Speaker value −23.50 dB, whereas earlier boots read −1024/256 = **−4.00 dB** when the
speaker level then was −4.00 dB. So FU2 *reads* correctly but does not follow the kernel's test SETs (−60/0 dB), and
the hardware speaker level persists across a genuine loss of USB power. (Interpretation, not proven: the speaker level
is stored in non-volatile memory, or the host/audio stack restores it; either way, the kernel's "sticky" verdict is
reproducible irrespective of the value.)

## Interpretation
- A pre-boot power interruption alone (8A.23) and a device that stays unpowered until the host starts (8A.23B,
  visually confirmed) both fail. "The device must be freshly powered *after* the host has started" remains the only
  condition that produced a healthy state (8A.21). That strongly weakens a retained-state-from-standby explanation and
  moves attention to **host-side context at boot**: the probe runs ≈9 s after enumeration during a busy early boot
  (udev worker concurrency), versus ≈0.15 s on an idle running host.
- Not excluded: the UEFI option changes host-port initialisation as well; the device needs a longer ready time after
  power-on than this boot gives it (power returned at host start in both cases of this experiment, while in 8A.21 the
  device had a fresh start on a *settled* host); a timing interaction between xHCI port reset and device firmware.
- Windows: no evidence for or against any vendor shutdown command. Windows-side work, if done, should measure
  start-up timing and post-enumeration behaviour.
- Next discriminator unchanged: the **Phase 8A.24 deferred `snd-usb-audio` probe** proposal (not modified or started
  here) remains the most direct test of "boot-time probe context". Needs its own design and approval.

## Limits
Single boot per condition; S5 condition judged by a display only; UEFI option unnamed; the shutdown immediately
before boot 0 was not OS-initiated; no latency measurements.
