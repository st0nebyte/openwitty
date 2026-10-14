# Testing

`ctest --test-dir build` — 30 tests (29 GoogleTest + 1 legacy Python).

| Suite | Kind | Hardware-verified? |
|---|---|---|
| Types/Q88/Settings/Status/Trigger (`test_protocol.cpp`) | byte vectors hand-built from disassembled offsets/encodings (not from the encoder) | **No** — derived from static analysis |
| Session / Mock / UsbTransport policy (`test_session.cpp`) | simulated: timeout, short read, disconnect/replug, stall, size checks, write-refusal | n/a (simulated) |
| USB error taxonomy (EACCES/EBUSY/EPIPE mapping), stall≠disconnect, failed-commit keeps edit, real-policy write refusal, unknown enum round-trip, field disjointness | simulated / unit | No |
| python_legacy_protocol | legacy prototype | No |

Hardware validation order (when the device is attached) is in `TODO.md`.
