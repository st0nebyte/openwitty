# CONNECT 2 control protocol

Status legend: **[V-bin]** verified by disassembly of the official macOS app
(Control Center 5.2.2, x86_64 slice), **[V-hw]** verified against hardware,
**[?]** hypothesis. **No hardware verification exists yet** — the device was
not attached when this was written, so everything below is [V-bin] or [?].

## Transport  [V-bin]

The app talks to the device with USB **class-specific control requests to the
AudioControl interface** (it is a Thesycon-based UAC device; the macOS app
uses `LewittDeviceThesyconMacBackend::{ACMemGet,ACMemSet}`).

| field          | read (GET)                        | write (SET)                       |
|----------------|-----------------------------------|-----------------------------------|
| bmRequestType  | `0xA1` (D→H, class, interface)    | `0x21` (H→D, class, interface)    |
| bRequest       | UAC2: `0x03` (MEM) / UAC1: `0x85` (GET_MEM) | UAC2: `0x03` / UAC1: `0x05` (SET_MEM) |
| wValue         | `(apiMajor << 8) \| messageId` = `0x01<id>` | same |
| wIndex         | `0x3300 \| AudioControl interface number`   | same |
| wLength        | message size (below)              | message size                      |
| timeout        | 500 ms                            | 500 ms                            |

* The UAC1/UAC2 switch is a flag inside the backend object; it is most likely
  derived from `bcdADC` in the AC header descriptor (`parseConfigDescriptor`) [?].
* Unit ID `0x33` is hard-coded in the high byte of wIndex [V-bin].

## Messages  [V-bin]

`PackedStructMessage<T>`: `serialize()`/`deserialize()` are empty, `data()`
is the struct itself ⇒ **raw packed little-endian structs, no framing,
no checksum**.

| id | name             | size | R | W |
|----|------------------|------|---|---|
| 0  | iface_settings_t | 0x80 | ✔ | ✔ |
| 2  | iface_status_t   | 0x40 | ✔ |   |
| 3  | iface_version_t  | 0x40 | ✔ |   |
| 4  | iface_triggers_t | 0x40 |   | ✔ |
| 0x0A | lewitt_api_t   | 0x40 | ✔ |   |

`lewitt_api_t` (id 0x0A, **apiMajor 0**, so wValue = `0x000A`) is the version
handshake, [V-bin, mac+function level]: `LewittDevice::open` reads it through
backend slot 0 and stores byte 0 / byte 1 as 16-bit API major / minor;
`LewittDevice::read/write` then throw *"Message API version not supported by
the device."* if `message.apiMajorVersion != device major` (`cmp word [dev+0x5a]`).
Also: *"Device must be opened in APP mode."* when the device-info DFU flag
(`[dev+0x5e]`) is set. Evidence: `LWT::LewittDevice::open/read` (macOS
0x100003c70 / 0x100005d50), vtable of `PackedStructMessage<lewitt_api_t,…>`
at 0x10084fc98 (slot 0 `mov eax,0xa`; slot 3 `xor eax,eax`; slot 4 `0x40`).
Byte 0 = major, byte 1 = minor is read from a `pmovzxbw` of two bytes (medium
confidence); bytes 2..63 unknown. (id 1 is unused by Connect2; Connect 6 has
additional levels/mixer/dsp messages.)

The app keeps three copies of the settings struct: *current* (last read),
*snapshot* (previous tick, for change detection) and *pending* (what gets
written back; setters edit this one). Writes therefore send the **entire**
0x80-byte struct, so read-modify-write is mandatory [V-bin, inferred from
`tick()`; write path itself not yet disassembled].

## iface_settings_t field map  [V-bin offsets + group names; names of enums ?]

Authoritative copy: `src/protocol/Messages.hpp` (`settings_off`). Offsets are
exact (getter/setter code). Group membership is confirmed **by identifier
name**: the tree builder loads the app's own `Microphone`, `Instrument`,
`Loopback`, `Speaker`, `Headphone`, `Monitor` and `Main` (settings) identifiers
immediately before constructing each group (macOS `buildConnect2Tree`).

