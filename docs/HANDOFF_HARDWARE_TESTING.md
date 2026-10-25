# Handoff: first real-hardware testing of OpenWitty (LEWITT CONNECT 2)

> **Historical document (written before the first hardware session, 2026-10-14).** Its statements that the
> software was never run on a CONNECT 2 or that all writes are disabled are outdated. Current status:
> `README.md`, `FEATURES.md`, `docs/HARDWARE_TEST_RESULTS.md`, `docs/VOLUME_INTEGRATION.md`,
> `docs/RELEASE_READINESS.md`.

Written for a fresh Claude Code (or human) session with **no access to the conversation that
produced this project**. Read this file, then `docs/CLAUDE_SESSION_START.md`.

> **The software has never been run against a physical CONNECT 2.** Every protocol statement
> below comes from static analysis of the vendor's macOS and Windows control applications and
> is *unverified*. Treat the first hardware session as an experiment whose purpose is to
> produce evidence, not as a deployment.

## 1. Purpose and status

OpenWitty is an independent, MIT-licensed Linux control application for LEWITT CONNECT audio
interfaces (initially the CONNECT 2; CONNECT 6 is only an architectural possibility). It is not
affiliated with LEWITT. Status: **experimental, not hardware validated**. A simulated device
exercises the whole stack. Hardware **writes are disabled** at three independent layers
(GUI policy, `DeviceSession`, `UsbTransport`).

## 2. Architecture (details: `ARCHITECTURE.md`)

```
src/protocol    message types, little-endian codecs, Q8.8, WriteGuard      (Qt-free, no I/O)
src/transport   IDeviceTransport; MockTransport; UsbTransport (libusb);
                SerializedTransport (1 request in flight, cancel, stats);
                UsbDiagnostics (sysfs only, no USB traffic); UsbErrors
src/core        DeviceSession (handshake, sync, staged edits, merge, policy),
                DeviceManager (sysfs discovery), Policy (documented restrictions)
src/cli         openwitty-cli  (read-only; alias lewitt-cli)
src/gui         Qt 6/QML app `openwitty`; DeviceController (GUI thread) + DeviceWorker (I/O thread)
tests/cpp, tests/gui   GoogleTest (core) and GUI integration (offscreen)
```

Stack: C++20, Qt 6.4+/QML, libusb-1.0, CMake ≥ 3.25 + Ninja, GoogleTest. Python/Node/Ghidra/REA are
*development-only* (reverse engineering), never runtime dependencies.

## 3. Build and test baseline

```sh
# normal system with Qt 6 dev packages (qt6-base-dev qt6-declarative-dev libusb-1.0-0-dev
# libgtest-dev ninja-build + qml6-module-qtquick{,-window,-controls,-layouts} on Ubuntu)
cmake --preset release && cmake --build --preset release
QT_QPA_PLATFORM=offscreen ctest --preset release
```

Rootless alternative (no sudo): `tools/setup-sysroot.sh` downloads/extracts the Qt dev
packages into `./.sysroot` (checksum-verified through apt); then export the variables it
prints (`PKG_CONFIG_PATH`, `LD_LIBRARY_PATH`, `QML_IMPORT_PATH`, `CMAKE_PREFIX_PATH`).

Baseline at hand-off: **96 CTest entries pass** on the full release build (the
`visual_geometry` entry needs a project venv with Pillow/numpy and is skipped otherwise, so a
clean machine reports 95). ASan+UBSan pass (GUI included); TSan passes for the Qt-free core only.
Expected non-regressions: do not lower this baseline; add tests with every behavioural change.

Mock smoke test: `build-release/src/gui/openwitty --mock --screenshot /tmp/x.png`
(or `openwitty-cli read settings --mock`).

## 4. USB transport (what the code sends)

All of it is in `src/transport/UsbTransport.cpp`; constants in `src/protocol/Types.hpp`.

| Item | Value |
|---|---|
| Vendor id | `0x29C2` (LEWITT) |
| Product ids | `0x0004` normal mode, `0x0002` "macOS streaming" mode (both are "APP" modes the vendor app accepts); `0x000A` = probable bootloader/DFU — **stop** |
| Request type | class request, **interface** recipient: GET `0xA1`, SET `0x21` (experimental: device recipient `0xA0`/`0x20`) |
| bRequest | `0x03` (UAC2 "MEM"); alternative UAC1 style GET `0x85` / SET `0x05` |
| wValue | `(apiMajor << 8) \| messageId`; `apiMajor = 1` for `iface_*` messages, `0` for the API-version message |
| wIndex | `0x3300 \| AudioControl interface number` (unit id `0x33`) |
| wLength | message size (`0x80` settings, `0x40` others) |
| Timeout | 500 ms |

