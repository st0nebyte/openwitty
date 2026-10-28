# Claude Code session start (home computer with a real CONNECT 2)

> **Historical document (written before the first hardware session, 2026-10-14).** Its statements that the
> software was never run on a CONNECT 2 or that all writes are disabled are outdated. Current status:
> `README.md`, `FEATURES.md`, `docs/HARDWARE_TEST_RESULTS.md`, `docs/VOLUME_INTEGRATION.md`,
> `docs/RELEASE_READINESS.md`. **Start here instead (2026-10-28):** `docs/FEATURE_COMPLETION_ROADMAP.md` →
> "Phase 11 status and remaining plan".

You are continuing **OpenWitty**, an unofficial Linux control app for the LEWITT CONNECT 2.
Read `docs/HANDOFF_HARDWARE_TESTING.md` first, then `docs/HARDWARE_TEST_PLAN.md`.

## Ground truth

* **The code has never been validated against physical hardware.** Protocol facts are from
  static analysis only. The simulated device is *not* evidence.
* This session is the first chance to produce real evidence. Be a careful experimenter.

## Rules

1. **Read-only first.** Use `openwitty-cli devices|diagnose|list|read …` as in the plan. No writes,
   no triggers, no mode switch, no factory reset, no firmware, no DFU. Do not enable
   `allowWrites`/`allowDisruptive` or add a write command without explicit user approval.
2. Do not detach kernel drivers, unbind interfaces, reset the device, or change udev/system
   settings yourself. Ask the user to run administrator steps (`! sudo …` in the prompt) and
   explain them first. Never use sudo on your own.
3. One variant at a time; stop at the first unexpected result (PID `000a`, vanished sound card,
   wrong sizes, repeated timeouts) and report verbatim output.
4. Record all results in a new `docs/HARDWARE_RESULTS_<date>.md` (raw hex, command, exit code,
   kernel/driver state). Update `PROTOCOL.md` evidence levels only for what was actually observed.
5. Keep the safety guards and tests (`ctest` baseline ≥ 96 entries; `audit_usb_call_sites`
   must keep passing). Add tests for every behaviour change.
6. Do not commit vendor installers, binaries, firmware, decompiled output or captures
   (`research/`, `.tools/`, `.sysroot/` are ignored). Do not push to the public.
7. Never present simulated data as real; keep the app labelled unofficial / experimental.

## Quick start

```sh
git clone git@github.com:st0nebyte/openwitty.git && cd openwitty
cmake --preset release && cmake --build --preset release
QT_QPA_PLATFORM=offscreen ctest --preset release       # expect all pass
build-release/src/cli/openwitty-cli diagnose            # (path may be build-release/openwitty-cli)
build-release/src/gui/openwitty --mock                  # simulated GUI
```

Dependencies (Ubuntu): `qt6-base-dev qt6-declarative-dev libusb-1.0-0-dev libgtest-dev ninja-build
cmake qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls
qml6-module-qtquick-layouts`; (Arch) `qt6-base qt6-declarative libusb gtest cmake ninja`.
Without admin rights use `tools/setup-sysroot.sh` (see DEVELOPMENT.md).

## First tasks

1. `openwitty-cli diagnose`; interpret EACCES / EBUSY (HANDOFF §6).
2. Ask the user to install `packaging/udev/70-openwitty.rules` if EACCES.
3. Run plan steps 5–8 read-only, variant by variant; save outputs.
4. Compare decoded settings/status with the device's LED/display state the user reports.
5. Report findings and propose the next experiment; wait for approval before any write.