| off | type | group / app id | meaning & encoding | evidence |
|-----|------|----------------|--------------------|----------|
| 0 | i16 | Microphone `Volume` | gain, Q8.8 dB; device clamp 0…85, **UI scale 0…72** | getter/setter + guide screenshot |
| 2,3,4,5 | u8 | Microphone | mute, high-pass (LC), Clipguard, **phantom 48 V** | `Mute`,`HighPassFilter`,`ClipGain`,`PhantomPower` |
| 6 | u8 | Microphone | preamp: 0 Clean, 1 Warm, 2 Vivid (three bool ids over one byte) | `CleanPreampType`… compare ==0/1/2 |
| 7, 9 | u8 | Microphone | Denoise, Compress | `Denoise`,`Compress` |
| 10 | i16 | Instrument | gain Q8.8; **UI scale 0…60** | guide screenshot |
| 12,13,14 | u8 | Instrument | mute, high-pass, Clipguard (no phantom id in this group) | |
| 15,16,18 | u8 | Instrument | preamp, Denoise, Compress | |
| 19 | u8 | Loopback | mute (group has levels + mute only, no volume) | group ids: LeftLevel, RightLevel, Mute |
| 20 | i16 | Speaker | volume Q8.8, **-60…0 dB** (setter clamp 0xC400…0) | `Volume` |
| 22 | u8 | Speaker | mute | `Mute` |
| 27 | i16 | Headphone | volume, same clamp | |
| 29 | u8 | Headphone | mute | |
| 32 | u8 | Monitor | Dim | `Dim` |
| 33 | u8 | Monitor | Mono | `Mono` |
| 34 | u8 | Monitor | direct-monitor source: **0 instrument only, 1 microphone only, 2 both**; the app's toggle logic (pair tables {(2,0),(1,0)} / {(2,1),(0,1)}) never reaches "none" | `MonitorMicrophone`,`MonitorInstrument` lambdas |
| 35 | i16 | Monitor | **input/playback mix** (id `Balance`), Q8.8 stored sign-inverted; range unverified | getter `-raw/256` |
| 37 | u8 | Main | LED brightness, clamp 0…100 | setter `cmp 0x64` |
| 38–40 | RGB | Main `Icons` | LED colour, byte order R,G,B | setter splits 0xRRGGBB |
| 41–43 / 44–46 / 47–49 / 50–52 | RGB | Main `Mute`/`Parameter`/`Metering`/`GainReduction` | LED colours | contiguous 5×3 bytes |
| 53 | u8 | Main `CustomButton` | custom-button function enum (value↔function map unknown) | |
| 23 | u8 | Main `MixToSpeakers` | `OutputSource` enum; guide: "input/playback mix applies to speakers" (value set unknown) | ordered match |
| 24 | u8 | Main `HighVolumeSpeaker` | bool | ordered match |
| 31 | u8 | Main `HighImpedanceHeadphone` | bool | ordered match |
| 57 | u8 | Main `ExternalGainControl` | bool, guide "gain lock" | ordered match + guide |
| 59 | u8 | Main `MasterSliderMap` | `NativeVolumeControlMapping` enum | type name matches |
| 67 | u8 | Main `MobileStreaming` | bool (routing change) | ordered match |

"ordered match": identifiers and lambda triplets inside the `Main` group are
paired by creation order; the enum type names (`OutputSource`,
`NativeVolumeControlMapping`) and the guide corroborate but do not prove
every pairing. **macOS streaming mode**: id `MacOSStreaming` exists but its
offset was not located (probably device-side/driver state) — unknown.
Everything not listed is unknown; read-modify-write preserves it.

## iface_status_t (0x40)  [V-bin offsets]

Data starts at app-object offset 0x250, so status offset = disp − 0x250.

| off | type | meaning |
|-----|------|---------|
| 0x00 | i16 Q8.8 | input 1 level; app clamps to [-60, 0] dB |
| 0x02 | u8 | input 1 "Gain" (auto-gain result, integer dB?) |
| 0x09 | i16 Q8.8 | input 2 level |
| 0x0B | u8 | input 2 "Gain" |
| 0x12,0x14 / 0x16,0x18 / 0x1A,0x1C | i16 Q8.8 | L/R meters of Loopback / Speaker / Headphone (group order; group names confirmed) |
| 0x23 | u8 | auto-setup result bits: bit6 ⇒ result 0; else bit5 ? 2 : 1 |
| 0x24 | u8 | auto-setup progress |
| 0x2E,0x30 | u16 | clip flags, compared against previous snapshot |

## iface_triggers_t (0x40, write-only)  [V-bin values, ? meaning]

The app writes a u16 at offset 0 of the trigger buffer from UI callbacks.
Values: 1, 2, 3 (auto-gain input 1 / input 2 / both), 6, 7, 8 (denoise
measurement 1 / 2 / both), 0x103 and 0x106 (attached to "ResetLedColors"
callbacks — treated as unknown, **never sent**). Assignment is inferred from
callback order and is unverified.

## iface_version_t (0x40)  [V-bin: no typed consumer]