Messages: `0` settings (R/W, 0x80 B) · `2` status (R, 0x40) · `3` version (R, opaque 0x40) ·
`4` triggers (W, 0x40) · `0x0A` API handshake (R, 0x40; byte0 major must be 1, byte1 minor).
Field-level layouts: **`PROTOCOL.md`** (authoritative), implemented in `src/protocol/Messages.*`.

### Settings (message 0, 0x80 bytes) — highlights

62 schema records cover bytes 0–70; bytes 71–127 are never touched by the vendor app and are
preserved verbatim. Gain: i16 LE Q8.8 dB (mic 0–72, instrument 0–60 enforced). Volumes: i16
Q8.8, −60…0 dB. Booleans are 0/1 (non-0/1 values are preserved, never rewritten). Preamp 0/1/2
(Clean/Warm/Vivid). Monitor source 0 instrument / 1 microphone / 2 both. Mix is stored
sign-inverted. Custom-button enum: 0, 1, 2, 8, 9, 0x0C, 0x0D, 0x0E (others preserved, shown as
unknown). Output groups: loopback, speaker, headphone (+ monitor). Offsets: `settings_off` in
`Messages.hpp`.

### Status (message 2) — highlights

Meters are Q8.8 dB clamped to [−60, 0]: input1 @0x00, input2 @0x09, loopback L/R @0x12/0x14,
speaker @0x16/0x18, headphone @0x1A/0x1C. Auto-setup progress @0x24, result bits @0x23 (bit6 =
idle/in-progress, else bit5 set = success; **meaning of 1 vs 2 unverified**), clip counters u16
@0x2E/0x30.

### Triggers (message 4, word 0 = u16 LE command, rest zero)

`1/2/3` autogain mic/instrument/both · `6/7/8` denoise measurement · `4/5` begin/end
measurement-session bracket · `0x103` reset LED colours (persistent) · `0x104` switch to
macOS-streaming mode (PID 0x0002) · `0x105` back to normal (PID 0x0004) · `0x106` factory reset.
Mode switches restart/re-enumerate the device. **`0x106` is never sent by any code path.**

### Device mode

`deviceModeFromPid`: `0x0004` → Normal, `0x0002` → macOS streaming. The vendor app rejects
other PIDs ("device mode not recognized"); OpenWitty does the same on hardware.

## 5. Known uncertainties (all unverified on hardware)

1. Whether the firmware accepts the control requests at all through the interface recipient
   *and* whether Linux lets userspace issue them (see §6).
2. UAC2 `0x03` vs UAC1 `0x85/0x05` selection (vendor app switches on a version-like field ≥ 0x0B;
   not auto-selected here — try both, read-only).
3. Meaning of autosetup result 1 vs 2; stale progress=100 behaviour (see `docs/AUDIT_PHASE5E.md`).
4. Input-2 phantom, native-volume-mapping (byte 59) and the macOS-streaming flag offsets
   (only mobile-streaming @67 is located).
5. Layout of `iface_version_t` (opaque).
6. The vendor behaviour for stalls/timeouts is unknown; OpenWitty treats a stall as "rejected",
   not as a lost link.

## 6. Linux driver ownership and permissions (details: `docs/USB_ACCESS.md`, `docs/USB_PERMISSIONS.md`)

* `snd-usb-audio` binds the AudioControl interface for normal audio. usbfs control requests
  with an **interface** recipient must *claim* that interface → `EBUSY` while the kernel
  driver owns it. Detaching the driver drops the whole sound card (disruptive). A device-recipient
  request is not blocked by usbfs, but firmware acceptance is unknown (`--method device-recipient`).
* `/dev/bus/usb/BBB/DDD` is `root:root 0664` by default → `EACCES` for a user (usbfs ioctls
  need a writable fd). Fix: `packaging/udev/70-openwitty.rules` (`TAG+="uaccess"`, must sort
  before `73-seat-late.rules`) — **administrator step, never done automatically**:
  `sudo install -m0644 packaging/udev/70-openwitty.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=29c2`
* `EACCES` = permissions (rule fixes it); `EBUSY` = driver ownership (rule does *not* fix it);
  `EPIPE` = the device rejected the request. The CLI exit codes: 3/4/5 respectively,
  6 timeout, 7 absent, 8 unsupported, 9 bad response.
* Long-term recommended route (not implemented): a `snd-usb-audio` quirk + hwdep interface
  (precedents: Scarlett2). See `docs/USB_ACCESS.md` option E.

## 7. Safety guards (do not weaken)

