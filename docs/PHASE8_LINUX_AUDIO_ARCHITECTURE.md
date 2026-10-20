# Phase 8 — Linux audio control architecture (Phase 8A proposal)

**Design only; no hardware writes or PipeWire changes are implemented.** Read
the measured topology in [`PHASE8A_LINUX_AUDIO_INVENTORY.md`](PHASE8A_LINUX_AUDIO_INVENTORY.md)
first. The required behavior is a *hardware-backed KDE volume*, not merely a
GUI slider that tracks a second software gain. The existing one-stereo-sink
topology already satisfies the single-destination requirement; preserve it
unless a native control-plane bridge proves insufficient.

**Phase 8A.1 constraint:** repeated physical reconnects retained one settled
playback sink, but the ALSA `PCM Playback Volume` control was absent at initial
system enumeration and present after reconnect. The kernel logged a sticky
mixer rejection for unit 2 only at initial enumeration. PipeWire accordingly
changed from `route.hw-volume=false` to `true`; the first reconnect coincided
with the hardware Speaker changing to the previously displayed Linux sink
level, without an OpenWitty write. See
[`PHASE8A1_HOTPLUG_REENUMERATION.md`](PHASE8A1_HOTPLUG_REENUMERATION.md).
Therefore neither permanent software-only volume nor reliable ALSA-backed
hardware volume may be assumed. The one-sink recommendation remains, but
the volume bridge must safely handle **both** mixer-exposure states and prevent
unwanted restoration before any implementation is approved.

## Non-negotiable semantics

* CONNECT 2 hardware is the source of truth. Startup reads hardware; it never
  restores a saved software volume to either output.
* PipeWire's CONNECT 2 playback path must remain at unity gain for system
  volume control. KDE's displayed volume/mute must instead represent the
  selected hardware output. Existing per-application stream volumes remain
  normal, separate user controls; they are not a hidden system master stage.
* Changing target publishes that output's **existing** level and mute to KDE;
  it never copies a level between outputs. Volume keys/slider and KDE Mute
  affect only the target. With both active and Speakers targeted, KDE Mute
  changes Speakers only. A physical touch-wheel update changes KDE's display
  without a reflexive USB write.
* If target, device identity, freshness, or readiness is ambiguous, refuse a
  KDE-originated hardware operation and explain why. No guessed target or
  automatic unmute. Separate read/write authorization and bounded verified
  transactions are prerequisites for any future actual volume write.

### Target selection policy

Fixed mapping is explicitly **Speakers** or **Headphones**. Follow active
output derives the target from the *two output mute bits*, not touch-surface
selected-channel byte 56:

| Speaker mute | Headphone mute | Follow target |
|---|---|---|
| Unmuted | Muted | Speakers |
| Muted | Unmuted | Headphones |
| Unmuted | Unmuted | User policy: Speakers (suggested default), Headphones, or Last active output |
| Muted | Muted | User policy: Last active output (suggested default), Speakers, Headphones, or Disable volume adjustments |

“Last active” must be an observed output transition, not whichever channel is
selected on the touch surface. If no valid last output exists under that policy,
disable adjustments rather than infer one. A target switch updates the KDE
representation atomically with the target identity; stale hardware state
cannot be presented as a new live level. KDE Mute toggles only the targeted
hardware mute while preserving its stored hardware volume.

## Candidate Linux integration approaches

