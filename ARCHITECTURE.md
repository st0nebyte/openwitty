# Architecture

```
QML ──► DeviceController (GUI thread) ══queued══► DeviceWorker (QThread)
                                                     │
                                              DeviceSession  (policy, staging, sync)
                                                     │
                       protocol/Messages (pure)      │
                                              IDeviceTransport
                                         ┌──────────┴───────────┐
                                    MockTransport          UsbTransport (libusb)
                                                          (AlsaTransport: not built —
                                                           no ALSA path exists, see USB_ACCESS.md)
```
* Raw bytes are the source of truth; typed setters edit in place so unknown
  bytes round-trip.
* Writes need `DeviceSession::writesEnabled()` (simulated transport, or
  explicit policy) **and** `UsbOptions::allowWrites`. Neither is set anywhere.
* USB I/O never runs on the GUI thread; requests are serialized by the
  worker thread's event loop.
