# Phase 7 plan: connecting the Qt/QML GUI to the real CONNECT 2

Status: **planning/audit only.** No code was changed, no hardware was used, no new write path
was added. Baseline: branch `phase6e-work-preparation` @ `7d8a4c0` (117 tests), which sits on
`hardware-validation-2026-10-14` @ `d4f99c6` (106 tests). The home computer's unpushed Panic Mute
commits (108/108 there) are **not** in this tree; section 7 lists where they will collide.

Evidence labels used below: **HW-read** = offset confirmed by physically operating the control
and diffing real reads (`docs/HARDWARE_TEST_RESULTS.md` §§10, 12); **HW-SET** = a real SET was
sent and verified (only LED brightness, §13/§15); **bin** = static analysis only; **none** = no
evidence of any kind.

## Phase 7A status (implemented on `phase7a-interface-discovery`, mock/unit-tested only)

| Plan item | State |
|---|---|
| §5.1 descriptor-driven interface selection | **Done** — `src/transport/InterfaceSelection.{hpp,cpp}` (pure), `UsbOptions::interfaceStrategy`, 15 table tests incl. the exact real CONNECT 2 layout |
| §5.1 GUI uses it | **Done** — `DeviceWorker::connectUsb()` builds `UsbTransport(readOnlyHardwareOptions())`; audit script enforces this |
| §5.2 `ui.state` + banner | **Done (minimal)** — `ui.state` ∈ disconnected / simulated / real-readonly / real-writable / connecting / error / connection-lost, `ui.error`, `ui.device`; banner and indicator in `Main.qml` rewritten; "protocol not verified" text removed |
| §4.6 sample rate honesty | **Done** — key published only for a connected simulation; UI shows "Unknown" |
| §5.7 stop-retry after persistent errors | **Done** — `core/RetryPolicy.hpp`; reads are not re-attempted until the device list/node permissions change |
| §5.3 `selectedChannel()` / highlight | Deferred to 7B (touches `Messages.*`, `publish()`, strips) |
| §5.4 evidence badges | Deferred to 7B (QML-wide) |
| §5.6 `ReplayTransport` fixtures | Deferred to 7B |
| §4.8 faster settings poll | Deferred to 7B (avoids `poll()` overlap with Panic Mute work) |
| Meter floor | Not a defect: values below −60 dB display at the meter floor; documented only |

Everything above is **unverified on real hardware**: the selection rule reproduces the descriptor
layout recorded on 2026-10-14, but the GUI has never opened the device.

## 1. Existing architectural readiness

| Component | Real-hardware ready? | Notes |
|---|---|---|
| `protocol/*` codecs, `WriteGuard` | **Yes** | Settings decode already validated against a real 128-byte read; status decode partly (see §2). |
| `UsbTransport` | **Partly** | Reads/writes work through an explicit `interfaceNumber` (CLI `--interface 3`). **Auto-discovery picks the UAC AudioControl interface (class 01/01 = interface 0), which fails with EIO on the real device** (`USB_ACCESS.md` §3b). No strategy for "pick the unbound vendor-specific interface". |
| `DeviceSession` (`connect`, `refreshSettings/Status`, `commitAndVerify`, `hardwareWriteOffsets`) | **Yes** | Used successfully by the CLI on real hardware for LED brightness. Failure counting, device-wins merge, stability check exist. |
| `DeviceManager` (sysfs scan) | **Yes** | Read-only discovery, PID 0x0004/0x0002 classification; no USB traffic. |
| `SerializedTransport` | **Yes** | One request in flight, cancel, stats. |
| GUI `DeviceWorker::connectUsb` | **No** | Constructs `UsbTransport{}` with defaults → interface 0 → EIO on this device. This is the single blocking defect for a real-hardware GUI. |
| GUI polling (`poll`) | **Yes (adequate)** | 250 ms status; settings every 4th tick (1 s) in USB mode; diffed publish; read-only. |
| Hotplug/reconnect | **Mostly** | Sysfs rescan with 1→8 s backoff, then `reconnect()` (handshake + re-sync, pending edits kept). Retries forever on EACCES/EBUSY/EIO (reads only). Not exercised on real hardware. |
| `MockTransport` | n/a | Simulation only; supports stale reads, external changes, faults. |
| QML | **Partly** | Every control is disabled unless `live && device.writable`; on real hardware `writable == false`, so the whole UI is read-only by construction. Value display works for everything decoded. |

## 2. Control-to-protocol mapping and readiness

