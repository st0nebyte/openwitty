# Feature completion roadmap (CONNECT 2) — Phase 10A audit

Audit of every GUI control against the code, the tests and the hardware evidence, as of the development branch
after Phase 10A (2026-10-25). Supersedes the status claims in older phase notes where they disagree; the evidence
itself is in `docs/HARDWARE_TEST_RESULTS.md`, `docs/VOLUME_INTEGRATION.md` and `PROTOCOL.md`.

**Bottom line:** OpenWitty reads and displays the CONNECT 2 well (validated live), and its desktop integration is
validated. **No GUI control has ever written to the real device.** Real writes so far went through the CLI (LED
brightness; speaker volume/mute) or the kernel path (desktop volume). Most controls are implemented against the
simulated device only.

## Evidence classes

| Class | Meaning |
|---|---|
| **R-dec** | field decoded from the vendor binaries only (not proof of anything on hardware) |
| **R-hw** | mapping confirmed by operating the physical control and diffing reads |
| **R-gui** | shown live and correctly in the real-device GUI |
| **W-cli** | a SET of exactly this field was sent with `openwitty-cli` and verified by read-back |
| **W-gui** | the GUI wrote this field to real hardware and it was verified end to end |
| **—** | not done |

A decoded field is not proof that writing it is safe. A W-cli result is not proof that the GUI path works.

## GUI permissions after Phase 10A

* **Defect fixed — triggers:** with the confirmed *Speaker/headphone control*, the Autogain button would have sent
  the measurement-session and autogain triggers to the real device (`sendTrigger()` only checked `writesEnabled()`;
  `hardwareWriteOffsets` limits settings bytes only). Now every trigger needs its own entry in
  `WritePolicy::hardwareTriggers`, which no policy in this build sets (audit rule).
* **Defect fixed — global enablement:** QML enabled *every* control when `device.writable` was true, so output
  control made inputs, 48 V, DSP, autogain, Autosetup, monitoring, LEDs, custom button and advanced settings look
  editable; a refused click left a pending edit that was displayed on top of the device state.
* **Now:** `core/ControlCapabilities` maps every control to the exact bytes or trigger sequence it writes; the backend
  publishes `can.<key>`; QML enables each control by its own flag; unauthorized changes are refused before staging
  (error, nothing sent, nothing shown as applied). The real-device banner lists what can be changed. Simulation is
  unchanged. Tests: `test_trigger_policy`, `test_control_capabilities`, `GuiPermissions.*`.

| Session | Controls that can change the device |
|---|---|
| Simulated device | all (unchanged) |
| Real device, default (read-only) | none |
| Real device, *Speaker/headphone control* confirmed | speaker volume, speaker mute, headphone volume, headphone mute |

## Feature matrix

Columns: **Function** · **QML → backend** (`setValue`/`trigger` key) · **Field / API** · **Write policy today (real
device)** · **Mock** · **HW read** · **HW write** · **GUI end to end** · **Risk / required hardware test** ·
**Gap or defect** · **Priority** (P0 first GUI write, P1 alpha, P2 alpha-desirable, P3 after alpha, X excluded).

### Inputs

| Function | QML → backend | Field | Policy today | Mock | HW read | HW write | GUI e2e | Risk / test | Gap / defect | Prio |
|---|---|---|---|---|---|---|---|---|---|---|
| Mic gain 0–72 dB | `InputStrip` GainKnob → `in1.gain` | settings 0–1, i16 Q8.8 | none | ✔ | R-hw, R-gui | — | read ✔, write — | Low–Med: louder monitoring/clipping; test decreases first, outputs muted | — | P1 |
| Instrument gain 0–60 dB | → `in2.gain` | 10–11 | none | ✔ | R-hw, R-gui | — | read ✔ | as above | — | P1 |
| Mic / instrument mute | MUTE → `in1.mute`/`in2.mute` | 2 / 12 | none | ✔ | R-hw, R-gui | — (instrument-mute CLI prepared, never executed) | read ✔ | Low–Med: unmute exposes a live signal; mute-first test | — | P1 |
| Low-cut | LC → `inN.highPass` | 3 / 13 | none | ✔ | R-dec (no physical control) | — | — | Low; verify by read-back + listening | — | P2 |
| Clipguard | → `inN.clipguard` | 4 / 14 | none | ✔ | R-dec (no physical control) | — | — | Low | — | P2 |
| 48 V phantom (mic) | 48V (confirm dialog) → `in1.phantom` | 5 | none | ✔ | R-dec (+ plausible first read) | — | — | **High**: ribbon/unsuitable mics, pops; own authorization, dynamic mic or nothing connected | input 2 has no phantom (correct) | P3 |
| Preamp Clean/Warm/Vivid | → `inN.preamp` | 6 / 15 (0/1/2) | none | ✔ | R-dec (no physical control) | — | — | Low | — | P2 |
| Denoise | → `inN.denoise` | 7 / 16 + measurement triggers 6/7/8 | none | ✔ (byte only) | R-dec | — | — | Med (measurement) | **incomplete:** the official app starts a noise-floor measurement; OpenWitty only sets the byte | P3 |
| Compressor | → `inN.compress` | 9 / 18 | none | ✔ | R-dec | — | — | Low | parameters unknown | P2 |
| DSP unavailable at 96 kHz | `dsp.available` | sample rate | — | ✔ | — | — | — | — | **defect:** on real hardware `dsp.available` uses the GUI's simulated rate (48 kHz default), not the real rate | P2 |
| Autogain | A → `trigger(autogainN)` | triggers 4, 1/2/3, 5; status 0x23/0x24 | **no trigger authorized** (Phase 10A fix) | ✔ workflow | R-dec | — | — | Med: device changes gain by itself; own trigger authorization | completion/result inference unverified | P3 |
| Autosetup wizard | Autosetup → preamp, Clipguard, Denoise, **48 V**, autogain | several + triggers | none (`can.autosetup` needs all of them) | ✔ reconstruction | — | — | — | **High** (bundles 48 V and a measurement) | exact step→field mapping, error codes unknown | P3 (last) |
| Selected-channel outline | read-only | 56 | — | ✔ | R-hw, R-gui | n/a | ✔ | — | — | done |
| Input meters | status 0x00/0x09 | status | — | ✔ | R-gui | n/a | ✔ | — | peak/clip flags 0x2E/0x30 not shown | P3 |

