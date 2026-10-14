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

Offsets exact (from getter/setter lambdas in `buildConnect2Tree`). Labels are
the app's own `juce::Identifier` names; the group a field belongs to is
inferred from build order and may be shifted by one in places.

| off | type | name / meaning | encoding |
|-----|------|----------------|----------|
| 0   | i16  | Input 1 gain   | Q8.8 dB; setter clamps 0…0x5500 (0…85 dB) |
| 2   | u8   | Input 1 mute   | bool |
| 3   | u8   | Input 1 high-pass filter | bool |
| 4   | u8   | Input 1 Clipguard | bool |
| 5   | u8   | Input 1 phantom power | bool (**safety relevant**) |
| 6   | u8   | Input 1 preamp type | 0 Clean, 1 Warm, 2 Vivid |
| 7   | u8   | Input 1 denoise | bool |
| 9   | u8   | Input 1 compress | bool |
| 10  | i16  | Input 2 gain   | Q8.8 dB |
| 12  | u8   | Input 2 mute |
| 13  | u8   | Input 2 high-pass |
| 14  | u8   | Input 2 Clipguard |
| 15  | u8   | Input 2 preamp type | as 6 |
| 16  | u8   | Input 2 denoise |
| 18  | u8   | Input 2 compress |
| 19  | u8   | mute (output group, ?) |
| 20  | i16  | volume (output group, ?) | Q8.8 |
| 22  | u8   | mute (?) |
| 23  | u8   | OutputSource enum |
| 24  | u8   | "HighVolumeSpeaker" bool |
| 27  | i16  | volume (?) | Q8.8, setter clamps ≤ 0xC400 as shown |
| 29  | u8   | mute (?) |
| 31  | u8   | bool (paired with OutputSource, ?) |
| 32  | u8   | Dim | bool |
| 33  | u8   | Mono | bool |
| 34  | u8   | Monitor microphone / instrument (shared flag, ?) |
| 35  | i16  | Balance | Q8.8, sign inverted |
| 37  | u8   | LED brightness (?) |
| 38,40 | u8,u8 | "Icons" (two bytes combined, ?) |
| 41,43 / 44,46 / 47,49 / 50,52 | | "Mute"/"Parameter"/"Metering"/"GainReduction" byte pairs (DSP/UI related, ?) |
| 53  | u8   | CustomButtonShortPress enum (hardware button action) |
| 57  | u8   | bool (ExternalGainControl related, ?) |
| 59  | u8   | NativeVolumeControlMapping enum |
| 67  | u8   | "MobileStreaming" bool (? — name likely belongs to a neighbour) |

Anything not listed is unknown. Do not write unknown bytes.

## Device identity  [V-bin]

Static initializer `Connect2.cpp` builds device configs with VID **0x29C2**
(LEWITT). PID lists: Connect 6 default {0x0005, 0x000B}, Connect 6 mobile
{0x0006}, **Connect 2 default {0x0002, 0x0004, 0x000A}**; an "any" config
uses PID 0xFFFF. Which of the three Connect 2 PIDs is normal mode vs.
DFU/bootloader/variant is **unknown** [?].

## Linux access problem (important)  [?→ needs hardware]

usbfs only permits interface-recipient control requests on an interface the
caller has **claimed**. The AudioControl interface is bound to
`snd-usb-audio`, so a naive userspace tool must detach it, which removes the
whole sound card for the duration. That conflicts with the
"don't interfere with audio" requirement. Options to evaluate with hardware:

1. Accept brief detach/reattach (current `transport.py`, gated behind `--allow-detach`).
2. Check whether the firmware routes purely on unit ID `0x33` and ignores
   the interface byte → a different, unbound interface might work. Pure guess.
3. A small kernel-side quirk/helper (hard; distribution-hostile).
4. Does the Linux snd-usb-audio already expose unit 0x33 as extension-unit
   controls in ALSA (`amixer -c N contents`)? Worth dumping first.

## Not yet examined

* Meaning of `iface_status_t` / `iface_version_t` fields.
* `iface_triggers_t` (actions: autosetup start, denoise measurement, …).
* Write path (`Connect2Device::writeSettings`-equivalent), tick cadence.
* Firmware download (DFU) — **will not be implemented without explicit permission**.
* Windows build (Inno Setup, needs `innoextract`) — expected to share the protocol.
