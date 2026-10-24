# Phase 8A.22 – Persistent USB power, Linux shutdown, cross-platform initialization

Status: offline / read-only research. Nothing was shut down, unplugged, reset or written. The only code change is
the reconnect completeness classifier (section 1). Kernel: `7.2.10-1-cachyos`, source tag `cachyos-7.2.10-1`.

## 0. Evidence recap (correlation, not causation)
| | Cold boot (8A.20) | Live reconnect (8A.21) |
|---|---|---|
| first `SET_INTERFACE(1,1)` | 40.9 ms | 0.117 ms |
| following `SET_INTERFACE(1,0)` | no completion, −110 after 5.45 s | 0.121 ms |
| FU2 readbacks | unchanged after SET (sticky) | follow writes |
| `PCM Playback Volume` / `route.hw-volume` | absent / false | present / true on one route |
| unit 5 channel 3/4 STALLs | 20 | 20 |
| kernel "sticky mixer" messages | units 2:0 and 5:0 | unit 5:0 only |

What is established: the same host, kernel and driver gave a failing outcome after a boot and a healthy outcome
after a live replug. What is *not* established: which of the many differences between the two (device power state,
bus state, host-side timing, who talks to the device first) is the cause. Every statement below about retained
device state is a hypothesis.

## 1. Reconnect completeness classifier (fixed)
`usb_ep0_trace.py analyze` used boot-failure criteria for every trace, so a healthy reconnect with 345 334 events,
0 overrun and a full probe was reported PARTIAL ("no collect report", "no timed-out request"). Added
`--mode reconnect` (`completeness(..., mode="reconnect")`), used with `live_report.json`:
- COMPLETE needs: buffer expanded, 0 per-CPU overrun, no header loss, 0 unpaired/malformed, disconnect **and**
  reconnect observed by the live session, a `SET_INTERFACE` request of the new device (initial probe present) and
  an FU2 sequence that starts with a read. No timeout is required; xHCI lifecycle is required only if something
  timed out. The boot "oldest event ≤ 3 s" rule does not apply.
- Unchanged and still strict in default `boot` mode (needs a timeout, an FU2 volume SET, boot coverage, joined
  lifecycle). Any loss is INVALID in both modes; a missing probe or missing observation is PARTIAL.
- Regression tests: `ReconnectCompleteness` (8 tests) in `test_usb_ep0_trace.py`. The real 8A.21 capture now yields
  `COMPLETENESS: COMPLETE`.

## 2. USB power-state vocabulary (what a "physical disconnect" can and cannot reset)
- **VBUS present**: the port supplies 5 V. A lit LED shows only that *some* circuit is powered; it does not show that
  the USB PHY, the audio DSP or the firmware that implements the mixer are running or in what state.
- **VBUS removed**: all USB-powered electronics lose supply; capacitor/regulator discharge time is unknown for this
  device, so the time needed to reach a true power-on reset is unmeasured. This is the only operation that is
  guaranteed to put *every* internal block through a power-on reset (if nothing is battery-backed, which is not
  known).
- **Logical disconnect (host side)**: the port's pull-up is seen absent; VBUS stays. Not done by Linux unless a port
  is disabled/power-cycled (we never do that).
- **Bus reset**: SE0 ≥ 10 ms driven by the host at enumeration. Resets the USB device controller state
  (address, configuration, alternate settings per the USB spec); a device's internal application state (e.g. a
  mixer unit's value registers) is not required to reset.
- **Suspend / resume / remote wakeup**: Idle bus ≥ 3 ms puts the device in suspend (VBUS stays, state retained). Host
  stops sending SOF when the controller halts.
- **xHCI controller init/reset**: HCRST at boot resets the *host* controller and its port state machines; the
  downstream device sees a bus reset when the port is next reset during enumeration.
