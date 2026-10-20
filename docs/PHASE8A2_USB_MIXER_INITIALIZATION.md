# Phase 8A.2 — USB mixer initialization and unexpected Speaker-volume restoration

Research-only assessment on CachyOS, 2026-10-20. **No reboot, USB reconnect,
volume change, ALSA mixer query, USB SET, or audio-service restart was performed
for this phase.** Earlier physical-reconnect observations are from
[Phase 8A.1](PHASE8A1_HOTPLUG_REENUMERATION.md). The component that wrote the
Speaker level has **not** been identified.

## Observed facts and installed versions

| Evidence | Observation | Limit |
|---|---|---|
| `uname -r`; installed packages | `7.2.10-1-cachyos`; PipeWire `1:1.6.9-1`; WirePlumber `0.5.18-1.1` | Installed kernel headers do not include `sound/usb/mixer.c`; upstream source below is not proven byte-identical to this build. |
| Prior USB/ALSA/PipeWire snapshots | One stereo playback PCM and one sink in settled states. At initial boot, ALSA PCM playback *volume* was absent and PipeWire `route.hw-volume=false`. After reconnect, that control was present and `route.hw-volume=true`. | The PCM playback *switch* was present in both states. These are not two output sinks. |
| This boot's kernel journal | At 18:16:43 local time, `2:0: sticky mixer values (-15360/0/128 => -6363), disabling` and a unit-5 sticky warning. Reconnects at 23:18:44 and 23:21:55 logged only unit 5. | Unit 2 correlates with the playback-volume control seen after reconnect; the journal does not explain the different device responses. |
| Prior read-only CONNECT 2 settings GETs | Speaker 0.00 dB before first reconnect, −20.50 dB afterward; Headphone −27.26 dB muted in both. PipeWire stayed at displayed 45% (−20.81 dB). | OpenWitty sent no SET. No transfer-level writer trace exists. |
| Current `/usr/lib/udev/rules.d/90-alsa-restore.rules` | On `sound/controlC*` add, the rule runs `alsactl --export restore $ALSA_CARD_NUMBER` unless state-daemon mode is configured. | An ALSA restore action exists on each hotplug, not only at login; it is not evidence that it changed this volume. |
| `/var/lib/alsa/asound.state` | File last modified before this boot. Its `state.C2` section contains PCM Playback Switch but **no PCM Playback Volume**. | The current file cannot explain a saved −20.50 dB value via `alsactl`; a different historical state file was not observed. |
| `systemctl show` | `alsa-restore.service` ran at 18:16:40 and exited successfully; `alsa-state.service` was inactive. WirePlumber started at 18:16:44, after the 18:16:43 sticky warning. | The service's boot invocation is separate from the udev per-card restore rule. Per-device udev execution was not independently traced. |
| WirePlumber 0.5.18 state/settings | `device.restore-routes=true`. Private `default-routes` state (mtime 18:24:42, before first reconnect) already stored the CONNECT 2 `analog-output` channel volumes as `[0.091123, 0.091123]`, mute false. | That linear value corresponds to roughly −20.81 dB/45% display, but saved intent does not prove a hardware write. Serial-bearing state keys remain private. |

The physical selected-control channel is not a Linux playback route. The
present ALSA PCM Playback Volume tracks the CONNECT 2 **Speaker** level in the
one before/after comparison; its effect on Headphones, simultaneous outputs,
and mute semantics has **not** been experimentally established.

## Kernel probe path and the boot/reconnect difference

The [upstream Linux USB-audio mixer implementation](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/+/f1e50b1bcc2377f16574f891c8650def928647ee/sound/usb/mixer.c)
reads range/current values, tries `SET_CUR` at test values, then checks
`GET_CUR`. If reads remain fixed, it rejects a volume control as “sticky”.
The same path may try to restore the saved value after a successful probe and
may set maximum on the sticky path. These are **kernel USB control writes**,
even when the initiating user action looks like enumeration. Its control-info
callback can also call range probing if the control is not initialized, so
`amixer ... contents` or similar control-information queries are not a safe
first step on a newly enumerated device. The boot message matches the upstream
warning text and values, but the exact CachyOS source/patch set and transfers
were not traced; do not assert an exact function-by-function match for the
installed module.

