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

(id 1 is unused by Connect2; Connect 6 has additional levels/mixer/dsp messages.)

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
