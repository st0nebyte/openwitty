# LEWITT CONNECT 2 for Linux (work in progress)

Native Linux control application for the LEWITT CONNECT 2, built from
interoperability analysis of the official Control Center. **No hardware
verification exists yet** (device not available during development).

Stack: C++20 core (Qt-free) · Qt 6 / QML GUI · libusb · CMake · GoogleTest.

| Path | What |
|---|---|
| `src/protocol` | message types, explicit LE codecs, Q8.8 (no Qt, no USB) |
| `src/transport` | `IDeviceTransport`, `MockTransport`, `UsbTransport` (libusb) |
| `src/core` | `DeviceSession`: sync, staged edits, write policy |
| `src/gui` | Qt Quick app, threaded `DeviceController`, QML components |
| `src/cli` | `lewitt-cli` (list / read / probe; **no write command**) |
| `tests/cpp` | 21 GoogleTest cases (+ legacy Python test via CTest) |
| `tools/re`, `tools/py` | reverse-engineering scripts; Python prototype (legacy) |
| `docs/USB_ACCESS.md` | the Linux driver-conflict analysis |
| `research/` | proprietary installers/extractions — git-ignored |

Safety defaults: hardware writes are refused (policy *and* transport level);
no driver detach unless `--detach` **and** `LEWITT_ALLOW_DISRUPTIVE=1`;
firmware/DFU is not implemented.

See `BUILDING` section in `DEVELOPMENT.md`, `PROTOCOL.md`, `FEATURES.md`.
