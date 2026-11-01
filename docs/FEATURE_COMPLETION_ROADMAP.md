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

## Phase 12 — Linux audio integration (binding owner spec, agreed 2026-10-31)

The original reason for OpenWitty. Agreed in the owner's earlier planning chat (Phase 8 there) and confirmed again on
2026-10-31; the owner asked to be told whenever a wish does not fit the implementation.

**Requirements (owner decisions):**
1. **One Linux playback device.** The CONNECT 2 is one sink; which outputs are audible is decided only by the device's
   speaker / headphone mutes (and the custom button). *Status: in place.*
2. **Linux volume = hardware volume, always.** Keyboard keys, KDE slider and tray scroll change the selected output's
   hardware volume directly; no second software level ("speaker 20 %, Linux 100 %" must never happen). The touch
   wheel's changes show up in KDE (bidirectional, no feedback loop). *Status: speakers only.*
3. **Mute key = hardware mute of the target output**, keeping its volume (unmute returns to the same hardware level).
   With both outputs on and speakers as target, only the speakers mute. *Status: speakers only.*
4. **Volume target.** Official model: "Map Native Volume Controls To: None / Speakers / Headphones" (seen on the
   CONNECT 6; on the CONNECT 2 most likely byte 59 `MasterSliderMap` / `NativeVolumeControlMapping`). OpenWitty adds
   **Follow active output** (Settings ▸ Audio): only speakers on → speakers; only headphones on → headphones;
   *both on* → setting (Speakers / Headphones / Keep last; default Speakers); *both muted* → setting (Keep last /
   Speakers / Headphones / Disable until an output is unmuted; default Keep last). Linux mute is never a selection input.
   Before each volume command the target is decided from a fresh hardware state; if the state is unclear, nothing is
   done.
5. **Extended custom-button modes** (OpenWitty extensions on top of the native functions):
   "Switch outputs + follow volume" (native: toggle output mutes) and "Switch outputs + mic + follow volume" (native:
   toggle output mutes + mic). While one is selected, *Follow active output* is forced on and greyed out with a short
   note why. The previous manual value comes back when another button function is chosen.
