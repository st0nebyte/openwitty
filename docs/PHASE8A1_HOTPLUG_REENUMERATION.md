# Phase 8A.1 — CONNECT 2 hotplug and Linux audio re-enumeration

Investigated on CachyOS, 2026-10-20. No USB write or audio-routing action was
performed by OpenWitty. This document distinguishes new observations
from the earlier owner-reported possibility that a reconnect changes Linux
audio-device choices. The same physical device works under Windows, but its
Windows driver/audio graph has not been examined here.

## Reproducible, privacy-filtered snapshot

`python3 tools/diagnostics/connect2_topology_snapshot.py` emits a timestamped
JSON snapshot to stdout; `--output <new-file>` creates a private (0600) file
and refuses to overwrite an existing one. As of Phase 8A.2 it reads sysfs,
`/proc/asound`, `pw-dump`, and `pactl info`, **without querying ALSA mixer
controls**. Earlier snapshots in this investigation did include `aplay` and
`amixer` queries; those cannot be treated as strictly passive because USB-audio
control-information queries may trigger deferred kernel SET_CUR probing.
The revised tool never opens a USB control handle, writes a mixer value, or
changes a PipeWire object; it does not prove mixer-control presence/absence.
It allowlists fields and replaces the serial-bearing part of CONNECT 2 node
names; do not commit raw `pw-dump`, `udevadm --property`, USB descriptors with
strings, or full application-process listings. Snapshot output includes
ephemeral object/card IDs only to correlate events within a run. Compare
profiles, routes, node kinds, PCM capabilities, and normalized names for
functional topology. The diagnostic can be used while CONNECT 2 is absent.

Passive `udevadm monitor --udev --subsystem-match=usb --subsystem-match=sound`
records device events; it does not request USB transfers. KDE's visible
playback-device list requires separate user observation because a PipeWire
default-node query alone does not prove the desktop UI is free of stale rows.
`connect2_reconnect_watch.py <private-dir>` waits for sysfs presence, then
invokes the snapshot tool at USB appearance and +5/+30/+90 seconds. It neither
reconnects the device nor opens a USB control endpoint.

## Connected baseline (Test A)

At the start of this run the branch was `phase7-hardware-integration` at
`006c954ef78dbe836554788799db89f8d0dfd64c`, with a clean worktree.
No OpenWitty process was running. The CONNECT 2 was present at `29c2:0004`:
configuration 1 of 1, high-speed USB. Interface 0 was AudioControl and
interfaces 1/2 were AudioStreaming, all bound to `snd-usb-audio`; interface 3
was unbound vendor-specific; interface 4 was unbound DFU. No driver was
detached and DFU was not accessed. The ALSA stream described one stereo
playback PCM (FL/FR, 44.1/48/96 kHz capability; playback interface 1
altsetting 1) and one four-channel capture stream. The USB device path was
recorded only in the private snapshot, not here.

PipeWire had one CONNECT 2 `Audio/Sink` (analog stereo, FL/FR) and one
`Audio/Source` (analog surround 4.0). WirePlumber's active profile was
`output:analog-stereo+input:analog-surround-40`; the active output route was
one `analog-output` with `route.hw-volume=false`. The CONNECT 2 sink was the
current default. A read-only PipeWire snapshot recorded sink soft volume and
mute for later comparison; its numerical value is not a hardware volume.
The baseline JSON remains outside Git.

At baseline, `amixer` showed only a joined PCM playback **switch**, not a
playback-volume control. The PipeWire sink was 45% (−20.81 dB), its route
advertised `route.hw-volume=false`, and a safe vendor-interface settings GET
read Speakers **0.00 dB**, Headphones **−27.26 dB (muted)**. No USB write was
sent by OpenWitty.

## Controlled sequence and comparison

The owner should stop important audio streams before each physical unplug.
OpenWitty must not programmatically reset or power-cycle the port. For each
owner-operated cycle: observe removal; snapshot while absent; reconnect only
when requested; snapshot immediately, around 5 s, 30 s, and 90 s; compare
one settled state with the connected baseline. Repeat up to three cycles if
normal and agreed. No profile, route, default sink, mixer level, audio service,
or firmware setting is changed to make a result look stable. A later optional
GUI-running reconnect is a separate decision after topology-only cycles.

