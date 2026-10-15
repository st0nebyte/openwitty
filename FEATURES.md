# Feature inventory / parity matrix (CONNECT 2)

Sources: official online guide (documented behaviour, screenshots), macOS and
Windows Control Center 5.2.2 (protocol). **HW** column: see each row; most
controls are still read-only or untested on hardware. A first hardware
session (2026-10-14, `docs/HARDWARE_TEST_RESULTS.md`) confirmed the
transport-level API handshake (message 0x0A) over a newly-discovered unbound
vendor interface; follow-up sessions (2026-10-14/15, same doc §§10, 12, 13)
physically confirmed several individual read controls and, as of 2026-10-15,
the project's first real hardware **write** (LED brightness, via a single
narrowly-scoped, explicitly-gated CLI command — see `src/cli/main.cpp`'s
`cmdWriteLedBrightness`). This does not mean writes are broadly enabled:
every other control remains read-only by design pending its own review.
"RE" = what reverse engineering still has to
establish.
Implementation key: **core** = Qt-free codec + session + tests; **GUI** = QML
control wired to the mock backend; writes to real hardware are disabled.

| # | Feature (official behaviour) | GUI location | Protocol | Implementation | Missing RE | HW |
|---|---|---|---|---|---|---|
| 1 | Mic / instrument gain (mic 0–72, inst 0–60 dB) | Input strips, knob | settings @0/@10 Q8.8 | core+GUI+tests | — | **yes, both** (physically confirmed 2026-10-14: mic +1.00 dB on one notch; instrument +11 dB then partial restore, both exact matches) |
| 2 | Low-cut filter | LC button | @3/@13 | core+GUI | — | no |
| 3 | 48 V phantom (mic only) | 48V button, confirmation | @5 | core+GUI (confirm dialog) | input 2 has none in tree ✔ | no |
| 4 | Clipguard | CG button | @4/@14 | core+GUI | — | **not testable on this unit** — no dedicated physical control and no long-press gesture found on the front panel (owner-confirmed 2026-10-14); CONTROL CENTER/software-only |
| 5 | Preamp Clean/Warm/Vivid | segmented | @6/@15 (0/1/2) | core+GUI | — | **not testable on this unit** — no dedicated physical control and no long-press gesture found (owner-confirmed 2026-10-14); CONTROL CENTER/software-only |
| 6 | Denoise (noise-floor measurement; n/a at 96 kHz) | button | @7/@16 + trigger 6/7/8 (?) | core+GUI+96 kHz restriction+tests | trigger semantics, measurement UI | no |
| 7 | Comp (n/a at 96 kHz) | button | @9/@18 | core+GUI+restriction | compressor parameters (presets show more) | no |
| 8 | Input mute | MUTE | @2/@12 | core+GUI | — | **yes, both** (mic and instrument mute toggled and verified reversed on real hardware, 2026-10-14, exact 0x01/0x00 matches; MUTE button confirmed to act on whichever channel is currently selected) |
| 9 | Autogain (10 s, progress, success/fail) | Autogain button | triggers 1/2/3; status @0x24 progress, @0x23 bit5/6 result | core+GUI+mock workflow+tests | timing/limits only | no |
| 10 | Autosetup wizard | top-left button | settings writes + autogain trigger | GUI wizard (reconstruction) | exact step→field mapping, noise/clipping error codes | no |
| 11 | Loudspeaker volume/mute | Outputs | @20 / @22 | core+GUI | — | **yes, both** (volume: exact −11.66 dB match then restored to exact 0.00 dB ceiling; mute: exact toggle; both physically confirmed 2026-10-14 — speaker deliberately left muted afterward as a monitoring-safety precaution, not restored to its original unmuted state) |
| 12 | Headphone volume/mute (independent) | Outputs | @27 / @29 | core+GUI | — | **yes, both** (volume: exact dB match on two separate wheel edits; mute: exact toggle and verified reversal; both physically confirmed 2026-10-14) |
| 13 | Loopback mute + meters | Outputs | @19, status 0x12/0x14 | core+GUI | — | no |
| 14 | Input/playback mix | Monitoring knob | @35 sign-inverted Q8.8 | core+GUI | value range, % mapping | no |
| 15 | Direct monitoring mic/inst/both | Mic/Inst buttons | @34 (0/1/2) | core+GUI+state-machine tests | — | no |
| 16 | Dim / Mono | Monitoring | @32 / @33 | core+GUI | dim amount (device-side) | **not testable on this unit** — no dedicated Dim or Mono button on this front panel and no long-press gesture found (owner-confirmed 2026-10-14); CONTROL CENTER/software-only |
| 17 | Metering (inputs, loopback, speaker, headphone) | meters | status 0x00,0x09,0x12–0x1C | core+GUI | peak-hold/clip flags @0x2E/0x30 | no |
| 18 | Sample rate display/select | header | audio stack, not control protocol | GUI (simulated only) | PipeWire/ALSA integration | n/a |
| 19 | Gain lock (external gain) | Settings▸Audio | @57 | core+GUI | — | no |
| 20 | Input/playback mix applies to speakers | Settings▸Audio | @23, only 0/1 reachable from UI | core+GUI+tests | enumerator names | no |
| 21 | High-impedance headphone mode | Settings▸Audio | @31 | core+GUI | — | no |
| 22 | High-volume speaker mode | Settings▸Audio | @24 | core+GUI (confirm) | — | no |
| 23 | Mobile streaming mode (routing change) | Settings▸Audio | @67 | core (gated, read) | routing semantics; disruptive | no |
| 24 | macOS streaming mode (restarts device) | Settings▸Audio | trigger 0x104 (on) / 0x105 (off); PID 0x0002 vs 0x0004 | simulation only (confirm dialog, restart sim); **refused on real hardware** | device-side effect; disruptive | no |
| 25 | LED brightness | Settings▸LEDs | @37 (0–100) | core+GUI; CLI `write led-brightness` (experimental, gated) | — | **yes — read AND write** (first real hardware write on this project, 2026-10-15: 100→60→100, one visually witnessed; see `docs/HARDWARE_TEST_RESULTS.md` §13) |
| 26 | LED colours (icons/mute/parameter/metering/gain reduction) | Settings▸LEDs | @38–52 RGB; reset = trigger 0x103 | core+GUI+tests (reset gated) | — | no |
| 27 | Custom button function (8 options) | Settings▸Custom button | @53: 0,1,2,8,9,0xC,0xD,0xE | core+GUI+tests | long-press config (bytes 54–56?) | no |
| 28 | Panic Mute (MUTE hold) | hardware only (front-panel MUTE, long-press) | **confirmed**: exactly bytes 2/12/22/29 (mic/instrument/speaker/headphone mute), all set simultaneously regardless of selected channel; byte 19 (loopback) and byte 56 (channel-select) confirmed untouched; no separate flag | read-only state handling only (`mute.allFour` derived display value, `MockTransport::simulatePanicMute()` for tests) — **no software trigger implemented**, per instruction | manufacturer-documented (online guide, verified 2026-10-16 — see `docs/OFFICIAL_GUIDE.md`): mutes all input+output simultaneously, recovery requires unmuting each channel individually (not re-tested; no code assumes a second hold undoes it) | **yes** — hardware-confirmed 2026-10-16, 3 read-only trials; see `docs/HARDWARE_TEST_RESULTS.md` §16 |
| 29 | API handshake / versions | Settings | message 0x0A (major,minor); `iface_version_t` opaque | handshake in core+tests; firmware string placeholder | `iface_version_t` layout | no |
| 30 | Firmware update / factory reset | Settings | DFU / trigger 0x106 | **disabled by policy** (FactoryReset refused in code) | out of scope | no |
| 31 | Presets (`.preset` JSON) | main window (Connect 6 shared code) | host-side | not implemented | CONNECT 2 use unclear | n/a |
| 32 | Tooltips, onboarding, update checks | app | host-side | tooltips partial | — | n/a |