- **Device firmware init**: occurs at VBUS power-on (and possibly on bus reset, firmware-defined).
- **Retained state across host S5**: if the board keeps 5 V on the port in S5 ("standby power"), the device is not
  power-cycled by the shutdown. The next host boot then meets a device that was powered *before* the host
  controller initialised (the xHCI's first view of the port may be "connected, in a defined line state").
Whether this board leaves 5 V on the port in S5 for this port is user-reported (the CONNECT 2 stays lit), not
measured.

The 8A.21 live replug removed VBUS *and* produced a fresh power-on enumeration (new device number 3, new address),
with the host fully running. The cold boot meets a device that has been continuously powered since before power-off.
These differ in at least: device power cycle, host-timing context (boot races vs idle system), who probes first
(udev-worker/probe vs. a quiescent system), bus idle history. The experiment in section 6 separates the first from
the others only partially.

### Interaction with the observed failure
Enumeration → `SET_CONFIGURATION` → UAC2 clock-source validity read → per-interface `SET_INTERFACE(alt 1)`
(stream endpoints active) → `SET_INTERFACE(alt 0)` → mixer FU2 get/range/set/readback "sticky" test. The failing
pattern shows the device taking 13–44 ms for its first alt-1 selection and then not completing the following alt-0
control status stage for >5 s. A device whose audio engine was left running/configured from a previous session
(retained state) is one plausible reason for a slow first alt-1; so would a device still initialising after
firmware start. Both are consistent with the data and neither is tested.

## 3. What Linux actually does at shutdown (source `cachyos-7.2.10-1`)
Paths that **exist** in the source:
- `kernel_power_off()` → `kernel_shutdown_prepare()` → `device_shutdown()` walks devices backwards, calling the
  bus/driver `shutdown` method (`kernel/reboot.c`, `drivers/base/core.c`).
- USB interface drivers: `usb_shutdown_interface()` calls `driver->shutdown(intf)` only if set
  (`drivers/usb/core/driver.c`). **`snd-usb-audio` defines `probe`, `disconnect`, `suspend`, `resume`, no
  `shutdown`** (`sound/usb/card.c`, driver struct near line 1334). So no snd-usb-audio code runs for the device at
  power-off; no alt-0 reset, no mixer restore, no vendor request is issued by the audio driver. Its `disconnect`
  path (`usb_audio_disconnect`) is only reached on device removal or driver unbind.
- Host controller: `xhci_pci_shutdown()` → `xhci_shutdown()` stops root-hub polling, `xhci_halt()` clears Run/Stop and
  waits for HCHalted; on controllers with quirks `XHCI_SPURIOUS_WAKEUP` / `XHCI_RESET_TO_DEFAULT` it also resets the
  controller, `XHCI_SPURIOUS_REBOOT` disables ports (`drivers/usb/host/xhci.c`, `xhci-pci.c`, `hcd-pci.c`
  `usb_hcd_pci_shutdown`). Nothing there removes port power (PP) on a generic controller.
- Consequence by design (not measured): after `xhci_halt` the controller stops sending frames, so a device sees the
  bus go idle (suspend condition) while VBUS stays; no bus reset or disconnect is generated unless firmware does it.

Paths **observed** in traces: none at shutdown (all existing traces are boot or live-replug). The previous boot's
journal ends with `systemd` unmounts, PipeWire stop messages, no `usb 3-2` disconnect message — consistent with the
source, but a journal lacking a line is weak evidence (the journal stops being written before the final power-off).

**No evidence** either way for: userspace sending class/vendor requests during shutdown (WirePlumber/PipeWire close
streams — possibly SET_INTERFACE alt 0 on stream close); a firmware reaction to bus idle; anything between
`machine_power_off()` and the platform cutting power; the board's S5 standby-power behaviour for this port. To
measure shutdown traffic a tracer would have to survive into `device_shutdown`, which the current private-instance
tool cannot do (userspace writes stop); a boot-time-armed ring buffer cannot be read after power-off. A bus analyser
or Windows USBPcap-style capture would be the realistic way. Linux behaviour was not modified.

Claims explicitly *not* made: that Windows sends a shutdown command, or that Linux omits one.

## 4. Standby-power hypothesis – fit with the data
Supports: failure only on boots where the device stayed powered; the only fix found is a power-removing replug;
a healthy reconnect shows fast, normal behaviour.
Does not (yet) support: a *retained-state* mechanism specifically. Alternative explanations that fit equally well:
(a) device firmware startup race with the host's first probe after boot (host boot hammers the device within
~5–11 s of power-up of the *host*, while the device may be mid-initialisation), (b) the xHCI's first port reset at
boot interacting with a device in an unusual line state, (c) udev/driver probe concurrency present only at boot,
(d) a host-side timing effect (boot is busy). Earlier negative tests (DELAY_INIT quirk, 8A.12–14) weaken only
simple "probe too early" variants. The off-state power-cycle (section 6) distinguishes "device must be power-cycled
before boot" from "boot-time host conditions".

## 5. iOS / mobile-mode findings (official LEWITT online guide, plus retailer listings)
- Official guide: CONNECT 2 is "a UAC2 (USB Audio Class 2) device"; "iOS devices are compatible with CONNECT 2";
  CONTROL CENTER runs on Mac/PC only. macOS: no driver; Windows: LEWITT ASIO driver (installed with CONTROL
  CENTER); Linux: not mentioned; Android: "we do not guarantee compatibility".
- Power: "requires 27W power via USB" (bus power or adapter with external supply); Lightning iPads/iPhones give no
  bus power (Apple Camera Connection Kit). A retailer lists a LEWITT C2L USB-C/Lightning cable; not verified
  against official docs.
