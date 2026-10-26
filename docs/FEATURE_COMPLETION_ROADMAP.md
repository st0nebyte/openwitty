# Feature completion roadmap (CONNECT 2) — Phase 10A audit, updated after Phase 10B

Audit of every GUI control against the code, the tests and the hardware evidence, as of the development branch
after Phase 10A (2026-10-25); Phase 10B changes (2026-10-26) are marked. Supersedes the status claims in older phase notes where they disagree; the evidence
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
| Real device, *speaker control* confirmed (10B) | speaker volume, speaker mute (bytes 20–22) |
| Real device, *headphone control* confirmed (10B) | headphone volume, headphone mute (bytes 27–29) |
| Real device, *LED brightness* confirmed (10B) | LED brightness (byte 37) |

## Phase 10B (offline, no hardware writes)

* **WP-B done.** Settings → Device control has three switches — speaker control, headphone control, LED
  brightness — each off by default, each with its own confirmation, each persisted (`speaker_control`,
  `headphone_control`, `led_brightness_control`). The session policy is `guiWritePolicy()`: exactly the union of
  the enabled scopes, no triggers; any change reconnects the session. Legacy `output_control = true` grants
  nothing (fail-closed) and only shows a reconfirmation notice. The switches had been below the visible area of the
  desktop page; the new section fixes that.