| Layer | Guard |
|---|---|
| `UsbOptions` | `allowWrites=false`; `allowDisruptive=false` gates detach/claim at the transport |
| `UsbTransport::write` | re-runs `checkWriteAllowed`; refuses FactoryReset and mode switches regardless of options |
| `DeviceSession` | `writesEnabled()` false on hardware; DSP/routing restrictions; unknown settings bytes must equal the device snapshot (`verifyOnlyKnownFieldsChanged`) |
| `WriteGuard` | whitelist of trigger words; payload beyond word 0 must be zero; size checks |
| CLI | no write command; `--method detach` additionally needs `LEWITT_ALLOW_DISRUPTIVE=1` |
| Static | `tools/audit/check_usb_call_sites.sh` (ctest `audit_usb_call_sites`) fails if a new USB call site appears |

Never enable hardware writes, mode switching, factory reset or firmware operations without
explicit, written approval from the owner and a separate test plan (HARDWARE_TEST_PLAN.md §11).

## 8. Exact diagnostic commands for the first session (all read-only)

```sh
openwitty-cli --version
openwitty-cli list
openwitty-cli devices            # sysfs: descriptors, interfaces, bound drivers, node permissions
openwitty-cli diagnose           # + interpretation and next steps
lsusb -v -d 29c2:                # optional; may need root for strings
openwitty-cli read api           # one GET of message 0x0A; default interface recipient, UAC2
openwitty-cli read api --request uac1
openwitty-cli read api --method device-recipient
# only after one variant succeeds:
openwitty-cli read version ; openwitty-cli read status ; openwitty-cli read settings
openwitty-cli probe              # dry-run matrix; `--execute` performs the same GET-only matrix
openwitty --mock                 # GUI sanity check without hardware; plain `openwitty` = USB, read-only
```

Follow `docs/HARDWARE_TEST_PLAN.md` (steps 1–10 + Appendix A abort criteria). Record raw hex
verbatim. Stop at the first surprise (PID `000a`, vanished sound card, wrong sizes, 3 timeouts).

## 9. Troubleshooting

| Symptom | Likely cause / action |
|---|---|
| `open failed [access-denied]`, exit 3 | install the udev rule (§6) or replug after installing |
| `[busy]`, exit 4 | snd-usb-audio owns the interface: expected; try `--method device-recipient`; do not detach without approval |
| `[stall]`, exit 5 | request variant/recipient/wValue not what the firmware expects: try `--request uac1` |
| `[timeout]`, exit 6 | retry once; check mode (PID) with `devices`; replug |
| GUI starts but "No LEWITT device connected" | no 29c2 device in sysfs, or PID not 0002/0004 |
| QML module not found at start | install `qml6-module-qtquick`, `-window`, `-controls`, `-layouts` (Ubuntu) / `qt6-declarative` (Arch) |
| TSan build fails in test discovery | run build and ctest under `setarch -R` |

## 10. Known limitations

* No hardware validation of any kind; simulated results are never real evidence.
* GUI is read-only on hardware by design.
* QML AOT disabled (Qt 6.4 singleton issue).
* USB mode retries a present-but-unusable device with 1→8 s backoff forever (reads only).
* CONNECT 6 not implemented. Firmware update/DFU and factory reset intentionally absent.
* Packages never installed on a real system; `namcap` never run; makepkg run only on Ubuntu.

## 11. Remaining engineering tasks (priority order)

1. Execute the read-only hardware plan; capture results; update `PROTOCOL.md` evidence levels.
2. Decide the access method from the results (interface vs device recipient vs hwdep).
3. Auto-select UAC1/UAC2 variant from `bcdDevice` once known.
4. Re-examine autogain completion inference and stale progress before any write test.
5. Write a *separate* approved write-test plan (one reversible setting first, e.g. headphone mute).
6. Locate remaining offsets (macOS-streaming flag, input-2 phantom, version layout).
7. Stop retrying after EACCES/EBUSY until the device list changes.
8. Architecture for CONNECT 6 (device-profile abstraction) — design only after CONNECT 2 works.

## 12. Where things are documented

`PROTOCOL.md` (wire/field evidence) · `ARCHITECTURE.md` · `FEATURES.md` · `docs/USB_ACCESS.md`
(kernel-source-verified driver analysis) · `docs/USB_PERMISSIONS.md` · `docs/HARDWARE_TEST_PLAN.md`
· `docs/AUDIT_PHASE5E.md` (audit, fixes, limitations) · `docs/PACKAGING.md`, `docs/INSTALLATION.md`
· `docs/THIRD_PARTY.md` · `TESTING.md`, `DEVELOPMENT.md`, `TODO.md`.