"Display (read-only)" is what the GUI would show on real hardware today if it connected
(`connectUsb` fixed). "Write" is the status of any SET path — all are **off** in the GUI.
Safety class: **A** cosmetic, **B** capture-level/processing, **C** audible output level or
routing, **D** hardware-risk (48 V, mode, firmware).

| GUI control | QML key | Settings/status field | Evidence | Display (read-only) | Write status | Class | Remaining tests before enabling a write |
|---|---|---|---|---|---|---|---|
| Mic gain knob | `in1.gain` | s[0–1] Q8.8, 0–72 dB | **HW-read** | ok | SET untested; no CLI | B | one SET + rollback, readback poll |
| Instrument gain | `in2.gain` | s[10–11], 0–60 dB | **HW-read** | ok | SET untested | B | same |
| Mic mute | `in1.mute` | s[2] | **HW-read** (+reversal) | ok | SET untested | B/C | same; Panic Mute interplay |
| Instrument mute | `in2.mute` | s[12] | **HW-read** | ok | **Prepared** (Phase 6E CLI `instrument-mute`, never executed) | B/C | the Phase 6E procedure |
| Speaker volume / mute | `spk.volume`/`spk.mute` | s[20–21], s[22] | **HW-read** | ok | SET untested | **C** | mute first, low level |
| Headphone volume / mute | `hp.volume`/`hp.mute` | s[27–28], s[29] | **HW-read** | ok | SET untested | **C** | same |
| Loopback mute | `loop.mute` | s[19] | none (only observed *unchanged* by Panic Mute) | shown, flagged unverified | none | B | physical or controlled SET |
| Selected channel (no QML control yet) | — (`selected.channel` to add) | s[56] = 1 mic / 2 inst / 3 speaker / 4 headphone | **HW-read** (4 values; enum possibly incomplete) | **not decoded anywhere in code today** | n/a (indicator) | A | add accessor + tests |
| LED brightness | `set.ledBrightness` | s[37] | **HW-SET** (2026-10-14/15, twice, + rollback) | ok | CLI only, double-gated | A | GUI path would reuse `commitAndVerify` |
| LED colours | `set.led.*` | s[38–52] | none (mock-tested) | ok, flagged unverified | none | A | physical/visual check |
| Mic phantom (48 V) | `in1.phantom` | s[5] | none; one real read showed `on` | shown, flagged unverified | **out of scope** without separate safety review | **D** | separate authorization |
| Low-cut | `in*.highPass` | s[3], s[13] | none | shown, unverified | none | B | — |
| Clipguard | `in*.clipguard` | s[4], s[14] | none; no front-panel access on this unit | shown, unverified | none | B | software-only validation |
| Preamp Clean/Warm/Vivid | `in*.preamp` | s[6], s[15] | none; no front-panel access | shown, unverified | none | B | software-only validation |
| Denoise / Comp | `in*.denoise/compress` | s[7,16] / s[9,18] | none | shown, unverified | none; 96 kHz rule enforced | B | — |
| Input/playback mix | `mon.mix` | s[35–36] (sign-inverted) | none | shown, unverified | none | **C** | — |
| Monitor source mic/inst | `mon.mic`/`mon.inst` | s[34] enum 0/1/2 | none | shown, unverified | none | C | — |
| Dim / Mono | `out.dim`/`out.mono` | s[32] / s[33] | none; no physical control on this unit | shown, unverified | none | C | software-only |
| Gain lock, mix→speakers, hi-Z, high-volume | `set.*` | s[57], s[23], s[31], s[24] | none | shown, unverified | none | B/C | — |
| Custom button function | `set.customButton` | s[53] | one real read (value 2); semantics bin only | shown | none | A | — |
| Input meters | `in1.level` / `in2.level` | status 0x00 / 0x09 Q8.8 | in1: one real observation (−100 dB floor → −62 dB with live mic); in2: none | in1 plausible; in2 unverified; note real floor is −100 dB while the code clamps to −60 | n/a | A | meter fixture test, in2 check |
| Output meters | `loop/spk/hp.level{L,R}` | status 0x12/14, 0x16/18, 0x1A/1C | **none** | shown, unverified | n/a | A | needs playback test |
| Autosetup / Autogain | `device.trigger("autogainN")` | triggers 4,1/2/3,5 + status 0x23/0x24 | bin only | button disabled (read-only) | **not authorized**; real-hardware completion inference unverified | B (runs a real ~10 s measurement) | dedicated plan |
| macOS streaming mode | `set.macosStreaming` | trigger 0x104/0x105, PID | bin only | read from PID | **never on hardware** | D | out of scope |
| Sample rate | `sampleRate` | **audio stack, not the protocol** | n/a | **GUI shows a fake default 48 kHz** | n/a | A | read `/proc/asound/.../hw_params`/PipeWire or show "—" |
| Firmware version | Settings text | `iface_version_t` | **GET 0x0103 returned the same 64 bytes as 0x0A on interface 3 (unexplained)** | "not decoded yet" | n/a | A | investigate; do not display |
| Unknown status bytes 3–4, 12–13 | — | moved during mic activity and Panic Mute | none | not shown | n/a | — | observation only; no meaning assigned |

