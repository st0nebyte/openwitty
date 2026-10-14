# Architecture

```
QML ──► DeviceController (GUI thread) ══queued══► DeviceWorker (QThread)
                                                     │  rescan/backoff (sysfs only)
                                              DeviceSession  (policy, staging, sync, failure accounting)
                                                     │  ↘ WriteGuard (size / command allow-list / known-bytes)
                                                     │
                       protocol/Messages (pure)      │
                                           SerializedTransport  (1 request in flight, cancel, stats)
                                                     │
                                              IDeviceTransport
                                         ┌──────────┴───────────┐
                                    MockTransport          UsbTransport (libusb)
                                    (fault injection)      method: interface | device-recipient | detach(opt-in)
                                                           (future: HwdepTransport -> snd-usb-audio quirk,
                                                            see docs/USB_ACCESS.md option E)
DeviceManager ── UsbDiagnostics ── sysfs (descriptors, drivers, node permissions; no I/O)
```

## Layers and invariants
| Layer | Responsibility | Invariants |
|---|---|---|
| `protocol/` | byte-level codecs, enums, schema, write guard | no Qt, no USB, no I/O; raw bytes are the source of truth; unknown bytes round-trip; boolean/enum bytes are rewritten only when the requested state differs |
| `transport/` | move bytes | one request in flight (`SerializedTransport`); every call bounded (500 ms) and cancellable between requests; errors classified, never merged (EACCES ≠ EBUSY ≠ EPIPE) |
| `core/DeviceSession` | handshake, sync, staging, failure accounting | handshake (0x0A, major 1, known PID mode) before any other read; stalls/busy/denied never kill the link; 3 consecutive timeouts/I/O errors → `Error` + reconnect; edits never leave the "known writable bytes" set |
| `core/DeviceManager` | discovery | sysfs only; hot-plug diffs; classifies APP modes |
| `gui/` | Qt facade | all device I/O on the worker thread; only changed keys cross threads; shutdown cancels the in-flight request first |

## State machine (DeviceSession)
`Disconnected → Connecting → Connected` ; `Connected → Error` after repeated timeouts/I/O errors
or a failed settings sync; `Error/Disconnected → (reconnect) → Connecting`. A `Stall` never changes
state. `Cancelled` never changes state or counters.

## Threading
* `DeviceSession`/transports are used by the worker thread only; `requestCancel()` is the one
  cross-thread call (atomic flag, non-blocking).
* GUI thread owns `values_`; worker emits diffed `QVariantMap`s via queued connections.
* Shutdown: `cancelAll()` → blocking `disconnectDevice` → `quit` → `wait`.

## Safety enforcement points (defence in depth)
1. GUI disables controls when `!writable`.
2. `DeviceSession` refuses writes unless `writesEnabled()`, checks `checkWriteAllowed`, and
   `verifyOnlyKnownFieldsChanged`.
3. `UsbTransport::write` refuses unless `allowWrites` and re-runs `checkWriteAllowed`
   (destructive words never pass on hardware).
4. CLI has no write command; `--method detach` needs an environment variable.
