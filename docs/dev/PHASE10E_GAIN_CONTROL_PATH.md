# Phase 10E — standard USB gain-control path (CONNECT 2): analysis and GAIN-1 plan

2026-10-26. **Offline analysis and passive evidence only.** Sources: Linux `sound/usb/mixer.c` (upstream, matching the
behaviour logged by the installed 7.3 kernel), PipeWire 1.6.10 `spa/plugins/alsa/acp/acp.c` (installed version), the
ACP path/profile files installed on the system, WirePlumber 0.5.18's saved state files, the Phase 8 USB traces, the
Phase 10D inventory (`docs/dev/PHASE10D_PASSIVE_AUDIO_INVENTORY.md`) and the current boot's kernel journal. No mixer was
opened, no PipeWire/ALSA control changed, no stream opened, no USB request issued, no service touched.

Labels: **established** (code or recorded observation shows it), **likely** (follows from code + metadata, not seen on
the wire or with signal), **unknown**.

## 1. Control path

```
 KDE/Plasma input slider & mute ─┐  (PulseAudio API via pipewire-pulse)
 apps: pa_context_set_source_*  ─┤
 browser/WebRTC analog AGC      ─┤  (may call the same API — unknown per application)
 wpctl / pw-cli                 ─┘
            │
            ▼
 PipeWire node "CONNECT 2 Analog Surround 4.0" ── route analog-input-mic
   route.hw-volume=true, route.hw-mute=true; node softMute=false, softVolumes=1.0        [established, metadata]
            │  acp.c set_volume(): always pa_alsa_path_set_volume(); hardware = volume / base_volume,
            │  base_volume = dB(-max_dB) = −72 dB → route 1.0 = +72 dB on the element          [established, code]
            │  acp.c set_mute(): pa_alsa_path_set_mute(), only when the mute state changes      [established, code]
            ▼
 ACP path analog-input-mic.conf: [Element Mic] switch=mute, volume=merge                  [established, config]
            ▼
 ALSA controls "Mic Capture Volume" (4 ch, 0…18432 = 0…+72 dB), "Mic Capture Switch" (ch 1–2)   [established, usbmixer]
            │  mixer_ctl_feature_put(): SET only if the value differs from the cache;
            │  snd_usb_set_cur_mix_value(): one UAC2 SET_CUR, retried up to 10× on error, cache updated on success
            ▼
 USB: UAC2 Feature Unit 5 (source: input terminal "Microphone", 4 ch)                     [established, descriptors]
   ch 1–2: mute + volume host-programmable; ch 3–4: volume programmable, mute read-only
   ch 3–4 volume SETs are answered with STALL (traces)                                     [established, 8A.19]
            ▼
 CONNECT 2 firmware ─► physical mic/instrument preamp gain, device mute (settings bytes 0–2, 10–12)   [UNKNOWN]
   External Gain Lock (settings byte 57) — official purpose: stop apps changing the gain             [UNKNOWN effect on FU 5]
```

## 2. Established, likely, unknown

| Statement | Status | Basis |
|---|---|---|
| The default CONNECT 2 source's volume and mute are hardware controls bound to FU 5 | established | route flags, ACP path file, `usbmixer` |
| PipeWire applies no software mute/volume on top for this source | established (current state) | node `softMute=false`, `softVolumes=1.0` |
| A route volume change makes ACP write the ALSA element; the kernel sends SET_CUR only if the value differs from its cache | established | `acp.c` `set_volume`, `mixer.c` `mixer_ctl_feature_put` |
| A mute change → one ALSA switch write → SET_CUR on FU 5 ch 1–2 | established (code) | `acp.c` `set_mute`, `mixer.c` |
| Route volume 1.0 = +72 dB requested on FU 5 | established | `base_volume` code, observed `volumeBase` 0.000251 |
| At login WirePlumber restores the saved route (`analog-input-mic`: volumes 1.0 ×4, mute true) → SET_CUR +72 dB (ch 1–4; ch 3–4 STALL) and mute on FU 5 | likely | WirePlumber `default-routes` state file + ACP code; not seen on the wire |
| Earlier ALSA reading `72,72,0,0` = the kernel cache after such a write (ch 3–4 never cached) | likely | cache semantics below |
| FU 5 volume changes the physical preamp gain | **unknown** | no evidence either way; see GAIN-1 step 1 |
| FU 5 mute changes the device's mic/instrument mute (bytes 2/12) | **unknown** | every Phase 8 vendor snapshot showed both muted while the route was muted — consistent, not causal |
| The loopback channels (3–4) have no mute in this path | likely | FU 5 mute covers ch 1–2 only, no software mute; needs a signal test |
| External Gain Lock blocks FU 5 SETs | unknown | official description only |
| Apps with automatic gain control (browsers/WebRTC, conferencing) change this source's volume | unknown per application | Chromium/WebRTC contain an analog AGC that can set the platform input volume via the PulseAudio API; default behaviour unverified |

**Consequence (likely, unverified):** a "muted" default CONNECT 2 source is not a software-silenced stream. If the
firmware ignores FU 5 mute, the microphone and instrument would still be captured, and the loopback channels are
captured regardless. Do not rely on the desktop's input mute for privacy or feedback protection until GAIN-1 step 2.

## 3. The broken read-back (`ignore_ctl_error`, `get_cur_broken`)

Kernel behaviour (`sound/usb/mixer.c`), per probe of the card:

1. `get_min_max_with_quirks()` saves the current value of the first channel and writes test values (min 0 dB, then
   max +72 dB), reading back after each. FU 5 always answers `6912` (27.00 dB) → "broken mixer GET_CUR (0/18432/256 =>
   6912)" (present in the current boot's journal as well). `get_cur_broken` is set; the saved value is **not** written
   back; the cache is cleared.
