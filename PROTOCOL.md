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

## iface_settings_t field map  [V-bin offsets, ? semantics]

Authoritative copy lives in `src/protocol/Messages.hpp` (`settings_off`).
Input N fields (N=1 / N=2): gain i16 Q8.8 @0/10 (setter clamps 0…0x5500 =
0…85 dB), mute @2/12, high-pass @3/13, Clipguard @4/14, preamp type @6/15
(0 Clean, 1 Warm, 2 Vivid), denoise @7/16, compress @9/18; phantom power @5
(input 1 only; input 2 not located). Output-side: mute @19, volume i16 @20,
mute @22, OutputSource @23, bool @24, volume i16 @27 (setter clamp
-60…0 dB), mute @29, bool @31, dim @32, mono @33, flag @34, balance i16 @35
(sign inverted), custom-button action @53, native-volume mapping @59.
Group semantics for 19–35 are provisional. Anything not listed is unknown;
read-modify-write preserves it.

## iface_status_t (0x40)  [V-bin offsets]

Data starts at app-object offset 0x250, so status offset = disp − 0x250.

| off | type | meaning |
|-----|------|---------|
| 0x00 | i16 Q8.8 | input 1 level; app clamps to [-60, 0] dB |
| 0x02 | u8 | input 1 "Gain" (auto-gain result, integer dB?) |
| 0x09 | i16 Q8.8 | input 2 level |
| 0x0B | u8 | input 2 "Gain" |
| 0x12,0x14,0x16,0x18,0x1A,0x1C | i16 Q8.8 | further L/R level pairs (output/mix meters, assignment ?) |
| 0x23 | u8 | auto-setup result bits: bit6 ⇒ result 0; else bit5 ? 2 : 1 |
| 0x24 | u8 | auto-setup progress |
| 0x2E,0x30 | u16 | clip flags, compared against previous snapshot |

## iface_triggers_t (0x40, write-only)  [V-bin values, ? meaning]

The app writes a u16 at offset 0 of the trigger buffer from UI callbacks.
Values: 1, 2, 3 (auto-gain input 1 / input 2 / both), 6, 7, 8 (denoise
measurement 1 / 2 / both), 0x103 and 0x106 (attached to "ResetLedColors"
callbacks — treated as unknown, **never sent**). Assignment is inferred from
callback order and is unverified.

## iface_version_t (0x40)  [?]

Layout not recovered. `getCurrentMode()` maps USB PID 0x0004 → one mode and
0x0002 → the other (anything else throws), so PIDs 0x0002/0x0004 are the two
operating modes; 0x000A is probably DFU/bootloader. The request variant flag
(0x85/0x05 vs 3) is set in `Connect2Device::open()` when a u16 in the base
device object at +0x56 is ≥ 0x0b (likely a firmware/version number) —
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