* **WP-C done (except the firmware-version decode).** Mix: the knob shows the raw device value ("device value N
  (of −100…0)") and explains that the scale is uncalibrated; out-of-range values are clamped for display; the
  setter still accepts only the simulator's −1…1 and is not authorized on the real device. Sample rate: read
  from the kernel (`/proc/asound/cardN/pcm0{p,c}/sub0/hw_params`, cross-checked with `stream0`), "unknown" while no
  stream is open or the sources disagree; the PipeWire graph rate is deliberately not used (it is not the device
  rate). `dsp.state` is available / unavailable / unknown from that rate. Onboarding and tooltips persist in
  `gui.conf`.
* **Defects fixed:** after a successful write `write.state` stayed "prepared" (now "verified"); a fractional
  `hw_params` rate was accepted as an integer (now requires an exact ratio).

## Phase 10B.1 (offline, no hardware writes): corrections before the first GUI write

An independent review of the HW-1 plan found three problems; all are fixed offline.

* **Competing device owner in the test plan.** The plan read the device with `openwitty-cli` while the window
  owned it (and omitted the CLI's `--interface auto`). Now all snapshots go through the window's own session:
  `openwitty --hw-check` (see the procedure). Tests show it uses the already-open transport (no second open, no
  write) and reports changed offsets without raw values. Also fixed: reopening the session after a permission
  change briefly released the control lease, so the background service could have taken the device in between;
  the window now asks the service to stay away before it reopens.
* **Many SETs per drag.** The LED slider sent a full 128-byte settings SET on press and on every pointer
  movement. On the real device it now previews while dragging and sends at most one SET per release, nothing for
  a release at the starting value or for value rebinding, and nothing while a previous change is being sent.
  Requests for one control that arrive while a write is in flight are merged into one. After a write that may have
  been sent but was not confirmed, the edit is discarded (never re-sent with a later change) and the session sends
  nothing more until the permission is turned off and on. Simulation keeps the live slider.
  Measured with fake hardware: a drag of 1,803 pointer moves → **1 SET**; 30 rapid clicks → **1 SET**; 100 requests
  during an in-flight write → **2 SETs** (the first and the latest).
* **Phase 10B.2 — late baseline after a hand-over.** The baseline was taken only right after the first connect
  attempt; when the service still held the device, the window took over later (reconnect) without recording
  one. Now the baseline comes from the first *successful* settings read (connect, reconnect after hand-over or a
  later poll), never from a failed read or a stale snapshot; it is kept across the permission reopen and every
  reconnect (logged as "baseline kept"); in `--hw-check` mode no device write is sent until it exists; a manual
  replacement is a two-step, logged owner action. Ordinary use without `--hw-check` is unchanged.
* **No stable-snapshot check in the GUI.** GUI writes on real hardware now use `requireStableSnapshot`: two
  matching reads immediately before the SET, otherwise nothing is sent ("Not sent"). Byte-37 authorization,
  the full-struct write guard, read-back verification and no-retry are unchanged; the background service's mirror
  path is unaffected.

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
| Input/playback mix | knob → `mon.mix` | 35–36, sign-inverted Q8.8 | none | ✔ (−1…1) | R-hw (−100…0), R-gui | — | read ✔ | Med: monitoring level | raw device value shown (10B); the setter still takes the simulator's −1…1 — a real-device write needs a calibrated mapping first | P2 |
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
| Sample rate | real device: rate of the open kernel stream, else "Unknown" (10B); selector simulation only | selection belongs to the desktop | done (display) |
| Firmware version | "not decoded yet" | decode `iface_version_t` | P2 |
| Firmware update, factory reset | disabled by policy (factory reset refused in code) | — | X |
| Presets (`.preset`) | not implemented | import/export format (shared with CONNECT 6) | P3 |
| Onboarding tour | shown once at first start, then on demand (10B) | — | done |
| Tooltips / footer hints | implemented; "Show tooltips" persisted (10B) | — | done |
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
| **HW-1** LED brightness via the GUI | 37 | *LED brightness* confirmed (since 10B), speaker/headphone control and vendor fallback off; window started with `--hw-check`; owner watches the LEDs, hands off the device | LEDs change; GUI shows "Verified" | in-window compare: `changed: 37 (LED brightness); unexpected: none` | any other offset; no visible change; error; not confirmed / not sent | original brightness, one separate deliberate action | cosmetic; no audio risk; procedure below |
| **HW-2** Speaker mute via the GUI | 22 | speakers turned down / amp off; *speaker control* confirmed, headphone control off | speaker muted / unmuted | byte 22 | anything else changes | original mute state | mute before unmute |
| **HW-3** Speaker volume via the GUI (decreases first) | 20–21 | as HW-2; desktop volume untouched | level follows the fader | bytes 20–21 | an increase beyond 10 dB; other bytes | original level with the knob | decreases only first; then one small increase |
| **HW-4** Headphone mute, then volume via the GUI | 29, then 27–28 | headphones **off the head**; *headphone control* confirmed, speaker control off; first-ever headphone SET | as above | bytes 29 / 27–28 | as above | original | decreases first |
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

### HW-1 procedure (LED brightness, first GUI write) — corrected in Phase 10B.1

**Not run yet.** Each numbered step is done by the owner, or by the assistant with the owner present. No step
sends anything to the device except step 6 and step 9, each a single deliberate GUI action.

**Why no `openwitty-cli` reads.** While the window is open it owns the device (exclusive control lease); a CLI
read would compete with it, and when the window closes the background service takes the device back. So every
before/after snapshot is taken **inside the window**, through its own session and serialized transport, with
the opt-in `openwitty --hw-check`: it records a baseline when the device connects, logs every GUI write with
its outcome, and on request ("Compare with baseline", Settings → Device control) does one ordinary settings GET
and reports which offsets differ from the baseline. It prints field names, offsets and the LED brightness value
only — no device identifiers, no raw settings. It cannot write (audit rule; no session or transport access in
`core/SettingsComparison`).

1. **Revision and configuration.** Record `git rev-parse HEAD` of the build under test. Record, read-only:
   `grep -E '^(desktop_volume_sync|vendor_fallback|speaker_control|headphone_control|led_brightness_control|output_control)' ~/.config/openwitty/openwitty.conf`,
   `systemctl --user is-enabled openwitty-volumed.service; systemctl --user is-active openwitty-volumed.service`,
   and whether `~/.config/autostart/io.github.st0nebyte.openwitty-tray.desktop` exists.
2. **Preconditions.** Speaker control, headphone control and vendor fallback must be off (missing keys = off; the
   legacy `output_control` grants nothing). Nothing is played; the owner does not touch the device (knob, buttons)
   from step 4 to step 10 — any physical change would show up as an unexpected offset.
3. **Install — only after a separate, explicit approval.** Close the OpenWitty window first. Install the build;
   confirm with `sha256sum` that the installed `openwitty` matches the build output and that
   `openwitty --help` lists `--hw-check`. The background service and tray may keep running (the service hands
   the device to the window and takes it back afterwards).
4. **Baseline.** Start the window from a terminal: `openwitty --hw-check --show settings-device 2>&1 | tee hw1.log`.
   While the background service still holds the device, the log says `no baseline yet (connect failed: … in use …)`
   and the page "No baseline yet"; that is the normal hand-over. Within a few seconds expect
   `hw-check: baseline recorded from the first successful settings read (connect|reconnect): LED brightness N`
   (N = the original value; the page shows it). If "no baseline yet" persists for more than about 30 s: **stop**.
   Without a baseline the window refuses every device write in this mode. Then expect the banner
   "CONTROL LINK, READ-ONLY" and, on the page, "Currently allowed: nothing (display only)". Press **Compare with
   baseline**: expect `changed: none; unexpected: none`.
5. **Enable LED brightness only**, through the switch and its confirmation dialog. The window reconnects (it keeps
   the device: the service is asked to stay away during the reopen). Expect
   `connected again (connect)` (or `(reconnect)`) `; baseline kept: LED brightness N` — the same N as in step 4; a reconnect never
   replaces the reference. Banner: "REAL DEVICE — can change: LED brightness. All other controls are
   display-only." Press **Compare with baseline**: still `changed: none`.
6. **One deliberate change.** Settings → LEDs: drag the brightness slider to a clearly different value and
   release. Dragging only previews ("release to send"); the release sends **one** settings SET at most; the slider
   is disabled until the result is shown.
7. **Verify.** Status line "Verified: the device reports M %"; the LEDs visibly change; the log shows
   `write set.ledBrightness: verified; SET sent: yes` and `since baseline: LED brightness N -> M; changed: 37 (LED brightness); unexpected: none`.
8. **Only byte 37.** Press **Compare with baseline**: exactly `changed: 37 (LED brightness); unexpected: none`.
9. **Restore** with a second, separate deliberate action: drag back to N and release. Expect "Verified" again.
10. **Verify restoration**: **Compare with baseline** → `LED brightness N -> N; changed: none; unexpected: none`.
    Then turn **LED brightness off** (no confirmation needed) and close the window.
11. **Afterwards**: repeat the read-only checks of step 1. Desktop sync, vendor fallback, the service state and the
    tray entry must be as recorded. The settings file is rewritten by the switch (the obsolete `output_control`
    line disappears); `led_brightness_control` must read `false` or be absent.

**Do not use "Replace baseline…"** during HW-1. Replacing needs a second confirmation and is logged as
`baseline REPLACED by owner action (#k)`; if it appears in the log, the run is void — stop and start again.

**On a timeout, an ambiguous or "Not confirmed" result, an unexpected offset, a USB error or anything
unexpected: stop.** Do not retry the SET and do not "restore" by sending another SET. OpenWitty itself stops
sending in that session (the control is disabled, "Device writes stopped"). Inspect the device only through the
open window (**Compare with baseline**, which reads), record the report and the log, and ask the owner for a new
decision. A "Not sent" result (the device changed while the write was being prepared) means nothing reached
the device; it is still a stop for HW-1.

**Remaining race (acknowledged).** The device's settings SET carries the whole 128-byte struct. OpenWitty reads
the settings twice immediately before the SET and refuses to send unless both reads match, then verifies by
read-back. That narrows the window in which a physical change on the device could be overwritten by the SET; it
cannot close it — a change landing between the second read and the SET's arrival would be overwritten and then
reported as Ambiguous at best. This is why the owner keeps hands off the device during the test.

## Dependencies

* HW-1…HW-4: WP-B done (10B) — only owner approval and the HW procedure remain.
* HW-9 (mix) needs a calibrated mix mapping for writes (WP-C showed the raw value only). HW-10 (mix-to-speakers) needs the `OutputSource` value set (RE).
* HW-13 needs per-trigger authorization (exists since 10A) plus a verified autogain completion/result model.
* HW-14 needs every component step. Denoise as the official feature needs HW-13's denoise measurement.
* Presets need a stable set of validated writes.

## Work packages

| WP | Content | Hardware |
|---|---|---|
| **A** (done in 10A) | per-control capabilities, trigger authorization, refused edits not staged, banner | none |
| **B** (done in 10B) | narrow GUI authorizations: a confirmed *LED control* (byte 37, later 38–52/53) and splitting *Speaker/headphone control* into speaker and headphone; each persisted in the settings file, off by default, own confirmation; tests | none |
| **C** (done in 10B, except the firmware-version decode) | offline corrections: mix mapping (−100…0), `dsp.available` from the real rate (unknown → hint), sample-rate display from PipeWire/ALSA, onboarding/tooltips persistence, firmware-version decode attempt | none |
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
* WP-C's corrections (mix display, DSP availability, persisted onboarding) — done in 10B.

## Longer-term CONTROL CENTER parity

Input processing (low-cut, Clipguard, preamp, Comp) → monitoring (Dim, Mono, source, mix, mix-to-speakers) → LED
colours and custom button → gain lock and headphone/speaker modes → 48 V → autogain and Denoise measurement →
Autosetup → presets → firmware information. Firmware update and factory reset stay excluded.
