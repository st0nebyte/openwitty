# Phase 8A.23 – Off-state USB power cycle test

Status: **PREPARED – not yet executed** (this section is the pre-shutdown checkpoint; results are appended after the boot).

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
- Branch `phase7-hardware-integration`, clean worktree, start HEAD `7e09a2c`, `main` = `ef614336`.
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

## Results
*(to be filled in after the boot)*