6. **Persistence per device (not per model).** At start OpenWitty reads the native button function first. If an
   extended mode was active for this interface before: when the native base function still matches, the follow part
   is re-activated (switching to the extended mode); when the hardware holds another function (e.g. "Show CONTROL
   CENTER"), the extension is **not** activated. Volumes are never restored at start.
7. **Hardware is the source of truth.** At start and after every reconnect: read, adopt, then decide; never write saved
   software levels; no level increase on switching or reconnect; after a USB loss no queued commands are replayed.
   Only the selected CONNECT 2 is affected.
8. **Service + tray.** One background process owns the device; the GUI talks to it. Tray click opens the window;
   X hides to the tray ("Minimize to tray on close"); "Start minimized to tray"; an explicit Quit stops it cleanly; the
   keys work with the window closed. *Status: tray and service exist; window and service still hand the device over;
   close/start-to-tray options missing.*
9. **Volume safety:** optional maximum volume (especially headphones; the dB value must be defined, not a percentage).

**12A result (2026-10-31):** byte 59 = "Map native volume controls to:" with **0 None, 1 Speakers, 2 Headphones**
(Windows 5.2.2: the dropdown's item list and the setter, which stores the value unchanged). The owner's device reads
**1** (speakers) — which is why the keyboard and KDE control only the speakers today. So the device itself can route the
native (kernel/PipeWire) volume to the headphones; "follow active output" = OpenWitty switching byte 59. Also:
`openwitty-cli` now uses the vendor control interface by default (`--interface auto`), the AudioControl interface
answers GETs with an IO error.

**12B result (2026-10-31, PASS with two findings):** byte 59 1 → 2 via the GUI (*routing*); one keyboard "volume down"
changed **only the headphone volume** (−32.5 → −37.0 dB, bytes 27/28), speakers untouched — the device routes the
native volume to the mapped output, exactly what the spec needs. Findings: (1) the mute key toggles the native mute,
which KDE tracked as "unmuted" while the headphones were muted, so the second press **unmuted the headphones** —
when the target changes, KDE must first be aligned to that output's level *and* mute; (2) OpenWitty's desktop link is
speaker-only: with the mapping on headphones it looped (~5 log lines/s, no device writes) until the mapping was back on
speakers. Both are 12C work. Restored exactly (compare: changed none); permissions off.

**12C step 1 (2026-10-31):** the desktop link follows byte 59: it mirrors the mapped output (speakers or headphones),
re-targets on a change by first showing that output's level *and* mute in KDE (no device write; an ALSA write reaches the
new output with the values it already has), and stays idle with "none". Tests
`DesktopLink.FollowsTheNativeVolumeMappingWithLevelAndMute`, `DesktopLink.NoneMappingLeavesEverythingAlone`. The
daemon's vendor fallback stays speaker-only by policy.

**12C step 2 (2026-10-31, offline):** "Follow active output" = permission `follow_active_output` (confirmed) + config
`follow_both_on` (speakers|headphones|keep) / `follow_both_muted` (keep|speakers|headphones|disable). Rule function
`followMapping()` and executor `OutputFollower` (core): one verified SET of byte 59 per state, never retried, never on top
of another unconfirmed edit. Runs in the background service (policy: byte 59 via `withFollowOutput()`, poll 250 ms while
on; audit-pinned) and in the window while it owns the device (GUI gates: no write before the hw-check baseline or after an
unconfirmed write). Needs desktop volume sync (the service only runs with it). The manual mapping buttons are locked
while it is on. Tests `OutputFollow.*`. Live test pending.

**12C live test (2026-10-31, PASS):** follow on, window closed (service). Custom button → headphones: mapping
speakers → headphones verified 22 ms after the mute change was read, KDE showed headphones −32.5 dB; volume keys moved
**only the headphones** (−32.5 → −60 dB); custom button → speakers: mapping back, KDE showed speakers −40.5 dB.
Fixed afterwards: the follower now runs before the link (no brief mirror of the old output's mute) and the log says
"device output" instead of "device speaker".
**Owner wish (2026-10-31):** the custom button should also move the touch-wheel focus (byte 56: 3 speakers,
4 headphones). The CONNECT 2 app never writes byte 56 (its only setters belong to the CONNECT 6 tree), so whether the
device accepts a host write is unknown → one hardware test before implementing. **Tested 2026-10-31, PASS:** the
device keeps a host write of byte 56 — custom button → mapping and focus to headphones (each verified, ~21 ms apart),
the touch surface showed the headphones selected; back to speakers the same. Option `follow_move_focus` (default on).

**12D (2026-10-31, offline):** extended custom-button modes "toggle mute: headphone / speakers + follow volume" (base
1) and "… + mic …" (base 2). Stored per device as `button_follow = <key>:<base>`; key = FNV-1a 64 of the USB serial
(never the serial). Follow is active when the manual switch is on, or when this device has an extended mode **and**
still holds that base function (another function, e.g. "Show CONTROL CENTER", leaves it inactive). Choosing it asks
once (it authorizes the follow writes 59/56 for the service and the window); choosing any other function removes it
for this device; the manual switch keeps its own value. In the GUI the follow switch is then on, greyed out, with a
note. Tests `ButtonFollow.*`. **Live test PASS (2026-10-31)** with the manual switch off: the service followed the
custom button (mapping + focus verified); the window first opened read-only (an extended mode alone did not count as a
write authorization; fixed in 630f725), afterwards the window followed too (focus moved to the headphones).

**Owner decisions (2026-10-31):** 12E stays as built — X closes the window while the tray icon and the background
service keep running (keys, follow and focus keep working; the tray reopens the window); "start minimized" = the tray
autostart (Desktop integration). **No volume limit** (requirement 9 dropped). Phase 12 is complete apart from the
later single device-owning service — **done 2026-11-01** (see below).

**Plan:** 12A decode byte 59 (binary + read-only device value) → 12B hardware test: byte 59 = headphones, KDE volume
moves the headphone level (headphones unplugged, low levels) → 12C follow active output in the service with both
special-case settings + volume limit → 12D extended custom-button modes with per-device memory → 12E tray behaviour
(close/start to tray), later one device-owning service with the GUI as its client.

**Single device owner (2026-11-01):** `openwitty-volumed` keeps the CONNECT 2 open and serves the window and the CLI over a
private Unix socket (`transport/DeviceService`: transport calls 1:1, so every client-side check stays; the service
re-checks each write: controller only, write guard, permitted bytes against a fresh read, permitted triggers -
`serviceClientPolicy`). One controller at a time (the window or a CLI write); while it is connected the service pauses
its own desktop link and follower but keeps the device; readers (CLI reads) work alongside. Without the service the
clients open USB directly (`ServiceOrDirectTransport`). Tests `DeviceService.*`; audit rule added. Live (read-only):
CLI read in 8 ms while the service runs; the window took control and the service paused; on closing the service
resumed with a read-only start.

## Phase 11 — fully working private version (owner mandate, 2026-10-27)

Goal: every officially documented CONNECT 2 function that the protocol work supports, usable from OpenWitty on Linux, each
validated on hardware before it is called working; safety rules unchanged (fail-closed permissions, one SET per deliberate
change, read-back, no automatic retries, physical safety confirmed by the owner for anything audible).

**11A — offline**
1. *Done:* faders, gain knobs and the mix knob commit once per release on the real device (they wrote on every pointer
   movement and wheel notch — the 10B.1 fix had covered only the LED slider); wheel bursts commit once after 400 ms; controls
   are disabled while their write is in flight (`inflight.<key>`). Mix codec on the device scale −100…0 everywhere.
2. *Done:* grouped, separately confirmed permissions for all supported settings: inputs (gain, mute, gain lock), processing (low-cut,
   Clipguard, preamp, Comp, Denoise flag), monitoring (source, mix, Dim, Mono, loopback mute), LEDs and custom button;
   own permissions with per-action confirmation for 48 V, high-volume speakers, high-impedance headphones; a measurement
   permission for autogain triggers. Gain increases bounded per SET.
3. *Done:* `--hw-check` judges changed offsets against the footprints of the controls written since the baseline.
4. Opt-in Linux configuration: named inputs (microphone, instrument, loopback L/R) and optional split sources on top of the
   four-channel source — example files, never installed automatically.
5. Stays display-only until researched: mix-to-speakers (byte 23 value set), mobile and macOS streaming modes; excluded by
   decision: firmware update, factory reset.

**11B — hardware validation with the owner present** (HW procedure template from HW-1): session 1 low-risk settings
(LED colours, custom button, Dim/Mono with outputs off, input mutes, low-cut/Clipguard/preamp/Comp, gain lock, monitoring
source, gain decreases); session 2 outputs (HW-2…HW-4); session 3 audio (LB-1/LB-2, then DAW-1…DAW-4); session 4 high-risk
(48 V, high-volume, high-impedance, autogain).

### Phase 11 status and remaining plan (as of 2026-10-28, end of day)

**Done and hardware-verified:** 11A.1–11A.3; 11B session 1 (all low-risk settings), session 2 (HW-2…HW-4 outputs),
session 3 part 1/2 (LB-2 loopback, LB-1 input channel identity); 11C (desktop input mute that works, installed on the
owner's system). Fixes found on hardware: overlay click-through (`dbc51c3`), device-maintained bytes 25/26 (`1b3d5e5`,
`d8ef700`), knob/fader flash and LED row descriptions (`1a192ef`), fader dB readout (`c4a7ecf`), desktop-mute warning and
detection (`ce3608e`, `4977413`). Results in the sections below and in `PROTOCOL.md` ("Hardware observations, Phase 11B").

**Owner system state after 2026-10-28:** OpenWitty installed user-local from `4977413`; all device permissions off; 11C
files installed in `~/.config/alsa-card-profile/mixer/{profile-sets,paths}/` and
`~/.config/wireplumber/wireplumber.conf.d/52-openwitty-connect2-input.conf` (undo: delete, restart WirePlumber); the
input route is now `openwitty-connect2-input` (software mute/volume, unmuted, 1.0). Device settings equal the session
baselines except the headphone volume (−32.5 dB instead of −32.398 dB, agreed). Steam was quit for the tests.

**Remaining plan, in order** (every hardware step: owner present, safety confirmed right before, one action at a time,
`--hw-check` window, restore, final compare):

1. **DAW-1…DAW-4 (rest of session 3) — done 2026-10-29 with option A (PASS, below).** Remaining: option B
   (`pipewire-jack`: *installed 2026-10-30 on the owner's system, owner decision — replaced `jack2`, libjack now from
   PipeWire; undo `sudo pacman -S jack2`; DAW test with it pending*) and DAW-5… (latency, xruns, long run). Original plan:
   *A (recommended start)* Ardour 9's ALSA backend — no system change; PipeWire releases the card through device
   reservation while Ardour runs (desktop audio on the CONNECT 2 pauses). *B* replace `jack2` with `pipewire-jack`
   (package swap with sudo; Ardour and desktop audio side by side). Procedure: DAW-1 record the port names per backend;
   DAW-2 four mono tracks (mic speech, instrument cable hum, loopback tone L/R via `tools/audio/`); DAW-3 mic → mono
   track, loopback → one stereo track; DAW-4 stereo playback L/R (speakers muted, meters/loopback as evidence);
   DAW-10 compare changed: none. Then DAW-5… (latency, xruns, long run) as listed in the parity section.
2. **11A.4 channel naming / split inputs (opt-in) — done 2026-10-29, live test 11A.4-1 PASS (below).**
   Done: `src/desktop/SplitSources` (config for three loopback-module sources: Microphone ← FL/ch 1, Instrument ←
   FR/ch 2, Loopback ← RL RR/ch 3–4; `node.passive`; node name validated), Settings ▸ Desktop integration switch +
   confirmed "Restart desktop audio", state off/on/stale/unavailable; the 11C WirePlumber rule names the 4-channel input
   "CONNECT 2 (1 Mic, 2 Instrument, 3+4 Loopback)" (positions unchanged). Tests `SplitSources.*`,
   `SplitSources11A4.*`. **Live test 11A.4-1** (owner present, usual safety): copy the updated 52 rule, switch on,
   restart audio once; check the three nodes and the description; record each virtual source with `pw-record` while
   the owner unmutes mic, then instrument (device mute), and a loopback tone plays (loopback unmuted): Microphone carries
   only speech, Instrument only the hum, Loopback L/R the tones; desktop mute of the main input silences all three;
   restore, final compare. Earlier idea for reference: Now that the channel order is verified: either name the four
   capture channels in the 11C profile set (e.g. AUX0–3 instead of the surround labels) and/or offer optional virtual
   sources "CONNECT 2 Microphone" (ch 1), "Instrument" (ch 2), "Loopback" (ch 3/4) via a PipeWire loopback module
   example. Then LB-5 (how mono/stereo apps downmix the 4-channel source) with signal. Offline first, then one live test.
3. **Session 4 — high-risk controls: procedure below ("Phase 11B session 4"), prepared offline 2026-10-29.** Original note: 48 V (dynamic mic or nothing
   connected — note: 48 V was **on** at every baseline so far, i.e. the owner's mic needs it; a test turns it off and on
   again), high-impedance headphones and high-volume speakers (outputs physically off, headphones unplugged), autogain
   (measurement permission; device changes the gain itself — expected-change handling like bytes 25/26 may be needed).
4. **Gain lock (byte 57) polarity.** Low risk now (11C means the desktop no longer writes FU 5): toggle in a session,
   observe what changes (official app behaviour, FU 5 interrupts), restore.
5. **Research items (offline RE before any write):** Denoise measurement — *prepared as a hardware observation,
   session 5 below* (no disassembler on this system; the bracket is known from the binary);
   mix-to-speakers `OutputSource` value set (byte 23), mobile/macOS streaming, custom-button value↔function map
   (2 and 12 known), native volume mapping (byte 59).
6. **Small GUI items:** *done 2026-10-29:* Shift + wheel/drag moves the output faders in 0.1 dB steps (test
   `Controls11B.ShiftWheelGivesFineFaderSteps` restores −32.4 dB = bytes 154/223 exactly); show "set by the device" bytes in `--hw-check` output (done) and in diagnostics (done 2026-10-30: `openwitty-cli read settings` prints dim/mono state and the Denoise levels 8/17).
7. **Obsolete / closed:** GAIN-1 steps 3–5 (FU 5 volume requests) — with 11C nothing writes FU 5 any more; kept as
   history in `docs/PHASE10E_GAIN_CONTROL_PATH.md`.
8. **Still open, owner decisions:** login-screen WirePlumber rule live test — *done 2026-11-01, PASS on Plasma
   Login*; GitHub `refs/pull/1..3` privacy
   blocker (first name in 18 commits); no merge to `main`, no tag, no release, repo stays private. The background
   service picks up the new binary at the next login.

### Phase 11B session 4 — high-risk controls (procedure, prepared 2026-10-29)

Offline preparation done: autogain asks before every measurement on the real device (`autogainConfirm<N>`); a
measurement that does not report completion within 45 s gets the end-session word once (`auto.state = timeout`, no
retry); `--hw-check` logs each trigger and the measurement result and treats the gain bytes of the measured input as
expected to change after an autogain trigger. Tests `Measurement11B.*`.

General: owner present; safety confirmed right before; `openwitty --hw-check`; one permission at a time (*inputs* for
gain lock, *output modes*, *48 V*, *measurements*); one GUI action per step; log line checked before the restore;
final compare; abort on an unexpected byte, "not confirmed", an error, or any audible event; no retries. A device byte
that follows the written one (as 25/26 for Dim/Mono) stops the session as "not confirmed" — restore with one deliberate
action after re-arming the permission, record it, teach OpenWitty offline, then continue.

| # | Control | Conditions | Action → expected | Restore | Extra observation |
|---|---|---|---|---|---|
| 4a | Gain lock (57, *inputs*) | standard | 1 → 0 (byte 57) | 0 → 1 | `alsactl monitor` on the card and the PipeWire input route: any FU 5 event while unlocked (11C: nothing writes FU 5) |
| 4b | High-impedance headphones (31, *output modes*, per-use dialog) | headphones **unplugged** | 0 → 1 | 1 → 0 | followers (byte 30?) |
| 4c | High-volume speakers (24, *output modes*, per-use dialog) | speakers **powered off** | 0 → 1 | 1 → 0 | followers |
| 4d | 48 V (5, *48 V*, per-use dialog) | mic muted on the device; speakers off, headphones off | 1 → 0 (pop possible) | 0 → 1 via the dialog; wait 10 s before any unmute (condenser recharge) | none expected |
| 4e | Autogain mic (*measurements*, per-use dialog) | mic unmuted only for the measurement; speakers off, headphones off; owner speaks at normal level ~10 s | trigger 4 → 1; progress → 100; result code; the device sets gain bytes 0–1 | gain back to the start value with the knob (≤ 6 dB per SET), mic muted | result code meaning (1 failed / 2 succeeded), measured gain, whether the device unmutes or changes anything else |

Not in session 4: instrument autogain (no real instrument; a cable hum is not a usable signal), Denoise measurement
and Autosetup (need research first), mix-to-speakers, streaming modes.

### Phase 11B session 7 — sample rates, JACK latency, long run (2026-10-30, PASS)

Owner present, safety confirmed; "Allow 44.1 and 96 kHz" on (PipeWire allows [ 44100 48000 96000 ]); instrument
unmuted (open-cable hum as the ch 2 signal), loopback unmuted for the test, mic muted, speakers off.

| # | Result |
|---|---|
| SR-1 | The device follows the client rate: kernel hw_params 48000 → 44100 → 96000 → 48000 for playback and capture (`pw-play`/`pw-record` with `node.rate`), when the device is idle between rates |
| SR-2 | Channel identity at every rate: ch 1 digital zero (muted), ch 2 hum −44 dBFS, ch 3 1 kHz only, ch 4 2 kHz only (−38 dBFS RMS, unity loopback) |
| SR-3 | At 96 kHz OpenWitty shows "96 kHz" and greys out Denoise/Comp; the tip now stays readable on hover (fixed) |
| DAW-7 | Round trip through the loopback via `pipewire-jack` + `jack_iodelay`, 48 kHz: 64 frames **8.7 ms**, 128 frames 12.1–13.5 ms, 256 frames 23.4 ms; no xruns |
| long run | 30 min at 128 frames: 7164 readings, 12.0–13.5 ms (mostly 12.08), no USB or PipeWire errors |
| USB | `usb_set_interface failed (-110)` + PipeWire `set_hw_params` timeout twice in ~30 stream starts (a rate switch right after another stream, and a jack_iodelay start); PipeWire recovers (nodes recreated); 10/10 clean starts in a repeat test. Cause open (concurrent control polling is one hypothesis) |

Open: the Ardour GUI with its JACK backend (headless `ardour9-lua` cannot instantiate it).

**USB timeout follow-up (2026-10-31, silent playback only, no audible signal):** 40 starts with the background
service (1 Hz polling), 40 without it, 40 with the window polling at 48 Hz: 120/120 clean, no timeout — polling is
**not** the cause. Then 30 starts alternating 96 / 44.1 / 48 kHz (device idle between them): **0/30 ran, 29 timeouts**;
afterwards the capture interface (2:1) timed out every ~23 s and playback no longer started — the CONNECT 2's USB audio
was hung until unplugged (owner replugged; everything worked again). Conclusion: repeated sample-rate changes can hang
the device's audio firmware (control still answered after a short timeout). Consequences: the rate option is marked
experimental with a warning; rate changes should be rare and with the device idle; a mitigation (e.g. one rate per
session, or a cool-down) and a report to LEWITT are open.

### Phase 11B session 6 — next hardware session (prepared 2026-10-30)

Research done offline: Windows 5.2.2 binary in Ghidra (PROTOCOL.md "Windows 5.2.2 cross-check"); `pipewire-jack`
replaced `jack2` on the owner's system (owner decision). Same safety rules as sessions 1–5.

| # | Test | Conditions | Observe | Restore |
|---|---|---|---|---|
| 6a | Noise-floor scale: record ch 1 (mic unmuted ~10 s, Denoise off, quiet room) and ch 2 (open cable) | speakers off, headphones unplugged | RMS dBFS vs bytes 8/17 read as int8 dB (−60 / −41?) | mutes |
| 6b | Mix to speakers on/off (*routing*, dialog) | speakers **off**, mic muted | byte 23 0 → 1 → 0, followers | off |
| 6c | Mobile streaming on/off (*routing*, dialog) | as 6b; record ch 1–4 for a few seconds with the instrument (open cable) unmuted | byte 67, what ch 1/2 carry (mix?), USB re-enumeration (none expected) | off |
| 6d | DAW with `pipewire-jack`: Ardour JACK backend, port names, 4-ch record, side by side with desktop audio | as DAW-1…4 | ports, xruns | close Ardour |
| 6e | DAW-5 latency (loopback round trip, `jack_iodelay`-style) and a 30 min long run | speakers off | latency, xruns | — |

If 6a confirms the scale: a "measured noise floor" marker in the input meters (meter range extended below −60 dB).

**Results (2026-10-30):**
* **6a PASS — scale confirmed:** bytes 8/17 = int8 dBFS ≈ the **peak** level of the background noise. Mic, phone
  noise: recording RMS −44 / peak −30 dBFS, device meter −36, stored 228 = **−28**. Mic, quiet room (same day):
  RMS −68.5 / peak −51, stored 203 = **−53**. Instrument, open cable: RMS −43.5 / peak −39.9, stored 215 = **−41**.
  Result code 0 every time. Next: noise-floor marker in the meters.
* **6b PASS:** mix to speakers byte 23 0 → 1 → 0 via the GUI (*routing*, dialog), no followers.
* **6c PASS:** mobile streaming byte 67 0 → 1 → 0; while on, capture ch 1 **and** ch 2 both carry the mic +
  instrument mix (instrument hum −43.6 dBFS on both, mic muted), ch 3/4 unchanged; no USB re-enumeration.
* **6d partial:** `pipewire-jack` works for JACK clients (ffmpeg JACK client, 4 ports linked to the CONNECT 2 capture
  ports with `pw-link`, 15 s recorded without errors; all channels digital zero as inputs were muted and nothing played).
  Headless `ardour9-lua` cannot instantiate its JACK backend ("shared_ptr is nil", also via `pw-jack`); the Ardour GUI
  is untested. `tools/audio/daw_ardour_test.sh` got `DAW_BACKEND`/`DAW_LIST_ONLY`/prefix options for that test.
* 6e (latency, long run) open.
* Noise-floor marker built (blue line in the input meters, test `NoiseFloor.MeterShowsTheMeasuredLevelOnlyAfterADenoiseMeasurement`).
* GUI: Settings categories are now an accordion (the open section directly under its header, the others below;
  every section scrolls) — owner report; test `SettingsLayout.OpenSectionSitsBetweenItsHeaderAndTheNext`.

### Phase 11B session 5 — Denoise measurement (procedure, prepared 2026-10-29)

Known (binary): Denoise uses the same bracket as autogain (word 4, then 6 mic / 7 instrument / 8 both, word 5 at
completion) and the shared progress/result bytes 0x24/0x23. Unknown: whether the device switches the Denoise byte
(7 / 16) on itself after the measurement, and what the official app writes around it. Prepared: the *measurements*
permission now also allows words 6 and 7 (not 8; audit rule extended to exactly these); switching Denoise on in the GUI
(real device, measurements authorized) opens a dialog **Measure / Only switch on / Cancel**; Measure sends 4 + 6 (or 7)
with the 45 s timeout; `--hw-check` treats the Denoise byte of that input as expected to change. Tests
`Measurement11B.DenoiseOnOffersTheMeasurement`, `Measurement11B.PolicyAllowsOnlySingleInputDenoiseWords`.

| # | Action | Conditions | Observe | Restore |
|---|---|---|---|---|
| 5a | Mic Denoise → Measure | room quiet, owner silent ~10 s; mic unmuted only for the measurement; speakers off, headphones unplugged | trigger log, progress → 100, result code, whether byte 7 changes by itself and when | Denoise off (plain write) if it came on; mic muted |
| 5b | (only if 5a left byte 7 at 0) Denoise → Only switch on | as 5a | byte 7 0 → 1 | off |
| 5c | Instrument Denoise → Measure (open cable) | as 5a | same for byte 16 | as 5a |

**Result (2026-10-30, PASS):** 5a mic: progress → 100 in about 10 s, the device set byte 7 to 1 itself and byte 8
202 → 196; 5b not needed; 5c instrument (open cable): byte 16 → 1, byte 17 211 → 215; mic repeated: byte 7 → 1, byte 8
stayed 196. Result codes 1, 0, 0 (no success meaning). Restored: Denoise off on both inputs (plain writes), mutes as
baseline; bytes 8/17 keep the device's new values. An unexpected speaker-volume change (−20.5 → −15.5 dB) happened
before the first measurement, reported by the device as a knob change (likely an accidental touch); restored with the
knob. OpenWitty now names 8/17 "Denoise level (set by the device)", `--hw-check` expects them after a Denoise
measurement, and the mock switches Denoise on after one. Open: the scale of 8/17 (int8 dB? compare with a recorded room
noise level) — needed before a noise-level marker in the meters.

Audible effect of Denoise (noise reduction) is not judged here; a recording of the room noise with Denoise off/on
(as LB-1) can follow.

### Phase 11B session 1 — low-risk settings via the GUI (procedure)

Owner present; speakers physically off or fully down, headphones off the head, nothing on the instrument input,
microphone pointing away from the speakers, nobody touching the device. Build installed user-local; window started with
`openwitty --hw-check --show settings-device`; baseline recorded from the first successful read. Permissions turned on,
each with its dialog: *LED colours and custom button*, *Input gain and mute*, *Input processing*, *Monitoring* (not
speaker, headphone, 48 V, output modes or autogain). Each step is **one** GUI action, then the log line
(`write <key>: verified; SET sent: yes` and `since baseline: … unexpected: none; values: …`), then the restore action.

| # | Action | Expected bytes | Restore |
|---|---|---|---|
| 1 | Custom button: another function | 53 | the original function |
| 2 | LED colour of one element (only if its current colour is one of the swatches) | 3 bytes of that element | the original swatch |
| 3 | Gain lock on | 57 | off |
| 4 | Mic low-cut, Clipguard, Comp toggles; mic preamp Clean | 3, 4, 9, 6 | toggles back; Vivid |
| 5 | Instrument preamp Clean | 15 | Vivid |
| 6 | Dim on, Mono on | 32, 33 | off |
| 7 | Monitoring source: microphone button off | 34 | on |
| 8 | Mix knob to about −30 | 35–36 | fully right (0) |
| 9 | Instrument unmute, mic unmute | 12, 2 | mute |
| 10 | Mic gain: three wheel notches down | 0–1 | three notches up (≤ 6 dB per write) |

End: **Compare with baseline** → `changed: none`; all four permissions off; window closed; service takes the device
back. Abort on any unexpected byte, "not confirmed", USB error or audible event; no retries. Evidence class per step:
W-gui (device accepted and reports the value); audible behaviour (low-cut, Comp, Dim …) needs A-live in session 3.

**Session 1 results (2026-10-27/28, owner present, safety conditions confirmed before each part): PASS.** Evidence
class W-gui for every row: one GUI action → one SET → device read-back verified → restored; final compare
`now vs baseline: changed: none`, all permissions off, window closed.

| # | Control | Bytes (start → test → start) | Result |
|---|---|---|---|
| 1 | Custom button | 53: 2 → 12 → 2 | PASS |
| 2 | LED colours, all five rows | Icons 38–40, Mute 41–43, Parameter 44–46, Metering 47–49, Gain reduction 50–52 | PASS; meaning observed: Icons = icons of unmuted channels, Mute = icons of muted channels, Parameter = gain/volume display of the selected channel, Metering = level meter, Gain reduction = its meter part (not visible without signal) |
| 3 | Gain lock | 57 was already **on** | not toggled (see below) |
| 4 | Mic low-cut, Clipguard, Comp, preamp | 3: 1→0→1, 4: 1→0→1, 9: 1→0→1, 6: 2→1→2 | PASS |
| 5 | Instrument low-cut, Clipguard, Comp, preamp | 13: 0→1→0, 14: 0→1→0, 18: 0→1→0, 15: 2→1→2 | PASS |
| 6 | Dim, Mono | 32 0→1→0 (+ device sets 25), 33 0→1→0 (+ device sets 26) | PASS after fix (below) |
| 7 | Monitoring source | 34: 2 → 0 (mic off) → 2, 2 → 1 (instrument off) → 2 | PASS; 0 inst, 1 mic, 2 both confirmed |
| 8 | Input/playback mix | 36: 0 → … → 30 (= −30) → 0, byte 35 stayed 0 | PASS (12 releases, each one verified SET) |
| 9 | Instrument / mic mute | 12: 1→0→1, 2: 1→0→1 | PASS |
| 10 | Mic gain | 1: 27 → 26 → 25 → 24 → 27 | PASS (+3 dB in one SET) |

Findings and fixes:
- **Overlay click-through** (part 1): a wheel turn on the Settings page lowered the instrument gain underneath
  (11: 14→11, restored). Fixed in `dbc51c3`.
- **Device-maintained bytes 25/26**: Dim and Mono each make the device set a second byte (25 = speaker dim state,
  26 = speaker mono state) to the same value. OpenWitty correctly stopped as "not confirmed" (twice, each restored to
  changed: none); since `1b3d5e5`/`d8ef700` these are device followers of Dim/Mono (accepted only for that write).
- **Gain lock was on at the start** (byte 57 = 1). Hypothesis: it is why the device re-muted FU 5 by itself in GAIN-1
  step 2R and why FU 5 never matched the preamp gain. Toggling it off is moved to GAIN-1 (the desktop route still
  requests +72 dB), not done in session 1. Caveat: the vendor field is `ExternalGainControl`; the polarity is unverified.
- **Knob/fader flash** after a wheel commit (old value shown until the read-back). Fixed: the committed value stays
  shown until the device reports it or 1.5 s pass.
- Each permission switch reconnects the session with the new policy (by design); the baseline is kept.
- Not tested in session 1: Denoise (needs the official measurement), 48 V, loopback mute, outputs (session 2).

**Session 2 results (2026-10-28, owner present, speakers powered off, headphones off the head): PASS.** Only one of
*speaker control* / *headphone control* was on at a time; every write verified by read-back.

| Test | Bytes | Result |
|---|---|---|
| HW-2 speaker mute | 22: 0→1→0 | PASS; desktop volume mirrored mute and unmute |
| HW-3 speaker volume | 20–21: −17.5 → −24 → −16.5 (+7.5 dB in one SET, within the 10 dB limit) → −17.5 dB | PASS; restored exactly |
| HW-4 headphone mute (first-ever headphone SET) | 29: 1→0→1 | PASS |
| HW-4 headphone volume | 27–28: −32.4 → −30 → −36 → −32.5 dB | PASS; the start value −32.398 dB (set with the device knob) is off the fader's 0.5 dB grid, restored to −32.5 dB with the owner's agreement — final compare: only byte 27 (154→128) |

Finding fixed in `c4a7ecf`: the output faders had no visible value; they now show dB (lime while previewed/awaiting).

**Session 3 part 1 — LB-2 hardware loopback (2026-10-28, owner present, speakers muted, headphones off): PASS.**
Method: 48 kHz stereo file (1 s silence, 4 s 1 kHz L-only, 1 s, 4 s 2 kHz R-only, −30 dBFS peak) played with `pw-play`
to the CONNECT 2 sink while `pw-record --channels 4 --channel-map FL,FR,RL,RR --format s32` recorded the 4-channel
source raw; offline per-channel FFT/RMS/peak. No desktop volume, mute or profile was changed.

| Run | Loopback mute (byte 19) | ch 1 mic | ch 2 instrument | ch 3 | ch 4 |
|---|---|---|---|---|---|
| LB-2a | 1 (muted, start state) | exact zero | exact zero | exact zero | exact zero |
| LB-2b | 0 (unmuted from the GUI, verified SET) | exact zero | exact zero | L tone only, peak −30.0 dBFS | R tone only, peak −30.0 dBFS |

Findings: capture ch 3 = loopback L, ch 4 = loopback R; no crosstalk (exact digital zero elsewhere); the loopback is
unity gain and independent of the speaker volume (−17.5 dB) and the desktop sink volume; loopback mute (byte 19) works
and the GUI write of it is verified (then restored, final compare changed: none). Muted inputs deliver exact zeros.
Opening the capture stream changed neither the device settings nor the PipeWire route (still muted, volumes
[≈1.0, ≈1.0, 0.000251, 0.000251]). The GUI meters showed L then R on loopback and speaker. LB-1 (mic/instrument
identity) waits for GAIN-1 (needs the inputs unmuted at the FU 5 / PipeWire level).

**Session 3 part 2 — LB-1 input channel identity (2026-10-28 23:09, owner present, physical speakers muted,
headphones off, an open cable in the instrument input): PASS, with a privacy finding.** One 4-channel recording
(`pw-record`, as LB-2) while the owner unmuted/muted the device's own mic mute (byte 2) and instrument mute (byte 12)
from the GUI (*input control* only; four verified SETs; final compare changed: none). Passive watchers: PipeWire input
route every 0.25 s, `alsactl monitor` on the card.

| Window | Device state | ch 1 | ch 2 | ch 3/4 |
|---|---|---|---|---|
| 23:09:49–57 | mic unmuted, one spoken sentence | speech, peak −20 dBFS | exact zero | exact zero |
| 23:10:00–06 | instrument unmuted | exact zero | hum ≈ −48 dBFS RMS (open cable) | exact zero |
| otherwise | both muted | exact zero | exact zero | exact zero |

Findings:
- Capture ch 1 = microphone, ch 2 = instrument (with LB-2: ch 3/4 = loopback L/R). No crosstalk.
- **The device's own input mute alone decides whether audio flows.** The PipeWire route stayed `mute: true` with volumes
  [≈1.0, ≈1.0, 0.000251, 0.000251] throughout, and there was no ALSA control event: FU 5 mute did not follow the device
  mute, and FU 5's "muted" state did not silence the capture. **The desktop input mute does not mute the CONNECT 2**
  (privacy issue for users). OpenWitty now says so on Settings ▸ Desktop integration.
- Speech at normal level peaked at −20 dBFS with the device at 27 dB, consistent with the FU 5 request (+72 dB cache)
  not acting on the mic preamp (not a calibrated proof).
- Planned fix (11C, needs owner approval): make the desktop's input mute/volume software-side for this device (ACP path
  override: the FU 5 switch/volume not used as hardware mute/volume), so a desktop mute silences apps and no FU 5 volume
  request is sent any more.

### Phase 11C — desktop input mute that works (software input mute/volume for the CONNECT 2)

Files (shipped as examples, installed by hand for now): `packaging/alsa-card-profile/profile-sets/openwitty-connect2.conf`
(the default mappings for this device: `analog-stereo` output with the default output paths, `analog-surround-40`
input), `packaging/alsa-card-profile/paths/openwitty-connect2-input.conf` (capture path with the FU 5 "Mic" element
`switch = ignore`, `volume = ignore`), `packaging/wireplumber/52-openwitty-connect2-input.conf` (selects the profile set
for USB `29c2:0004` only). PipeWire's ACP reads profile sets/paths from `~/.config/alsa-card-profile/mixer/` too.
Expected: same profile name and output route (`analog-output`, saved speaker state restored as at every login); a new
input route (software mute/volume, `route.hw-mute`/`hw-volume` false); no FU 5 write at all.

**Live test 11C-1 (owner present; speakers physically muted, headphones off, mic away):**
1. Before: profile, routes, node props, default source; `alsactl monitor` on the card running.
2. Install the three files into `~/.config/…`; `systemctl --user restart wireplumber` (one restart).
3. Check: profile unchanged; output route and speaker level unchanged (vendor compare in an `--hw-check` window);
   input route new, software mute/volume; no `Mic Capture` ALSA event.
4. Function: 4-channel recording; owner unmutes the device mic (GUI, *input control*), speaks; `wpctl set-mute <source> 1`
   → all four channels exact zero; `wpctl set-mute <source> 0` → speech again; device mic muted; permission off.
   (Software mute applies to the whole capture node, loopback channels included.)
5. Abort: speaker level or any vendor byte changes, profile changes, any FU 5 write, any error. Rollback: delete the
   three files, restart WirePlumber, verify the old route (`analog-input-mic`) is back.

**11C-1 result (2026-10-28 23:19–23:21, owner present): PASS.** After installing the three files and one WirePlumber
restart: profile set `openwitty-connect2.conf` in use, profile name unchanged, output route `analog-output` unchanged
(hardware mute/volume, 0.13335, speaker −17.5 dB), input route `openwitty-connect2-input` with `route.hw-mute=false`,
`route.hw-volume=false`, unmuted at 1.0; **no ALSA control event** during the restart or the test (nothing written to
FU 5). Function: device mic unmuted from the GUI, owner counting; `wpctl set-mute <source> 1` at 23:20:17.4 → all four
channels exact zero until `wpctl set-mute <source> 0` at 23:20:25.4 → speech again; device mic muted; final vendor
compare changed: none. The files stay installed on the owner's system. Settings ▸ Desktop integration detects them
(warning only when missing); install steps in docs/INSTALLATION.md.

**Session 3 part 3 — DAW-1…DAW-4 with Ardour 9.8, ALSA backend (2026-10-29, owner present, speakers muted, headphones
muted/off): PASS.** Headless Ardour session (`tools/audio/daw_ardour_test.sh`, `daw_ardour.lua`): 4 mono tracks on
capture 1–4, one stereo track on capture 3/4, two `SinGen` buses (1 kHz → playback 1 only, 2 kHz → playback 2 only,
−30 dBFS, not via master); device loopback unmuted from the GUI; the owner unmuted/muted mic and instrument during a
180 s recording; analysis `tools/audio/wavinfo.py`.

| Test | Result |
|---|---|
| DAW-1 discovery | ALSA device "CONNECT 2"; ports `system:capture_1…4` ("Main In 1…4"), `system:playback_1/2` ("Out Left", "Out Right"); 48 kHz, buffer 1024 |
| DAW-2 four tracks | Mic: speech only while unmuted (9.5–15 s, peak −15.7 dBFS); Instrument: cable hum only while unmuted (15–21 s); tones in Mic/Instrument ≤ −141 dBFS |
| DAW-3 assignment | stereo track L = capture 3 (1 kHz), R = capture 4 (2 kHz); L/R kept |
| DAW-4 playback | 1 kHz only on loopback L, 2 kHz only on loopback R, both −30.0 dBFS peak (other tone ≤ −237 dBFS) |

Notes: `ardour-request-device` could not take the device reservation from PipeWire ("Device or resource busy"), but the
device was idle (no PipeWire stream), so Ardour opened it directly; PipeWire re-created the CONNECT 2 nodes after
Ardour exited (default sink/source kept). With a desktop stream playing on the CONNECT 2 the ALSA backend would fail to
open it — use option B (`pipewire-jack`) for side-by-side use; not tested yet. A first run missed the mic/instrument
window (instructions reached the owner after the 60 s recording); repeated with 180 s. Final vendor compare:
changed: none; all permissions off.

**11A.4-1 result (2026-10-29 20:06–20:21, owner present, usual safety): PASS.** Updated 52 rule copied; the owner
switched *Separate inputs for apps* on and used *Restart desktop audio* (GUI path, confirmation dialog). PipeWire
created `openwitty_connect2_microphone` (MONO ← FL), `_instrument` (MONO ← FR) and `_loopback` (FL FR ← RL RR); the main
input is described "CONNECT 2 (1 Mic, 2 Instrument, 3+4 Loopback)". One recording per virtual source (`pw-record`):

| Time | Event | Microphone | Instrument | Loopback L / R |
|---|---|---|---|---|
| 20:20:22–29 | device mic unmuted, counting | speech, peak −13 dBFS | exact zero | exact zero |
| 20:20:29–35 | device instrument unmuted | exact zero | cable hum | exact zero |
| 20:20:36–47 | desktop mute of the main input, tones playing | exact zero | exact zero | exact zero |
| 20:20:49–59 | desktop unmuted, tones | exact zero | exact zero | L 1 kHz, then R 2 kHz, peak −30.3 dBFS |

Cross-talk ≤ −134 dBFS. The first attempt used a fixed 200 s recording that ended before the owner's actions (messages
reach the owner with a delay — recordings in live tests now run until the scripted sequence ends). Final vendor compare
changed: none; permissions off. The separate inputs stay installed on the owner's system
(`~/.config/pipewire/pipewire.conf.d/53-openwitty-connect2-split.conf`).