### Outputs

| Function | QML → backend | Field | Policy today | Mock | HW read | HW write | GUI e2e | Risk / test | Gap / defect | Prio |
|---|---|---|---|---|---|---|---|---|---|---|
| Speaker volume | `OutputStrip` Fader → `spk.volume` | 20–21 | output control (confirmed; ≤ 10 dB louder per step) | ✔ | R-hw, R-gui | W-cli (Phase 8C) + kernel path | write — | **Med**: audible level; decreases first | GUI path never exercised | P1 |
| Speaker mute | MUTE → `spk.mute` | 22 | output control | ✔ | R-hw, R-gui | W-cli (8C) | write — | Low–Med | — | P1 |
| Headphone volume | → `hp.volume` | 27–28 | output control | ✔ | R-hw, R-gui | **— never SET** | write — | **Med–High**: headphones on the head; first-ever SET; decreases first, headphones off | the first headphone SET would happen through the GUI | P1 |
| Headphone mute | → `hp.mute` | 29 | output control | ✔ | R-hw, R-gui | — | — | Med | — | P1 |
| Loopback mute | → `loop.mute` | 19 | none | ✔ | R-dec (untouched by Panic Mute) | — | — | Med: feedback if monitored | — | P2 |
| Output meters | status 0x12–0x1C | status | — | ✔ | R-gui (48 Hz) | n/a | ✔ | — | exact meter/peak semantics unverified | done |
| Panic Mute (hardware) | read-only `mute.allFour` | 2/12/22/29 | no software trigger (by design) | ✔ | R-hw (3 trials) | n/a | ✔ | — | — | done |
| Desktop volume / knob mirror | Settings ▸ Desktop integration | ALSA PCM + vendor GET | mirror never writes the device | ✔ | ✔ | kernel path | ✔ (Phases 8G–8I) | login-screen side effect | see `docs/VOLUME_INTEGRATION.md` | done |

### Monitoring

| Function | QML → backend | Field | Policy today | Mock | HW read | HW write | GUI e2e | Risk / test | Gap / defect | Prio |
|---|---|---|---|---|---|---|---|---|---|---|
| Input/playback mix | knob → `mon.mix` | 35–36, sign-inverted Q8.8 | none | ✔ (−1…1) | R-hw (−100…0), R-gui | — | read ✔ | Med: monitoring level | **defect (latent):** the setter takes the simulator's −1…1, the device uses −100…0; must be fixed before any write | P2 |
| Direct monitoring mic / inst | → `mon.mic`/`mon.inst` | 34 (0 inst, 1 mic, 2 both) | none | ✔ state machine | R-dec | — | — | Med: routing, feedback | both buttons share one byte | P2 |
| Dim | → `out.dim` | 32 | none | ✔ | R-dec (no physical control) | — | — | Low (quieter) | dim amount device-side | P2 |
| Mono | → `out.mono` | 33 | none | ✔ | R-dec | — | — | Low | — | P2 |

### Settings ▸ Audio / LEDs / Custom button