Product-behaviour sources for each row are in `docs/OFFICIAL_GUIDE.md`.

| 33 | Device-side change sync (hardware knob/button) | — | schema merge, full-struct write | core (merge)+tests | — | no |
| 34 | Footer hint bar + "Show tooltips" | footer / Settings | host-side | GUI | — | n/a |
| 35 | Onboarding tour (4 steps) | overlay / Settings | host-side | GUI (on demand) | first-run persistence | n/a |
| 36 | Unsent-edit tracking (retry/discard) | footer bar | session staging | core+GUI+tests | — | n/a |
| 37 | Measurement session bracket (words 4/5) | Autogain/Denoise/Autosetup | trigger buffer | core+mock+GUI | device-side effect | no |
| 38 | Read-only diagnostics: `devices`, `diagnose`, classified `read` (api/version/status/settings), `probe` | CLI | sysfs + libusb GET | implemented, fixture/mock tested | — | **yes** (all four read-only message types confirmed against a real CONNECT 2, 2026-10-14, via the unbound interface-3 access path; see `docs/HARDWARE_TEST_RESULTS.md`) |
| 39 | Hot-plug discovery + auto-reconnect with backoff (USB mode) | GUI status | sysfs scan | implemented, simulated | needs device | no |
| 40 | Backend write guard (destructive words, unknown bytes, enum preservation) | n/a | — | implemented + tests | — | n/a |
| 41 | Touch-surface channel selection (LED-ring focus state) | front-panel LEDs (hardware), not yet surfaced in GUI | settings byte 56 (new; not in `settings_off`) | not implemented in OpenWitty | the 4 channel-select values are confirmed; mute/custom/mix/autogain button states unobserved (those are action-trigger buttons, not pure selectors, so may not populate this byte the same way — untested to avoid side effects) | **yes** — all 4 values confirmed 2026-10-14: `0x01` mic, `0x02` instrument, `0x03` speaker, `0x04` headphone, each via a select-only press with zero value change; see `docs/HARDWARE_TEST_RESULTS.md` §§10.5, 12 and PROTOCOL.md |
| 42 | Write-state verification (`commitAndVerify`): distinguishes transfer-ok from device-confirmed | n/a (backend + GUI status label) | `core/WriteVerification.hpp`; bounded read-only polling, never retries the SET | core (11 new tests, all mock); GUI wired for `setValue`/`retryPending` | not yet exercised against any field but LED brightness on real hardware | n/a — this is infrastructure, not a device setting; see PROTOCOL.md's progressive-control-policy table for per-field status |
| 43 | GUI status label distinguishing Disconnected / Connecting / Hardware error / Mock mode / Connected read-only / Connected with authorized controls / Pending verification | header / status area (`values()["ui.status"]`, not yet bound in QML) | derived from existing `ConnectionState`/`isSimulated`/`writesEnabled` plus the new `verifying_` flag | GUI backend done, QML binding not added this session | "Pending verification" is tracked but not externally observable mid-flight with the current synchronous `commitAndVerify()` call (see code comment on `DeviceWorker::retryPending`) | n/a |
| 44 | Showcase mode (hidden icon sequence) | hardware only | device-side | not applicable | icon sequence not reproduced | n/a |
| 45 | `mute.allFour` derived display state (purely computed, no protocol concept) | not yet bound in QML | `s.mute(0) && s.mute(1) && s.speakerMute() && s.headphoneMute()` | GUI backend done (`publish()`), `MockTransport::simulatePanicMute()` for tests | QML binding not added this session | n/a — confirmed by evidence that real hardware has no dedicated flag (§16 of `docs/HARDWARE_TEST_RESULTS.md`), so this is deliberately computed, not a new stored field |

## Phase 7A additions (mock/unit-tested; not hardware-verified)

* GUI connects to a real CONNECT 2 read-only through the descriptor-selected vendor control
  interface (`InterfaceSelection`), no detach/claim/write; **never opened on real hardware yet**.
* Distinct connection presentation: disconnected, simulated, real read-only, connecting,
  communication error, connection lost; transport errors with next-step hints are visible.
* Sample rate shown as "Unknown" unless simulated; persistent open errors are not retried until
  the device list or permissions change.
* `openwitty-cli read … --interface auto` (same selection as the GUI).
