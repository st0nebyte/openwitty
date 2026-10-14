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
| `src/cli` | `lewitt-cli` (devices / diagnose / read / probe; **no write command**) |
| `docs/HARDWARE_TEST_PLAN.md` | ordered, read-only validation procedure for the real device |
| `tests/cpp` | 21 GoogleTest cases (+ legacy Python test via CTest) |
| `tools/re`, `tools/py` | reverse-engineering scripts; Python prototype (legacy) |
| `docs/USB_ACCESS.md` | the Linux driver-conflict analysis |
| `research/` | proprietary installers/extractions — git-ignored |

Safety defaults: hardware writes are refused (policy *and* transport level);
no driver detach unless `--detach` **and** `LEWITT_ALLOW_DISRUPTIVE=1`;
firmware/DFU is not implemented.

See `BUILDING` section in `DEVELOPMENT.md`, `PROTOCOL.md`, `FEATURES.md`.

## Status

**Experimental — not hardware validated.** No real CONNECT 2 has been used. The protocol comes
from static analysis; hardware writes are disabled; the simulated device is always labelled as
such. See docs/AUDIT_PHASE5E.md (audit), docs/HARDWARE_TEST_PLAN.md (first-device procedure) and
docs/LICENSE_ASSESSMENT.md (no licence chosen yet).

## Installing

See [docs/INSTALLATION.md](docs/INSTALLATION.md) (Ubuntu `.deb`, Arch/CachyOS PKGBUILD, user-local),
[docs/USB_PERMISSIONS.md](docs/USB_PERMISSIONS.md) (udev rule, EACCES vs EBUSY) and
[docs/PACKAGING.md](docs/PACKAGING.md). Packages are **unofficial and experimental**; the
protocol is not validated on hardware, hardware writes are disabled, and no project licence has
been chosen yet.