Layout not recovered, and probably not recoverable from the app: the message
object's vtable is referenced in exactly one place (the `Connect2Device`
constructor inside `LewittDeviceManager::refresh`, which zero-initialises it);
no code decodes its fields. Treat as opaque; capture it from hardware.
 `getCurrentMode()` maps USB PID 0x0004 → one mode and
0x0002 → the other (anything else throws), so PIDs 0x0002/0x0004 are the two
operating modes; 0x000A is probably DFU/bootloader. The request variant flag
(0x85/0x05 vs 3) is set in `Connect2Device::open()` when a u16 in the base
device object at +0x56 is ≥ 0x0b (inside the device-info block: +0x50 VID, +0x52 PID as used by `getCurrentMode`, +0x56 probably the USB `bcdDevice`/firmware version — unverified) —
replacing the earlier "bcdADC" guess.

## Device identity  [V-bin]

Static initializer `Connect2.cpp` builds device configs with VID **0x29C2**
(LEWITT). PID lists: Connect 6 default {0x0005, 0x000B}, Connect 6 mobile
{0x0006}, **Connect 2 default {0x0002, 0x0004, 0x000A}**; an "any" config
uses PID 0xFFFF. Which of the three Connect 2 PIDs is normal mode vs.
DFU/bootloader/variant is **unknown** [?].

## Linux access problem

See `docs/USB_ACCESS.md` (kernel-source-verified analysis and probe plan).

## Not yet examined

* Meaning of `iface_status_t` / `iface_version_t` fields.
* `iface_triggers_t` (actions: autosetup start, denoise measurement, …).
* Write path (`Connect2Device::writeSettings`-equivalent), tick cadence.
* Firmware download (DFU) — **will not be implemented without explicit permission**.
* Windows build (Inno Setup, needs `innoextract`) — expected to share the protocol.

## Phase 5A findings (binary evidence; confidence per item)

### Settings schema and sync algorithm  [V-bin, high]
`Connect2Device::tick` walks a 62-record table at macOS 0x100846f50 (stride
0x28; `u16 offset`, `u16 length` at record+0x10/+0x12). Together the records
cover **bytes 0…70 exactly once**; **bytes 71…127 are never read, merged or
written individually** by the app (reserved/unknown, but the whole 0x80 struct
is written back, so they must round-trip). Field list (offset:length):
0:2, 2–9:1 each, 10:2, 12–19:1 each, 20:2, 22–26:1 each, 27:2, 29–34:1 each,
35:2, 37–62:1 each, 63:4, 67:1, 68:2, 70:1.
Fields known to the schema but with no UI id yet: 8, 17, 25, 26, 30, 54–56,
58, 60–62, 63–66 (u32?), 68–69, 70.

Sync loop (every tick; `LewittDevice::tick`): snapshot ← current;
`readStatus()`; optionally `readSettings()`; then for each schema field, if
`pending != snapshot` **or** (the device's current value differs from the
snapshot) the app copies the *device's* value into the outgoing buffer —
i.e. **device-side changes win, untouched user edits stay**; if anything
differs (or a force flag `[dev+0x388]==1`) the **entire 0x80-byte settings
struct is written** via MEM SET; sample-rate/buffer-size changes go through
the audio driver (not the control protocol); finally, if any byte of the
trigger buffer is non-zero it is written (message 4) and then cleared.
Implemented as `SettingsMessage::mergeExternalChanges` + `DeviceSession`.

### Custom button (settings byte 53)  [V-bin Windows, medium-high]
Radio buttons are `EnumCircleRadioButton<CustomButtonShortPress>`
constructed with an immediate enum value (Windows `FUN_140252390`; each
radio at member offset *k* belongs to the label at `k − 0x4F` qwords):

| value | function | evidence |
|---|---|---|
| 0 | Show CONTROL CENTER | ctor imm 0 at 0x140252450 |
| 1 | Toggle mute: headphone / speakers | imm 1 |
| 2 | Toggle mute: headphone+mic / speakers | imm 2 |
| 8 | Dim outputs | imm 8 |
| 9 | Mono/stereo outputs | imm 9 |
| 0x0C | Toggle mic preamp styles | imm 0xC |
| 0x0D | Toggle instrument preamp styles | imm 0xD |
| 0x0E | Toggle brightness settings | imm 0xE |

Values 3–7, 10, 11 are not offered. "…ShortPress" implies a long-press
setting; the neighbouring bytes 54–56 are in the schema but unmapped
(unverified guess). macOS ctor call sites were not found (no direct calls).