| Function | QML → backend | Field | Policy today | Mock | HW read | HW write | GUI e2e | Risk / test | Gap / defect | Prio |
|---|---|---|---|---|---|---|---|---|---|---|
| Gain lock | → `set.gainLock` | 57 | none | ✔ | R-dec | — | — | Low | — | P2 |
| Mix applies to speakers | → `set.mixToSpeakers` | 23 `OutputSource` enum | none | ✔ (bool) | R-dec | — | — | Med: speaker monitoring | **defect (latent):** GUI writes 0/1, the enum's value set is unknown | P3 |
| High-impedance headphones | → `set.highImpedance` | 31 | none | ✔ | R-dec | — | — | Med–High: headphone level | — | P3 |
| High-volume speakers | confirm → `set.highVolume` | 24 | none | ✔ | R-dec | — | — | **High**: louder speakers | — | P3 |
| Mobile streaming | disabled | 67 | none | read | R-dec | — | — | routing change | semantics unknown | X (until RE) |
| macOS streaming | sim only | trigger 0x104/0x105 | refused on hardware | ✔ | — | — | — | device restart | — | X |
| LED brightness | `Fader_h` → `set.ledBrightness` | 37 | none in the GUI (CLI only) | ✔ | R-hw | **W-cli** (6C/6D) | write — | **Low** (cosmetic) | — | **P0** |
| LED colours (5 elements) | swatches → `set.led.*` | 38–52 RGB | none | ✔ | R-dec | — | — | Low (cosmetic, persistent) | — | P1 |
| Reset LED colours | — (no GUI) | trigger 0x103 | persistent-change policy | ✔ | — | — | — | Low | not in the GUI | P3 |
| Custom button | radio → `set.customButton` | 53 enum | none | ✔ | R-dec | — | — | Low (button behaviour) | long-press config (54–56?) unknown | P1 |

### Application

| Function | State | Gap | Prio |
|---|---|---|---|
| Sample rate | real device shows "Unknown"; selector simulation only | read the rate from PipeWire/ALSA (display); selection belongs to the desktop | P2 |
| Firmware version | "not decoded yet" | decode `iface_version_t` | P2 |
| Firmware update, factory reset | disabled by policy (factory reset refused in code) | — | X |
| Presets (`.preset`) | not implemented | import/export format (shared with CONNECT 6) | P3 |
| Onboarding tour | on demand | **first-run flag not persisted** | P2 |
| Tooltips / footer hints | implemented | "Show tooltips" not persisted | P3 |
| Write verification, retry/discard | implemented (`commitAndVerify`), mock-tested | never exercised on hardware from the GUI | P1 (with the first GUI write) |
| Unsent-edit display | edits shown on top of the device state, flagged | refused edits no longer staged (10A) | done |
| Device-change merge (knob/button) | device-wins merge, live-validated (reads) | — | done |
| Hot-plug / reconnect | validated (reads) | — | done |
| Usage data, auto-update, register/support links | not applicable (vendor services) | — | X |
| Showcase mode | hardware only | — | X |

## Cross-cutting risks for any GUI write

* **Full 128-byte settings write.** Every settings change is a read-modify-write of the whole message: a fresh GET,
  then one SET. The guard ensures only authorized bytes differ from that fresh snapshot, but a physical change
  (knob, Mute button) in the milliseconds between the GET and the SET would be written back to its old value. Owner
  must not touch the device during a write test; the read-back reveals it.
* **Concurrent writers.** The control lease keeps one OpenWitty writer; the kernel path (desktop volume) is a second
  writer of the speaker level. Speaker tests run with the desktop volume untouched.
* **Bundled authorizations.** The existing *Speaker/headphone control* setting authorizes speaker **and** headphone
  in one confirmation. Hardware tests must still run one field at a time; a later phase should split it.
* **Volume increases.** GUI and service limit increases to 10 dB per step; decreases first in every test.

## Hardware-validation roadmap (each step needs its own explicit approval)

Common rules: one field per test; owner present; nothing played or a known safe signal at low level; listening
equipment physically secured for anything that can get louder; no software state trusted across steps; record
the before/after settings bytes; stop on any byte other than the authorized one changing, on any USB error, or on
any unexpected audible change; restore the original value at the end.

