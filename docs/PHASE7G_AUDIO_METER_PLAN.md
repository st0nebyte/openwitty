# Phase 7G plan — smooth, honest, read-only audio meters

Status: **planning only** (Phase 7F). Do not enable hardware writes, change the production
poll schedule, capture audio, or run a high-rate GET experiment from this document alone.

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