| Stage | USB/ALSA | PipeWire/WirePlumber | KDE-visible choice | Interpretation |
|---|---|---|---|---|
| Baseline | One USB device; one stereo ALSA playback PCM; **no ALSA playback-volume control** | One analog-stereo sink, `route.hw-volume=false`, CONNECT 2 default | Not newly captured before unplug | Connected snapshot and read-only settings GET |
| Cycle 1 unplug | USB and ALSA card/PCM absent | CONNECT 2 nodes removed; `auto_null` became current sink while WirePlumber still remembered CONNECT 2 as configured default | Owner screenshot showed no CONNECT 2, only other inactive cards | Normal removal; no stale CONNECT 2 sink |
| Cycle 1 immediate USB appearance | Configuration 1 present; interface 0 visible, ALSA card but no playback PCM yet | No CONNECT 2 PipeWire object or node; `auto_null` still current | Not captured at this instant | **Transient initialization**, not an alternative stable topology |
| Cycle 1 +5/+30/+90 s | All five expected interfaces and stereo PCM returned; **ALSA PCM Playback Volume appeared** alongside playback switch | One analog-stereo sink and the same active duplex profile/analog route; `route.hw-volume=true`; CONNECT 2 current default; sink 45% | Owner screenshot showed one active “Analoge Ausgabe” playback choice at 45%, no duplicate | Stable structural topology, changed **volume-control capability** |
| Cycle 2 immediate | Interface 0 and ALSA card present, but no playback PCM yet | No CONNECT 2 sink; `auto_null` current | Not sampled at this instant | Same brief enumeration transition as cycle 1 |
| Cycle 2 +5 s | All interfaces, one stereo PCM, and **PCM Playback Volume present** | One analog-stereo sink, same duplex profile/analog route, `route.hw-volume=true`; CONNECT 2 default | Pending owner confirmation | First-cycle post-reconnect control exposure repeated |
| Cycle 2 +30/+90 s | One stereo PCM; PCM Playback Volume remained available at step 79 | One analog-stereo sink, same active profile/route, CONNECT 2 default; `route.hw-volume=true` | Owner confirmation pending | No duplicate or profile change through +90 s |

The expected diagnostic distinctions are: a changed card number or PipeWire
numeric ID alone is **not** a capability change; altered USB descriptors or
ALSA playback PCM count is; unchanged ALSA but changed PipeWire profile/route
points toward session-manager policy; a stable PipeWire graph with different
KDE rows points toward desktop/default-node presentation. Temporary nodes
must be separated from stable post-90-second topology. No root cause will be
assigned without a reproduced difference.

### Cycle 1 event and volume timeline

`udevadm` reported removal of USB interfaces, the ALSA sound card, and its
playback/capture PCMs at monotonic event time ~18044.9 s. The absence snapshot
contained zero CONNECT 2 USB/ALSA/PipeWire objects. On the owner's reconnect,
udev reported USB/ALSA additions and audio-interface bindings at ~18131.8 s.
The immediate JSON snapshot at 21:18:44 UTC caught only interface 0 and an
ALSA card without a playback PCM; PipeWire still had no CONNECT 2 sink. The
+5 s snapshot at 21:18:49 UTC had the complete interface set, one stereo PCM,
one PipeWire sink, and the original active profile/route. The +30 s and +90 s
snapshots retained that same functional topology. PipeWire object IDs changed
but are not stable identities.

An important **non-identifier change** occurred: after reconnect, ALSA
`amixer -c 0 contents` and `/proc/asound/card0/usbmixer` exposed `PCM Playback
Volume` on USB mixer unit 2 (0–120 steps, −60…0 dB), where baseline ALSA had
only the playback switch. The control read step 79/120, or −20.50 dB. A fresh
read-only CONNECT 2 settings GET found Speaker volume **−20.50 dB** while
Headphones remained **−27.26 dB muted**. PipeWire still displayed 45%/
−20.81 dB, now reported `route.hw-volume=true`, and its `softVolumes` moved
from ~0.0911 before unplug to ~0.9652 after reconnect. The owner explicitly
reported **no volume, mute, or profile adjustment**—only screenshot/chat and
the physical unplug/replug. OpenWitty issued no SET request.

These observations are **strongly consistent** with the existing 45% Linux
sink level being applied to the newly available ALSA/hardware speaker-volume
control at reconnect, with a small quantization residual in software. They do
**not** yet prove whether WirePlumber, PipeWire's ALSA monitor, another client,
or device firmware initiated the change; no write trace was captured, and the
pre-unplug full USB descriptors were not archived. The USB configuration,
interface class/number set, and ALSA playback PCM count were otherwise the
same in the settled states. The second owner-approved cycle showed the volume
control remained available; it cannot explain the initial cold/warm difference
by itself.