### MixToSpeakers / OutputSource (byte 23)  [V-bin, medium]
The Settings UI binds this value to a `CapsuleToggleButton` via
`ref::Paths(MixToSpeakers)` (macOS `AudioSettingsComponent` ctor
0x1000bb331); `dev::Value<OutputSource,true>::setValue` forwards the
bool-convertible `juce::var` as a byte. Therefore the UI only ever writes
**0 (off, default) and 1 (on)**; no other enumerators are reachable. Enumerator
*names* are not present in the binary.

### macOS streaming mode and device mode  [V-bin, medium]
* `MacOSStreaming` is an identifier that is **defined but never read or
  written by code** — it is not a settings property.
* `AudioSettingsComponent::onMacOSStreamingClicked` (0x1000bd210) reads the
  current mode (`Connect2Device::getCurrentMode`, from the **USB PID**:
  0x0004 → mode 0 = normal, 0x0002 → mode 1 = macOS streaming; other PIDs
  throw) and opens `PowerCycleWindow(DeviceRoot, Mode, text)` with Mode 1
  (turning on) or 0 (off). The window only shows instructions/progress
  ("do not unplug…"); `PowerCycleComponent::timerCallback` is a progress
  animation. **The command that makes the firmware switch mode was not found**
  (not in the tree builder, not in the window). PID 0x000A is not a mode the
  app accepts (probably bootloader/DFU). Treat mode switching as unknown and
  disruptive; not implemented.
* `MobileStreaming` (byte 67) *is* a normal settings bool.

### Triggers  [V-bin, high for assignment; meaning of effects medium]
Each trigger lambda is constructed immediately before the identifier it
serves (see tree order), so the pairing is exact:

| word | id (group) | note |
|---|---|---|
| 1 | AutoGain (Microphone) | |
| 2 | AutoGain (Instrument) | |
| 3 | AutoGainMicPlusInst | |
| 6 | DenoiseMeasurement (Microphone) | |
| 7 | DenoiseMeasurement (Instrument) | |
| 8 | DenoiseMicPlusInst | |
| 0x103 | **ResetLedColors** | persistent LED colour change |
| 4 / 5 | begin / end measurement session | see below |
| 0x104 / 0x105 | switch to macOS-streaming / normal mode | device restarts; see below |
| 0x106 | **FactoryReset** | destructive; never sent by this project |

### Measurement status  [V-bin, medium]
Shared by both inputs: progress `u8 @0x24` (0…100; 100 = finished; the UI
treats ≥22 as "signal expected"), result bits `@0x23`: bit 6 set ⇒ idle/in
progress (0); else bit 5 clear ⇒ **failed (1)**, set ⇒ **succeeded (2)**.
On finish (progress 100) with result 1 and no clip-counter change the app shows
"No signal detected! Please repeat measurement."; a clip-counter change gives
"Clipping detected! …"; "No calibration response received" is shown when the
status bit `(@0x…4a0 & 2)` was never set (Connect 6/shared panel). Clip
counters: `u16 @0x2E` (input 1) and `@0x30` (input 2), compared with the
previous snapshot. Auto-gain result `u8 @0x02`/`@0x0B` = measured gain.
Denoise measurement shares the same progress/result bytes.

### Mode switching and measurement bracket (phase 5B, corrected scanners)  [V-bin mac+win, high]
Earlier "not found" results for the macOS-streaming command were wrong: the
call-graph filter hid `PowerCycleComponent::onOKButtonClicked`. It reads the
target mode (0 normal / 1 macOS streaming), compares with `getCurrentMode()`,
and if different stores a **trigger word into the trigger buffer** and sends it
with `LewittDevice::write`, then clears the buffer:

| target mode | trigger word | macOS | Windows |
|---|---|---|---|
| 0 normal (PID 0x0004) | **0x0105** | `mov ax,0x105` @0x1000be6f2 | `uVar2 = 0x105` in FUN_140201510 |
| 1 macOS streaming (PID 0x0002) | **0x0104** | `mov ax,0x104` @0x1000be705 | `uVar2 = 0x104`, else "Device mode not recognized" |

`SettingsComponent::onFactoryResetButtonClicked` stores **0x106** directly
(`mov word [dev+0x2e0],0x106`) and shows the same restart window; the tree
lambdas for `ResetLedColors`/`FactoryReset` store 0x103/0x106 (Windows: same
immediates at `[obj+0x300]`). A whole-binary scan for 16-bit stores into the
trigger buffer (mac `+0x2e0`, win `+0x300`) yields exactly this word set:
1, 2, 3, 6, 7, 8, **4**, **5**, 0x103, 0x104, 0x105, 0x106 (0 = clear).

