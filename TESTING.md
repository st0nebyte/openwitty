# Testing

`ctest --test-dir build` — 23 tests.

| Suite | Kind | Hardware-verified? |
|---|---|---|
| Types/Q88/Settings/Status/Trigger (`test_protocol.cpp`) | byte vectors hand-built from disassembled offsets/encodings (not from the encoder) | **No** — derived from static analysis |
| Session / Mock / UsbTransport policy (`test_session.cpp`) | simulated: timeout, short read, disconnect/replug, stall, size checks, write-refusal | n/a (simulated) |
| python_legacy_protocol | legacy prototype | No |

Hardware validation order (when the device is attached) is in `TODO.md`.
