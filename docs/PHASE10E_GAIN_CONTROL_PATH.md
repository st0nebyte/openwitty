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

### Step 2 — one source mute/unmute cycle (later, separate approval)

Safety: speakers off/down, headphones off the head, direct monitoring of mic/instrument **off** (device), nothing
connected to the instrument input, microphone pointing away from speakers, no recording application open.
Possible writes: SET_CUR FU 5 mute ch 1–2 (unmute, then mute). Before/after: vendor bytes 2/12 (window `--hw-check`
compare and the input-strip mute), ALSA switch cache, route state. Procedure: `wpctl set-mute <source> 0`, read, `wpctl
set-mute <source> 1`, read. Abort: any other byte changes, any sound, any error → stop; restore only the PipeWire mute
to true (one command) if it is not already. Restoration condition: route mute back to true, bytes 2/12 as before.

### Step 3 — one small input-volume decrease and restoration (later, separate approval)

Safety as step 2, plus device mic/instrument mute on. Possible writes: SET_CUR FU 5 volume ch 1–4 (ch 3–4 STALL ×10
each, expected and harmless per traces) to a lower value, then back to the saved value. Use `wpctl set-volume <source>
0.97` (wpctl's cubic scale: 0.97³ ≈ 0.913 linear ≈ −0.8 dB, i.e. ≈ +71.2 dB requested on FU 5) and then the exact saved
value `1.0`. Before/after: vendor gain bytes 0–1/10–11,
ALSA cache, route. Abort: any increase beyond the saved value, any gain change other than the expected, any error.
Restoration condition: route 1.0 as before (WirePlumber saves it), device gain as before.

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