2. `init_cur_mix_raw()` per channel: the read fails (broken, not cached) → the kernel writes **the minimum (0 dB)** to
   each channel; ch 1–2 accept and are cached at 0 dB, ch 3–4 STALL (10 retries each = the 20 STALLs in the traces).
3. From then on reads of FU 5 volume come **only from the kernel cache** (the last value the kernel successfully wrote);
   uncached channels return `-ENXIO`, which `ignore_ctl_error` turns into "success"; the value reported is then the
   control's minimum or a stale buffer value — hence ch 3–4 read 0.
4. Mute switches are not subject to the broken check; their value is cached at init and read from the cache.
5. No resume/restore path re-reads the device.

| Observation | Trust |
|---|---|
| "broken mixer GET_CUR (… => 6912)" in the journal | trustworthy as *what FU 5 answers to GET_CUR at probe*; not shown to track the preamp (equals an early vendor reading of 27 dB, differs from the later 28 dB) |
| ALSA `Mic Capture Volume` value (amixer, PipeWire's displayed input volume) | **synthetic**: the last value the kernel wrote (or 0 for never-written/failed channels); never the device's gain |
| ALSA `Mic Capture Switch` value | the last value written or read at init; not re-read from the device afterwards |
| PipeWire route volume/mute | PipeWire's requested state, read back from the same kernel cache |
| OpenWitty vendor settings read (bytes 0–1, 2, 10–12) | the device's own state (validated against physical controls in Phase 6B) |

The fixed 27 dB value is **not** the hardware gain.

## 4. External Gain Lock

Official (online guide): gain can be adjusted from third-party apps (e.g. a DAW); some apps (e.g. Microsoft Teams) may
change it unwantedly; enable *External gain lock* (Settings > Audio) to prevent that. Protocol: settings byte 57,
`ExternalGainControl` (type name from the binary; R-dec). OpenWitty: displayed, not writable. Unknown: whether the lock
makes the firmware ignore FU 5 SET_CUR, answer it with STALL, or something else; whether "third-party gain" means FU 5
at all. Needs: HW step for byte 57 (write), then GAIN-1 step 4.

## 5. Read-only operations by class

| Class | Operations | Device traffic |
|---|---|---|
| **P — passive** | `/proc/asound/cardN/{stream0,pcm*/sub0/*,usbmixer}`, `/sys/bus/usb/devices/*/descriptors`, kernel journal, `pw-dump`/`pw-metadata`/`pactl list`/`wpctl status`/`wpctl inspect` (registry), WirePlumber state files | none |
| **C — cache-only by code analysis, not passive by rule** | `amixer -c N cget numid=<Mic Capture Volume>` and `…<Mic Capture Switch>` (only these two; **not** `amixer contents`, which would also read the clock-validity and other controls) | expected none (FU 5 volume: cache/-ENXIO; switch: cached since init) — still requires approval |
| **V — vendor read** | one OpenWitty settings GET (window or CLI) | yes (interface 3 control transfer; the same request the service already makes continuously) |
| **W — writes** | any `wpctl set-volume/set-mute`, `amixer cset`, KDE slider, OpenWitty SET | yes — not in step 1 |

## 6. GAIN-1 test plan

### Step 1 — read-only comparison (proposed next action; needs owner approval because of class V)

Goal: decide whether the FU 5 volume value the kernel last wrote (likely +72 dB at login) is reflected in the device's
own microphone/instrument gain.

1. Record (class P, timestamps): `usbmixer`, the journal's "broken mixer GET_CUR" line, `wpctl inspect` of the source
   and the route, `pw-dump` route params, WirePlumber `default-routes` line.
2. With approval for class C: `amixer -c <card> cget` of the two FU 5 controls only (card found via sysfs USB id).
3. With approval for class V: one vendor settings read through the OpenWitty window (it takes the device over from the
   service exactly as in HW-1; no permission switched on), owner reads the mic and instrument gain and mute shown in the
   input strips (or, preferably, a small offline improvement first: `--hw-check` also logs mic/instrument gain and mute
   at its baseline, so the values are recorded, not read off the screen). Close the window; the service takes the
   device back.
4. Interpretation, without writes: if the cache says +72 dB on ch 1–2 and the device reports a different gain (e.g. 28
   dB), an accepted FU 5 volume SET did **not** set the preamp (strong evidence against FU 5 = preamp volume, at least
   in normal mode with the current lock state). If both say 72 dB, FU 5 likely drives the preamp. If the cache is 0 dB,
   no userspace write happened since the probe; the comparison is then inconclusive. Mute: compare switch cache with
   bytes 2/12 (consistent ≠ causal).

Abort: any OpenWitty error, any setting change seen in the window's compare, any audible event. Nothing to restore.

### Step 1 — result (2026-10-26, owner-approved, read-only)

| Time | Source (class) | Observation |
|---|---|---|
| 21:04:38 (boot probe) | kernel journal (P) | FU 5 "broken mixer GET_CUR (0/18432/256 => 6912)": the device answered GET_CUR with 27.00 dB |
| 22:21:09 | PipeWire route, WirePlumber state (P) | `analog-input-mic` volumes 1.0 ×4, mute true; node software mute off; no stream open |
| 22:21:17 | `amixer cget` of the two FU 5 controls (C) | `Mic Capture Volume` = 72,72,0,0 dB; `Mic Capture Switch` = off,off (muted); no kernel message |
| 22:21:36–22:22:50 | OpenWitty window `--hw-check`, all permissions off (V) | took the device over from the service; device reports **microphone 27 dB, muted; instrument 14 dB, muted** (input strips, owner screenshot); no write, no error |
| 22:22:50 | service journal | the service took the device back (`device connected`), same process, no restart |

What this establishes: in this boot the kernel's cache holds +72 dB for FU 5 ch 1–2 — by the kernel code the cache is
reset at probe (0 dB) and changes only on a *successful* SET_CUR, so an FU 5 volume SET of +72 dB was accepted by the
device after 21:04:38. At 22:21 the device reported 27 dB (mic) and 14 dB (instrument). The mute states agree (all muted).
The probe-time GET_CUR answer (27.00 dB) equals the mic gain the device reported.

What it does **not** establish: whether that SET changed the preamp at all, for a time, or was overridden later; when it
was sent; what External Gain Lock was set to; whether GET_CUR reports the real mic gain. Consistent with "FU 5 volume
does not drive the preamp", but **causality is unproven** without a controlled before/after write.

**Safety consequence for later writes:** the PipeWire route stands at 1.0 = +72 dB while the device reports 27 dB (mic)
and 14 dB (instrument). Any FU 5 volume write from that state — including a nominal "decrease" such as `wpctl set-volume … 0.97`
— requests ≈ +71 dB, i.e. **far above both channels' current gains**, and restoring the saved route (1.0) requests +72 dB.
Step 3 below is revised accordingly (Phase 10E.1: bound by the lower of both gains; restoration decided in advance).

### Step 2 — one source mute/unmute cycle (later, separate approval)

**Code trace of `wpctl set-mute <source> 0` and `… 1` (installed WirePlumber 0.5.18 / PipeWire 1.6.10; upstream sources
at those tags; distribution patches not checked):**

| Stage | Behaviour | Status |
|---|---|---|
| `wpctl` (`src/tools/wpctl.c`) | builds a dictionary with **only `mute`** and emits the mixer API's `set-volume` | established (code) |
| mixer API (`modules/module-mixer-api.c`) | for a node on a device route builds a Route param with `index`, `device`, `save = true` and props containing **only `mute`**; `channelVolumes` is added only when the caller passes a volume | established (code) |
| PipeWire `alsa-acp-device.c` `impl_set_param(Route)` | calls `acp_device_set_port()` for the same port, then `apply_device_props()`; `acp_device_set_volume()` is called only for `volume`/`channelVolumes` props, `acp_device_set_mute()` only for `mute` | established (code) |
| `acp.c` `acp_device_set_port()` | the port is already active → returns before `sync_mixer()` (no path re-selection, no volume/mute re-write) | established (code); "already active" = the current route, A-sys |
| `acp.c` `acp_device_set_mute()` → `set_mute()` | no-op if unchanged; otherwise, because the path has a mute switch (`has_mute`), `pa_alsa_path_set_mute()` writes **only the capture switch** (`snd_mixer_selem_set_capture_switch_all`); the volume-as-mute branch (`has_volume_mute`) requires *no* mute switch and a −99999.99 dB minimum — not this path (route advertises `hw-mute=true`, minimum 0 dB) | established (code + metadata) |
| kernel `mixer_ctl_feature_put()` | `Mic Capture Switch` (ch 1–2): SET_CUR (mute control) per channel whose cached value differs; no volume request | established (code) |
| ALSA event → ACP `mixer_callback` → `read_volume()`/`read_mute()` | **reads** the kernel cache (no USB, no write); may update PipeWire's volume view | established (code) |
| WirePlumber `state-routes.lua` | for an already-active route a Route change is **stored** (`save = true`), not re-applied; re-applying stored mute *and* channel volumes happens only on `select-routes` (profile change, newly active route — `apply-routes.lua`) | established (code) |

So the mute cycle is expected to send only FU 5 **mute** SET_CUR requests (ch 1–2) and to leave the stored +72 dB volume
untouched. **Needs the hardware test:** what the firmware does with the FU 5 mute (bytes 2/12 or nothing); whether the
re-read after the mixer event changes the stored route volume (compare `default-routes` and the route params before and
after); that no profile change/route re-selection happens during the test (journal: no WirePlumber "restoring route" /
"setting route" lines). Not covered: KDE's own mute button (goes through `pipewire-pulse`, a different entry point) — not
used in this test.

Safety: speakers off/down, headphones off the head, direct monitoring of mic/instrument **off** on the device, nothing
connected to the instrument input, microphone pointing away from speakers, no recording application open, owner hands
off the device. Possible USB writes: FU 5 mute SET_CUR, ch 1–2 (unmute, then mute). Procedure: passive record (route,
`default-routes`, `usbmixer`, journal) → `wpctl set-mute <source-id> 0` → vendor read (OpenWitty window `--hw-check`,
read-only) → `wpctl set-mute <source-id> 1` → vendor read → passive record. Abort: any byte other than 2/12 changes, any
volume change in the route or stored state, any WirePlumber route re-application in the journal, any sound, any error —
then set the PipeWire mute back to true (one `wpctl set-mute <id> 1`) if it is not already, and stop. Restoration
condition: route mute true, stored volumes unchanged (1.0 ×4), bytes 2/12 as before.

### Step 2 — result (2026-10-26, owner-approved): STOPPED on an unexplained automatic re-mute

Physical conditions confirmed by the owner; OpenWitty window `--hw-check`, all permissions off; source id 65 verified.

| Time | Observation |
|---|---|
| 23:21:22 | route `analog-input-mic` mute **true**, volumes 1.0 ×4; node mute false, softMute false; saved state 1.0 ×4, mute true; no stream |
| 23:28:27–23:28:29 | window took the device over from the service; baseline from the first successful read (LED 100) |
| before the write | device (input strips): mic **27 dB, muted**; instrument **14 dB, muted**; loopback muted; headphones muted |
| 23:29:23 | **one** `wpctl set-mute 65 0` (rc 0). Immediately after: route mute **false**; route/node volumes **[0.99995, 0.99995, 0.000251, 0.000251]** (= +72, +72, 0, 0 dB = the kernel cache values) — PipeWire's re-read, not a 1.0 re-application; profile unchanged; no kernel message |
| 23:29:24.69 | WirePlumber saved the route: **mute true**, volumes [0.99995, 0.99995, 0.000251, 0.000251] |
| 23:29:50 | route mute **true** again, node mute **true**, node **softMute true** — without a second command |
| 23:32 | window compare: `changed: none; unexpected: none`; device mic 27 dB muted, instrument 14 dB muted (bytes 2/12 unchanged vs. baseline) |
| 23:33:06/07 | window closed; the service took the device back (same process, no restart) |

The planned re-mute (`wpctl set-mute 65 1`) was **not** sent: the input was already muted and the event was unexplained.

**Inferred USB operations** (not observed on the wire): at most one FU 5 *mute* SET_CUR per channel 1–2 (unmute) from the
`wpctl` command (code trace above). Unknown: whether it succeeded (a rejection would be hidden by `ignore_ctl_error`), and
what produced the re-mute ~1 s later.

**Not established:** whether the device ever unmuted (the compare ran after the re-mute; a sub-second transient would not
show). Hypotheses for the re-mute: (a) the device rejected the mute SET_CUR, the kernel kept "muted" in its cache, and
ACP's event-driven `read_mute()` flipped PipeWire back; (b) WirePlumber treated the route as newly active and re-applied
the stored props (mute true + volumes) via `apply-routes.lua` — which for channels 1–2 would request +72 dB, equal to the
cached value, so no new volume SET_CUR is expected there, and channels 3–4 reject writes; (c) another client. The journal
has no WirePlumber/PipeWire messages at the current log level. The change of the node's `softMute` from false to true
is also unexplained.

**State left behind (owner decision needed, nothing restored):** route and node muted (safe direction); the saved
WirePlumber route changed from 1.0 ×4 to [0.99995, 0.99995, 0.000251, 0.000251] — at the next login this requests
≈ +72 dB on channels 1–2 (as before) and 0 dB on channels 3–4 (rejected by the device). Restoring the exact old saved
value would need a volume write (requests +72 dB) or an edit of WirePlumber's state file — neither authorized.

**Next diagnosis (each needs separate approval):** (1) class-C read of `Mic Capture Switch` to see the kernel cache
(on/off) — distinguishes (a) from (b) partly; (2) a repeat of the single unmute with WirePlumber/PipeWire debug logging
for the duration (`wpctl set-log-level` or environment, reverted afterwards) and a usbmon capture of EP0 for the CONNECT 2
(kernel debug interface; passive on the bus), so the SET_CUR result and the re-mute's origin are observed directly.

### Step 2 follow-up — cache-only diagnosis (2026-10-26 23:50, no writes)

**The authorized `amixer cget` of `Mic Capture Switch` was not executed.** Re-reading the kernel source
(`sound/usb/mixer.c`) found a path not covered in the earlier analysis: the device's audio-control **interrupt endpoint**
(EP 0x83). A UAC2 status interrupt for a unit makes the kernel clear that control's cache (`clear_bit(channel,
info->cached)` / `bitmap_zero`, skipped only for controls flagged `get_cur_broken` — not the mute switch) and notify
userspace; the next read then goes to the **device** (`get_cur_mix_raw`), and a failed read is masked by
`ignore_ctl_error`, the reported value falling back to the control's minimum (for the switch: off = muted). The CONNECT 2
does use this endpoint for mute (Phase 8H: its Mute button updates `PCM Playback Switch` with no OpenWitty involved). So
the cache bit of `Mic Capture Switch` may currently be clear and the read would not be cache-only; and every hypothesis
below predicts "off", so the read could not discriminate. Stopped before executing it, as required.

Current state (passive, 23:50): route `analog-input-mic` **mute true**, `save` **true**, volumes [0.99995, 0.99995,
0.000251, 0.000251]; node 65 mute true, **softMute true**; saved WirePlumber route: mute true, same volumes, file written
23:29:24.69; output route `analog-output` `save` false; no WirePlumber/PipeWire journal messages 23:29–23:30; no audio
mixer GUI process besides the Plasma shell.

Code facts (PipeWire 1.6.10 `acp.c`, `alsa-acp-device.c`; WirePlumber 0.5.18 scripts), all **established**:
* `mixer_callback()` runs on every ALSA value event of the path's elements and calls `read_volume()` *and* `read_mute()`.
  The immediate volume change after the unmute (to the cache view +72/+72/0/0 dB) is this re-read.
* The kernel emits a value event for a put only when the put reports a change; a failed SET is turned into "no change"
  by `ignore_ctl_error` (no event). The immediate re-read therefore **fits a successful FU 5 mute SET** (or a coinciding
  device notification) — it is not proof.
* `on_mute_changed()` emits node Props with `mute` **and `softMute` set to the same value** — `softMute=true` is a
  consequence of an ACP mute-changed event to "muted", not an independent software-only change.
* ACP's mute-changed event has two sources: `acp_device_set_mute()` (a Route/Props set from a client or WirePlumber) and
  `read_mute()` after a mixer event.
* The Route param's `save` field is the port's `ACP_PORT_SAVE` flag, and **every** Route set overwrites it
  (`p->port.flags = ACP_PORT_ACTIVE | flags`, also for the active port). WirePlumber's re-application (`apply-routes.lua`)
  uses `save = false` (the output route shows that state from login); `wpctl`'s mixer API uses `save = true`.

| Hypothesis | Evidence for | Evidence against | Status |
|---|---|---|---|
| (b) WirePlumber re-applied the stored route (mute + volumes) | would re-mute and set softMute | input route `save = true` now — a WirePlumber re-application would have left `false` | **contradicted** (unless a later save=true set followed, of which there is no sign) |
| (c) another client set the mute (`save = true`, e.g. via pipewire-pulse) | fits the save flag and softMute | no client active, owner hands off, no such application running | unlikely, not excluded |
| (d) device notification: the CONNECT 2 reported unit 5 on its interrupt endpoint, the kernel dropped the cache, ACP's re-read found "muted" (device answer, or a masked read failure) | mechanism in kernel code; the device uses this endpoint for mute (8H); fits softMute, save = true, no journal lines; device mute bytes 2/12 unchanged afterwards | not observed | **plausible, unverified** |
| (a) the USB unmute itself was rejected | — | a masked rejection produces no value event, yet a re-read happened at once | less likely than (d), not excluded |

**23:29:23–23:29:24.69, what the records establish:** the unmute command returned 0; within the same second PipeWire
showed the route unmuted with the cache-view volumes; by 23:29:24.69 WirePlumber had saved mute true. Nothing establishes
the device's transient mute state, whether the device ever unmuted, or the order of USB transactions.

**Instrumented reproduction:** justified before any further FU 5 work, because the result decides whether the desktop input
mute is meaningful on this device. It would need, each separately approved: the same physical safety conditions; a
usbmon capture of the CONNECT 2's EP0 *and* interrupt EP 0x83 (root/debugfs, passive on the bus); PipeWire/WirePlumber
debug logging for the test window only (log level changed and reverted); an ALSA event monitor that only prints events
(`alsactl monitor`, no value reads); one `wpctl set-mute 65 0`, the observation, then the decision on re-muting with the
owner. Expected discriminators: a SET_CUR mute status (success/STALL), an interrupt packet for unit 5, a GET_CUR answer,
and which component logged the mute change.

### Step 2R — instrumented reproduction plan (Phase 10E.2; prepared 2026-10-27, NOT executed)

One repeat of the single unmute, fully instrumented, to tell apart: **A** a successful FU 5 unmute followed by a device
notification and cache invalidation; **B** a failed FU 5 unmute masked by `ignore_ctl_error`; **C** a WirePlumber/PipeWire
route re-application; **D** another client setting the mute. Needs the owner's separate approval; nothing below has been
enabled, loaded, mounted or started.

#### Facilities (checked passively 2026-10-27)

| Item | State | Use |
|---|---|---|
| CONNECT 2 identity | `29c2:0004`, sysfs `3-2`, bus 3, device 2 (re-derived at test time) | capture/correlation |
| Other devices on that bus | none (root hub only) | a per-bus capture contains no unrelated device |
| usbmon | built as module (`CONFIG_USB_MON=m`), **not loaded**; debugfs mounted at `/sys/kernel/debug` (root only); no capture tool installed — the kernel text interface is used | capture |
| PipeWire daemon log | `settings` metadata `log.level = 2` (subject 0), no environment overrides | optional raise, exact restore to 2 |
| WirePlumber log | default (`F E W N` = level 2; no `WIREPLUMBER_DEBUG`, no config) | temporary raise via its own client ID |
| `alsactl monitor` 1.2.16 | source: only `snd_ctl_open/subscribe_events/poll/read(events)/close`; never reads, lists or queries control values | passive event log, user privileges |
| `pw-mon` | PipeWire registry/param monitor client; Route/Props come from ACP state, no mixer read | passive timeline |
| PipeWire clients now | desktop (plasmashell, kded6, portals, kwin), WirePlumber (2 connections), **Steam incl. "Steam Voice Settings"** | Steam is a candidate for D — must be quit before the test |

Logging facts (installed versions, source): WirePlumber's `module-log-settings` applies `settings` metadata `log.level`
only for its **own** client ID and falls back to `"2"` when the entry is deleted (exact restore, no restart).
`wpctl set-log-level LEVEL` **without** an ID writes the entry for *every* client — not used. The PipeWire daemon applies
`log.level` only for subject 0. Other clients (e.g. `pipewire-pulse`) ignore runtime entries, so D is identified
indirectly (below). ACP — the ALSA card code that logs `Set hardware mute: N` (set path) and `mixer changed` (re-read
path) at info level — runs inside WirePlumber; WirePlumber's route scripts log `new active route found`,
`restoring route`, `storing route(...) props` (info) and `setting route(...)` (debug, topic `s-device`).

#### Operations by kind

| Operation | Kind | Root | Undo |
|---|---|---|---|
| `modprobe usbmon` | temporary diagnostic configuration (kernel module) | yes | `modprobe -r usbmon` (it is not loaded today) |
| `cat /sys/kernel/debug/usb/usbmon/<bus>u` | passive bus monitor (copies URBs on that bus; no request of its own) | yes | stop the process |
| `pw-metadata -n settings <WP-ID> log.level "I,s-device:D"` | temporary diagnostic configuration | no | `pw-metadata -n settings -d <WP-ID> log.level` (→ default `2`) |
| `pw-metadata -n settings 0 log.level 3` (optional) | temporary diagnostic configuration | no | `pw-metadata -n settings 0 log.level 2` (the existing value) |
| `alsactl monitor hw:<card>`, `pw-mon`, `pw-dump`, `journalctl`, sysfs/procfs reads | passive monitoring | no | stop the process |
| OpenWitty window `--hw-check` (permissions off) | vendor read (same as steps 1–2) | no | close the window |
| `wpctl set-mute <id> 0` | **the test write** (FU 5 mute SET_CUR ch 1–2 expected) | no | see restoration |
| `wpctl set-mute <id> 1` | **restoration write**, only under the conditions below | no | — |

Not used: `amixer` (any form), `wpctl set-volume`, `wpctl set-log-level` without ID, service restarts, profile switches,
USB resets/re-enumeration, any edit of WirePlumber's state files.

#### Capture filter (keeps only standard audio-control requests and the interrupt endpoint)

Applied *before* anything is written to disk; vendor requests (`bRequest 0x03`, `wIndex 0x33xx` — OpenWitty's service/window
polling with proprietary payloads) and all other endpoints are dropped. Saved as
`<scratch>/fu5filter.py` (not in the repository) and run as the normal user, reading the root `cat` from a pipe:

```python
import sys, time
dev = sys.argv[1]                              # e.g. "3:002" (bus:device, zero-padded device number)
keep = set()
for line in sys.stdin:
    f = line.split()
    if len(f) < 4 or (":%s:" % dev) not in f[3]:
        continue
    ep = f[3].rsplit(":", 1)[1]
    stamp = time.strftime("%H:%M:%S") + ".%06d" % (time.time() % 1 * 1e6)
    if ep == "3":                                # interrupt EP 0x83: UAC2 status messages
        print(stamp, line.rstrip(), flush=True); continue
    if ep != "0":
        continue
    tag, kind = f[0], f[2]
    if kind == "S" and len(f) > 8 and f[4] == "s":
        breq, windex = f[6], f[8]
        if breq in ("01", "02") and not windex.startswith("33"):   # UAC2 CUR/RANGE, not vendor
            keep.add(tag); print(stamp, line.rstrip(), flush=True)
    elif tag in keep:
        keep.discard(tag); print(stamp, line.rstrip(), flush=True)
