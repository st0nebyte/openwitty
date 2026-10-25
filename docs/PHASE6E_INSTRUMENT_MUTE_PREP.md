# Phase 6E: instrument-mute write — preparation (no hardware, nothing executed)

Status: **prepared and tested against simulation only.** No instrument-mute SET has ever
been sent to a real CONNECT 2. Authorization to *execute* it on hardware has **not** been
given; the existing hardware-write approval covers LED brightness (byte 37) only.

Baseline: branch `hardware-validation-2026-10-14` @ `dd34dc3` (106 tests). This branch adds
11 tests (117 total on the full build).

## What was added

| Piece | Where | Purpose |
|---|---|---|
| `WritePolicy::hardwareWriteOffsets` | `src/core/DeviceSession.{hpp,cpp}` | On a non-simulated transport a settings write may change **only** these byte offsets. Default empty = nothing is authorized on hardware, even with `allowHardwareWrites`. Enforced in `commit()` (so also in `commitAndVerify()`). Simulated transports ignore it. |
| `PollPolicy::requireStableSnapshot` | `src/core/WriteVerification.hpp`, `commitAndVerify()` | Two consecutive settings reads must agree immediately before the SET; otherwise **nothing is sent** (`Failed`, "device settings changed while preparing"). Off by default. |
| `openwitty-cli write instrument-mute <on\|off>` | `src/cli/main.cpp` | The only new write surface: settings byte 12 only. Shares one implementation with `led-brightness` (`cmdWriteField`). |
| Tests | `tests/cpp/test_hardware_scope.cpp`, `tests/cli/test_cli_write_scope.sh` | Offset scoping, stability check, external/multi-channel changes, CLI gating. |
| Audit | `tools/audit/check_usb_call_sites.sh` | Exactly 2 field specs, per-field authorization variable present, CLI scopes `hardwareWriteOffsets`, GUI never enables hardware writes. |

## Safety properties (all unit-tested on simulation)

1. **No arbitrary settings write.** Two named fields exist; nothing takes an offset or a raw payload.
2. **Core-level scope.** The CLI sets `hardwareWriteOffsets = {12}` for this command; any other
   differing byte (even a valid, known field such as byte 2 or 37) is refused with
   `WritesDisabled` *before* any transfer (`HardwareScope.OtherOffsetsAreRefused…`).
3. **Unrelated bytes preserved.** The payload is the fresh 128-byte snapshot with byte 12
   changed (`setMute(1, …)` → `setBoolByte`); unknown bytes cannot differ
   (`verifyOnlyKnownFieldsChanged`), and the CLI re-checks the diff is exactly `{12}`.
4. **Identity/API.** `connect()` validates API major == 1 and a recognized CONNECT 2 APP-mode PID;
   the command additionally requires PID `0x0004` on real hardware (exit 8 otherwise).
5. **Fresh snapshot.** `connect()` reads, then `refreshSettings()` again before the payload is
   built, then `commitAndVerify()` reads again (device-wins merge) and, with
   `requireStableSnapshot`, once more back-to-back.
6. **Explicit authorization.** Real hardware + `--execute` needs `LEWITT_ALLOW_HARDWARE_WRITE=1`
   **and** `LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE=1`. Without `--execute` it is a dry run that sends
   no write at all (transport `allowWrites=false`). The LED authorization does not unlock this
   field and vice versa (`cli_write_scope`).
7. **One SET, never retried.** Convergence polling is read-only and bounded (≤ ~570 ms).
8. **No** detach/claim, reset, mode switch, trigger word, firmware/DFU. Interface 3 only.
9. **GUI stays read-only on hardware** (`allowHardwareWrites` is never set in `src/gui`;
   enforced by the audit script).

CLI result codes for the new path: `0` verified (or nothing to write), `2` authorization missing,
`8` not PID 0x0004 / unsupported, `10` SET sent but **not confirmed** (timed out or ambiguous),
others as for reads.

## Known hazard: the SET is not atomic

The device takes the whole 128-byte struct. Any physical action between our last read and the
SET (e.g. a **Panic Mute** long-press that flips bytes 2, 12, 22 and 29) is silently reverted for
every byte we resend with a stale value — and the post-write read would then look "clean".
Mitigation implemented: the back-to-back stability check shrinks the window to roughly one USB
round trip; it cannot remove it. Consequently:

* run the experiment with hands off the device, at low monitor volume;
* a later improvement would be to find whether the firmware accepts a narrower write — unknown;
* never run this unattended or in a loop.

## Proposed hardware procedure (NOT authorized; for a later session)

1. `openwitty-cli read settings --interface 3` → record bytes 2, 12, 22, 29, 19, 56 (instrument
   currently unmuted, headphone/speaker unmuted).
2. `openwitty-cli write instrument-mute on` (dry run) → expect `changed byte offsets: 12`.
3. With explicit approval: `LEWITT_ALLOW_HARDWARE_WRITE=1 LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE=1
   openwitty-cli write instrument-mute on --execute`; expect `write state: verified`.
4. Confirm physically (instrument channel mute indication) and by `read settings`.
5. Roll back: same command with `off`. Record both transcripts in HARDWARE_TEST_RESULTS.md.
6. Abort on `ambiguous`, `verification-timed-out`, or any unexpected offset; do not retry the SET.

## Overlap with the unpushed home-computer Panic Mute work

Files this branch touched that the home-computer commits may also touch:

| File | Risk | Handling |
|---|---|---|
| `tests/cpp/test_hardware_scope.cpp` (new) | none | independent; no use of `simulatePanicMute()` |
| `src/transport/MockTransport.*` | **not modified here** | Panic Mute simulation lands untouched |
| `CMakeLists.txt` | low | one `target_sources(…)` line + two `add_test` blocks; the Panic Mute commits' 3 tests may add to an existing test file or this list |
| `src/core/DeviceSession.{hpp,cpp}` | low-medium | additive (`hardwareWriteOffsets`, stability check, one block in `commit()`); Panic Mute work is expected to be mock/derived-property only |
| `src/cli/main.cpp` | medium if the home work touched the LED command | the two write paths were unified into `cmdWriteField`; resolve by keeping both semantics |
| `tools/audit/check_usb_call_sites.sh` | low | additions only, near the end |
| `docs/*` | **not modified** except this new file | `TODO.md`, `FEATURES.md`, `PROTOCOL.md`, `HARDWARE_TEST_RESULTS.md` deliberately left for the integration |

Deferred until the Panic Mute commits are integrated: tests that combine a *simulated physical
Panic Mute* with `commitAndVerify()` (multi-channel mute while a write is in flight) using
`MockTransport::simulatePanicMute()` and the derived `mute.allFour` property; documentation
updates in PROTOCOL/FEATURES/TODO; the status bytes 3–4/12–13 remain unidentified and are not
interpreted anywhere here. No software Panic Mute trigger exists or was added.