**Session 4 results (2026-10-29 21:30–22:00, owner present, speakers powered off, headphones unplugged): PASS.**

| # | Control | Bytes (start → test → start) | Result / observation |
|---|---|---|---|
| 4a | Gain lock | 57: 1 → 0 → 1 | PASS. Each toggle made the device report UAC2 FU 5 mute changes for ch 1 and ch 2 (~0.5 s apart, ALSA `Mic Capture Switch` events); with the lock on FU 5 reads muted. Consistent with "lock on = the host's FU 5 control is locked out" (explains GAIN-1 step 2R); the unlocked FU 5 value was not sampled. No effect on the desktop (11C) |
| 4b | High-impedance headphones | 31: 1 → 0 → 1 (was on) | PASS, no follower byte |
| 4c | High-volume speakers | 24: 1 → 0 → 1 (was on) | PASS, no follower byte |
| 4d | 48 V | 5: 1 → 0 → 1 (dialog on enable) | PASS |
| 4e | Mic autogain | triggers 4, 1, then 5 after progress 100 | PASS. The device dropped the gain to 0, listened while the owner spoke, set **39 dB**; the gain change appeared in the settings shortly **after** progress 100. **Result code read 1** although the measurement succeeded → the decoded meaning (1 = failed) is doubtful; the GUI must not report failure from it. Gain restored to 27 dB with the knob (one decrease) |