| word | stored by | meaning (confidence) |
|---|---|---|
| 4 | `InputChannelPanel::onAutoGainButtonClicked`, `::onDenoiseButtonClicked`, `CaptionInputChannel::onAutoSetupButtonClicked` | begin measurement session (name inferred; device effect unverified) |
| 5 | `autoGainFinished`, `denoiseFinished`, `~AutoSetupWindow` | end measurement session (same caveat) |

Sequence implemented in the mock/GUI: `4` → `1|2|3` (or `6|7|8`) → poll
progress/result → `5`. The handlers stop the sync thread, write the word and
restart it, so the word goes out in the next tick's trigger write.

### Cross-validation summary (phase 5B)
| Finding | macOS | Windows | Status |
|---|---|---|---|
| Transport: MEM GET/SET, entity 0x33, req 3/0x85/0x05, 500 ms | ✔ | ✔ | high |
| Custom-button values 0,0xC,0xD,1,2,8,9,0xE | ✔ (CustomSettingsComponent ctor, rdx imm) | ✔ (EnumCircleRadioButton ctor, r8b imm) | values high; label↔value mapping medium-high (Windows member/label offset rule, same order on macOS) |
| Trigger words 1,2,3,6,7,8 | ✔ lambdas | ✔ stores to +0x300 | high |
| 0x103 / 0x106 | ✔ | ✔ | high |
| 0x104 / 0x105 mode switch | ✔ | ✔ | high (effects unverified on HW) |
| 4 / 5 | ✔ | ✔ (3 + 4 sites) | high that they are written; semantics medium |
| MixToSpeakers boolean | ✔ (toggle bound via Paths) | not re-traced | medium |
| Auto-gain result/progress | ✔ | ✔ (timer callback decompile) | medium |
| API handshake 0x0A | ✔ | not re-traced | medium |
| Settings schema / sync merge | ✔ | not re-traced | high (single binary) |

Nothing above is hardware-verified.

### Not found / still open
* `OutputSource` enumerator names; bytes 54–56, 58, 60–70 semantics.
* Authoritative range for the input/playback mix value (Q8.8 sign-inverted).
* `iface_version_t` layout (no consumer).
* Windows has not been re-traced for the above (same codebase; cross-check of
  the transport only).

## Windows build cross-check  [V-bin]
Same C++ codebase (identical `LWT::Connect2::APIv1`, `buildConnect2Tree`,
`doAsyncFWDownload` RTTI). Windows loads Thesycon's `tusbaudioapi` and resolves
`TUSBAUDIO_AudioControlRequestSet/Get` and `ClassVendorRequestOut/In` into the
API object at +0x138/+0x140/+0x148/+0x150. In
`LewittDeviceThesyconWinBackend` (vtable slots 15–18, Ghidra 12.1.4):

| method | API call | entity | request | selector / channel | size | timeout |
|---|---|---|---|---|---|---|
| MEM GET (slot 15) | AudioControlRequestGet | **0x33** | **3**, or **0x85** if `this[8]==1` | `wValue>>8` / low byte of wValue (= apiMajor / messageId) | arg | 500 ms |
| MEM SET (slot 16) | AudioControlRequestSet | **0x33** | **3**, or **0x05** if `this[8]==1` | same | arg | 500 ms |
| OUT/IN CTRL (17/18) | ClassVendorRequestOut/In | — (interface from `this+0x14`) | — | — | — | 500 ms |

This matches macOS byte-for-byte (entity 0x33, requests 3/0x85/0x05,
wValue = apiMajor<<8 | messageId, 500 ms). Windows lets the Thesycon driver
pick the AudioControl interface number (the app passes only the entity ID);
macOS uses `0x3300 | interface`. Failure → "THESYCON: MEM GET/SET failed".
The `this[8]` UAC1 flag is set by the same "≥ 0x0b" logic as on macOS (not
re-traced on Windows).

## Backend contract derived from the protocol (phase 5C)
* Handshake first: read message 0x0A (wValue `0x000A`, apiMajor 0); major must be 1; the USB
  product id must be 0x0004 or 0x0002 (the app's two modes). Anything else → refuse.
* Writes only of messages 0 (settings, whole 0x80 struct) and 4 (trigger word at bytes 0–1,
  rest zero). Never: 0x106 (factory reset), 0x104/0x105 (mode switch) on hardware.
* Only bytes with a verified setter may differ from the last device read; all other bytes —
  including schema bytes without a UI binding and bytes 71–127 — are written back unchanged.
* A boolean/enum byte holding an unexpected value is never "normalised".