A [Linux USB-audio patch submitted to the kernel list](https://lists.openwall.net/linux-kernel/2026/08/15/722)
notes that asynchronous mixers or incorrect ranges can cause false sticky
classification and proposes more samples/waiting. Whether this fix is in the
installed CachyOS build, or whether CONNECT 2 has that particular failure,
is **unknown**. Do not apply a quirk or patch based on this analogy.

Ranked, **unproven** explanations for unit 2's boot-only rejection:

1. Timing/device readiness: the boot probe may see temporarily stale `GET_CUR`
   while the device/firmware or USB audio path initializes; a later physical
   reconnect probes it in a different state. The timing and kernel messages
   support investigating this first, not declaring it proven.
2. Firmware power-state difference: the interface may remain powered across a
   PC shutdown but lose power when unplugged. No power-state measurements were
   taken, so this remains a user-supplied lead.
3. Driver/version-specific sticky-check behavior or a device-specific range
   quirk. Source for this exact compiled module and a control-transfer trace
   are needed to evaluate it.
4. Descriptor/configuration difference. The visible VID/PID, release, and
   interface set matched; full descriptors were not compared byte-for-byte.

## Why the Speaker volume moved: competing mechanisms

WirePlumber's [documented `device.restore-routes` behavior](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html)
includes restoring route properties such as sink volume. The installed
`state-routes.lua` loads stored route properties and `apply-routes.lua` calls
`device:set_param("Route", ...)` with those properties. At reconnect, the
ALSA/PipeWire route switched from software-only volume to hardware-volume
capability; therefore applying the previously stored 0.091123 route value
could plausibly become a hardware Speaker-volume operation through PipeWire's
ALSA backend. The old 45% display, new −20.50 dB hardware step, and changed
`softVolumes` are consistent with that path. They are **not** writer proof.

Other plausible writers are the kernel's mixer probe/restore sequence, udev's
`alsactl restore`, another ALSA/PipeWire client, or firmware behavior. The
current ALSA state lacks a saved playback-volume entry, making *this stored
ALSA state* a weaker explanation for the particular −20.50 dB outcome. The
kernel sticky path tends to test endpoints/restore a saved value, not obviously
the previously displayed 45% value, but no exact transfer trace was captured.
The user made no manual volume or mute adjustment during the two reconnects.

Ranked **writer hypotheses**, not findings: (1) WirePlumber's saved route
property applied through PipeWire/ALSA when hardware volume became available;
the saved value and quantized hardware result are unusually close. (2) Kernel
probe or another PipeWire/ALSA client changed the control coincidentally;
possible without a writer trace. (3) Udev's ALSA restore; the rule runs, but
the available `state.C2` has no stored volume entry. (4) Firmware behavior
independent of host volume policy. This ranking could change completely with
a timestamped trace; no mechanism should be disabled based on it alone.

To identify the writer, a future **separately approved** study would need a
timestamped trace of class-specific USB `SET_CUR` recipients/values (not
payload dumps), ALSA control change events, PipeWire device `Route` parameter
updates, and WirePlumber/udev/alsactl process timing. Correlate the first
hardware change against kernel probe completion and `device:set_param` timing.
USB traces alone name the host transfer but often cannot attribute it to a
userspace process; process-level ALSA/PipeWire tracing may be needed. None of
that tracing or any intentional reconnect is authorized in this phase.

## Manually invoked next-normal-boot observation

No boot helper or persistent logger is installed. After the **next normal
boot** (do not reboot for this investigation), stop important playback before
any later physical tests. Run the first commands before any `amixer`,
`alsactl`, mixer GUI inspection, or OpenWitty USB client. They do not query ALSA
control information. Keep detailed output private; redact serials before
sharing or committing.

```bash
cat /proc/sys/kernel/random/boot_id
uptime -s
uname -r
pacman -Q pipewire wireplumber
journalctl -k -b --no-pager -o short-iso | rg '29c2|CONNECT 2|sticky mixer|snd-usb-audio'
journalctl --user -b -u wireplumber.service -u pipewire.service --no-pager -o short-iso
python3 tools/diagnostics/connect2_topology_snapshot.py --output "$HOME/connect2-next-boot-topology.json"
wpctl settings device.restore-routes
systemctl show alsa-restore.service -p ExecMainStartTimestamp -p ActiveState --no-pager
stat -c '%n %y' "$HOME/.local/state/wireplumber/default-routes" /var/lib/alsa/asound.state
```

Choose a **new**, private output pathname; `--output` refuses to overwrite a
file and creates it mode 0600. The snapshot records sysfs USB interfaces,
`/proc/asound` card/PCM/stream data, PipeWire device/route/node summaries and
the default sink. It deliberately reports `mixer_controls_queried=false`:
**absence of a mixer field is not evidence that the control is absent**.
`pw-dump` and `pactl info` query the existing session graph but do not directly
call ALSA control-info APIs. The kernel journal's unit-2 sticky warning and
PipeWire `route.hw-volume` provide the safer first comparison. Also record
`uname -r`, package versions, and the file modification time (not raw contents)
of WirePlumber's `default-routes` and ALSA's `asound.state`.

Only after this first capture, and only if no OpenWitty process owns the
control lease, an optional previously validated vendor-interface **GET-only**
read can record Speaker and Headphone levels:

```bash
build-release/openwitty-cli read settings --interface auto
```

Do not run `amixer contents`, `amixer scontrols`, `alsactl restore`, or another
ALSA control-information query as a supposedly passive early-boot check.
Determining the exact ALSA control's presence without triggering a deferred
probe remains pending a reviewed kernel-specific method or a separately
approved trace. Do not use the reconnect watcher for this boot capture.

## Architecture decision gate

The one existing PipeWire playback sink remains the best logical Linux
destination; no extra sink is justified by this evidence. **Reliable native
hardware-backed KDE volume is not yet established.** The ALSA volume may be
absent at boot and present after reconnect; route restoration may apply a
saved software volume to newly exposed hardware. A future bridge must avoid
two competing hardware writers, must not restore saved software volume to
hardware, and must handle separate Speaker/Headphone targets rather than
assuming the single PCM control covers both. Merely hiding or disabling
software volume would not solve boot-time mixer probing or independently
prove hardware-source-of-truth semantics.

Before Phase 8B hardware writes: reproduce/characterize a normal boot without
provoking an ALSA probe; inspect the exact packaged kernel source/patches;
identify the writer of the reconnect level change; establish what PCM Volume
and Switch actually control; prove that route restoration can be coordinated
without an extra attenuation stage or volume jump; and test both mixer-present
and mixer-absent paths with one USB owner. Any configuration change, quirk,
trace requiring reconnect, or actual SET needs a separate safety review and
approval. Until then, OpenWitty stays read-only and no volume bridge is
implemented.
