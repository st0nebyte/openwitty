# Phase 8A.3 — normal-boot evidence and hardware-volume writer attribution plan

Research plan only, prepared on 2026-10-20. **No new normal start has been
reported by the owner**, so no fresh boot snapshot or post-boot hardware GET
was taken for this phase. No tracing, reconnect, mixer query, audio-policy
change, hardware SET, or Phase 8B implementation was performed. Compare with
[Phase 8A inventory](PHASE8A_LINUX_AUDIO_INVENTORY.md),
[hotplug findings](PHASE8A1_HOTPLUG_REENUMERATION.md), and
[mixer analysis](PHASE8A2_USB_MIXER_INITIALIZATION.md); do not promote old
observations to new boot results.

## Next *natural* boot: passive evidence gate

Wait for the owner to say that a normal system start has occurred. Do not
request a reboot or unplug merely to collect this evidence. Before opening a
mixer UI or running `amixer`/`alsactl` control-information queries:

1. Record boot ID, boot time, kernel release and installed PipeWire/WirePlumber
   versions. Read the existing kernel journal for CONNECT 2 enumeration,
   `snd-usb-audio` binding and the exact unit-2/unit-5 sticky messages with
   wall-clock and monotonic ordering. Read existing udev and user-session
   journal entries; absence of a log line is not proof that a component did
   nothing. Do not enable verbose logging retroactively.
2. Run `tools/diagnostics/connect2_topology_snapshot.py` once after the graph
   has settled, saving its allowlisted JSON outside Git. The current source
   reads sysfs, `/proc/asound`, `pw-dump` and `pactl info`; it does **not** run
   `amixer`, `aplay`, `alsactl`, a USB control transfer, or a setter. Record
   one stereo PCM/sink or deviations, stable profile/route/default-node,
   and `route.hw-volume`. Its `mixer_controls_queried=false` field means it
   cannot itself establish ALSA control presence/absence.
3. Read `device.restore-routes` and the modification times of WirePlumber's
   `default-routes` and ALSA's `asound.state`; keep their serial-bearing keys
   private. If no OpenWitty control owner exists, an optional **GET-only**
   `openwitty-cli read settings --interface auto` can establish both hardware
   output levels. Never run two OpenWitty clients concurrently.