| Approach | Fit and cost | Decision gate |
|---|---|---|
| A. Existing PipeWire sink with ALSA hardware mixer | Simplest when the ALSA PCM volume exists, but exposure is inconsistent: absent at initial boot, present after physical reconnect. It is one PCM control, not proven to represent both independent outputs. The first reconnect coincided with a hardware Speaker-level change to the saved Linux volume. A generic sink `HARDWARE` flag is not sufficient. | Prove reliable exposure and exact mapping for both Speaker and Headphone without startup restoration or volume jumps; the current direct path does not meet this gate. |
| B. One existing sink plus a WirePlumber/PipeWire control-plane bridge to the OpenWitty service | Preferred research direction: keep the real ALSA playback sink and per-app streams; observe KDE/PipeWire sink-volume intents, map them to the chosen hardware output, and publish hardware-derived target state back. No extra audio node is required **if** the installed PipeWire/WirePlumber APIs can expose a KDE-visible logical value while the actual sink DSP remains unity. WirePlumber's documented hooks are suitable for policy events, but interception/representation at this exact boundary is **not yet proven**. | In an isolated mock PipeWire graph, prove KDE volume keys/slider/mute, target switching, readback and reconnect without soft master attenuation, feedback or WirePlumber restoration races. Then hardware-write authorization is a separate decision. |
| C. Virtual/proxy playback sink in front of the physical sink | Could offer a custom KDE-facing volume representation if B cannot. But it adds a graph node, routing policy, lifecycle, latency, stream-migration and fallback risks; a naïve virtual sink also adds attenuation and violates the requirement. | Consider only after B fails a documented proof-of-concept; prove unity forwarding, stable identity, per-app stream handling, disconnect behavior and no processing engine. |
| D. Other native kernel/ALSA solution | A suitable standard hardware mixer control could let the ordinary sink reflect hardware state with little policy code. Current ALSA inventory does not expose one, and fabricating one or replacing the kernel driver would be disproportionate. | Only pursue with evidence of a maintainable upstreamable control path; do not issue UAC SETs or detach `snd-usb-audio` to test it. |

| Requirement | A: present direct sink | B: native control bridge | C: proxy sink |
|---|---|---|---|
| KDE keys, slider, mute | Work today, but change the wrong/software-backed state | Can preserve KDE's existing controls **if** intent interception and reported-value override are proven | Can expose controls, but needs its own node/policy |
| Physical wheel; separate output levels | Not reflected in the single sink | Service can observe both and publish the current target | Same bridge still required behind the proxy |
| Immediate target switching; no level copy/jump | One sink has no target concept | Policy maps to existing hardware values; atomic display update must be tested | Also possible, with more graph state to coordinate |
| Feedback and no second master attenuation | Current soft gain violates the goal | Origin-tagged state flow plus proven unity audio path required | Extra gain stage is a major risk unless explicitly unity |
| Reconnect, per-app streams, GUI closed | Native sink/streams survive GUI exit; restored soft volume is a risk | Per-user service can remain active; must handle WirePlumber restore and lost device generations | Must additionally recreate proxy links and keep stream targets stable |
| Qt/C++20 fit; complexity/maintenance | Minimal code, but does not meet requirements | C++ service with a small versioned WirePlumber/PipeWire adapter; medium-to-high integration risk | Highest complexity and ongoing graph-policy burden |

