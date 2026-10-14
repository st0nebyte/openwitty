# Testing

`ctest --test-dir build` — 43 CTest entries: 41 GoogleTest cases in `lewitt_tests`, the 9-case GUI integration suite (`gui_integration`, offscreen, simulated device) and 1 legacy Python test.

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
