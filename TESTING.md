# Testing

`ctest --test-dir build` — 65 CTest entries: 62 GoogleTest cases in `lewitt_tests`, `gui_integration` (17 cases: controller/worker thread, merge, pending edits, mode switch, QML load; offscreen, simulated device), `visual_geometry` (offscreen render vs. measured invariants) and 1 legacy Python test.

| Suite | Kind | Hardware-verified? |
|---|---|---|
| Types/Q88/Settings/Status/Trigger (`test_protocol.cpp`) | byte vectors hand-built from disassembled offsets/encodings (not from the encoder) | **No** — derived from static analysis |
| Session / Mock / UsbTransport policy (`test_session.cpp`) | simulated: timeout, short read, disconnect/replug, stall, size checks, write-refusal | n/a (simulated) |
| USB error taxonomy (EACCES/EBUSY/EPIPE mapping), stall≠disconnect, failed-commit keeps edit, real-policy write refusal, unknown enum round-trip, field disjointness | simulated / unit | No |
| python_legacy_protocol | legacy prototype | No |

Hardware validation order (when the device is attached) is in `TODO.md`.

## Added in this phase (all simulated / unit; none hardware-verified)
Outputs (speaker/headphone independence, loopback mute), monitor state machine
(12 transitions), input/playback mix inversion, LED RGB/brightness,
output meter offsets, 96 kHz DSP restriction (session + GUI), risk
assessment (phantom / high-volume / routing), simulated auto-gain workflow,
GUI backend round trips, QML load without errors, unplug/replug and clean
shutdown, USB backend read-only.

## Phase 5B additions
Verification class: **simulated / unit / visual-regression**; none hardware-verified.
* Sync: failed write kept and flagged, retry, discard, external device change merged
  without losing a pending edit, identical value sends no command.
* Trigger set: all 12 command words distinct and as recovered; mode switch refused on
  non-simulated transports even with every policy flag on.
* GUI: simulated mode switch restarts and preserves settings; auto-gain bracket.
* Visual: `visual_geometry` (dividers ±4 px, region colours ±8) — guards regressions,
  not a pixel-accuracy claim. Manual: `tools/visual/compare.py` against local references.
* Measured (offscreen *software* renderer, so indicative only): no-device idle 0 CPU ticks
  /10 s; mock with 25 Hz meters ≈ 10 % of one core; RSS ≈ 65 MB; startup to first frame
  ≈ 1.5 s.