For approach B, do **not** assume that calling `wpctl set-volume`, changing a
WirePlumber route property, or mirroring the hardware number to the current
sink is enough: the observed route is software-volume-backed and may attenuate
audio twice. The service needs a tested distinction between an incoming KDE
*intent* and an outgoing hardware-derived *display update*. Verify the
installed PipeWire/PulseAudio compatibility behavior, not just a mock data
model. WirePlumber currently restores route and stream properties by default
([settings](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html),
[state storage](https://pipewire.pages.freedesktop.org/wireplumber/daemon/locations.html));
any device-specific adaptation must prevent a restored soft sink volume from
silently reintroducing a master stage without changing unrelated devices.

## Proposed ownership and flow

```text
KDE volume keys / slider / mute
        |  PipeWire/PulseAudio-compatible sink intent
        v
CONNECT 2-specific WirePlumber integration (feasibility to prove)
        |  typed target + requested operation, no audio samples
        v
OpenWitty per-user background service -- sole CONNECT 2 control-lease owner
        |  hardware state / target policy / future verified command queue
        +---- session D-Bus ----> Qt/QML GUI and optional Qt tray
        |                          (neither owns USB)
        +---- read-only GETs now; separately approved SETs in later phase
        v
CONNECT 2 vendor control interface

Application audio -> existing PipeWire CONNECT 2 sink at unity -> ALSA PCM
                  -> device internal Speaker / Headphone output paths
```

One service serializes USB access as the current worker does and preserves the
cooperative control lease; it is not a privileged system daemon. Session D-Bus
offers typed state/command methods, change signals, and service activation
([D-Bus session-service specification](https://dbus.freedesktop.org/doc/dbus-daemon.1.html)).
A systemd **user** unit is a possible supervised lifecycle, not an automatic
Phase 8A installation. Closing/hiding a GUI window need not stop its process;
quitting the GUI process must not stop the service; only an explicit service
stop or user-session shutdown stops hardware control. The optional Qt tray
opens/focuses the GUI, may support close-to-tray/start-minimized, and has a
clearly separated Quit GUI versus Stop Service action. Avoid two simultaneous
USB owners during any later migration.

Service state should include device identity (redacted at logs/IPC boundaries),
connection/freshness, both hardware levels and mutes, selected *volume target*,
policy, and generation/sequence IDs. On a KDE-originated request, validate the
current fresh target and bounds, queue one transaction, confirm by fresh read,
then publish. On a hardware-originated read, publish only; origin/sequence tags
and equality checks suppress feedback writes. Switching targets publishes the
new target's observed level; it never writes to either output. On reconnect,
invalidate old generations, read anew before accepting commands, and do not
restore software-saved hardware levels. Exact dB↔KDE-percent scale, mute
semantics and maximum safe level need separate validation before writing.

## Custom Button enhanced modes

OpenWitty may later remember per-device enhanced modes **Switch Outputs +
Follow Volume** and **Switch Outputs + Microphone + Follow Volume**. Device
settings byte 53 stores only the native base mode; current enum values 1 and 2
come from binary/UI research and are **not fully hardware-validated**. Startup
must read the native mode and the locally remembered enhancement, activate it
only if their base modes match, otherwise leave the hardware setting untouched
and enhancement inactive. Never restore output volume from local state. While
an enhancement is active, Follow active output is mandatory and locked in the
GUI with a short explanation. Physical switching should be inferred from fresh
output mute transitions, not selected-channel focus. Future USB research must
confirm the base-mode byte, transitions, long/short-press effects, and any
firmware variance using read-only physical tests before proposing SET support.

## Risks, gates, and small phases

1. **8B: read-only observation + mock control-plane prototype.** Trace KDE
   slider/key/mute event ownership and value mapping without changing actual
   levels; build a pure target-policy state machine with exhaustive table,
   stale/unknown/reconnect/feedback tests. Investigate WirePlumber event hooks
   and PipeWire node/route APIs in a private mock graph. Keep real GUI read-only.
2. **8C: prove one-sink unity representation.** In an isolated test graph,
   demonstrate a KDE-visible hardware-derived volume and mute with no sink
   software gain, no duplicate attenuation, no volume jump on target switch,
   per-app stream compatibility and correct restore/reconnect behavior. If
   impossible, document the API limitation before a proxy-sink design review.
3. **8D: service/GUI split after architecture approval.** Per-user D-Bus
   service owns the existing lease/transport and read-only state; GUI/tray are
   clients. Test normal close, process exit, recovery and singleton ownership.
4. **Later, separately authorized hardware-write phase.** Validate volume and
   mute encodings/ranges and readback on the physical device with safe levels,
   explicit target, stale-state refusal and rollback. Only then connect KDE
   intents to scoped, verified SETs. Do not infer authority from this plan.

Acceptance before any USB write: user-approved controlled test procedure;
confirmed output-target and dB mapping; verified absence of unexpected soft
attenuation; read-only measurement of physical wheel/mute/button transitions;
mock proof of no feedback, no cross-output copying, no startup restoration,
and safe disconnect/reconnect; explicit handling of audio-in-use and failure.
No Phase 8A observation establishes that these gates are already met.