| Step | Field (only) | Preconditions / owner actions | Expected | Read-back | Abort | Restore | Safeguards |
|---|---|---|---|---|---|---|---|
| **HW-1** LED brightness via the GUI | 37 | a GUI authorization for byte 37 only (to be built, WP-B); owner watches the LEDs | LEDs dim to the chosen value; GUI shows it | byte 37 = value, all other bytes unchanged | any other byte differs; no visible change; error | original brightness | cosmetic; no audio risk |
| **HW-2** Speaker mute via the GUI | 22 | speakers turned down / amp off; *Speaker/headphone control* confirmed | speaker muted / unmuted | byte 22 | anything else changes | original mute state | mute before unmute |
| **HW-3** Speaker volume via the GUI (decreases first) | 20–21 | as HW-2; desktop volume untouched | level follows the fader | bytes 20–21 | an increase beyond 10 dB; other bytes | original level with the knob | decreases only first; then one small increase |
| **HW-4** Headphone mute, then volume via the GUI | 29, then 27–28 | headphones **off the head**; first-ever headphone SET | as above | bytes 29 / 27–28 | as above | original | decreases first |
| **HW-5** Mic / instrument mute | 2 / 12 | outputs muted or monitoring off | mute follows | byte 2 / 12 | — | original | instrument-mute CLI step first (prepared) |
| **HW-6** Mic / instrument gain (decreases first) | 0–1 / 10–11 | monitoring off, outputs down | gain follows | 2 bytes | clipping/feedback | original | small steps |
| **HW-7** LED colours, custom button | 38–52 / 53 | none | colours / button function change | the 3 bytes / byte 53 | — | original | cosmetic; colours are persistent |
| **HW-8** Low-cut, Clipguard, preamp, Comp | one byte each | outputs down | audible character change | the byte | — | original | one at a time |
| **HW-9** Dim, Mono, monitoring source, mix (after the mix mapping fix) | 32 / 33 / 34 / 35–36 | speakers off, headphones off the head | monitoring changes | the byte(s) | feedback | original | strongest for mix/source |
| **HW-10** Gain lock, high-impedance, mix-to-speakers (after enum RE) | 57 / 31 / 23 | outputs secured | — | the byte | — | original | high-impedance can raise headphone level |
| **HW-11** High-volume speaker mode | 24 | amp off/down, nothing playing | — | byte 24 | — | off | **strongest**: can be much louder |
| **HW-12** 48 V phantom | 5 | dynamic mic or nothing connected, no ribbon mic, gain down | 48 V LED on/off | byte 5 | anything unexpected | off | own authorization |
| **HW-13** Measurement triggers (autogain, denoise) | per-trigger | signal source ready, outputs down | measurement runs, result in status | status 0x23/0x24, resulting gain | timeout, gain jump | original gain | per-trigger authorization |
| **HW-14** Autosetup | all of its parts | all of HW-5…HW-13 passed | guided setup | every touched byte | any | original | last |

Excluded: firmware update, factory reset, device-mode switching, mobile streaming (until reverse-engineered).

## Dependencies

* HW-1 needs **WP-B** (narrow GUI authorizations); HW-2…HW-4 need WP-B's split of speaker and headphone.
* HW-9 (mix) needs the mix mapping fix (WP-C). HW-10 (mix-to-speakers) needs the `OutputSource` value set (RE).
* HW-13 needs per-trigger authorization (exists since 10A) plus a verified autogain completion/result model.
* HW-14 needs every component step. Denoise as the official feature needs HW-13's denoise measurement.
* Presets need a stable set of validated writes.

## Work packages

| WP | Content | Hardware |
|---|---|---|
| **A** (done in 10A) | per-control capabilities, trigger authorization, refused edits not staged, banner | none |
| **B** | narrow GUI authorizations: a confirmed *LED control* (byte 37, later 38–52/53) and splitting *Speaker/headphone control* into speaker and headphone; each persisted in the settings file, off by default, own confirmation; tests | none |
| **C** | offline corrections: mix mapping (−100…0), `dsp.available` from the real rate (unknown → hint), sample-rate display from PipeWire/ALSA, onboarding/tooltips persistence, firmware-version decode attempt | none |
| **D** | live validation HW-1 (LED via GUI) | owner-approved |
| **E** | live validation HW-2…HW-4 (outputs via GUI, one field at a time) | owner-approved |
| **F** | input mute/gain (HW-5/HW-6), cosmetic (HW-7) | owner-approved |
| **G** | processing and monitoring (HW-8…HW-10) | owner-approved |
| **H** | high-risk: high-volume, 48 V, measurement triggers, Autosetup (HW-11…HW-14) | owner-approved, strongest safeguards |
| **I** | presets, LED colour reset, remaining parity | mixed |

## Recommended minimum for the first public alpha

* Everything validated today: live read-only view, hot-plug, desktop integration, tray.
* GUI writes, each validated live: **LED brightness**, **speaker volume/mute**, **headphone volume/mute**, **mic and
  instrument mute**; ideally mic/instrument gain.
* Every other control clearly display-only on the real device (now the case) — documented as such.
* WP-C's corrections (mix mapping, DSP availability, persisted onboarding).

## Longer-term CONTROL CENTER parity

Input processing (low-cut, Clipguard, preamp, Comp) → monitoring (Dim, Mono, source, mix, mix-to-speakers) → LED
colours and custom button → gain lock and headphone/speaker modes → 48 V → autogain and Denoise measurement →
Autosetup → presets → firmware information. Firmware update and factory reset stay excluded.
