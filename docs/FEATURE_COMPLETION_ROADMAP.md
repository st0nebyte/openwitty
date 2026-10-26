# Feature completion roadmap (CONNECT 2) — Phase 10A audit, updated after Phase 10B

Audit of every GUI control against the code, the tests and the hardware evidence, as of the development branch
after Phase 10A (2026-10-25); Phase 10B changes (2026-10-26) are marked. Supersedes the status claims in older phase notes where they disagree; the evidence
itself is in `docs/HARDWARE_TEST_RESULTS.md`, `docs/VOLUME_INTEGRATION.md` and `PROTOCOL.md`.

**Bottom line:** OpenWitty reads and displays the CONNECT 2 well (validated live), and its desktop integration is
validated. **The first GUI write passed on 2026-10-26: LED brightness (HW-1, W-gui).** No other GUI control has written to the real device. Real writes so far went through the CLI (LED
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
| DSP unavailable at 96 kHz | `dsp.state` / `dsp.available` | sample rate | — | ✔ | rate from the kernel stream (10B) | — | — | — | fixed in 10B: real rate, unknown = unknown; live check SR-3 | P2 |
| Autogain | A → `trigger(autogainN)` | triggers 4, 1/2/3, 5; status 0x23/0x24 | **no trigger authorized** (Phase 10A fix) | ✔ workflow | R-dec | — | — | Med: device changes gain by itself; own trigger authorization | completion/result inference unverified | P3 |
| Autosetup wizard | Autosetup → preamp, Clipguard, Denoise, **48 V**, autogain | several + triggers | none (`can.autosetup` needs all of them) | ✔ reconstruction | — | — | — | **High** (bundles 48 V and a measurement) | exact step→field mapping, error codes unknown | P3 (last) |
| Selected-channel outline | read-only | 56 | — | ✔ | R-hw, R-gui | n/a | ✔ | — | — | done |
| Input meters | status 0x00/0x09 | status | — | ✔ | R-gui | n/a | ✔ | — | peak/clip flags 0x2E/0x30 not shown | P3 |

### Outputs

| Function | QML → backend | Field | Policy today | Mock | HW read | HW write | GUI e2e | Risk / test | Gap / defect | Prio |
|---|---|---|---|---|---|---|---|---|---|---|
| Speaker volume | `OutputStrip` Fader → `spk.volume` | 20–21 | *speaker control* (confirmed; ≤ 10 dB louder per step) | ✔ | R-hw, R-gui | W-cli (Phase 8C) + kernel path | write — | **Med**: audible level; decreases first | GUI path never exercised | P1 |
| Speaker mute | MUTE → `spk.mute` | 22 | *speaker control* | ✔ | R-hw, R-gui | W-cli (8C) | write — | Low–Med | — | P1 |
| Headphone volume | → `hp.volume` | 27–28 | *headphone control* | ✔ | R-hw, R-gui | **— never SET** | write — | **Med–High**: headphones on the head; first-ever SET; decreases first, headphones off | the first headphone SET would happen through the GUI | P1 |
| Headphone mute | → `hp.mute` | 29 | *headphone control* | ✔ | R-hw, R-gui | — | — | Med | — | P1 |
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
| LED brightness | `Fader_h` → `set.ledBrightness` (one SET per release, 10B.1) | 37 | *LED brightness* permission (10B) | ✔ | R-hw | **W-cli** (6C/6D), **W-gui** (HW-1) | ✔ write + restore (HW-1 PASS) | **Low** (cosmetic) | — | **P0** |
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

**Run on 2026-10-26: PASS** (`docs/HARDWARE_TEST_RESULTS.md` §19). Kept as the template for later GUI write steps. Each numbered step is done by the owner, or by the assistant with the owner present. No step
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

* HW-1 passed (2026-10-26). HW-2…HW-4: WP-B done (10B) — owner approval and the HW procedure remain.
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
| **D** (done 2026-10-26) | live validation HW-1 (LED via GUI) — PASS | owner-approved |
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
* Core audio verified with signal: four-channel capture identity and hardware loopback (LB-1, LB-2) and at least one
  DAW/JACK workflow (DAW-1…DAW-4); see "Official CONNECT 2 feature parity" below.

## Official CONNECT 2 feature parity (core project goal)

**Goal:** every official CONNECT 2 function works on Linux — audio streams, loopback, DAW use and the settings of
LEWITT CONTROL CENTER — without weakening the hardware-write safety rules above. Loopback, the four-channel capture
interface and DAW operation are **core functionality**, not extras.

**Primary references:** the official online guide (<https://www.lewitt-audio.com/support/online-guides/connect2>)
and product page (<https://www.lewitt-audio.com/connect-2>), read 2026-10-26; a summary of the guide is in
`docs/OFFICIAL_GUIDE.md`. Third-party projects are not a specification for this work.

### Additional evidence classes (audio)

| Class | Meaning |
|---|---|
| **A-sys** | observed read-only in the kernel's or PipeWire's view of the device (`/proc/asound/…/stream0`, `pactl list`), development system only; no audio passed |
| **A-live** | an audio test with a known signal confirmed it (recording/playback, channel identity, level) |
| **A-daw** | confirmed in the named DAW/JACK workflow |

Nothing in this section has class A-live or A-daw yet.

### What Linux sees today (A-sys, 2026-10-26, read-only)

Read from `/proc/asound/cardN/stream0` and `pactl list` without opening the mixer, starting a stream or changing
anything (kernel 7.3, PipeWire 1.6, WirePlumber 0.5):

| Item | Observation | Consequence |
|---|---|---|
| USB Audio Class | UAC2, high speed; playback interface 1, capture interface 2, both 24-bit in S32_LE, 44.1/48/96 kHz | class-compliant streaming needs no OpenWitty code |
| Playback | 2 channels, kernel map `FL FR` | matches the official "Outputs L/R" |
| Capture | **4 channels**; the input terminal is typed *Microphone* with **no channel positions** (`bmChannelConfig = 0`); the kernel shows its default map `FL FR FC LFE` (corrected in Phase 10D) | the surround labels are Linux defaults, not device semantics |
| Clocking | playback is asynchronous with **implicit feedback from the capture endpoint** (sync EP 0x82) | playback and capture share one clock and one rate; a rate change affects both directions |
| PipeWire default profile | `output:analog-stereo+input:analog-surround-40`; the source is "Analog Surround 4.0", map **`FL FR RL RR`** | PipeWire relabels the same four channels as 4.0 surround; neither label says microphone/instrument/loopback |
| Other profiles | analog-stereo out only, IEC958 variants, input-only, **Pro Audio**, off | Pro Audio would expose unlabelled `AUX0…AUX3` (not checked; profile not switched) |
| Source state | the 4.0 source is the default source and is **muted at its hardware route** (`analog-input-mic`, `route.hw-mute/hw-volume = true` → ALSA `Mic Capture Switch/Volume` = UAC2 Feature Unit 5); node software mute off (corrected in Phase 10D) | a muted source records silence; unmuting/moving it may act on the device (GAIN-1) |
| Graph rate | PipeWire `clock.rate 48000`, `clock.allowed-rates [ 48000 ]`; no stream open, so the device's current rate is unknown | PipeWire clients run the device at 48 kHz only under this configuration |

**Linux-specific risk (to verify, A-live):** a mono or stereo client recording from the 4.0 source gets PipeWire's
channel mix. With the 4.0 labels that can merge the microphone with loopback L and the instrument with loopback R
(rear → front downmix), or drop channels — exactly the silent merging this project must never make the default.
OpenWitty must not "fix" this by hiding channels: the full four-channel source stays the reference interface.

### Phase 10D — passive audio inventory (A-sys)

Full record: `docs/PHASE10D_PASSIVE_AUDIO_INVENTORY.md`. Summary: no stream open (device rate unknown); graph 48 kHz
with only 48 kHz allowed; capture terminal *Microphone*, 4 channels, no declared positions (kernel default `FL FR FC
LFE`, PipeWire `FL FR RL RR`); default profile `output:analog-stereo+input:analog-surround-40`; alternatives
declared but not activated (`pro-audio`, IEC958 variants that have no S/PDIF terminal in the descriptors, output- or
input-only, off); the default source's mute/volume are hardware controls bound to **Feature Unit 5** (0…+72 dB, mute on
channels 1–2; channels 3–4 reject writes); nothing proves FU 5 is the preamp gain.

**Phase 10E (offline):** the full control path, the kernel's broken-read-back handling and the GAIN-1 plan are in
`docs/PHASE10E_GAIN_CONTROL_PATH.md`. Established: the default source's mute/volume are FU 5 hardware controls with no
software mute/volume on top; the ALSA/PipeWire FU 5 values are the kernel's cache of its own last write, never the
device's gain; at every probe the kernel writes 0 dB to FU 5 (after test writes of 0/+72 dB). Likely: WirePlumber writes
+72 dB and mute at login; the loopback channels have no mute in this path. Unknown: whether FU 5 reaches the preamp or the
device mute; what External Gain Lock does. That plan supersedes the outline below. **GAIN-1 step 1 done (2026-10-26):** kernel cache +72 dB/muted vs device 27 dB/14 dB, muted — consistent with FU 5 volume not driving the preamp, causality unproven; step 3 revised so no request exceeds the device's reported gain. **Phase 10E.1:** the bound is the lower of the mic and instrument gains (FU 5 ch 2 likely = instrument, unproven); restoring the route to 1.0 (= +72 dB) is never assumed safe (restoration strategy R-A/R-B decided before the test); the `wpctl set-mute` path is traced in code and sends only FU 5 mute requests — step 2 is ready for separate approval.

**GAIN-1 proposal (outline from 10D; superseded by the Phase 10E plan):**
1. *Read-only correlation:* one vendor settings read through the OpenWitty window (it takes the device over from the
   service; `openwitty-cli read settings --interface auto` only if no OpenWitty process owns the device) plus the
   passive `usbmixer`/PipeWire state; compare mic/instrument gain and mute with FU 5 and the route. No writes.
2. *Single FU 5 mute test* (inputs quiet, monitoring off, speakers off, headphones off the head): toggle the PipeWire
   source mute once via `wpctl set-mute` and back; vendor read after each; expected evidence: whether bytes 2/12 follow.
3. *Single FU 5 volume step, decreasing only*: one small decrease of the source volume, vendor read, restore; abort on
   any increase or on monitoring.
4. *External Gain Lock (byte 57)* only after its write step: repeat 3 with the lock on.
5. *AGC observation*: a browser/WebRTC test page with AGC on, monitoring off, vendor reads before/after.
Pass/abort criteria as for HW steps; restore the original PipeWire route state exactly; record WirePlumber's
restore behaviour (`wpctl inspect` of the route before/after).

**First LB-1/LB-2 proposal (later, separately approved; device read-only, no settings writes):**
* Preconditions: speakers off or down, headphones off the head, device monitoring of the loopback **off**, nothing
  else playing; the PipeWire source unmuted **only if GAIN-1 step 2 has shown what that does** — otherwise record via
  the Pro Audio profile or a direct ALSA `hw:` capture after the owner approves the profile switch / PipeWire
  suspension (each a separate decision).
* LB-2 first (no physical input needed): play a 10 s file with an L-only 1 kHz tone then an R-only 2 kHz tone at
  −30 dBFS to the CONNECT 2 sink; record all four capture channels raw (4-channel WAV, no channel mixing); pass: tone
  1 only in ch 3, tone 2 only in ch 4, ch 1/2 at noise floor; record the level and the sample offset.
* LB-1: then a short spoken phrase into the mic and a DI signal into the instrument input (gains unchanged); pass:
  only ch 1 resp. ch 2 carry them.
* Evidence: the WAV files' per-channel RMS/peak (computed offline), PipeWire profile/route/rate before and after,
  settings comparison before/after (no change).

### Categories

**A. Native hardware/class functions that already work without OpenWitty** (OpenWitty must not reimplement them):
UAC2 playback and four-channel capture, the hardware loopback itself (the device mixes computer playback into
inputs 3/4), direct monitoring and the input/playback mix (device DSP), all front-panel controls, Panic Mute,
autogain started on the device, the kernel's desktop playback volume (with the 7.3 quirk, `docs/VOLUME_INTEGRATION.md`).

**B. Linux configuration/compatibility OpenWitty may provide** (opt-in, documented, reversible, never installed
silently): meaningful channel names for the four capture channels; optional extra mono "Microphone", mono
"Instrument" and stereo "Loopback" sources that coexist with the four-channel source; a profile/channel-map
recommendation for DAWs (likely Pro Audio); setup checks (source muted, wrong profile, rate mismatch, 96 kHz with
Denoise/Comp on); guidance for PipeWire rate configuration; the knob mirror (done).

**C. Proprietary settings OpenWitty must implement** (vendor protocol, write policy and HW steps above): gains,
mutes incl. loopback mute, low-cut, Clipguard, preamp, Denoise (+ measurement), Comp, 48 V, monitoring source,
mix, mix-to-speakers, Dim, Mono, high-volume speakers, high-impedance headphones, External Gain Lock, system-volume
mapping, LED brightness/colours, custom button, autogain/Autosetup triggers.

**D. Official features needing research before safe implementation:** mobile streaming mode (byte 67, semantics),
macOS streaming mode (trigger, reboot, PID change), the `OutputSource` value set (byte 23), the system-volume
mapping enum (byte 59), third-party gain control through UAC2 (Feature Unit 5) and its interaction with External
Gain Lock and with desktop input-volume sliders, Denoise/autogain measurement results, firmware version decode,
showcase mode, factory reset (excluded), firmware update (excluded).

### Feature-parity checklist

Columns: **Official behaviour** (guide/product page) · **OpenWitty today** · **Depends on** (K = kernel/UAC2, PW =
PipeWire/WirePlumber, V = vendor protocol byte/trigger) · **GUI needs** · **Evidence** · **Missing** · **Hardware
validation** · **Safety / compatibility**. Category = A/B/C/D above.

#### Audio streams, channels and loopback (core)

| Feature | Official behaviour | OpenWitty today | Depends on | GUI needs | Evidence | Missing | HW validation | Safety / compat | Cat |
|---|---|---|---|---|---|---|---|---|---|
| Four capture channels | In 1 mic, In 2 instrument, In 3/4 loopback L/R | not involved (kernel/PipeWire) | K, PW | show the map; warn on a muted source or a profile without the 4 channels | A-sys (4 ch present) | channel identity never verified with signal | **LB-1** | never hide or merge channels by default | A/B |
| Channel labels | (no Linux labels defined) | none | PW, K descriptor | names "Microphone, Instrument, Loopback L, Loopback R" | A-sys: kernel `FL FR FC LFE`, PipeWire `FL FR RL RR` | an opt-in naming/split configuration | LB-1, LB-5 | a wrong map can merge or drop channels in stereo clients | B |
| Stereo playback | Outputs L/R to speakers and headphones | not involved | K | — | A-sys | L/R identity with signal | LB-2 | level: start quiet | A |
| Hardware loopback | computer playback returned on In 3/4 | shows loopback meters + mute state | device DSP; V (mute) | loopback strip; feedback warning | R-gui meters; A-sys channels | captured content, L/R, gain, latency | **LB-2…LB-4** | do not monitor loopback (feedback) | A |
| Loopback mute | mute the loopback | display only | V byte 19 | write after HW step | R-dec | effect on captured In 3/4 vs on monitoring | LB-4 (+ write HW step) | unmute can start a feedback loop if monitored | C |
| Loopback vs PipeWire monitor | — (hardware feature) | none | device vs PW `.monitor` | explain the difference | A-sys: PW also offers a sink monitor | equality never measured | **LB-6** | never claim they are interchangeable until LB-6 passes | B |
| Simplified stereo / mono inputs | (not official) | none | PW virtual nodes | optional, labelled, coexisting | — | design + tests | LB-5 | must never replace the 4-ch source | B |
| Simultaneous record + play | full duplex | not involved | K (implicit feedback) | rate shown (10B) | A-sys (shared clock) | long-run stability | DAW-5, DAW-8 | one rate for both directions | A |

#### Monitoring and outputs

| Feature | Official behaviour | OpenWitty today | Depends on | GUI needs | Evidence | Missing | HW validation | Safety / compat | Cat |
|---|---|---|---|---|---|---|---|---|---|
| Direct monitoring source | mic, instrument or both | display (mock writes) | V byte 34 | two buttons, one byte | R-dec | write | HW-9 | feedback; speakers | C |
| Input/playback mix | full left 100 % direct … full right 100 % playback | raw value shown (10B) | V 35–36 | calibrated knob | R-hw (−100…0) | calibrated mapping, write | HW-9, MON-2 | level jump | C |
| Mix applies to speakers | speaker direct monitoring off by default | display | V byte 23 (enum) | switch | R-dec | value set | HW-10 | feedback through speakers | D→C |
| Duplicate monitoring | warning: not in DAW and device at once | — | — | hint in monitoring/DAW docs | — | — | MON-3 | doubled, delayed signal | B |
| Dim, Mono | reduce level; mono output | display | V 32 / 33 | buttons | R-dec | write | HW-9 | low | C |
| Speaker / headphone volume, mute | independent, hardware and software | **permission + confirmed write (10B)**; knob mirror | V 20–22 / 27–29; K | done | R-hw, W-cli | GUI write on hardware | HW-2…HW-4 | levels | C |
| System-volume mapping | "mapping of system volume controls" (product page) | not shown | V byte 59 (`NativeVolumeControlMapping`) | selector | R-dec (type name only) | value set | after RE | changes what the desktop slider controls | D |
| High-volume speakers / high-impedance headphones | modes (no details in the guide) | display | V 24 / 31 | confirmed switches | R-dec | write | HW-10/HW-11 | **much louder** | C |

#### Inputs, gain and processing

| Feature | Official behaviour | OpenWitty today | Depends on | GUI needs | Evidence | Missing | HW validation | Safety / compat | Cat |
|---|---|---|---|---|---|---|---|---|---|
| Mic gain 0–72 dB, instrument 0–60 dB | knob, software | display | V 0–1 / 10–11 | done | R-hw, R-gui | write | HW-6 | feedback/clipping | C |
| Third-party gain control | DAWs may change the gain | none | K: UAC2 Feature Unit 5 `Mic Capture Volume` (needs the 7.3 quirk; channels 3/4 stall) | explain; show External Gain Lock | Phase 8C (control exists) | whether desktop input-volume sliders or apps (WebRTC AGC) change hardware gain | **GAIN-1** | an app can silently change preamp gain | D |
| External Gain Lock | blocks unwanted gain changes by apps | display | V byte 57 | switch | R-dec | write; effect on FU5 | GAIN-1, HW-8 | — | C |
| Low-cut, Clipguard, preamp, Comp | DSP | display | V | done | R-dec | write | HW-8 | Comp/Denoise off at 96 kHz | C |
| Denoise | noise-floor measurement when enabled | byte only | V + triggers | measurement flow | R-dec | measurement | HW-13 | — | C/D |
| 48 V | phantom power | display | V byte 5 | confirmed | R-dec | write | HW-12 | ribbon mics | C |
| Autogain / Autosetup | device-guided measurement | display; triggers unauthorized | V triggers | wizard | R-dec | trigger authorization | HW-13/HW-14 | gain jumps | C/D |

#### Modes, rates, buttons, maintenance

| Feature | Official behaviour | OpenWitty today | Depends on | GUI needs | Evidence | Missing | HW validation | Safety / compat | Cat |
|---|---|---|---|---|---|---|---|---|---|
| Sample rate 44.1/48/96 kHz | selector in CONTROL CENTER | shows the open stream's rate (10B); selection simulation only | K, PW (the stream rate *is* the device rate) | rate display; guidance | A-sys (rates in descriptor) | per-rate validation | **SR-1…SR-3** | changing the rate interrupts every client | B |
| Denoise/Comp at 96 kHz | "not available when recording in 96 kHz" | enforced in the session; unknown rate = unknown | rate + V | disabled with reason | tests (`dsp.state`) | live check | SR-3 | — | C |
| Mobile streaming mode | mic+instrument mixed on In 1/2, loopback 3/4 | display only, disabled | V byte 67 | switch (later) | R-dec | semantics, routing evidence | **MODE-1** (research) | changes channel meaning for every app | D |
| macOS streaming mode | loopback on 1/2, mic/inst on 3/4; **device reboots** | refused on hardware | V trigger 0x104/0x105; PID 0x0004 → 0x0002 | switch with warning | PROTOCOL.md (binary) | Linux behaviour after re-enumeration (card name, quirk is keyed to PID 0x0004, PipeWire profile) | **MODE-2** (research) | reboot; the kernel quirk would not apply to the other PID | D |
| Custom button (8 functions) | user-chosen function | display; write prepared | V byte 53 | radio list | R-dec | write | HW-7 | low | C |
| LEDs, brightness, colours | settings | brightness permission (10B); colours display | V 37, 38–52 | done | W-cli (brightness) | GUI write | HW-1, HW-7 | cosmetic | C |
| Panic Mute | long-press MUTE; unmute each channel | read-only display | device | done | R-hw | — | done | — | A |
| Showcase mode | hidden lock (button sequence) | none | device | (display if detectable) | — | detection | later | — | D |
| Firmware update, factory reset | via CONTROL CENTER | **excluded** | V | — | — | — | — | bricking/settings loss | X |
| Drivers | Windows needs LEWITT ASIO; macOS/iOS class-compliant | Linux: kernel UAC2 + PipeWire/JACK | K, PW | — | — | — | — | Linux JACK/PipeWire support is **not** the Windows ASIO driver and must not be described as such | A |

### Loopback validation plan (later, each step separately approved)

Common: a known test signal (e.g. a 1 kHz sine at −30 dBFS, separate L-only and R-only files), outputs at a low
level, headphones off the head, **no monitoring of the loopback**, desktop sync settings unchanged, every step
records the PipeWire profile, source mute state, rate and `pw-top`/xrun counts before and after. Read-only for the
device: nothing in these steps writes CONNECT 2 settings.

| Step | What | Pass criteria (evidence A-live) |
|---|---|---|
| **LB-1** | Channel identity: tap/speak into the mic, play an instrument/DI signal, play L-only then R-only to the outputs; record all four channels raw (`pw-record --channels 4` / `arecord -c 4` on the PipeWire ALSA device) | signal appears only in ch 1 (mic), ch 2 (instrument), ch 3 (L-only) and ch 4 (R-only); no crosstalk above an agreed floor; channel order identical across restarts |
| **LB-2** | Desktop playback captured through loopback while mic/instrument are silent | ch 3/4 carry the playback; level and polarity consistent; ch 1/2 silent |
| **LB-3** | Loopback independent of the physical inputs: mic and instrument muted on the device, loopback still captured; then loopback silent while mic is live | independence in both directions |
| **LB-4** | Loopback mute (byte 19) — only after its own write HW step | ch 3/4 silent when muted; mic/instrument unaffected; outputs unaffected |
| **LB-5** | Mono and stereo clients (e.g. a browser call test, Audacity mono/stereo) against the default 4.0 source and against any opt-in split sources | documented exactly which channels each client hears; no client gets mic and loopback merged unless the user chose that |
| **LB-6** | Hardware loopback vs the PipeWire sink monitor of the same playback | latency, level and content compared; differences documented; no equivalence claim without this |
| **LB-7** | Feedback safety: confirm the GUI/docs warning; with outputs muted, verify that monitoring the loopback source in PipeWire would create a loop (by inspection of the graph, **not** by producing one) | warning shown wherever loopback can be monitored |

### DAW / professional-audio compatibility (later, each run separately approved)

Applications: **Ardour** (JACK/PipeWire-JACK), **REAPER** (ALSA and JACK/PipeWire-JACK), **Audacity** (PortAudio:
ALSA/PulseAudio/JACK), plus a plain **JACK-API workflow** (`pw-jack jack_lsp`/`jack_capture` or `jackd` on the ALSA
`hw:` device when PipeWire is stopped — the latter only with the owner's explicit approval because it stops the
desktop's audio). Backends per application: ALSA direct (`hw:`/`plughw:`), PipeWire native/Pulse, PipeWire-JACK, and
classic JACK where relevant.

| Step | Test | Pass criteria |
|---|---|---|
| **DAW-1** | Discovery | the CONNECT 2 appears with 4 inputs / 2 outputs (names recorded per backend) |
| **DAW-2** | Four-channel recording, one track per input | each track contains only its channel (LB-1 signals) |
| **DAW-3** | Individual track assignment (mic → track A, loopback L/R → stereo track B) | correct routing; stereo track keeps L/R |
| **DAW-4** | Stereo playback | L/R correct, no level change on the device |
| **DAW-5** | Simultaneous record + playback, overdub | no dropouts; recorded timing consistent |
| **DAW-6** | Loopback recording during DAW playback (re-record the DAW's own output) | expected content; documented as a feedback-prone setup |
| **DAW-7** | Latency/buffer: 64/128/256/512 frames at 48 kHz (and 96 kHz) | round-trip latency measured with a loop cable or the loopback, xruns per minute recorded |
| **DAW-8** | 60-minute recording | no xrun-induced gaps, file integrity, CPU noted |
| **DAW-9** | Recovery: provoked xrun (load), DAW restart, device unplug/replug (separately approved) | DAW reconnects or reports clearly; channel order and gains unchanged afterwards (vendor read-back) |
| **DAW-10** | No side effects | settings bytes before/after identical (in-window `--hw-check` compare, or a read with no other owner); OpenWitty's own GUI may stay open read-only |

### Sample rates and monitoring (read-only investigation first)

* **SR-1 (read-only, now possible):** record the stream rate the kernel reports while a client plays/records at 44.1,
  48 and 96 kHz — the client chooses; OpenWitty only displays. PipeWire's `default.clock.allowed-rates` decides
  whether the device follows a client's rate; OpenWitty documents it and never changes it silently.
* **SR-2:** each rate with LB-1 channel identity and DAW-7 latency.
* **SR-3:** at 96 kHz, Denoise/Comp are shown unavailable and the device reports them off or ignored (read-back).
* **MON-1:** direct monitoring source (mic/inst/both) vs DAW software monitoring — documented combinations.
* **MON-2:** input/playback mix calibration: device value −100…0 against measured monitor levels.
* **MON-3:** duplicate-monitoring warning (device + DAW) present in the docs/GUI.

No stream or rate is changed during planning; SR-2 onwards need approval because they interrupt audio.

### Streaming modes and external gain (research only; nothing switched)

* **MODE-1 mobile streaming (byte 67):** official: mic and instrument mixed onto inputs 1/2, loopback on 3/4.
  Before any write: decode whether 1/2 carry identical mono mixes or a stereo mix, and how the GUI should warn.
* **MODE-2 macOS streaming:** official: loopback on 1/2, mic/instrument on 3/4, the device reboots; per PROTOCOL.md the
  USB PID changes 0x0004 → 0x0002. Linux consequences to investigate offline first: the example kernel quirk is keyed to
  0x0004 (the udev `uaccess` rule covers the whole vendor ID); the PipeWire card/profile names may change; OpenWitty's device detection already knows both PIDs.
  Never switched without a dedicated plan and approval; returning needs the second trigger.
* **GAIN-1 third-party gain:** whether the desktop input-volume slider, PipeWire's source volume or apps with automatic
  gain control reach UAC2 Feature Unit 5 and change preamp gain; what External Gain Lock blocks. Read-only observation
  first (mixer contents with the 7.3 quirk), writes only as a separate HW step.

### Acceptance criteria and evidence

A parity feature counts as **done** only with: the official behaviour cited; the category; an automated test where
code is involved; the HW/LB/DAW/SR step passed with its evidence class recorded in `docs/HARDWARE_TEST_RESULTS.md`
(date, kernel, PipeWire/WirePlumber versions, profile, rate, application versions, signals used, before/after settings
comparison); and no unexpected settings change. Audio claims need A-live; DAW claims need A-daw for that application
and backend. Anything else is documented as "not validated".

### Recommended ordering

1. **HW-1** (LED via GUI) — **passed 2026-10-26**.
2. **Read-only audio inventory** — **done passively in Phase 10D** (SR-1 only as far as possible without a stream:
   rate unknown); GAIN-1 step 1 (read-only correlation) next.
3. **LB-1/LB-2** channel identity and loopback content (core; no device writes).
4. **B-category design**: channel naming and optional split sources as an opt-in configuration with tests (offline),
   then LB-5.
5. **DAW-1…DAW-4** in Audacity and one JACK workflow, then Ardour/REAPER; DAW-5…DAW-10 afterwards.
6. HW-2…HW-4 (outputs), then input mutes/gains (HW-5/HW-6) — needed for DAW gain workflows.
7. Monitoring writes (HW-9, MON-1/2), loopback mute (LB-4), SR-2/SR-3.
8. Research: MODE-1, MODE-2, byte 23/59 value sets, External Gain Lock effect.
9. High-risk items (HW-11…HW-14), then presets and the remaining CONTROL CENTER settings.

Firmware update and factory reset stay excluded.
