# Phase 8A.3 — normal-boot evidence and hardware-volume writer attribution plan

Prepared on 2026-10-20 and updated after the owner reported a normal shutdown
and start the same evening. A fresh **passive** boot snapshot and one validated
vendor-interface settings GET were taken. No tracing, deliberate reconnect,
ALSA mixer query, audio-policy change, hardware SET, or Phase 8B implementation
was performed. Compare with
[Phase 8A inventory](PHASE8A_LINUX_AUDIO_INVENTORY.md),
[hotplug findings](PHASE8A1_HOTPLUG_REENUMERATION.md), and
[mixer analysis](PHASE8A2_USB_MIXER_INITIALIZATION.md).

## New normal-boot evidence: owner-reported shutdown/start

The worktree was clean at `acdb41b54f0d60c0d03101e2a8a6cf85a18b031e`
before capture. The host booted at approximately **23:46:20 CEST** with the
same `7.2.10-1-cachyos` kernel, PipeWire `1:1.6.9-1` and WirePlumber
`0.5.18-1.1`. The private 0600 topology snapshot was captured at 23:47:35
CEST, outside Git. It includes no serial-bearing raw `pw-dump`. The source
tool was reviewed first: it does not invoke ALSA mixer-control queries.

| Layer/time (CEST) | New observation | Comparison/limit |
|---|---|---|
| USB, 23:46:22 | CONNECT 2 `29c2:0004`, release 1.21, high-speed, configuration 1; expected AudioControl, two AudioStreaming, vendor interface 3 and DFU interface 4. Audio interfaces bound to `snd-usb-audio`; vendor/DFU unbound. | Same visible configuration/interface set as before. DFU was not accessed. |
| Kernel, 23:46:30 | Unit `2:0` again logged `sticky mixer values (-15360/0/128 => -5248), disabling`; unit `5:0` also rejected. | **Boot-only unit-2 rejection reproduced on a second normal system start.** The previous boot readback was `-6363`; different readbacks do not by themselves identify firmware timing or control mapping. |
| ALSA, after settling | One CONNECT 2 stereo playback PCM (`card 0, device 0`; FL/FR, 44.1/48/96 kHz capability). | Same functional PCM count as prior boot/reconnect. The new tool deliberately did not query ALSA mixer-control information; kernel rejection and PipeWire route capability are indirect evidence of the missing volume control. |
| Session, 23:46:31 onward | One PipeWire analog-stereo sink, one analog-surround-4.0 source, previous duplex profile and one analog-output route; CONNECT 2 is the default sink. Route `hw-volume=false`, `hw-mute=true`, displayed volume data 0.091123 per channel, matching its `softVolumes`. | Reproduces the earlier **software-backed** boot state, not the post-reconnect `hw-volume=true` state. No duplicate playback destination. |
| Boot services/state | `alsa-restore.service` began at 23:46:27; `alsa-state.service` inactive. WirePlumber `device.restore-routes=true`. `default-routes` remained last modified at 18:24:42. | Service execution and saved state do not prove a specific hardware write. Standard session logs gave no route-volume writer trace. |
| Prior-shutdown ALSA state | `/var/lib/alsa/asound.state` was modified at 23:44:54 before this boot; `state.C2` now **does** contain `PCM Playback Volume` step 79/120 (−20.50 dB). | Unlike the file examined in Phase 8A.2, it was saved after the mixer control became available. A future hotplug restore now has a saved volume candidate; this file alone does not show whether it was applied at boot. |
| Kernel, 23:46:37 | One `usb_set_interface failed (-110)` timeout for the device's audio interface appeared after driver registration. | Unexpected initialization warning; the settled PCM/sink and later GET worked. No reset, retry, detach or audio-routing action was attempted. Cause and audible impact were not tested. |
| One optional GET, after the passive snapshot | No OpenWitty process held the control lease; USB identity/permissions checked. `openwitty-cli read settings --interface auto` exited successfully and decoded Speaker **−20.50 dB**, Headphones **−27.26 dB muted**. The lease was released afterward. | Speaker matches the prior post-reconnect reading. A single later GET cannot rule out a transient change or prove whether the level persisted through power-off versus being restored during boot. No OpenWitty SET was sent. |

The successful CLI GET generated a later usbfs warning that the process had
not claimed vendor interface 3 before use. The interface was unbound and
OpenWitty did not claim/detach it; the request succeeded. This warning is
separate from the earlier `usb_set_interface` timeout and is **not** evidence
of a volume write. Raw CLI settings bytes and device identifiers were not
included in this document.

**Conclusion of this boot capture:** initial system enumeration again causes
the unit-2 sticky rejection and software-backed PipeWire route, whereas the
two earlier owner-operated reconnects did not reject unit 2 and exposed a
hardware-backed route. The trigger for the different mixer response and the
writer of the earlier Speaker change remain unknown. Do not infer that
WirePlumber restored −20.50 dB on this boot merely because that value appears
in both its audio state and the hardware GET.

## Next *natural* boot: passive evidence gate

The above capture was made only after the owner confirmed a normal start.
For any later natural boot, do not request a reboot or unplug merely to
collect evidence. Before opening a
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

| Comparison | Earlier evidence | This normal boot / remaining question |
|---|---|---|
| USB/ALSA graph | One stereo PCM after settling; no extra playback sink | Same functional PCM/interface/profile identity; numeric card IDs are not the acceptance criterion. |
| Kernel unit 2 | Sticky at initial enumeration; no warning on two physical reconnects | **Repeated** at this natural boot; cause still unknown. |
| PipeWire output route | `route.hw-volume=false` initially, then `true` after reconnect | **False again** after this natural boot. |
| Hardware Speaker | 0.00 dB before first reconnect, −20.50 dB afterward | **−20.50 dB** after this boot; persistence versus restoration remains unproven. |
| WirePlumber route state | Stored analog-output ~45% before first reconnect | Restoration remains enabled and state present. No state file alone proves a write. |

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
Phase 8A.2 `state.C2` had no saved playback-volume entry, but the new
pre-shutdown state file does. Neither state file proves a particular write.
WirePlumber's
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
