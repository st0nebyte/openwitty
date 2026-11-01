# Phase 8A.23 – Off-state USB power cycle test

Status: **EXECUTED once – result NEGATIVE.** The plan sections below are the pre-shutdown checkpoint (commit `c2c71ab`); results follow at the end.

## Baseline (Phases 8A.19–8A.22)
Cold boots with CONNECT 2 continuously powered: slow first `SET_INTERFACE(1,1)` (≈41 ms), 5.45 s `SET_INTERFACE(1,0)`
timeout, FU2 (unit 2) sticky verdict, no `PCM Playback Volume`, `route.hw-volume=false`. A live replug with the host
running: fast requests, no timeout, FU2 readbacks follow, volume control back (8A.21). Linux has no `snd-usb-audio`
shutdown callback and `xhci_shutdown` only halts the controller (8A.22); shutdown traffic is unmeasured.

## Hypothesis
H1: removing CONNECT 2's USB power while the PC is off (and restoring it before power-on) makes the next normal
Linux boot healthy. H0: the failure reproduces. One observation only.

## Variables
Changed: device power cycle while host off; unplug/replug history of the port. Unchanged: kernel 7.2.10-1-cachyos,
normal (uninstrumented) boot entry, same USB port, cable, BIOS settings, userspace, no quirk flags, no trace.

## Pre-shutdown state (recorded)
- Branch `phase7-hardware-integration`, clean worktree, start HEAD `872b86f`, `main` = `ef614336`.
- Running kernel `7.2.10-1-cachyos`. Device: 29c2:0004, bcdDevice 1.21, bus 3 port 2 (`3-2`) on the xHCI at PCI
  `0000:0d:00.4`; device number 3 (from the 8A.21 replug).
- This *running* boot still carries the 8A.20 trace parameters on `/proc/cmdline`
  (`kprobe_event=…`, `trace_event=…`, `trace_buf_size`, `dyndbg`). They are NOT in the boot configuration:
  `/boot/limine.conf` is byte-identical to the pre-8A.19 backup, has no temporary entry, `default_entry: 2` (normal
  `linux-cachyos` 7.2.10, cmdline `quiet nowatchdog splash rw rootflags=… root=…`), no modprobe quirks. The next
  boot is therefore uninstrumented provided the default entry (or the normal CachyOS entry) is chosen.
- No tracefs probes/instances; `tracing_on=1`, buffer 1410 KB (stock).
- Current (post-replug, healthy) state: `PCM Playback Volume` present; `route.hw-volume`: analog-output **true**,
  analog-input-mic false (the mic route has no capture-volume control; irrelevant to the speaker sink); sticky
  messages in this boot: FU2 (unit 2) and unit 5 at the 10:13 cold boot, unit 5 only after the 10:40 replug;
  software sink 40 %; GET-only snapshot Speaker −23.50 dB, Headphone −32.40 dB muted.
- Reference kernel lines of the failing 10:13 boot kept outside Git (`~/.local/state/openwitty/trace-8a23/`).

## Physical audio safety (must be confirmed immediately before shutdown)
Playback/recording stopped; speakers/amplifiers disconnected, off or independently protected; headphones and
listeners protected; no reliance on KDE software volume or CONNECT 2 hardware mute; these instructions available;
exactly one experiment. The next enumeration may change output level or mute state (earlier: −33.34 dB muted →
−4.00 dB unmuted); the kernel's FU2 probe may attempt 0 dB writes. Do not play audio or change volume after boot.

## Manual procedure (the user performs every physical step)
1. Normal FULL shutdown (not reboot, not suspend). Wait until completely off (fans stopped, no lights).
2. Keep the PC off. Unplug CONNECT 2's USB cable (remove its USB power). Note whether its display/LED goes dark.
3. Wait ~30–60 s (practical interval; not proof of capacitor discharge time).
4. Replug into the SAME motherboard USB port while the PC is still off. Do not touch it again.
5. Power on, choose the normal CachyOS entry (not a trace entry). Log in; do not play audio or change volume.
6. Resume: tell Claude "8A.23 resume". Claude will run `python3 -I tools/diagnostics/post_boot_health.py --cli
   build-release/openwitty-cli --json ~/.local/state/openwitty/trace-8a23/post_boot.json` (passive, no USB requests
   beyond the existing GET-only CLI snapshot), compare to the reference, classify, and finish this document.
If anything unexpected happens (device missing, different port, boot problems): stop, do not repeat, report.

## Classification (decided at analysis time)
POSITIVE: `PCM Playback Volume` present AND output-route `route.hw-volume` true AND no unit-2 sticky verdict.
NEGATIVE: unit-2 sticky present AND no `PCM Playback Volume` AND hw-volume false. Otherwise INCONCLUSIVE (also if the
device did not enumerate, port/identity changed, or evidence is incomplete). Unit-5 sticky alone is not evidence.
No dynamic debug is enabled, so absence of a "timed out or killed" line does not show absence of a timeout; only
`usb_set_interface failed (-110)` and the sticky verdicts are used. Request latencies are not inferred.

