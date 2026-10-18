# Phase 7G plan — smooth, honest, read-only audio meters

Status: **initial implementation complete; hardware visual review in progress** (Phase 7G).
Do not enable hardware writes, change the production poll schedule, capture audio, or run a
high-rate GET experiment from this document alone. The sections below preserve the original
Phase 7F design rationale; implementation details follow at the end.

## Evidence and current pipeline

`DeviceWorker::poll()` serially reads one 64-byte `Status` message every 250 ms (4 Hz), then
one 128-byte `Settings` message every second poll (2 Hz). `DeviceSession::refreshStatus()`
decodes status; `StatusMessage` reads signed little-endian Q8.8 values at offsets 0x00 and
0x09 (two inputs), and pairs at 0x12/0x14, 0x16/0x18 and 0x1A/0x1C (loopback, speaker,
headphone). The codec clamps the displayed range to −60…0 dB. `DeviceWorker::publish()`
copies these eight level values into `in1.level`, `in2.level`, and `loop/spk/hp.levelL/R`;
`DeviceController` forwards changed keys to QML. `LevelMeter.qml` binds rectangle heights
directly to those keys. It has **no timer or smoothing**. The mock runs status every 40 ms
and generates sinusoidal sample values; its appearance does not represent real USB cadence.

The wire format's nominal Q8.8 step is 1/256 dB. This is **encoding granularity**, not
validated sensor accuracy, physical dynamic resolution, peak-detection window, RMS/peak
semantics, or firmware update rate. The input-1 status field was seen responding to sound in
earlier real-hardware work; the full eight-channel meter dynamics and peak/clip semantics
remain unmeasured. A 4 Hz snapshot stream can miss a transient between reads entirely. A
60 FPS display cannot recover a peak never observed by the device/control path.