Cycle 2's immediate snapshot again caught only interface 0 and no playback
PCM or PipeWire CONNECT 2 node. At +5 s the expected full interface set,
one stereo ALSA playback PCM, one PipeWire sink, and the original profile and
route had returned. At +30 and +90 s they remained stable. The PipeWire sink
numeric ID changed again, but the normalized sink name, profile and channel
capabilities did not. The ALSA PCM playback-volume control remained present
at step 79 and the read-only vendor settings GET still reported Speaker
−20.50 dB and Headphones −27.26 dB muted. Thus the **first reconnect
changed control exposure and Speaker level; the second reconnect did not
change them further**. Both cycles retained exactly one settled playback sink.
After removing only snapshot timestamps and ephemeral PipeWire object IDs,
the cycle-2 connected baseline and +90 s JSON snapshots compared byte-for-
byte equal. This confirms repeatability of the **observed graph state**, not
audible playback or future boot behavior.

### Kernel evidence and safety limit

The host runs `7.2.10-1-cachyos`. Read-only boot kernel logs add a narrower
explanation than the topology snapshots alone: the **initial system
enumeration** at 18:16 logged `2:0: sticky mixer values ... disabling` for USB
mixer unit 2 and a similar warning for unit 5. Both later physical reconnects
logged only the unit-5 warning, not unit 2. Unit 2 is the `PCM Playback Volume`
unit shown in `/proc/asound/card0/usbmixer` after reconnect. This directly
correlates the initial absence of that ALSA control with the kernel rejecting
unit 2 as sticky. It does **not** prove why the same physical CONNECT 2 passed
the check on reconnect. The owner's observation that the interface may retain
power across a PC shutdown suggests a boot-versus-physical-replug firmware
state difference, but power/firmware state was not measured.
The kernel identified VID/PID `29c2:0004` and device release `1.21` on the
initial and both later enumerations. The observed configuration/interface
class set was unchanged before and after cycle 1, but no full descriptor
byte-for-byte baseline was saved; descriptor-level identity is not proven.

Upstream `snd-usb-audio` mixer source explains that the sticky-control check
compares GET_CUR after trying SET_CUR test values and may unregister a control
if reads stay constant; its fallback may also set a maximum value before
returning. See [Linux `sound/usb/mixer.c`, sticky-volume check](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/+/f1e50b1bcc2377f16574f891c8650def928647ee/sound/usb/mixer.c).
This is a **kernel enumeration behavior**, not an OpenWitty USB SET. We did not
trace the exact transfers on this CachyOS kernel. Since a reconnect changed
physical Speaker volume and the kernel probe can issue USB mixer SETs, the
investigation stops at two owner-approved cycles; no third unplug is requested.
Do not add a kernel quirk, force a driver reprobe, reset USB, or change volume
to test this hypothesis in Phase 8A.1.
The optional GUI-running reconnect (Test E) was likewise not performed:
the unexpected hardware-level change deserves a separate safety plan first.

## Architecture gate and future regression criteria

In these **two physical-reconnect cycles**, one **structurally equivalent**
playback sink returned by +5 s, the same WirePlumber duplex profile and analog
route were selected, and CONNECT 2 became the current default again. KDE's
first-cycle screenshot showed exactly one playback choice. This supports the
existing sink as the eventual single logical destination; no virtual sink is
justified by numeric-ID changes or the temporary `auto_null` transition.
Audible playback after each reconnect was not tested, so functional audio
availability beyond graph enumeration remains a separate acceptance check.
However, *volume capability is not reliable across boot versus reconnect*:
the absent/present ALSA volume control changes PipeWire's hardware/soft-volume
strategy, and the first reconnect coincided with an unsolicited hardware
Speaker-level change. The Phase 8A control-plane bridge remains provisional;
its feasibility and safe no-restoration semantics must be evaluated in both
control-exposed and control-missing states. A plain ALSA/PipeWire hardware
volume path cannot yet be trusted as the final solution, and it exposes only
one PCM volume, not both independent outputs. The USB-selected touch channel
is not a Linux audio route.

On the next **normal** system start, a passive baseline snapshot (without a
deliberate reboot for this task) should compare whether unit 2 again fails the
kernel sticky check while the device retained USB power. Separately, a future
approved diagnostic can inspect the installed kernel driver's probe behavior
and WirePlumber restoration trace without altering volume or trying a quirk.
The exact component that moved the Speaker to −20.50 dB is still unproven.

Future OpenWitty integration should have deterministic tests for: node/card
IDs changing while capabilities remain the same; temporary extra or missing
nodes during enumeration; profile/route change detection; stale sink removal;
default-node failover and return; independent hardware Speaker/Headphone
state; ALSA volume control present versus missing; PipeWire switching between
hardware/soft-volume modes; **external** volume restoration being detected
without OpenWitty restoring software state to hardware; reconnect freshness
reset; one control-lease owner; and read-only operation until separately
authorized. Do not make the future service reinterpret `route.hw-volume=true`
as independent Speaker/Headphone volume support.