## Results (executed 2026-10-24)
**Safety:** all six physical-safety items confirmed by the user before shutdown.

**Procedure as performed (user-reported, one attempt):** full shutdown; CONNECT 2 unplugged with the PC off and its
display/LED went dark (so it genuinely lost its externally supplied USB power); replugged into the same port before
power-on and not touched again; normal boot entry. Journal: previous boot ended 11:13:54, new boot began 11:15:54
(2 min total off-time, so the unplugged interval was under that; the exact length is not logged).

**Boot configuration:** kernel 7.2.10-1-cachyos, `/proc/cmdline` without any trace/quirk parameters, no tracefs
probes, no temporary Limine entry. Identity: 29c2:0004, bcdDevice 1.21, same port `3-2`, same xHCI (`0d:00.4`);
device number 2 (fresh-boot numbering), enumerated by the kernel 21 ms after kernel start.

**Passive evidence (`post_boot_health.py`):**
| | Pre-shutdown (healthy after the 8A.21 replug) | After off-state power cycle |
|---|---|---|
| ALSA `PCM Playback Volume` | present | **absent** |
| `route.hw-volume`, analog-output | true | **false** |
| FU2 (unit 2) sticky verdict | not in this state | **present** (`-15360/0/128 => -6016`) |
| unit 5 sticky message | present | present |
| `usb_set_interface failed (-110)` | — | **present**, 6.04 s after the sticky lines, interface 2 alt 0 |
| snd-usb-audio registered | — | 11:16:03.50 (≈9.4 s after first enumeration) |
| software sink volume | 40 % | 40 % |
| GET-only hardware snapshot | Speaker −23.50 dB; Headphone −32.40 dB muted | identical |
No dynamic debug was enabled, so no "timed out or killed" line for the probe-time stall is expected; the absence
of an early timeout line is not evidence of absence. SET_INTERFACE latencies were not measured. The sticky readback
differs numerically from earlier boots (−6016 vs −1024 raw): the sticky *value* the device returned is not constant,
it is the verdict that repeated.

**User audio observations:** no clicks, crackles or level changes. The hardware output state did not change across the
power cycle (Speaker −23.50 dB, Headphone muted), i.e. the device kept its output settings through a true power
removal — so those settings are stored non-volatile or are restored by the host/audio stack, not the kernel probe.

**Classification: NEGATIVE.** FU2 sticky present, `PCM Playback Volume` missing, hardware volume unavailable.

## Interpretation
- Supported: removing the device's USB power while the host is off is **not sufficient** for a healthy Linux boot in
  this run. The failure appeared although the device had been fully de-powered before the host started.
- Weakened: the specific "state retained in the device across host S5 because of standby power" hypothesis (H1). The
  device that booted had been through a real power-on, and still showed the bad outcome. (One observation; a
  probabilistic failure cannot be excluded, but every boot-time enumeration observed so far has failed (6 incl. this one).)
- Strengthened: the difference lies in **host-side circumstances at boot** versus an idle running system — e.g.
  the first probe happening during early boot (udev-worker concurrency, device still settling, port/controller state
  after xHCI init), or the device being enumerated at the moment the kernel starts (21 ms in) with the audio
  firmware not yet ready for the probe's rapid SET_INTERFACE/mixer sequence. The 8A.21 live replug differs in that the
  device is powered up on an idle host with the stack warm, and the probe starts ~0.15 s after enumeration.
- Not distinguished: timing of power restoration relative to xHCI init vs. other boot-time effects, ordering of probe
  vs. firmware readiness, device-side vs host-side contribution.
- Windows investigation: a Windows shutdown *command* is no longer needed to explain things (power loss alone did not
  help); a Windows comparison would instead target **startup initialisation** (what Windows does after enumeration,
  how fast it probes). Priority: moderate, not urgent.
- Remaining uncertainty: single trial; unplugged duration not timed; no latency measurements this boot.

## Recommended next step
Do not repeat this test. The remaining question is whether the boot-time *probe context/timing* matters. Candidate
discriminators, each needing its own design and approval (none performed here): (a) one boot with the `snd-usb-audio`
load deferred until after login, which would show whether a quiet, late probe of a freshly booted device is healthy;
(b) a single-flag `SKIP_IFACE_SETUP` trial; (c) the Windows-Codex startup-timing comparison. Option (a) is the most
direct test of "probe too early after boot". Independently, the read-only OpenWitty health diagnostic (8A.22 section 8)
can already report this failure state.