```

Reading the result: a setup `21 01 0101 0500` / `21 01 0102 0500` is SET_CUR mute ch 1 / ch 2 of entity 5; its completion
status (`0` or `-32` = STALL, repeated up to 10 times by the kernel) follows; `a1 01 0101 0500` is GET_CUR mute with the
1-byte answer (`01` = muted). An `Ii:…:3` completion carries the 6-byte UAC2 interrupt message `bInfo bAttribute wValue(lo
hi) wIndex(lo hi)`: `bAttribute 01` = CUR change, `wValue` hi byte = control selector (01 mute, 02 volume), lo byte = channel,
`wIndex` hi byte = **entity** (05 = FU 5, 02 = FU 2, 29 = clock 41), lo byte = interface. An interrupt is attributed to FU 5
only if its entity byte is 05.

#### Procedure (command by command; owner present throughout)

Preparation
1. Owner confirms the physical conditions of step 2 (speakers off/down, headphones off the head, direct monitoring off, nothing
   on the instrument input, microphone away from speakers, no recording/conferencing application, hands off the device) **and
   quits Steam completely**.
2. `pw-dump > <scratch>/r0-dump.json` — list PipeWire clients; stop if any unexpected client remains (record which).
3. Verify: CONNECT 2 source ID (expected 65 — re-derive), active profile `output:analog-stereo+input:analog-surround-40`, route
   `analog-input-mic` **mute true**, `save` value, route/saved volumes **[0.99995, 0.99995, 0.000251, 0.000251]**; record
   `~/.local/state/wireplumber/default-routes` line and mtime. Stop on any difference.
4. Derive bus/device from sysfs (`for d in /sys/bus/usb/devices/*; …` matching `29c2:0004` → `busnum`, `devnum`); card index
   from `/sys/class/sound/card*` (as in Phase 10D).
5. Derive WirePlumber's client IDs from `pw-dump` (`application.name` "WirePlumber" and "WirePlumber [client]",
   `wireplumber.daemon=true`).

Start instrumentation (terminals A–D; root only in A)
6. A (owner, root): `sudo modprobe usbmon` then
   `sudo cat /sys/kernel/debug/usb/usbmon/<bus>u | python3 -I <scratch>/fu5filter.py <bus>:<dev3> > <scratch>/r-usb.txt`.
7. B: `alsactl monitor hw:<card> | while read l; do echo "$(date +%T.%N) $l"; done > <scratch>/r-alsa.txt`.
8. C: `pw-mon 2>&1 | while read l; do echo "$(date +%T.%N) $l"; done > <scratch>/r-pwmon.txt`.
9. `pw-metadata -n settings <WP-ID> log.level "I,s-device:D"` for each WirePlumber client ID; optionally
   `pw-metadata -n settings 0 log.level 3`. D: `journalctl --user -f -o short-precise -u wireplumber -u pipewire -u pipewire-pulse > <scratch>/r-journal.txt`.
10. Start `openwitty --hw-check --show settings-device` (all permissions off); wait for the baseline line; owner reads mic/
    instrument gain and mute (expected 27/14 dB, muted); press **Compare with baseline** (expect none).
11. Quiet period ≥ 30 s: record what the monitors show with nothing happening (expected: no FU 5 requests, possibly
    interrupts for other reasons).

Test
12. Note the time; **exactly one** `wpctl set-mute <id> 0`.
13. Observe ≥ 60 s without any action: route/node state every 5 s (passive `pw-dump` extract), all monitors running.
14. **Compare with baseline** in the window (device bytes 2/12; mute offsets are expected differences here — the tool
    lists only LED byte 37 as "expected", so inspect the changed offsets: only 2 and/or 12 are permitted).

Restoration
15. If the input is muted again by itself: no command. If it is **still unmuted** after step 13: exactly one
    `wpctl set-mute <id> 1`, **only if** no profile change, no route re-application, no volume change beyond the known
    cache view and no other anomaly were seen, and the physical conditions still hold; then 30 s observation and a
    compare. Otherwise no software write: keep the physical safety, record, and ask the owner.

Cleanup and verification
16. Close the window (service takes the device back — check its journal line).
17. `pw-metadata -n settings -d <WP-ID> log.level` (each ID); if raised: `pw-metadata -n settings 0 log.level 2`; check with
    `pw-metadata -n settings` that only `0 log.level 2` remains.
18. Stop D, C, B; stop A; `sudo modprobe -r usbmon`; `lsmod | grep usbmon` empty.
19. Final passive record as in step 3; saved route volumes unchanged; route muted; OpenWitty settings unchanged.
20. Summaries only go into the repository (timestamps, request types, statuses, entity/channel, log message kinds); raw
    traces and dumps stay in the scratchpad (they contain the device serial in node names).

Abort at any point (then: no further software write; ensure physical safety; record; owner decides): any gain or other
settings byte changes; profile or route changes other than the mute; a re-application of the stored volume; any USB error
other than the documented channel-3/4 STALLs; any sound; an unexpected client; a monitor failing.

#### Discriminating evidence

| Case | usbmon | `alsactl monitor` | WirePlumber log | Route after |
|---|---|---|---|---|
| A | SET_CUR mute ch 1–2, status 0; later an interrupt with entity 05 (mute); a GET_CUR mute answering `01` | event on `Mic Capture Switch` at the write and again at the notification | `Set hardware mute: 0`, later `mixer changed`, mute change **without** `Set hardware mute: 1` | `save` true |
| B | SET_CUR mute with status −32 (×10), no success | **no** event at the write | `Set hardware mute: 0`, later state flip via re-read | `save` true |
| C | a later SET_CUR mute `01` | event | `new active route found` / `restoring route` / `setting route` before `Set hardware mute: 1` | `save` **false** |
| D | a later SET_CUR mute `01` | event | `Set hardware mute: 1` without route-script lines; `pw-mon` shows a Route/Props change | `save` true (mixer API/pulse) |

A successful SET_CUR does not prove the device's own mute changed — only the vendor compare (bytes 2/12) speaks to that,
and only at its sampling time. An interrupt is attributed to FU 5 only by its entity byte.

### Step 2R — result (2026-10-27 07:52–08:04, owner-approved, one run): device-initiated re-mute (case A)

Conditions: physical safety confirmed by the owner immediately before; Steam quit (its two PipeWire clients gone; a
Battle.net launcher under Proton was running but was never a PipeWire client); state as documented (source 65, profile
`output:analog-stereo+input:analog-surround-40`, route muted, `save` false after login, saved volumes [0.99995, 0.99995,
0.000251, 0.000251]); all OpenWitty permissions off; instrumentation live before the write (usbmon heartbeat, `alsactl
monitor` attached to the card's control device, `pw-mon`, WirePlumber at info via its own client IDs, wall-clock and usbmon
timestamps). No optional PipeWire daemon log change. Quiet period ~30 s before the write: no UAC2 request, no interrupt,
no ALSA event. Device before: mic 27 dB muted, instrument 14 dB muted; compare `changed: none`.

| Time (wall clock) | Source | Observation |
|---|---|---|
| 08:01:00.5344 | WirePlumber (ACP) | `Set hardware mute: 0` — the single `wpctl set-mute 65 0` (rc 0) |
| 08:01:00.5345 | USB EP0 | SET_CUR `21 01 0101 0500`, data `00` (FU 5 mute ch 1 = unmute) → completion status **0** |
| 08:01:00.5346 | USB EP0 | SET_CUR `21 01 0102 0500`, data `00` (FU 5 mute ch 2) → status **0** |
| 08:01:00.5347, .5350 | ALSA | two `Mic Capture Switch` VALUE events (the two channel writes) |
| 08:01:00.5355 | WirePlumber | `storing route(analog-input-mic) props` (unmuted) |
| 08:01:00.8313 (+297 ms) | USB EP 0x83 | interrupt `00 01 01 01 00 05` = bAttribute CUR, control 1 (mute), **channel 1**, interface 0, **entity 5** |
| 08:01:00.8313 | ALSA, USB EP0 | VALUE event; GET_CUR `a1 01 0101 0500` → device answers **`01`** (muted) |
| 08:01:00.8315 | WirePlumber (ACP) | `mixer changed`, `New hardware muted: 1`, mute changed, route stored (muted) |
| 08:01:01.3433 (+809 ms) | USB EP 0x83 | interrupt `00 01 02 01 00 05` = mute, **channel 2**, entity 5 |
| 08:01:01.3433 | ALSA, USB EP0 | VALUE event; GET_CUR `a1 01 0102 0500` → **`01`** (muted); `mixer changed` |
| 08:01:05 … 08:01:55 | PipeWire (sampled every 5 s) | route mute true, `save` true, volumes unchanged, node mute/softMute true, profile unchanged |

Absent: any FU 5 volume request (incl. channels 3/4), any other UAC2 request, `Set hardware mute: 1`, any route re-application
(`new active route`, `restoring route`, `setting route`), any new PipeWire client, kernel errors. After: compare `changed:
none` (mic 27 dB muted, instrument 14 dB muted). The input re-muted itself, so no restoration command was sent.

**Established:** the device accepted both FU 5 mute SET_CUR transfers (status 0), then *itself* reported an FU 5 mute change
for channel 1 (+297 ms) and channel 2 (+809 ms) on its interrupt endpoint; the kernel dropped its cache, PipeWire's ACP
re-read the switch from the device, the device answered "muted", and PipeWire/WirePlumber followed. Cases B (rejected
transfer), C (route re-application) and D (another client) did not occur in this run. Steam was not needed for the behaviour.

**Not established:** whether the device's own mute (settings bytes 2/12) or the physical inputs ever unmuted during the
0.3 s / 0.8 s windows (the vendor compares bracket the event but did not sample it); why the firmware reverts FU 5 mute — a
plausible reading is that FU 5 mute mirrors the device's own input mute while that is on, but the test does not show it;
whether a desktop mute *does* mute the inputs when the device's own mute is off (not tested). The earlier step-2 run
(2026-10-26) showed the same outward pattern, consistent with the same mechanism (not re-examined).

### Step 3 — one controlled FU 5 volume request (later, separate approval; revised in Phase 10E.1)

**Per-channel mapping (offline evidence):** FU 5 sits on the 4-channel *Microphone* input terminal; the official channel
order is 1 microphone, 2 instrument, 3/4 loopback; channel names exist only as an unread string descriptor; the kernel
exposes one control with one range (0…72 dB, taken from the first channel) for all four channels, which differs from the
instrument's official 0…60 dB. So FU 5 channel 2 **likely** corresponds to the instrument path, but whether it acts on
the instrument preamp — and with which scale — is **unknown**. ACP writes the same value to all channels (route channel
volumes are equal; `volume = merge`).

**Bound:** a request must not exceed **either** channel's device-reported gain: at most min(G_mic, G_inst) − 1 dB, from a
fresh vendor read immediately before (step 1: 27 dB and 14 dB → at most **13 dB**). For 13 dB: linear 10^((13−72)/20) ≈
0.001122; `wpctl` cubic value ≈ 0.1039 (`wpctl set-volume <source> 0.104`; re-compute from the fresh read). If FU 5 drives
the preamps, the effect is a decrease on both (mic 27→13, instrument 14→13). Device mic and instrument mute on, outputs
physically off/down, headphones off, monitoring off. Possible USB writes: FU 5 volume SET_CUR ch 1–4 (ch 3–4 STALL ×10,
expected). Abort: any reported gain above its value before the request, any other byte change, any error, any sound.

**Restoration (decided by the owner before the test, not afterwards):** returning the route to its saved 1.0 requests
**+72 dB** and is never treated as safe because an earlier step showed no change. Options:
* **R-A (recommended): end in the safe direction.** Leave the FU 5 request at the tested value (≤ both device gains).
  WirePlumber stores it, so later logins request that value instead of +72 dB; if FU 5 acts on the preamps in any state,
  the worst case is a lower gain (quieter), visible in OpenWitty and correctable on the device. Visible side effect: the
  desktop shows the CONNECT 2 input at a low volume. Changes the stored WirePlumber state persistently — needs explicit
  approval.
* **R-B: return to 1.0** only with the owner's explicit acceptance of a +72 dB request, under the step-3 safety conditions,
  followed at once by a vendor read; if a gain changed, the owner restores it with the device's own controls (OpenWitty has
  no gain write permission); never automated.
* Never: an automatic "restore" to 1.0, a per-channel ALSA write (`amixer cset`) without its own plan, or any request
  above a reported gain.

### Step 4 — External Gain Lock (after its own byte-57 write step)

Repeat step 3 with the lock on and off; record whether the device gain changes in either state and whether FU 5 SETs are
STALLed. Restore the lock to its original state with one deliberate GUI action.

### Step 5 — automatic gain control of a third-party application

Safety as step 2; device monitoring off; the app's audio output muted. Use one browser WebRTC test page with "auto gain"
on, speak for 30 s at normal level. Observe passively: PipeWire route volume over time (`pw-mon`/`wpctl inspect`, class
P), then one vendor read. Distinguish: OS volume changes (route), app requests (route changes attributed to the client in
`pw-mon`), physical device control (vendor bytes, knob). Abort: any route volume increase, feedback, error. Restore:
route volume to the saved value with one command; close the page.

All steps: owner present, timestamps for every observation, no inference of a hardware change from differing cached
values alone.

## 7. Implications for DAW and loopback work

* **Four-channel recording through PipeWire:** the default source delivers all four channels at software unity; the
  desktop input slider/mute act on FU 5 (ch 1–2 mute; ch 1–4 volume requests, ch 3–4 rejected), not on the samples.
* **Mic/instrument independence:** FU 5 mute is one switch for ch 1–2 as exposed by ACP (`volume=merge`, one mute);
  per-channel control would need a different mapping — unknown whether the device treats the channels separately.
* **Loopback L/R:** not muted by the desktop's input mute (likely); level not changed by the desktop input volume (ch 3–4
  writes rejected). A muted desktop input can still carry computer playback — relevant for privacy and feedback.
* **Application-controlled gain:** possible through the same path (AGC); effect on the preamp unknown until GAIN-1.
* **PipeWire vs direct ALSA:** direct `hw:` capture bypasses the PipeWire route state but not the device; FU 5 state set
  by WirePlumber at login persists in the device (whatever it means) regardless of the capture path.
* **Future opt-in split sources** (mono Microphone, mono Instrument, stereo Loopback): must be software nodes on top of the
  full four-channel source, must not inherit the hardware route's mute/volume semantics silently, and must say whether
  muting them is a software mute (it should be) — never a hidden FU 5 write.
* The CONNECT 2 is never treated as a surround-recording interface; the 4.0 labels stay an artefact to be documented
  or relabelled, not a basis for remixing.

## Update 2026-10-28 (Phase 11B session 1)

Vendor read during the GUI session: **External Gain Lock (byte 57) is on.** Working hypothesis for GAIN-1: with the
lock on the device refuses external (UAC2 FU 5) gain/mute changes — which would explain the self re-mute after
`wpctl set-mute 65 0` (step 2R) and the FU 5 cache (+72 dB) not matching the preamp (27 dB). Test it in GAIN-1 with
the step-3 rules; switching the lock off while the desktop route requests +72 dB is the risk to plan for. Caveat: the
vendor's field name is `ExternalGainControl`, so the polarity (1 = locked or 1 = external control allowed) is not
verified; OpenWitty's label "gain lock" assumes 1 = locked. GAIN-1 must settle this first, read-only where possible.

## Update 2026-10-28 (LB-1): FU 5 mute does not gate the capture

With the device's own mic/instrument mute (bytes 2/12) switched off from OpenWitty while the PipeWire route stayed
muted (FU 5 mute = 1, cache +72 dB), the 4-channel capture carried the mic (ch 1) and the instrument (ch 2); with the
device mute on, both were exact digital zero. No FU 5/ALSA event occurred in either direction. So for the capture
path FU 5 mute is not effective and FU 5 does not mirror the device mute; the device's own mute is the only gate. This
answers step 2R's open question for practical purposes (the device re-mutes FU 5 but that state has no audio effect)
and makes steps 3–4 lower priority: the planned fix is to stop using FU 5 as the desktop's hardware mute/volume.
