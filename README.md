<p align="center"><img src="assets/github/openwitty-banner.svg" alt="OpenWitty — Native Linux control for LEWITT CONNECT audio interfaces" width="900"></p>

# OpenWitty

**Your interface. Your Linux. Your control.**

OpenWitty is an independent, open-source project bringing native Linux control to **LEWITT CONNECT audio interfaces**.

The project aims to provide an alternative to the proprietary LEWITT CONTROL CENTER software, allowing Linux users to configure and control their audio interfaces without relying on Windows, macOS, or virtual machines.

## Why OpenWitty?

LEWITT makes great audio hardware. Unfortunately, its CONTROL CENTER software is currently only available for Windows and macOS.

While CONNECT interfaces can retain their settings when moved between operating systems, Linux users currently need access to Windows or macOS for software-based configuration.

**OpenWitty aims to change that.**

By reverse engineering the communication protocol used by LEWITT CONTROL CENTER, this project aims to implement the necessary device controls natively on Linux.

This project is not about criticizing LEWITT or its software. It's about making great hardware more accessible to users of an operating system that isn't officially supported.

## Project Status

**CONNECT 2 read-only GUI validated on one physical device; broader control features remain under development.**

The initial target is the **LEWITT CONNECT 2**; **CONNECT 6** support is planned but not implemented.

What exists today:

- A native **C++20** backend (protocol codecs, transports, device session, write policy) with no Qt dependency.
- A **Qt 6 / QML** control application and a read-only diagnostic CLI (`openwitty-cli`).
- A reconstructed **device-control protocol** for the CONNECT 2, derived from static analysis and cross-checked between the macOS and Windows control applications.
- A **simulated device** (mock mode) that exercises every control in the GUI and is always labelled as simulated.
- Automated protocol, session, hardening, GUI, QML, packaging, and sanitizer test suites, plus a documented USB security audit.
- A hardware-validated, read-only CONNECT 2 GUI with **16 Hz settings / 48 Hz status** polling by default; meters render independently with a 60 FPS target.
- **Ubuntu/Debian (`.deb`) and Arch/CachyOS (PKGBUILD) packaging**, desktop integration and a udev rule (documented, never installed automatically).

What does **not** exist yet:

- The real-device GUI remains read-only. Broad hardware writes remain disabled by design; the only
  exercised write is the separately gated, narrowly scoped CLI LED-brightness validation.
- Firmware update and factory reset are intentionally not implemented.
- OpenWitty is **not yet a functional replacement** for CONTROL CENTER.

## Device Support

| Device | Status |
|---|---|
| LEWITT CONNECT 2 | Read-only transport and GUI validated on one device; most controls remain simulated or individually unvalidated |
| LEWITT CONNECT 6 | Planned (architecture keeps device support separable; nothing implemented) |

## Goals

- Provide native Linux control for LEWITT CONNECT audio interfaces.
- Understand and document the communication protocol used by LEWITT CONTROL CENTER.
- Enable device configuration without requiring Windows, macOS, or virtual machines.
- Support multiple CONNECT devices through an extensible architecture.
- Encourage community contributions and knowledge sharing.
- Keep the project free, transparent, and accessible.

## Features (CONNECT 2; mixed evidence levels)

- Device discovery and connection state, with clear permission/driver diagnostics (`EACCES` vs `EBUSY` vs stall)
- Input gain, mute, low-cut, Clipguard, 48 V, Clean/Warm/Vivid, Denoise, Compressor
- Loopback / speaker / headphone volume and mute, input–playback mix, dim, mono
- LED brightness and colours, custom-button function, settings preservation of unknown device bytes
- Autosetup / autogain workflow (simulated)
- Pending-edit tracking and merge with external device changes

See [FEATURES.md](FEATURES.md) for the exact list and evidence level.

### Desktop volume (KDE/PipeWire)

On Linux 7.3 the CONNECT 2's hardware volume may be missing from the desktop because of a broken microphone
control on the device; a per-device kernel compatibility quirk works around it (opt-in, documented). The optional
`openwitty-volumed` user service keeps the desktop volume in step with the hardware knob. Run
`openwitty-volumed --check` for a read-only diagnosis, and see [docs/VOLUME_INTEGRATION.md](docs/VOLUME_INTEGRATION.md).

## Quick start (no hardware needed)

```sh
cmake --preset release && cmake --build --preset release
ctest --preset release
build-release/src/gui/openwitty --mock      # simulated device
```

With a connected CONNECT 2 and the documented USB permissions, launch the
hardware-validated **read-only** GUI normally (16 Hz settings / 48 Hz status):

```sh
build-release/src/gui/openwitty
```

Rootless build instructions, packaging and the first real-device procedure:

- [docs/INSTALLATION.md](docs/INSTALLATION.md) — Ubuntu `.deb`, Arch/CachyOS, user-local install
- [DEVELOPMENT.md](DEVELOPMENT.md) — toolchain, presets, sanitizers
- [docs/USB_PERMISSIONS.md](docs/USB_PERMISSIONS.md) and [docs/USB_ACCESS.md](docs/USB_ACCESS.md) — udev, driver ownership
- [docs/HARDWARE_TEST_PLAN.md](docs/HARDWARE_TEST_PLAN.md) — ordered, read-only first-device validation
- [docs/HANDOFF_HARDWARE_TESTING.md](docs/HANDOFF_HARDWARE_TESTING.md) — everything needed to continue on a machine with the device
- [PROTOCOL.md](PROTOCOL.md), [ARCHITECTURE.md](ARCHITECTURE.md), [docs/AUDIT_PHASE5E.md](docs/AUDIT_PHASE5E.md)

## Repository layout

| Path | Contents |
|---|---|
| `src/protocol`, `src/transport`, `src/core` | Qt-free core: message codecs, transports (libusb, mock), device session and write policy |
| `src/gui`, `src/cli` | Qt 6 / QML application, diagnostic CLI |
| `assets/` | OpenWitty branding (icons, banner) |
| `packaging/` | Debian, Arch, desktop, AppStream, udev |
| `tests/`, `tools/` | tests; packaging/audit/reverse-engineering tooling |
| `docs/` | engineering documentation |

## Philosophy

We believe choosing Linux shouldn't mean giving up control over your hardware.

OpenWitty is built around three simple ideas:

- **Freedom:** Use your hardware on the operating system of your choice.
- **Openness:** Share knowledge, document discoveries, and develop in public.
- **Community:** Build solutions together where official support is unavailable.

**Made for Linux. Open for everyone.**

## Contributing

Contributions, ideas, protocol discoveries, testing, and documentation are welcome.

Whether you're experienced in reverse engineering, audio programming, USB communication, or simply interested in helping, your involvement is appreciated.

More detailed contribution guidelines will be added as the project evolves.

## License

OpenWitty is released under the **MIT License**.

See [LICENSE](LICENSE) for details, and [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) for dependencies and asset notes.

## Disclaimer

OpenWitty is an independent, community-driven open-source project.

**This project is not affiliated with, sponsored by, or endorsed by LEWITT GmbH.**

LEWITT, CONNECT, and CONTROL CENTER are trademarks or product names of their respective owners.

All trademarks are used solely for identification and compatibility purposes.

OpenWitty does not distribute proprietary LEWITT software or firmware, and does not use LEWITT artwork.

---

*Made for Linux. Open for everyone.*