The manually invoked commands and privacy notes are in
[Phase 8A.2](PHASE8A2_USB_MIXER_INITIALIZATION.md#manually-invoked-next-normal-boot-observation).
The first snapshot is retrospective: a journal or settled PipeWire state can
show what happened, but cannot by itself reveal a USB writer or measure
transient mixer responses. A normal boot with no unit-2 warning would update
the repeatability assessment; it would **not** establish why earlier boot and
reconnect differed. If the control is again absent and the warning appears,
the old pattern is reproduced without deliberately perturbing the device.

| Comparison | Earlier evidence | Next-boot question |
|---|---|---|
| USB/ALSA graph | One stereo PCM after settling; no extra playback sink | Same PCM/interface/profile identity, ignoring card numbers? |
| Kernel unit 2 | Sticky at initial enumeration; no warning on two physical reconnects | Does a naturally occurring start repeat the rejection, and when relative to driver binding? |
| PipeWire output route | `route.hw-volume=false` initially, then `true` after reconnect | Which capability is reported after this boot? |
| Hardware Speaker | 0.00 dB before first reconnect, −20.50 dB afterward | Does a safe post-boot GET show a new change? A single value cannot identify its writer. |
| WirePlumber route state | Stored analog-output ~45% before first reconnect | Is restoration enabled and is state present? No state file alone proves a write. |

## Two separate causal questions

### A. Why unit 2 is rejected at boot

The boot kernel logged `2:0: sticky mixer values ... disabling`; the two
reconnects did not. A timing/readiness hypothesis predicts that the device's
GET_CUR response is initially delayed/stale relative to the kernel's SET_CUR
test, then changes after initialization. A driver/descriptor hypothesis
predicts a reproducible response tied to a particular probe algorithm,
reported range, configuration, or firmware state. Distinguishing these needs
timestamps for the exact range/GET_CUR/SET_CUR sequence and returned values,
not just the eventual ALSA control list. Compare USB descriptors and device
power state **only via read-only observation**. Do not run a probe or quirk
experiment now.

The installed kernel was `7.2.10-1-cachyos` in Phase 8A.2. Its installed
headers lacked `sound/usb/mixer.c`; upstream source is an explanatory model,
not a verified copy of this build. `/proc/kallsyms` currently names
`snd_usb_mixer_set_ctl_value`, `snd_usb_set_cur_mix_value`,
`snd_usb_ctl_msg`, and `snd_ctl_elem_write` (addresses are masked). The
packaged source/patches or debug information must be matched to the *running*
kernel before attaching function-specific probes. The source-level risk of
deferred SET_CUR from a control-info query remains: do not use `amixer
contents` as the initial boot test. The kernel's
[sticky-control implementation](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/+/f1e50b1bcc2377f16574f891c8650def928647ee/sound/usb/mixer.c)
and a [submitted false-sticky-check patch](https://lists.openwall.net/linux-kernel/2026/08/15/722)
motivate the timing question but do not prove CONNECT 2 has that bug.

### B. Who changed Speaker volume to −20.50 dB

The first reconnect coincided with a new ALSA hardware-volume control and a
Speaker setting close to the previously displayed 45% Linux sink value.
Potential writers are kernel mixer probing/restore, udev-triggered `alsactl
restore`, WirePlumber's saved-route application through PipeWire's ALSA
backend, another ALSA/PipeWire client, or device firmware. The installed
udev rule runs `alsactl --export restore` on sound control-card add; the
available `state.C2` had no saved playback-volume entry. WirePlumber's
`device.restore-routes=true` and saved route volume support a hypothesis, not
an attribution. Its [documented route hooks](https://pipewire.pages.freedesktop.org/wireplumber/scripting/existing_scripts/device.html)
can apply stored route properties. The value match alone is insufficient;
quantization, kernel tests and other writers can produce similar results.

## Future *separately approved* writer-attribution trace

This is a design, **not an execution command**. It requires explicit approval
for privileged tracing and a separate owner-operated physical reconnect;
important recording/playback must first be stopped. No automatic reboot,
USB reset, driver detach, audio-policy change, *deliberate* mixer write, or
OpenWitty SET is part of the test. Linux's own enumeration may send mixer
SET_CUR requests; detecting those is the point of the trace. Keep the default
audio sink/profile and stored state unchanged so the original behavior can
be observed.

| Layer/tool candidate | Can establish | Limit and risk |
|---|---|---|
| Existing journals, passive topology snapshot, `udevadm monitor` | Enumeration/binding, session startup, settled topology and route capability timeline | Usually no USB request or process-level volume-write attribution. Low overhead, no privilege beyond journal access. |
| Focused [usbmon](https://docs.kernel.org/usb/usbmon.html) control-transfer capture | Host-side request direction/type/recipient, request number, SET_CUR value, transfer ordering/error and monotonic USB timing | Bus-level record normally lacks initiating PID; shared-bus captures can expose unrelated traffic or audio payloads. Debugfs/module/root access may be needed. Filter to CONNECT 2 **control OUT** and redact/discard other traffic; never commit raw captures. |
| [tracefs kprobe/ftrace events](https://docs.kernel.org/trace/kprobetrace.html) at reviewed USB-audio mixer setter and ALSA control-write paths, with call stack/current task | Whether a SET passed through kernel probe/restore versus ALSA ctl-write path; task/comm may identify an ALSA userspace caller | Requires privilege, mounted tracefs, matching symbols/ABI and careful filtering. A `kworker` context or async queue can obscure origin. Bad probes may miss calls or add latency; capture only event metadata. |
| ALSA control-write syscall/ioctl trace, optionally targeted `strace` of `alsactl`/PipeWire | Which userspace process submitted a control write and when | `strace` can slow/perturb a process; PipeWire may be sensitive. ALSA ioctl alone may not reveal the exact USB transfer or catch kernel-internal probe writes. Requires attach permission and process targeting. |
| PipeWire device Route-param event monitor plus WirePlumber logs at existing level | Whether stored route props were applied before the hardware change; route values and timing | A Route event is not itself proof of a USB SET. Raising debug level or restarting daemons would change conditions and needs separate approval; serial-bearing node names require redaction. |
| Safe vendor-interface settings GET immediately before and after the window | Actual Speaker/Headphone levels before/after; distinction from ALSA/PipeWire display | Only a state comparison, not a writer identity. GET must use one OpenWitty lease owner; do not poll concurrently with a GUI. GET timing must not be mistaken for time of change. |

The kernel configuration advertises `CONFIG_USB_MON=m`, `CONFIG_FTRACE=y`,
`CONFIG_KPROBE_EVENTS=y`, and `CONFIG_BPF_EVENTS=y`; this does **not** mean
access is currently configured. At planning time neither tracefs/debugfs
was mounted, `/dev/usbmon0` was absent, and common trace tools (`bpftrace`,
`perf`, `trace-cmd`, `tshark`, `strace`) were not installed. Do not mount,
load, install, attach or enable anything without a reviewed plan and approval.

### Proposed bounded sequence after separate authorization

1. Verify device identity, correct kernel build/symbols, available privileges,
   tracing filters, free disk space and exclusive OpenWitty lease. Ensure no
   GUI/control client is running. Record the current hardware levels via one
   verified GET-only CLI read and release its lease.
2. Start metadata-only, device-filtered USB/control-path tracing **before**
   the owner reconnects. Include a monotonic clock and dropped-event counter.
   Keep raw trace private, mode 0600; bound it to one owner-approved reconnect,
   at most 120 seconds or a small fixed event/size cap. Stop on trace loss,
   unexpected USB errors, audio disruption, or an abnormal device state.
3. Ask the owner to perform one physical unplug/reconnect. Do not automate
   power cycling. Capture passive udev/PipeWire/ALSA events and the first
   control OUT after enumeration. No mixing or routing commands are sent.
4. Stop tracing before the post-event GET-only level read. Compare the exact
   volume SET_CUR recipient/value/time with kernel call stack and any ALSA
   ctl-write PID, Route parameter update, and udev `alsactl` event.
5. Classify only if the evidence chain is complete: **kernel probe** if the
   SET comes from probe/restore code with no ALSA user write; **userspace
   ALSA writer** if a specific process's ctl write leads to the same USB SET;
   **PipeWire/WirePlumber route path** only if its Route update and ALSA
   write chain match the transfer; **firmware/internal** only if hardware
   changes with no matching host write in a loss-free trace. Otherwise report
   unresolved. A run with no volume change is inconclusive, not exculpatory.

Control and audio packets can reveal private data. Do not record isochronous
payloads, device serials, unrelated USB devices or application audio;
retain only the minimal metadata needed for attribution. Review every
capture for redaction before sharing; never commit raw traces.

## Architecture gate for both mixer-exposure states

Keep the one existing PipeWire playback sink unless a controlled graph test
proves it cannot provide a hardware-derived KDE display without attenuation.
The future integration must treat these as different runtime capabilities:

| ALSA PCM playback volume | Required safe behavior before a future volume write |
|---|---|
| **Unavailable** (`route.hw-volume=false` observed) | Do not infer that KDE's soft sink volume equals Speaker or Headphone hardware state. Do not translate or restore that number into hardware. Expose read-only hardware-derived state; refuse ambiguous KDE-originated operations until an approved bridge supplies a unity-gain intent/display path. |
| **Available** (`route.hw-volume=true` observed) | Do not trust the single ALSA control as both outputs or allow generic route restoration to compete with OpenWitty. Prove exact Speaker/Headphone mapping and the writer chain first. Prevent saved route volume from overriding the hardware on re-enumeration before enabling any separate OpenWitty write path. |

`route.hw-volume` is a PipeWire capability report, not an authorization or a
readiness flag. In both states, hardware remains authoritative; target
switching must publish the selected output's existing level without copying
or jumping either hardware volume. One control owner and write-origin
suppression are required; no duplicate digital master gain may be hidden
behind a correct-looking KDE slider. A policy fix must be proven against
boot, reconnect and GUI-closed cases without altering unrelated audio devices.

**Before Phase 8B writes:** obtain a natural-boot comparison; identify the
actual hardware-volume writer with a loss-free, time-correlated trace or
equally strong evidence; characterize the ALSA control's effect on both
outputs; demonstrate no silent restoration or second attenuation stage in
both mixer states; validate target switching/mute and single-owner behavior
in mocks and an isolated audio graph; then request separate authorization
for any real hardware-write experiment. No policy workaround is chosen here.