Current source inspection finds only this vendor-control `Status` GET as an OpenWitty meter
source. Passive sysfs descriptors for this connected unit show vendor interface 3, alternate
setting 0, with zero non-control endpoints; this is **not proof** that no undocumented or
alternate path exists. The [official CONNECT 2 guide](https://www.lewitt-audio.com/support/online-guides/connect2)
describes hardware/software metering and UAC2 audio, but does not document a public meter-data
API. Do not probe unknown vendor requests or DFU. Capturing host PCM via PipeWire/ALSA would
measure a potentially different signal point, omit some direct-monitoring/loopback paths, and
raise privacy and routing concerns; it is a separate research option, not a drop-in source.

## Proposed separation

1. **Acquisition:** keep one `DeviceWorker`/`DeviceSession` control owner, lease, and serialized
   USB requests. Treat status and settings schedules independently. Timestamp each successful
   status frame with the existing monotonic clock; publish only actual decoded samples and
   freshness metadata. Do not move USB calls to QML or create another handle/process.
2. **Meter state:** keep per-channel latest observed sample, sample timestamp, valid/stale flag,
   display envelope, and maximum *observed* peak. Preserve raw decoded values separately from
   animation state. No extrapolated peaks and no smoothing that changes a stored measurement.
3. **Rendering:** update only the meter visuals on frame callbacks while the window is visible
   and status fresh. Qt Quick `FrameAnimation` (available in the project's Qt 6.12, introduced
   in Qt 6.4) is a candidate because it follows render frames; benchmark it against a minimal
   scene-graph animation before choosing. Do not run a 16 ms QML `Timer` that implies 60 USB
   reads/s. Pause animation when hidden/minimized; dim or freeze with an explicit stale marker
   if the status stream is delayed/stale, and clear on disconnect.

At each real sample, an upward bar may move quickly toward the **observed** level; a falling
bar may follow a time-based display release. Never interpolate toward an unknown future sample
or fabricate micro-peaks. Peak hold records the maximum *sampled* value only. If useful, an
advanced detail/tooltip can distinguish latest sampled dB from the visual envelope.

## Rendering and envelope candidates to test

| Option | Display target | Trade-off |
|---|---:|---|
| A | 30 FPS while active | Lower frame work; may look adequate for narrow 8-pixel bars. |
| B (preferred trial) | 60 FPS while active | Smoother release/peak decay on a 60 Hz display; **does not improve acquisition**. |

Start with illustrative, configurable envelope candidates: 10–30 ms attack toward a newly
observed higher sample, 300–600 ms exponential release, 500–1000 ms observed-peak hold, then
approximately 20–30 dB/s peak-marker decay. These are UI design hypotheses, **not** device
measurements. Tune by owner review and measured frame cost; reset on disconnect/reconnect and
avoid animating old values as live during status-stale periods. A sustained tone should settle
at its observed level, not oscillate merely to appear lively.

## Acquisition-rate experiment — separate approval required

Keep the production 4 Hz status / 2 Hz settings baseline in Phase 7G until a controlled
single-owner, read-only trial is approved. First implement/test the rendering layer at 4 Hz,
then compare optional status-only 8 Hz and, if safe and useful, 12–16 Hz. Do **not** increase
settings GETs, run a second control client, or infer a safe status rate from 60 FPS animation.
At 4/8/16 Hz status with 2 Hz settings, the scheduled requests are 6/10/18 GETs/s and status
payloads 256/512/1024 B/s respectively; these are arithmetic payload counts, excluding USB
transaction overhead and firmware work. A 16 Hz status stream still has 62.5 ms between
samples and may miss shorter peaks. Higher rates (including 20–30 Hz) should be considered
only if measured visual/peak-detection benefit justifies them and control/audio reliability
remains intact. If the status message itself updates slowly, faster reads may yield identical
frames and should be rejected.

For each approved rate, run **one** read-only GUI owner with unchanged audio routing and no
other OpenWitty client. Record monotonic GET start/end intervals, distinct-frame ratio, status
age, timeout/EIO/stall counts, settings cadence, CPU/RSS, frame times, audio XRUNs and owner
feedback. Include an idle run and a safe, repeatable signal/control trial approved by the owner.
Do not record raw audio or device identifiers. Compare repeated status values to determine
whether firmware actually supplies new data at the proposed interval.

## Test strategy and measurable acceptance

* Deterministic mock/replay frames: silence → step → sustained level → step down, short burst
  between samples, alternating stereo channels, identical repeated status frames, clip-counter
  change, delayed/stale/recovered status, disconnect/reconnect, and irregular frame timing.
* Unit-test envelope math with injected monotonic times: attack/release, peak-hold boundary,
  decay, clamping, no overshoot/new peak beyond observed samples, and reset/freeze semantics.
* QML/offscreen tests: 30/60 FPS options, visibility pause, stale warning, no animation-driven
  USB call, unchanged settings cadence and read-only refusal. Run Release, ASan/UBSan and USB
  call-site audit without weakening existing gates.
* Candidate display acceptance on the target desktop: active meter animation near 60 FPS
  (median ≥55 FPS on a 60 Hz display, 99% of frame gaps <33 ms), no sustained CPU increase
  above 2 percentage points from a measured baseline, and <20 MiB added steady-state RSS.
  These are proposed gates, not current measurements. New-sample-to-visible-frame delay should
  be ≤2 display frames for 99% of samples under normal load, measured with instrumented
  monotonic timestamps. A genuine sound/action-to-screen latency claim additionally requires
  an independent synchronized physical/audio marker and visible-frame marker.
* Hardware acceptance: no USB SET/detach/reset/DFU, no lease contention, no new transfer errors,
  no audio XRUN increase or audible interruption, and no unexplained settings/status freshness
  warnings. Verify meter behavior on input and output paths that can be exercised safely;
  unverified route semantics remain labeled unverified.

Rollback to the Phase 7F binary and 4 Hz status default if the device returns repeated/unchanged
frames with no benefit, GET latency/errors rise, audio glitches/XRUNs appear, freshness regresses,
CPU/frame-time gates fail, or the display suggests peaks the sampled data cannot support.
Before any physical signal experiment, concurrent-client test, or unplug/replug, request the
owner's explicit approval and agree on the exact safe actions.

## Phase 7G first implementation (2026-10-17)

`DeviceWorker` now emits a monotonic timestamp with the eight decoded values after each
successful status read. It emits no new sample for a failed GET. The existing serialized
4 Hz real status / 2 Hz settings schedules and the single-owner lease are unchanged; mock
status remains 25 Hz. The old `values` keys remain available for diagnostics, but the bars
bind to eight persistent `MeterChannel` objects instead of whole-map state changes.

`MeterDynamics` is Qt-free and deterministic under injected steady-clock timestamps. Each
channel keeps the last **observed** dB, a presentation target/envelope, an observed-sample
peak marker, and validity. QML runs one `FrameAnimation` only while the visible, active,
connected window has moving envelopes. No frame callback performs I/O. A successful status
sample can start motion; no predicted future samples or random motion are generated.

Initial visual parameters: 20 ms exponential attack, 450 ms exponential release, 750 ms
observed-peak hold and 24 dB/s peak-marker decay. The display is clamped to −60…0 dB and
cannot overshoot the latest observed target. A frame gap of at least one second snaps the
bar to the last observed target instead of animating an obsolete transition. These are
presentation choices, **not** measurements of the CONNECT 2's signal dynamics. A peak marker
represents the highest *sampled* value; it cannot reveal an inter-sample transient.

Status freshness gates the presentation independently of settings freshness. Delayed status
freezes/dims meters; stale or unavailable status clears their visible level and validity.
Recovery and reconnection require a new successful sample. No old nonzero bar or peak is
presented as live after a disconnect. The existing readiness/freshness banner remains the
primary textual warning.

Release unit/QML tests cover attack, release, hold, decay, time-step independence, long pauses,
invalid and clamped inputs, eight independent channels, stale/recovery/disconnect, all eight
QML bindings, and mock operation. Existing replay/read-only tests remain in the full suite.
`OPENWITTY_TRACE_METER_FRAMES=1` opt-in logging summarizes 240 active frame-swap gaps
(mean, median, p95, p99) without payloads; idle or hidden periods must not be interpreted
as a 60 FPS measurement. Two offscreen mock runs yielded median gaps 15.43–15.53 ms and
p99 17.18–17.48 ms; these are not physical-screen measurements. Production
animation targets the display refresh (~60 FPS on a 60 Hz screen), not a fixed USB rate.

The 8 Hz/16 Hz status-GET comparison is **still pending separate approval**. Its measurements
must distinguish rendered cadence from 4 Hz hardware sampling, and must check GET errors,
audio coexistence, CPU/RSS, and whether repeated frames indicate a slower firmware source.

## Phase 7G.2 bounded acquisition diagnostic (hardware-run after approval)

The standalone `openwitty-meter-probe` uses the same read-only auto-discovery and cooperative
control lease as the GUI. It does not alter `DeviceWorker` or production polling. Real USB is
dry-run-only unless `--execute` is passed after separate owner approval. Valid rates are only
4 or 8 Hz, valid durations 1–10 seconds, and the hard ceiling is 80 status GETs plus one API
GET. Device string-descriptor reads are disabled for this diagnostic, so it does not fetch a
serial number. A first error stops the run; delayed requests are never followed by a catch-up burst.
Its output compares the full 64-byte status frame and only the 16 raw meter bytes separately,
while logging decoded/clamped eight-channel values rather than raw payloads. The first sample
is a baseline, not counted as a change. `interval_hz` is computed from the first and last
GET start timestamps, not request count divided by a shortened final wall time. CPU and RSS
are whole-process diagnostics, not an isolated USB cost. Mock tests validate the bounds and
read-only path; the full Release suite passed 166/166 and the USB call-site audit passed.

The owner explicitly approved these exact commands and prepared a comparable signal; they
were executed sequentially after a fresh safety preflight on 2026-10-18:

```sh
build-release/openwitty-meter-probe --rate 4 --duration 10 --execute
build-release/openwitty-meter-probe --rate 8 --duration 10 --execute
```

Run them sequentially with only one OpenWitty control client, unchanged device settings and
audio routing, and a comparable steady signal supplied by the owner. These commands imply
40 + 80 status GETs and two API GETs at most across both runs: 120 status requests total,
7,680 status-payload bytes, plus two 64-byte API responses (128 bytes total),
excluding USB transaction overhead.
Monitor audio for glitches and confirm ALSA/PipeWire presence before/after. A 16 Hz trial is
not part of this diagnostic and needs its own decision and approval.

Measured results are in `docs/HARDWARE_TEST_RESULTS.md` §23. The 4/8 Hz commands returned
40/80 successful status GETs with no errors; measured start intervals were 250/125 ms.
Both runs changed active speaker meter bytes on every consecutive response. Within the 8 Hz
run, 38/39 interior 125 ms midpoint speaker-L values differed from both neighboring
250 ms-spaced values, including several >1 dB local extrema. That is concrete additional
meter information, **not** proof of improved true peak accuracy or of how a GUI would look.
GET-duration p95 was 2.908/2.927 ms and no control instability was seen. The probe's high
identical peak-RSS outputs need independent verification; audio enumeration survived, while
audible-glitch feedback and objective XRUN monitoring remain open. Keep production 4 Hz until
the GUI benefit and resource picture are validated. A 16 Hz trial is not automatic.

The owner later confirmed no audible interruptions, clicks or dropouts in those two probe
runs. Phase 7G.3 adds a **temporary** real-GUI `--experimental-status-8hz` startup option
for visual comparison, without changing the 4 Hz default or enabling writes. The 8 Hz
mode uses the same serialized worker/USB connection, retains 2 Hz settings, and sets
status freshness to fresh through 188 ms, delayed through 375 ms, then stale. The QML
window title labels the experiment; the eight-channel frame animation and observed-only
peak handling are unchanged. The first 4 Hz real-GUI trace and independent `/proc` memory
snapshot are documented in `docs/HARDWARE_TEST_RESULTS.md` §24. The owner found the
4 Hz real GUI meters "way too slow"; the opt-in 8 Hz GUI subsequently produced
stable 125 ms polling with no traced USB errors and similar whole-process RSS.
The owner judged 8 Hz better than 4 Hz, but still far from a real-time level bar;
the visual acceptance target was **not met**. Audible-quality feedback for the GUI
run remains pending. Further work should first determine what hardware sample rate
and meter semantics are achievable; a bounded 16 Hz read-only trial is a possible
next measurement, not an approved run or a production-rate change.

## Phase 7G.4 — match the reported original acquisition rate (prepared, not run)

The owner reports a native-Windows capture of the original application's 64-byte
status GET at 46.845/s visible idle and 46.775/s visible with playback (mean
intervals 21.347 and 21.379 ms), versus 34.813/s minimized with playback
(28.725 ms). Two probable stereo meter fields reportedly changed on every
successive playback response; a separate 128-byte read did not change and no
periodic control-transfer errors were observed. The original report and JSON
remain on another computer and have not been independently inspected here.
`docs/ORIGINAL_METER_RESEARCH.md` keeps those Windows observations distinct
from the Linux measurements below.

The existing `openwitty-meter-probe` now accepts only 4, 8, 40, and 48 Hz, with
the same dry-run default, read-only `UsbTransport`, auto-selected vendor control
interface, per-user lease, and serialized status GETs. Its hard ceiling is 480
status requests and 10 seconds; the 40 Hz/10-second plan is bounded to 400 GETs,
the 48 Hz plan to 480. Each adds one API-version GET, but **no** settings GET,
SET, driver detach, reset, or DFU access. The probe stops on the first read or
decode error, never catches up with a burst after a delayed request, and prints
monotonic start times, eight decoded levels, changed/repeated meter snapshots,
GET duration, start-interval distribution, schedule-lag p95, CPU time, and
`getrusage` peak plus current `/proc` RSS/PSS. The latter independently checks
the earlier suspicious peak-RSS figure; it is not an incremental USB-memory
measurement. No serial, raw status payload, or proprietary asset is stored.

Approval sequence:

1. Recheck device identity, interface, permissions, audio presence, and an empty
   OpenWitty control lease; prepare a comparable steady audio signal. Ask for
   **explicit approval** of `build-release/openwitty-meter-probe --rate 40
   --duration 10 --execute`. This yields at most 400 status GETs, 25,600 status
   payload bytes, and one 64-byte API GET over at most 10 seconds, excluding
   USB transaction overhead. Stop immediately on transfer/decode error, device
   loss, or audible disruption. The owner subsequently approved and ran this
   exact command; results follow below.
2. Review achieved frequency, interval jitter, GET median/p95, genuinely changed
   meter samples, USB errors, CPU, `/proc` memory, and ALSA/PipeWire/audio
   coexistence. Do not infer peak accuracy merely from changed bytes.
3. Only if 40 Hz is stable and informative, propose the separate
   `--rate 48 --duration 10 --execute` command (maximum 480 status GETs,
   30,720 status payload bytes, plus one 64-byte API GET) and obtain **new approval**.

Do not change the real GUI's 4 Hz production default, the optional 8 Hz mode,
2 Hz settings schedule, or 60 FPS presentation until approved hardware results
and a separate GUI-rate authorization justify it. The owner has already judged
4 Hz too slow and 8 Hz better but still far from a real-time meter; that is a
visual observation, not a measured sound-to-screen delay. Hidden-window rate
reduction and GUI acceptance need separate measurement and visual review.

### Approved 40 Hz result — transport stable, meter-information test inconclusive

The single approved 40 Hz/10-second run completed 400/400 status GETs with zero
transport errors, an achieved 40.000 Hz start cadence, 25.000 ms median interval,
0.247 ms median GET and 0.306 ms p95 GET. CPU time was 10.8 ms over the run
(0.11%). Independent current `/proc` RSS was 5,204 KiB (PSS 1,258 KiB), while
`getrusage` again reported an inconsistent ~787 MiB historical peak; the cause
is not established. Audio interfaces and ALSA/PipeWire nodes remained present.

However, all eight decoded meters stayed at −60 dB and the *entire* status
frame repeated on all 399 comparisons. The owner confirmed that no audio was
playing during this run and reported no audible clicks or other problems;
the latter is subjective feedback, not XRUN instrumentation. An active-signal
40 Hz repeat requires a newly approved command. Until then, this result cannot
answer whether 40 Hz yields additional genuine level observations during
playback. Do not advance to 48 Hz, change the GUI rate, or infer a firmware
update ceiling from this run. Details and caveats are in
`docs/HARDWARE_TEST_RESULTS.md` §25.

### Separately approved active-playback 40 Hz repeat

With owner-confirmed white-noise playback reaching CONNECT 2, a new approval
was obtained for the same 40 Hz/10-second command. It achieved 400/400 GETs,
zero errors, 40.000 Hz, 25.000 ms median interval, and 0.255/0.312 ms
median/p95 GET duration. The active speaker meter bytes changed on all 399
consecutive comparisons; decoded speaker L/R ranged approximately −17…−12 dB.
CPU was 0.09% of one core over the run, current `/proc` RSS 5,176 KiB, and
the owner heard no clicks or other audible problem. ALSA/PipeWire and the
kernel audio driver remained present afterward. This is evidence that the
device/control path supplies distinct readings at 40 Hz under playback; it
does not prove true peak accuracy or that the GUI will feel responsive.

A separately approved 48 Hz/10-second read-only trial is now **reasonable to
propose**, because 40 Hz was stable and informative. It is **not approved or
executed**. It would be capped at 480 serialized status GETs (30,720 payload
bytes) plus one 64-byte API GET, with the same first-error stop and owner
audio monitoring. Keep the 4 Hz GUI default and 2 Hz settings schedule until
high-rate hardware results and a further GUI-specific authorization exist.