- "Mobile streaming mode" and "macOS streaming mode" are selected in CONTROL CENTER (Settings > Audio). Mobile: mic and
  instrument are mixed to inputs 1/2. macOS mode: loopback on 1/2, mic/instrument on 3/4, and "your device will
  reboot with the following channel routing". So the mode changes **channel routing and triggers a device reboot**;
  the guide does not say whether it changes descriptors, alternate settings, sample rates or mixer units, nor how
  the mode is stored (flash vs volatile) or whether it survives power loss.
- Implications for us: (1) the user's device currently exposes 4 capture channels to PipeWire
  (`analog-surround-40`), i.e. macOS-style routing — which mode the device boots into after power loss is unknown;
  (2) a firmware-internal reboot on mode change shows the device has a software-driven re-initialisation path, so
  a clean "device reboot" is something the firmware does by itself — this suggests, but does not prove, that a
  full device reinit is not host-visible except through disconnect/re-enumeration; (3) nothing implies iOS uses the
  same FU2 sticky-probe algorithm as Linux (it is a Linux driver heuristic). No iOS test was attempted. Official
  compatibility ≠ verified behaviour on this unit.

## 6. Proposed off-state USB power-cycle experiment (NOT executed)
**Hypothesis H1**: a physical interruption of CONNECT 2's USB power *while the PC is off* is sufficient to make
the next Linux boot healthy. **Null (H0)**: the boot-time failure reproduces regardless.
Procedure (one attempt, no repetition): 1) audio-safety confirmation (nothing playing, speakers/amps off or
independently safe, headphones not worn, no reliance on 40% KDE volume or hardware mute, outputs may come up
louder or unmuted after the next enumeration); 2) Linux shutdown to full power-off, wait until the board has
dropped standby (CONNECT 2 LED state noted); 3) user unplugs CONNECT 2 from the USB port; 4) wait ≥ 30 s (long
enough to be well past any plausible discharge; actual discharge time unknown); 5) replug into the **same**
port; 6) normal boot; 7) passive checks only: `amixer -c C2 controls` (PCM Playback Volume?), `pw-dump`
`route.hw-volume`, kernel log for "sticky mixer" / "usb_set_interface failed", read-only
`openwitty-cli read settings --method interface --interface 3`.
Changed: device power cycle while host off; replug time/port history. Unchanged: kernel, drivers, userspace,
boot order, port, cable, BIOS settings, cold host boot.
Outcomes: **healthy** → device power state before boot matters; the shutdown/host-side behaviour is then a
candidate for a mitigation, and a Windows comparison becomes valuable. **Failure reproduces** → retained device
state across host-off is *not* sufficient/necessary; boot-time host conditions (probe timing, controller init) become
the leading branch. Inconclusive: a single healthy result could still be luck if the failure is probabilistic
(5 of 5 prior boots failed — all with the device continuously powered; so a single healthy result is informative
but not statistically strong); cannot separate power-loss from "different port-state history"/the act of
unplugging; cannot tell what within the device was reset. Exactly one test; no automatic repetitions.

## 7. Windows investigation
See `PHASE8A22_WINDOWS_CODEX_HANDOFF.md`. A healthy Linux boot after a Windows shutdown would not prove a Windows
shutdown command: Windows *startup* initialisation may have set the retained state. Not needed immediately.

## 8. OpenWitty implications
A read-only, non-privileged health diagnostic can reliably report: `PCM Playback Volume` presence (`amixer -c <card>
controls`/ALSA via `snd_ctl`), PipeWire/WirePlumber `route.hw-volume` (parse `pw-dump` or `wpctl inspect`), and kernel
log lines ("sticky mixer values", "usb_set_interface failed (-110)", "timed out or killed") **when the log is readable
without privilege** (many systems restrict `dmesg`; `journalctl -k` needs group membership) — otherwise "diagnosis
incomplete". It can distinguish one healthy CONNECT 2 sink (volume control + hw-volume true) from missing hardware
volume, but cannot attribute cause. States: available / unavailable after boot / device not detected / diagnosis
incomplete. It must never suggest that reconnecting is safe while speakers or headphones are active. No privileged
tracing at startup, no automatic reconnect, no hardware-volume write, no USB reset, no firmware workaround, no
speculative shutdown command.

## 9. Recommended order
1. (Free) Keep collecting passive facts on cold boots (`amixer`, `route.hw-volume`) with the user's normal routine.
2. Off-state power-cycle experiment (section 6), one attempt.
3. If H1 holds: Windows Codex investigation (startup/shutdown traffic) to see what a vendor stack does differently.
4. If H0: boot-time host-side experiments (a single `SKIP_IFACE_SETUP` or probe-ordering trial) — separate design/approval.
5. Optionally OpenWitty read-only health diagnostic (independent of the above).

## 10. Remaining uncertainty
S5 standby power on this port (user-reported only); discharge time; contents of device firmware state; whether the
failure is deterministic; what userspace sends at shutdown; whether the mode (mobile/macOS) persists across power loss.
