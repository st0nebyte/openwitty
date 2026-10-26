# Phase 10E — standard USB gain-control path (CONNECT 2): analysis and GAIN-1 plan

2026-10-26. **Offline analysis and passive evidence only.** Sources: Linux `sound/usb/mixer.c` (upstream, matching the
behaviour logged by the installed 7.3 kernel), PipeWire 1.6.10 `spa/plugins/alsa/acp/acp.c` (installed version), the
ACP path/profile files installed on the system, WirePlumber 0.5.18's saved state files, the Phase 8 USB traces, the
Phase 10D inventory (`docs/PHASE10D_PASSIVE_AUDIO_INVENTORY.md`) and the current boot's kernel journal. No mixer was
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