**Rule applied:** a known offset is *not* "supported". Only rows with HW-read evidence are
candidates for "real value" display without a caveat; only LED brightness has ever been written.

## 3. GUI connection states

Present today (`Main.qml`, `DeviceStatusIndicator`, `DeviceWorker::publish`):

* `device.connection` ∈ Disconnected / Connecting / Connected / Error — shown with a dot.
* `device.simulated` → amber banner "SIMULATED DEVICE…"; otherwise a red banner "Hardware writes are DISABLED: the control protocol is not verified on a real CONNECT 2."
* `ui.status` string exists in `publish()` (“Mock mode”, “Connected read-only”, “Connected with authorized controls”, “Pending verification”, “Hardware error”…) and `write.state`/`write.verifying` too — **but no QML file binds to any of them** (grep: zero hits).

| Required state | Today | Gap |
|---|---|---|
| No device connected | “No LEWITT device connected!” centred text | OK; no distinction "absent" vs "present but not openable" |
| Simulated device | amber banner, "simulated" detail | OK |
| Real device, read-only | red banner text + dot "Connected · usb" | **Misleading**: red "DISABLED/not verified" reads as an error, and the claim "protocol not verified" is now outdated (reads and 8 fields are verified). No explicit "REAL DEVICE — READ-ONLY" state or device identity (PID/mode/serial-tail). |
| Real device, individually authorized writes | none | Needs per-field authorization model (§4) before it can exist. `ui.status` string would say "Connected with authorized controls" only if `writesEnabled()`; GUI never sets it. |
| Pending write verification | `verifying_` flag, but synchronous worker ⇒ never observable | Becomes real only with an async poll policy. |
| Ambiguous device state | emits `errorOccurred` text; `write.state == "ambiguous"` not displayed | Needs a persistent, non-modal indicator tied to `write.state`. |
| USB transport error | `Error` dot + `lastError` string only when a pending edit exists (`hasPending` row) | **Errors are invisible in read-only mode**: the error line lives in the pending-edit row. EACCES/EBUSY/EIO need their own banner with the `suggestionFor()` text. |

## 4. Design of the first real-hardware GUI milestone (read-only)

Goal: **the existing window, connected to the real CONNECT 2, showing real values, changing
nothing.**

1. **Interface selection (transport):** add `UsbOptions::interfaceStrategy`
   (`AudioControl` (legacy default) | `VendorControlAuto`). `VendorControlAuto` walks the active
   configuration and selects the interface with class `0xFF` and no bound kernel driver
   (hardware: `ff/f0/00`), **never** class `0xFE` (DFU) and never an interface bound to
   `snd-usb-audio`; ambiguous or none → `NotFound` with a clear message. Implement the choice
   as a pure function over the descriptor structures `UsbDiagnostics` already produces so it is
   unit-testable without hardware. No new USB call site; reads only (`allowWrites=false`).
