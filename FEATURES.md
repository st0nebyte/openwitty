# Feature inventory / parity matrix (CONNECT 2)

Sources: official online guide (documented behaviour, screenshots), macOS and
Windows Control Center 5.2.2 (protocol). **HW** column: no individual feature
below is verified on a real device yet. A first hardware session (2026-10-14,
`docs/HARDWARE_TEST_RESULTS.md`) confirmed the transport-level API handshake
(message 0x0A) over a newly-discovered unbound vendor interface, which is a
prerequisite for testing any feature below but does not by itself validate any
specific setting/control. "RE" = what reverse engineering still has to
establish.
Implementation key: **core** = Qt-free codec + session + tests; **GUI** = QML
control wired to the mock backend; writes to real hardware are disabled.

| # | Feature (official behaviour) | GUI location | Protocol | Implementation | Missing RE | HW |
|---|---|---|---|---|---|---|
| 1 | Mic / instrument gain (mic 0–72, inst 0–60 dB) | Input strips, knob | settings @0/@10 Q8.8 | core+GUI+tests | — | no |
| 2 | Low-cut filter | LC button | @3/@13 | core+GUI | — | no |
| 3 | 48 V phantom (mic only) | 48V button, confirmation | @5 | core+GUI (confirm dialog) | input 2 has none in tree ✔ | no |
| 4 | Clipguard | CG button | @4/@14 | core+GUI | — | no |
| 5 | Preamp Clean/Warm/Vivid | segmented | @6/@15 (0/1/2) | core+GUI | — | no |
| 6 | Denoise (noise-floor measurement; n/a at 96 kHz) | button | @7/@16 + trigger 6/7/8 (?) | core+GUI+96 kHz restriction+tests | trigger semantics, measurement UI | no |
| 7 | Comp (n/a at 96 kHz) | button | @9/@18 | core+GUI+restriction | compressor parameters (presets show more) | no |
| 8 | Input mute | MUTE | @2/@12 | core+GUI | — | no |
| 9 | Autogain (10 s, progress, success/fail) | Autogain button | triggers 1/2/3; status @0x24 progress, @0x23 bit5/6 result | core+GUI+mock workflow+tests | timing/limits only | no |
| 10 | Autosetup wizard | top-left button | settings writes + autogain trigger | GUI wizard (reconstruction) | exact step→field mapping, noise/clipping error codes | no |
| 11 | Loudspeaker volume/mute | Outputs | @20 / @22 | core+GUI | — | no |
| 12 | Headphone volume/mute (independent) | Outputs | @27 / @29 | core+GUI | — | no |
| 13 | Loopback mute + meters | Outputs | @19, status 0x12/0x14 | core+GUI | — | no |
| 14 | Input/playback mix | Monitoring knob | @35 sign-inverted Q8.8 | core+GUI | value range, % mapping | no |
| 15 | Direct monitoring mic/inst/both | Mic/Inst buttons | @34 (0/1/2) | core+GUI+state-machine tests | — | no |
| 16 | Dim / Mono | Monitoring | @32 / @33 | core+GUI | dim amount (device-side) | no |
| 17 | Metering (inputs, loopback, speaker, headphone) | meters | status 0x00,0x09,0x12–0x1C | core+GUI | peak-hold/clip flags @0x2E/0x30 | no |
| 18 | Sample rate display/select | header | audio stack, not control protocol | GUI (simulated only) | PipeWire/ALSA integration | n/a |
| 19 | Gain lock (external gain) | Settings▸Audio | @57 | core+GUI | — | no |
| 20 | Input/playback mix applies to speakers | Settings▸Audio | @23, only 0/1 reachable from UI | core+GUI+tests | enumerator names | no |
| 21 | High-impedance headphone mode | Settings▸Audio | @31 | core+GUI | — | no |
| 22 | High-volume speaker mode | Settings▸Audio | @24 | core+GUI (confirm) | — | no |
| 23 | Mobile streaming mode (routing change) | Settings▸Audio | @67 | core (gated, read) | routing semantics; disruptive | no |
| 24 | macOS streaming mode (restarts device) | Settings▸Audio | trigger 0x104 (on) / 0x105 (off); PID 0x0002 vs 0x0004 | simulation only (confirm dialog, restart sim); **refused on real hardware** | device-side effect; disruptive | no |
| 25 | LED brightness | Settings▸LEDs | @37 (0–100) | core+GUI | — | no |
| 26 | LED colours (icons/mute/parameter/metering/gain reduction) | Settings▸LEDs | @38–52 RGB; reset = trigger 0x103 | core+GUI+tests (reset gated) | — | no |
| 27 | Custom button function (8 options) | Settings▸Custom button | @53: 0,1,2,8,9,0xC,0xD,0xE | core+GUI+tests | long-press config (bytes 54–56?) | no |
| 28 | Panic mute / showcase mode | hardware only | device-side | not applicable | showcase key-combo is hardware | n/a |
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
| 38 | Read-only diagnostics: `devices`, `diagnose`, classified `read` (api/version/status/settings), `probe` | CLI | sysfs + libusb GET | implemented, fixture/mock tested | needs device | no |
| 39 | Hot-plug discovery + auto-reconnect with backoff (USB mode) | GUI status | sysfs scan | implemented, simulated | needs device | no |
| 40 | Backend write guard (destructive words, unknown bytes, enum preservation) | n/a | — | implemented + tests | — | n/a |