Final compare changed: none; all permissions off. Owner question answered the same evening (read-only check,
no permissions): the **rear-panel 48 V button toggles the same setting, byte 5** (1 → 0 → 1, OpenWitty's 48V indicator
followed) — no separate hardware master switch. During that check the custom button was also pressed by accident
(front panel): one press toggled bytes 2, 22 and 29 together (mic 1→0, speaker 0→1, headphone 1→0) = its function
"Toggle mute: hp + mic/spkr" (value 2) confirmed on hardware; a second press restored all three; byte 56 (selected
channel) 3→2 from touching the surface. The knob mirror mirrored the device speaker mute to the desktop and, by design,
did not unmute the desktop afterwards ("an alignment never unmutes") — the owner unmutes it in KDE.

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
| Presets | ✅ OpenWitty `.owpreset` (2026-11-01); official `.preset` not decoded (may be CONNECT 6 only) | importer if the official format applies | P3 |
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
and reports which offsets differ from the baseline. It prints field names, offsets and (since Phase 11A) the old and
new values of the changed bytes only — no device identifiers, no full settings dump. It cannot write (audit rule; no session or transport access in
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
device mute; what External Gain Lock does. That plan supersedes the outline below. **GAIN-1 step 1 done (2026-10-26):** kernel cache +72 dB/muted vs device 27 dB/14 dB, muted — consistent with FU 5 volume not driving the preamp, causality unproven; step 3 revised so no request exceeds the device's reported gain. **Phase 10E.1:** the bound is the lower of the mic and instrument gains (FU 5 ch 2 likely = instrument, unproven); restoring the route to 1.0 (= +72 dB) is never assumed safe (restoration strategy R-A/R-B decided before the test); the `wpctl set-mute` path is traced in code and sends only FU 5 mute requests — step 2 is ready for separate approval. **GAIN-1 step 2 (2026-10-26): stopped** — after one unmute the input was re-muted automatically within ~1 s (cause unknown); device mutes unchanged afterwards; saved route volumes now show the cache view; see the step 2 result. Follow-up (no writes): WirePlumber re-application contradicted by the route's save flag; a device status interrupt (EP 0x83) dropping the kernel cache is the most plausible, unverified cause; an instrumented reproduction (usbmon incl. EP 0x83, temporary debug logs, event monitor) is proposed. **Phase 10E.2:** the command-by-command procedure (step 2R) is prepared — usbmon text capture of bus 3 filtered to UAC2 CUR/RANGE requests and EP 0x83, WirePlumber logging raised via its own client ID and restored exactly, passive `alsactl monitor` and `pw-mon`, Steam to be quit (a PipeWire client with voice settings, candidate for an external mute). **Step 2R run 2026-10-27:** the device accepted the FU 5 unmute (status 0) and then re-muted itself — UAC2 interrupts for entity 5 mute ch 1/2 at +297/+809 ms, re-reads answered muted; no client, no route re-application, no volume request. On this device, with its own input mute on, the desktop input mute cannot unmute the inputs; whether it can mute them when the device mute is off is untested.

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

### Feature-parity checklist (current status, 2026-10-31)

The detailed table written in Phase 10D (before any GUI write) was replaced by this status; the evidence for each row is
in the session results above (Phase 11B sessions 1–7, 11C, 11A.4, Phase 12) and in PROTOCOL.md.

| Area | Official function | OpenWitty status | Evidence |
|---|---|---|---|
| Inputs | gain, mute, low-cut, Clipguard, preamp Clean/Warm/Vivid, Comp | ✅ written + verified | session 1 |
| | 48 V (rear button = same byte) | ✅ confirmed per use | session 4 |
| | Denoise with noise measurement | ✅ measure (words 6/7), reuse stored level, right-click re-measure, noise-floor line | sessions 5–6 |
| | Autogain | ✅ mic (live); instrument not tested (no instrument) | session 4 |
| | Autosetup wizard (whole flow) | ⚠ built, parts tested; **full flow live test open** | — |
| | Gain lock (external gain) | ✅ | session 4 |
| Outputs | speaker / headphone volume + mute | ✅ | session 2 |
| | high-volume speakers, high-impedance headphones | ✅ confirmed per use | session 4 |
| | map native volume controls (None/Speakers/Headphones) | ✅ byte 59 | Phase 12A/B |
| Monitoring | source mic/inst/both, mix, Dim, Mono | ✅ (Dim/Mono followers 25/26 handled) | session 1 |
| | mix applies to speakers | ✅ byte 23 (audible effect not judged) | session 6 |
| | duplicate-monitoring hint (device + DAW) | ✅ docs (INSTALLATION.md, DAW section) | — |
| Routing | loopback (ch 3/4), loopback mute | ✅ | LB-1/LB-2 |
| | mobile streaming (inputs 1/2 = mix) | ✅ | session 6 |
| | macOS streaming mode (device restart, other PID) | ✗ intentionally not enabled | PROTOCOL.md |
| Custom button | all official functions | ✅ | session 1 |
| | OpenWitty extensions (+ follow volume) | ✅ | Phase 12D |
| LEDs | brightness, colours of five rows | ✅ | HW-1, session 1 |
| Rates | 44.1 / 48 / 96 kHz | ✅ display; optional PipeWire rates (experimental: rapid changes hung the device) | session 7 |
| | Denoise/Comp off at 96 kHz | ✅ greyed out with reason | session 7 |
| Linux integration | one device, volume keys = hardware level of the audible output, touch focus follows | ✅ | Phase 12 |
| | software input mute, separate inputs for apps | ✅ | 11C, 11A.4 |
| | DAW (Ardour ALSA, JACK via pipewire-jack) | ✅ ALSA + JACK clients; Ardour GUI recorded the mic through JACK | DAW-1…4, session 7, 2026-11-01 |
| Application | firmware version | ✅ shown as USB revision | 2026-10-31 |
| | presets (save / load, OpenWitty `.owpreset`) | ✅ simulated + permission tests; official `.preset` import open | 2026-11-01 |
| | firmware update, factory reset | ✗ intentionally disabled | policy |

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

* **SR-0 (done 2026-10-30, offline):** the owner's PipeWire allows only 48 kHz (`clock.allowed-rates [ 48000 ]`), so a
  per-device rule cannot change the device rate; OpenWitty offers *Allow 44.1 and 96 kHz for apps* (user file
  `54-openwitty-sample-rates.conf`, global, restart needed, removes only its own file; tests `SampleRates.*`,
  `SampleRatesSR.*`). Hardware tests SR-1…3 go into session 7 together with 6d/6e.
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
