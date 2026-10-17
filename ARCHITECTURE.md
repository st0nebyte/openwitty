# Architecture

```
QML ──► DeviceController (GUI thread) ══queued══► DeviceWorker (QThread)
                                                     │  rescan/backoff (sysfs only)
                                              DeviceSession  (policy, staging, sync, failure accounting)
                                                     │  ↘ WriteGuard (size / command allow-list / known-bytes)
                                                     │  ↘ WriteVerification (commitAndVerify: bounded read-only
                                                     │     convergence polling after a write; see below)
                                                     │
                       protocol/Messages (pure)      │
                                           SerializedTransport  (1 request in flight, cancel, stats)
                                                     │
                                              IDeviceTransport
                                         ┌──────────┴───────────┐
                                    MockTransport          UsbTransport (libusb + ControlLease)
                                    (fault injection)      method: interface | device-recipient | detach(opt-in)
                                                           (future: HwdepTransport -> snd-usb-audio quirk,
                                                            see docs/USB_ACCESS.md option E)
DeviceManager ── UsbDiagnostics ── sysfs (descriptors, drivers, node permissions; no I/O)
```

## Layers and invariants
| Layer | Responsibility | Invariants |
|---|---|---|
| `protocol/` | byte-level codecs, enums, schema, write guard | no Qt, no USB, no I/O; raw bytes are the source of truth; unknown bytes round-trip; boolean/enum bytes are rewritten only when the requested state differs |
| `transport/` | move bytes | one request in flight per process (`SerializedTransport`); cooperative per-user `ControlLease` on real control opens; every call bounded (500 ms) and cancellable between requests; errors classified, never merged (EACCES ≠ EBUSY ≠ EPIPE) |
| `core/DeviceSession` | handshake, sync, staging, failure accounting | handshake (0x0A, major 1, known PID mode) before any other read; 3 consecutive unresponsive transport failures or 3 unresponsive settings refreshes (even with good meter reads between them) → `Error` + reconnect; stall/busy/permission never escalate; old status is cleared on reconnect |
| `core/WriteVerification` | turns "the write ioctl didn't error" into "a read confirms it" | `commitAndVerify()` always re-reads fresh before writing (reuses the device-wins merge); never retries the SET itself; bounded, read-only polling only; any byte changing outside the intended diff is `Ambiguous`, never silently accepted |
| `core/DeviceManager` | discovery | sysfs only; hot-plug diffs; classifies APP modes |
| `gui/` | Qt facade | all device I/O on the worker thread; only changed keys cross threads; shutdown cancels the in-flight request first |

## State machine (DeviceSession)
`Disconnected → Connecting → Connected` ; `Connected → Error` after repeated timeouts/I/O errors
or a failed settings sync; `Error/Disconnected → (reconnect) → Connecting`. A `Stall` never changes
state. `Cancelled` never changes state or counters.

This is the *connection* state machine; it's separate from the per-write `WriteState` machine
(`core/WriteVerification.hpp`): `Prepared → Submitted → TransferCompleted → (AwaitingConvergence →)
Verified | VerificationTimedOut | Ambiguous | Failed`. A successful USB transfer alone
(`TransferCompleted`) is never surfaced as success to callers — only `Verified` is. The GUI uses
`values()["ui.state"]` as its sole connection-presentation state. `ui.detail` is descriptive;
`write.state` and `write.verifying` preserve separate write-verification state. On real hardware,
“connected” means a successful API handshake and settings decode, **not** proven audio or
front-panel readiness. Initial status-read failure is currently non-fatal.

## Threading
* `DeviceSession`/transports are used by the worker thread only; `requestCancel()` is the one
  cross-thread call (atomic flag, non-blocking).
* GUI thread owns `values_`; worker emits diffed `QVariantMap`s via queued connections.
* Opt-in `OPENWITTY_TRACE_LATENCY=1` logs monotonic poll/GET/decode/merge/GUI/QML/frame stages
  without payloads or identifiers. These markers do not timestamp the physical action itself.
* `ControlLease` is a per-user, process-lifetime `flock` on a private runtime file. It prevents
  concurrent access by updated OpenWitty clients, not unrelated software or older binaries.
* Shutdown: `cancelAll()` → blocking `disconnectDevice` → `quit` → `wait`.

## Safety enforcement points (defence in depth)
1. GUI disables controls when `!writable`.
2. `DeviceSession` refuses writes unless `writesEnabled()`, checks `checkWriteAllowed`, and
   `verifyOnlyKnownFieldsChanged`.
3. `UsbTransport::write` refuses unless `allowWrites` and re-runs `checkWriteAllowed`
   (destructive words never pass on hardware).
4. CLI has one `write` dispatch with two narrowly scoped field specs (LED brightness and
   instrument mute); real execution requires `--execute`, `LEWITT_ALLOW_HARDWARE_WRITE=1`, and
   the offset allowlist, with an additional opt-in for instrument mute. Neither is used in Phase
   7D. The USB call-site audit checks this scope. `--method detach` needs a separate opt-in.
5. `commitAndVerify()` never retries a SET on an uncertain result (`Ambiguous`/timeout) — the caller
   decides, it isn't automatic.