2. **GUI uses it** for `connectUsb()`; the CLI `--interface N` override remains for experiments.
3. **Explicit state model:** one `ui.state` enum published by the worker and used by QML:
   `NoDevice`, `DeviceUnusable(reason)`, `Simulated`, `RealReadOnly`, `RealWritable(fields)`,
   `Verifying`, `Ambiguous`, `TransportError(reason)`. Banner text and colour derive from it.
   `RealReadOnly` is a calm, distinct state (e.g. neutral/blue: "REAL DEVICE — READ-ONLY ·
   CONNECT 2 · PID 0004 · API 1.x"), never styled like the simulation banner.
4. **Evidence badges:** a static table (field key → evidence level: `hw-read`, `hw-set`,
   `unverified`) exposed to QML; unverified values render with a subtle marker and tooltip
   ("decoded from static analysis; not confirmed on hardware"). Makes the §2 table visible.
5. **Decode what is confirmed but unused:** `SettingsMessage::selectedChannel()` (byte 56, with
   unknown values preserved) → `selected.channel` key and a highlight on the matching strip.
6. **Honest sample rate:** do not show 48 kHz on a real device; show "—" until read from the
   audio stack (read-only `/proc/asound` parse or PipeWire query as a separate step).
7. **Meters:** keep the −60 dB display clamp but treat the real floor (−100 dB while muted) as
   "silence"; mark output meters unverified until a playback test.
8. **Live refresh:** settings poll ≥ 2 Hz in USB mode (currently 1 Hz) so a physical mute/wheel
   or Panic Mute shows within ~0.5 s; status stays 4 Hz; all reads stay serialized; backoff and
   failure counters unchanged. Pending-edit machinery stays but is unreachable while read-only.
9. **Disconnect/reconnect:** reuse `rescan()`; add a visible "device removed / reconnecting"
   state and clear stale values on `Disconnected` (already diffed away since Phase 5E).
10. **Per-field write authorization (future, not this milestone):** a `FieldPolicy` table
    (offset set, class, evidence, authorization token) consumed by both CLI and GUI, so the GUI
    can only enable a control whose entry is `authorized` *and* backed by
    `hardwareWriteOffsets`. Nothing is added to the GUI in the read-only milestone.

## 5. Recommended implementation order

1. **Descriptor-driven interface selection** (pure function + tests) — unblocks everything.
2. **`ui.state` + banner/indicator rewrite** (QML + worker; read-only wording; errors visible).
3. **`selectedChannel()` + QML highlight + `selected.channel`.**
4. **Evidence-badge table + QML marker.**
5. **Sample-rate honesty** (hide/“—” on real devices).
6. **Fixture-replay mode** (a `ReplayTransport` that serves recorded, serial-redacted 0x80/0x40
   blobs) so the whole real-value path is testable in CI without a device.
7. **Poll-rate and hotplug hardening** (faster settings refresh; stop retrying after
   EACCES/EBUSY until the device list changes).
8. *(Later, separate approvals)* one control at a time via the policy table: instrument mute →
   headphone mute → mic gain …; each with its own hardware session.

## 6. Required tests

| Area | Test (all without hardware) |
|---|---|
| Interface selection | table-driven: the real descriptor set (`01/01`, `01/02`×2, `ff/f0`, `fe/01`) → interface 3; kernel-bound `ff` skipped; DFU never chosen; two candidates → refuse; none → `NotFound`; mock-only build unaffected |
| `ui.state` | worker unit tests for each state transition: absent → unusable (EACCES/EBUSY/EIO) → real read-only → unplug → reconnect; simulated never reports real states and vice versa |
| Read-only guarantee | with `RealReadOnly`, every `setValue`/`trigger`/`retryPending` is refused at the worker (no transport write); extend `audit_usb_call_sites` (GUI must not set `allowHardwareWrites`/`hardwareWriteOffsets`, already present) |
| Selected channel | accessor on values 0–4, 5+ preserved/unknown; QML binding smoke test |
| Replay fixtures | serial-redacted real settings/status blobs → expected `device.values` (gain 27.00 dB, mutes, byte 56, …); Panic-Mute-shaped transition (2/12/22/29 flip, 19/56 unchanged) updates all four mute keys within one settings refresh |
| Meters | status floor handling (−100 dB) and clamp; in2/outputs flagged unverified |
| External change | physical change during idle polling updates values; with a pending edit (simulated writable only) device-wins merge |
| Hotplug | sysfs scanner stub: remove/add, PID change to 0x0002/0x000A (refuse, show reason), repeated EACCES backoff, no I/O storm |
| Sample rate | real device never displays the fake default |
| Packaging/regression | full suite (≥117), ASan/UBSan, TSan core, USB call-site audit, `packaging_staging` |

## 7. Known synchronization limitations (carry into the design)

* **Settling delay:** a GET right after a SET may return the old value (`WriteVerification`);
  irrelevant for a read-only milestone, central for any write.
* **Non-atomic 128-byte SET:** a physical action (Panic Mute) between our read and SET is
  reverted for every byte we resend. `requireStableSnapshot` narrows, cannot close it.
* **Polling latency:** values lag the device by ≤ 1 s today (settings), ≤ 250 ms (meters);
  button presses between polls are visible only as their end state.
* **500 ms transfer timeout on the worker thread:** a stuck request blocks that thread up to
  0.5 s; the GUI thread stays responsive; shutdown uses cooperative cancel.
* **Sysfs-only presence detection** (1–8 s granularity): a quick unplug/replug can be missed
  until the next failed read.
* **Unknown status bytes (3–4, 12–13)** change with mic activity and Panic Mute; no meaning is
  assigned and none must be inferred.
* **Version message** returns the same bytes as the API handshake on interface 3: firmware
  version cannot be shown.
* **Real meter floor (−100 dB)** vs the protocol clamp (−60 dB).
* **Autogain completion** is inferred from status progress ≥ 100 (stale-100 risk, unverified).

## 8. Likely conflicts with the missing Panic Mute commits

Known from the home computer (not present here): `MockTransport::simulatePanicMute()`, a
derived `mute.allFour`, three tests, docs, 108/108.

| Planned change here | Overlaps with | Handling |
|---|---|---|
| `DeviceController.cpp::publish()` — new keys (`ui.state`, `selected.channel`, badges) | `mute.allFour` derived key is added in the same function | Keep each addition in its own contiguous block; add `mute.allFour` consumers last. |
| QML banner/state rewrite (`Main.qml`) | a Panic Mute indicator bound to `mute.allFour`, if the home work added one | Do the state-model work first as a separate commit; bind the panic indicator to `ui.state`-independent `mute.allFour` after integration. |
| `MockTransport` fixtures / replay | `simulatePanicMute()` | Do **not** touch `MockTransport` until integrated; put replay in a new `ReplayTransport` file. |
| Poll-rate change in `DeviceWorker::poll` | none expected; but Panic Mute tests may assume 1 s settings refresh in mock mode | Make rate USB-only (mock keeps `10`). |
| New `selectedChannel()` accessor | none | additive. |
| Tests | home's 3 tests are in existing files | add new test files only; keep `target_sources` lines separate. |

Integration order recommendation: merge the home branch first (its changes are small and
mock-only), re-run 117+3, then land §5 items 1–3.

## 9. Implementation checklist

- [ ] Integrate the home-computer Panic Mute commits (merge, resolve, 117+3 green).
- [ ] `selectControlInterface()` pure function + `UsbOptions::interfaceStrategy`; table tests.
- [ ] `DeviceWorker::connectUsb` uses `VendorControlAuto`; no other behaviour change.
- [ ] `ui.state` enum + QML banner/indicator rewrite (real-read-only distinct from simulation;
      transport errors visible with `suggestionFor()` text); update the outdated red text.
- [ ] `SettingsMessage::selectedChannel()` + `selected.channel` + strip highlight.
- [ ] Evidence-badge table + QML marker; document the table next to PROTOCOL.md.
- [ ] Sample rate: no fake value on real devices.
- [ ] `ReplayTransport` + redacted fixtures (no serial numbers committed) + GUI value tests.
- [ ] Settings poll ≥ 2 Hz on USB; stop-retry-until-device-list-changes after EACCES/EBUSY/EIO.
- [ ] Extend `audit_usb_call_sites.sh`: interface selection never returns class 0xFE; GUI sets no write policy.
- [ ] Full regression: Release, ASan/UBSan, TSan core, packaging staging, mock-only build.
- [ ] Hardware session (separate, approved): launch GUI against the real device read-only;
      compare displayed values to `openwitty-cli read settings --interface 3`; toggle physical
      mute/wheel/channel select and Panic Mute; verify disconnect/reconnect; record in
      `HARDWARE_TEST_RESULTS.md`. **No writes.**

## 10. Gaps summary and smallest safe milestone

Most important gaps between the GUI and real hardware support:

1. **`connectUsb()` targets interface 0, which fails on the real device** — the GUI cannot
   connect at all today.
2. **No explicit "real device, read-only" state;** the banner is a red "writes disabled /
   not verified" message and transport errors are hidden unless an edit is pending.
3. **Unverified fields are displayed as facts** and a fake 48 kHz sample rate is shown.
4. **Confirmed selected-channel byte 56 is not decoded.**
5. **No GUI-level per-field write model** (intentionally absent; all controls are read-only).

Smallest useful milestone developable without the device: **items 1–3 of §5** — the
descriptor-driven interface selection with table tests, `connectUsb()` wired to it, and the
`ui.state`/banner rewrite — all testable with synthetic descriptors and the mock; it makes the
first real-hardware GUI session a one-command, read-only experiment.
