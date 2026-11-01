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

**[V-hw] 2026-10-15 — MEM SET hardware-confirmed** (`docs/HARDWARE_TEST_RESULTS.md` §13): a real
`bm=0x21 req=0x03 wValue=0x0100 wIndex=0x3303` SET of the full 128-byte settings struct (one byte
changed: offset 37, LED brightness) against interface 3 — the same unbound interface already
confirmed for GET — was accepted by a real CONNECT 2 and visibly changed device state (LED
brightness; one transition directly witnessed by the owner), then reversed with a second SET,
confirmed back to the original byte value. This is the first real-hardware write of any kind on this
project. **Finding:** a GET sent immediately after a SET completes (same process, microseconds later)
returned the *pre-write* value; a GET from a separate process moments later (tens-to-hundreds of ms
later) returned the correct post-write value. The device appears to need a short settling delay
between accepting a SET and a subsequent GET reflecting it — **do not treat an immediate blind
re-read after a write as authoritative**; delay or poll instead. Only this single field (LED
brightness) and this single interface/request shape have been write-tested; this does not confirm SET
works for every message type, every field, or via interface 0/4.

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
confidence, now **hardware-confirmed**: a real CONNECT 2 (serial redacted)
returned byte0=`0x01`/byte1=`0x00` for this GET — see
`docs/HARDWARE_TEST_RESULTS.md` §4); bytes 2..63 are **not unknown-and-presumed-zero
any more** — the real device returned non-zero, partially-ASCII data there
(including its own `iSerial` string at bytes 26–45), captured verbatim but not
yet mapped field-by-field. This response was obtained over interface 3 (an
undocumented unbound vendor-specific interface, not the AudioControl interface);
see `docs/USB_ACCESS.md` §3b for why interface 0 did not answer. (id 1 is unused
by Connect2; Connect 6 has additional levels/mixer/dsp messages.)

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
| 35 | i16 | Monitor | **input/playback mix** (id `Balance`), Q8.8 stored sign-inverted; physically accessible low/high stops on one CONNECT 2 read −100.00/0.00 (2026-10-17); authoritative protocol/SET range still unverified | getter `-raw/256` |
| 37 | u8 | Main | LED brightness, clamp 0…100 | setter `cmp 0x64` |
| 38–40 | RGB | Main `Icons` | LED colour, byte order R,G,B | setter splits 0xRRGGBB |
| 41–43 / 44–46 / 47–49 / 50–52 | RGB | Main `Mute`/`Parameter`/`Metering`/`GainReduction` | LED colours | contiguous 5×3 bytes |
| 53 | u8 | Main `CustomButton` | custom-button function enum (value↔function map unknown) | |
| 23 | u8 | Main `MixToSpeakers` | `OutputSource` enum; guide: "input/playback mix applies to speakers" (value set unknown) | ordered match |
| 24 | u8 | Main `HighVolumeSpeaker` | bool | ordered match |
| 31 | u8 | Main `HighImpedanceHeadphone` | bool | ordered match |
| 57 | u8 | Main `ExternalGainControl` | bool, guide "gain lock" | ordered match + guide |
| 59 | u8 | Main `MasterSliderMap` | **"Map native volume controls to:" 0 None, 1 Speakers, 2 Headphones** (Windows 5.2.2: item list `FUN_14028c460`, setter `lambda_112` stores the value unchanged); the owner's device reads 1 — the OS/kernel volume drives the speakers | binary + device read 2026-10-31 |
| 67 | u8 | Main `MobileStreaming` | bool (routing change) | ordered match |

**Hardware observations, Phase 11B (2026-10-27/28, GUI writes verified by read-back) [V-hw]:**

| Byte(s) | Observation |
|---|---|
| 2 / 12 | mic / instrument mute: the **only** gate for capture ch 1 / ch 2 (muted = exact digital zero; UAC2 FU 5 mute has no audio effect) |
| 3, 4, 6, 9 / 13, 14, 15, 18 | low-cut, Clipguard, preamp (0/1/2, 2 = Vivid), Comp: device accepts and reports each value |
| 19 | loopback mute: muted = capture ch 3/4 exact zero; unmuted = computer playback at unity gain (independent of output volumes) |
| 20–21, 22, 27–28, 29 | speaker / headphone volume (Q8.8 dB, any value incl. off-grid e.g. −32.398 dB from the device knob) and mutes |
| **25** | **device-maintained**: set by the device to the Dim value (0↔1) whenever Dim (32) changes; OpenWitty never edits it |
| **26** | **device-maintained**: set by the device to the Mono value whenever Mono (33) changes |
| 34 | monitoring source: 0 instrument only, 1 microphone only, 2 both |
| 35–36 | mix: byte 36 = −(value) in whole steps (−30 → 30), byte 35 stayed 0 |
| 38–52 | LED colours: Icons = channel icons of unmuted channels, Mute = icons of muted channels, Parameter = gain/volume display of the selected channel, Metering = level meter, Gain reduction = its meter part |
| 53 | custom button: value 2 = "Toggle mute: hp + mic/spkr" (official label), 12 = another function |
| 57 | was 1 on the owner's device (GUI label "gain lock"); each toggle makes the device report UAC2 FU 5 mute changes (ch 1, ch 2); locked ⇒ FU 5 reads muted — consistent with 1 = host control locked out (unlocked value not sampled) |
| 5, 24, 31 | 48 V, high-volume speakers, high-impedance headphones: accepted and reported; no follower bytes. The rear-panel 48 V button toggles byte 5 itself (same setting) |
| 53 = 2 | custom button "Toggle mute: hp + mic/spkr": one press flips bytes 2, 22 and 29 together |
| sample rates | device follows 44.1 / 48 / 96 kHz (kernel hw_params), channel order unchanged at each rate; rare `usb_set_interface` timeout (-110) at stream start (session 7); **repeated rate changes (96/44.1/48 every few seconds) hung the audio interfaces until replug** (2026-10-31); constant-rate starts 120/120 clean regardless of control polling |
| 56 (host write) | the device **accepts** a host write of the touch focus (3 speakers, 4 headphones) and shows it on the touch surface (2026-10-31); the CONNECT 2 app itself never writes it |
| 8 / 17 scale | **int8 dBFS ≈ peak level of the measured background noise** (session 6: phone noise peak −30 → stored −28; quiet room peak −51 → −53; open cable peak −39.9 → −41) |
| 67 | mobile streaming: while 1, capture ch 1 and ch 2 both carry the mic + instrument mix; ch 3/4 unchanged; no re-enumeration |
| 23 | mix to speakers: accepted 0/1, no followers (audible effect not judged; speakers off) |
| 7, 8 / 16, 17 | Denoise measurement (words 4, 6 / 7, 5; session 5, 2026-10-30): the **device switches Denoise on itself** (7 / 16: 0 → 1) and stores a new value in 8 / 17 (mic 202 → 196, again 196; instrument with open cable 211 → 215). 8 / 17 = most likely the measured noise level; as int8 that would be −60 / −41 dB (unverified). Result code 0x23 read 1, then 0, 0 — not a success flag |
| autogain (2026-11-01) | **while the measurement runs, the settings read a neutral mic state** (gain 0, unmuted, Low cut / Clipguard / Comp / Denoise off, preamp Clean); after the end-session word the device restores them and keeps only the new gain (44 → 39 dB). Progress 0x24 **stays 100 after a measurement** until the next one starts: a host must see it below 100 before treating 100 as done. Result code 0x23 read 1 and 0 on two successful autogains: not a success flag. **Speaker volume dropped by itself** during the measurement runs (−26.5 → −30.5 → −35 → −40.5 dB over four measurements, no host write, knob untouched); not restored afterwards - likely feedback protection (unconfirmed which measurement lowers it) |

Capture channel order (LB-1/LB-2): ch 1 microphone, ch 2 instrument, ch 3 loopback L, ch 4 loopback R; no crosstalk.

"ordered match": identifiers and lambda triplets inside the `Main` group are
paired by creation order; the enum type names (`OutputSource`,
`NativeVolumeControlMapping`) and the guide corroborate but do not prove
every pairing. **macOS streaming mode**: id `MacOSStreaming` exists but its
offset was not located (probably device-side/driver state) — unknown.
Everything not listed is unknown; read-modify-write preserves it.

**Panic Mute (MUTE button hold)** — **[V-hw, hardware-confirmed 2026-10-16]**. The official online
guide (verified 2026-10-16, see `docs/OFFICIAL_GUIDE.md`) states: a short MUTE press toggles the
currently-selected channel only (matches our hardware-confirmed behaviour for mic/instrument/
speaker/headphone — §10/§12 of `docs/HARDWARE_TEST_RESULTS.md`); holding MUTE mutes **all input and
output channels simultaneously**; recovery requires unmuting each channel individually, not holding
MUTE again.

Hardware-confirmed (3 read-only trials, `docs/HARDWARE_TEST_RESULTS.md` §16): holding MUTE sets
**exactly** offsets 2 (`kMute[0]`), 12 (`kMute[1]`), 22 (`kSpeakerMute`), 29 (`kHeadphoneMute`) to
`1` simultaneously, regardless of which channel is selected (rules out "selected channel only").
**Confirmed NOT affected**: offset 19 (`kLoopbackMute`) and offset 56 (channel-select) — both stayed
unchanged across every trial. **No separate/dedicated Panic-Mute flag exists** anywhere in the
128-byte struct, including the unmapped ranges (54–55, 58, 60–70) — nothing outside those four bytes
ever moved. An unidentified but *reproducible* `status` message change (offsets 3–4, 12–13, same
values in all 3 trials) accompanies it — not interpreted, flagged for future investigation.
Individual-unmute recovery was not independently re-tested (would require unmuting live channels
with nothing new to learn); no code assumes holding MUTE again undoes it.

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

## Firmware version shown by OpenWitty (2026-10-31)

The Windows app shows "CONNECT 2 firmware version <text>" from a string member of `Connect2Component` (+0xe748) that
is filled indirectly; it uses the Thesycon driver (`TUSBAUDIO_GetDeviceProperties`, which reports the USB revision).
OpenWitty shows the USB `bcdDevice` from sysfs as "1.21 (USB revision)" (owner's device: 0x0121); whether the
official app formats it the same way is unconfirmed.

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

**Hardware-confirmed 2026-10-14/2026-10-15** (`docs/HARDWARE_TEST_RESULTS.md` §§10, 12): physically
operating the real CONNECT 2's front panel and diffing `read settings --interface 3` before/after
confirms the following offsets exactly — in every case the predicted field, and only that field (plus
byte 56, see below), changed:

| Offset(s) | Field | Confirmed via |
|---|---|---|
| 0–1 | `kGain[0]` (mic gain) | wheel turn, exact +1.00 dB |
| 10–11 | `kGain[1]` (instrument gain) | wheel turn, exact +11 dB then partial restore |
| 2 | `kMute[0]` (mic mute) | MUTE button, exact toggle + verified reversal |
| 12 | `kMute[1]` (instrument mute) | MUTE button, exact toggle + verified reversal |
| 20–21 | `kSpeakerVolume` | wheel turn, exact −11.66 dB then exact restore to the 0.00 dB ceiling |
| 22 | `kSpeakerMute` | MUTE button, exact toggle (deliberately left muted afterward, not reversed, as a monitoring-safety precaution during the session) |
| 27–28 | `kHeadphoneVolume` | wheel turn, exact dB match on two separate edits including a partial restore |
| 29 | `kHeadphoneMute` | MUTE button, exact toggle + verified reversal |

First real (non-tautological) validation of any settings offsets on this project. The MUTE button
was confirmed (by direct observation on hardware) to act on whichever channel is currently selected,
not a fixed channel — consistent with the byte-56 finding below.

**Byte 56 — CONFIRMED channel-selection indicator** (`docs/HARDWARE_TEST_RESULTS.md` §§10.5, 12):
repeatedly and predictably changes to reflect which channel is currently *selected* on the touch
surface, **independent of any value edit** — confirmed by pressing a select button alone, with no
wheel turn, and seeing only this byte move, for all four primary channels, each confirmed more than
once:

| Value | Channel |
|---|---|
| `0x01` | microphone |
| `0x02` | instrument |
| `0x03` | speaker |
| `0x04` | headphone |

This matches the button numbering (1–4) on the device's own front-panel diagram exactly. This
**contradicts** the unverified guess below (§"CustomButton (byte 53)") that bytes 54–56 relate to
custom-button long-press configuration; that guess should be treated as superseded for byte 56
specifically (54–55 remain unmapped). Still not necessarily a *complete* enum — the mute, custom,
mix/monitor and autogain buttons are action-trigger buttons rather than pure selectors and were not
tested against this byte (doing so would have side effects beyond selection), so whether they also
populate byte 56, and with what values, remains open.

Phase 7B exposes this as the typed `SelectedChannel` decoder. Values outside `0x01`–`0x04` remain
raw-preserved and are presented as Unknown; the GUI only reflects this physical focus and has no
channel-selection write/control path.

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
(unverified guess — **superseded for byte 56 by hardware evidence 2026-10-14**: byte 56 tracks
touch-surface channel selection, not custom-button configuration; see the note above the schema field
list). macOS ctor call sites were not found (no direct calls).

### MixToSpeakers / OutputSource (byte 23)  [V-bin, medium]
The Settings UI binds this value to a `CapsuleToggleButton` via
`ref::Paths(MixToSpeakers)` (macOS `AudioSettingsComponent` ctor
0x1000bb331); `dev::Value<OutputSource,true>::setValue` forwards the
bool-convertible `juce::var` as a byte. Therefore the UI only ever writes
**0 (off, default) and 1 (on)**; no other enumerators are reachable. Enumerator
*names* are not present in the binary.

### Windows 5.2.2 cross-check (Ghidra 12.1, 2026-10-30)  [V-bin]
Binary `LEWITT CONTROL CENTER.exe` 5.2.2 (SHA-256 `aed77fbd…cf4a`, extracted with innoextract 1.10-dev; nothing
tracked here). Findings:
* **Result byte decode confirmed:** `FUN_1402ba420` / `FUN_1402bbb20` read status byte 0x23: bit 6 → 0, else
  bit 5 set → 2, clear → 1. The autogain panel (`FUN_14022b3f0`) reads `AutoSetupResult` when `AutoSetupProgress`
  reaches 100 and shows "No signal detected!" for value 1. On the hardware a successful autogain read 1 in the same
  poll as progress 100 — so the byte is probably updated after progress; OpenWitty does not treat 1 as a failure.
* Denoise has its own failure text "Background noise too loud! Please repeat measurement." (a flag in the
  AutoSetup ambient-noise panel). The app warns "Attention! Microphone/Instrument will be unmuted if currently
  muted." before a measurement (the official flow unmutes the input).
* **Bytes 8 / 17 are not in the app's settings schema**: the official app never reads or shows the Denoise level, so
  its scale can only be found by measurement.
* `MixToSpeakers` (23) is labelled "Input/playback mix applies to speakers"; `MobileStreaming` (67) is labelled
  "Mobile streaming mode" with "(Microphone and instrument mix to software input 1/2)". OpenWitty offers both under
  the *routing* permission (each switch-on confirmed); not yet hardware-tested.

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
**[V-hw 2026-10-29]** Mic autogain on hardware: trigger 4 then 1; the device dropped the gain to 0, then set 39 dB while
the owner spoke; progress reached 100 *before* the new gain appeared in the settings; the result byte decoded to **1**
although the measurement succeeded — the 1 = failed / 2 = succeeded reading above is doubtful.

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
| Transport: MEM GET/SET, entity 0x33, req 3/0x85/0x05, 500 ms | ✔ | ✔ | high; **GET hardware-verified 2026-10-14, SET hardware-verified 2026-10-15 (LED brightness only, via interface 3 — see note above)** |
| Custom-button values 0,0xC,0xD,1,2,8,9,0xE | ✔ (CustomSettingsComponent ctor, rdx imm) | ✔ (EnumCircleRadioButton ctor, r8b imm) | values high; label↔value mapping medium-high (Windows member/label offset rule, same order on macOS) |
| Trigger words 1,2,3,6,7,8 | ✔ lambdas | ✔ stores to +0x300 | high |
| 0x103 / 0x106 | ✔ | ✔ | high |
| 0x104 / 0x105 mode switch | ✔ | ✔ | high (effects unverified on HW) |
| 4 / 5 | ✔ | ✔ (3 + 4 sites) | high that they are written; semantics medium |
| MixToSpeakers boolean | ✔ (toggle bound via Paths) | not re-traced | medium |
| Auto-gain result/progress | ✔ | ✔ (timer callback decompile) | medium |
| API handshake 0x0A | ✔ | not re-traced | medium (bytes 0–1 **hardware-verified** 2026-10-14, bytes 2–63 hardware-captured/unmapped) |
| Settings schema / sync merge | ✔ | not re-traced | high (single binary) |

Nothing above is hardware-verified **except API handshake bytes 0–1**, first confirmed against a
real CONNECT 2 on 2026-10-14 (`docs/HARDWARE_TEST_RESULTS.md`). Everything else remains
static-analysis-only.

### Not found / still open
* `OutputSource` enumerator names; bytes 54–55, 58, 60–70 semantics (byte 56 is now confirmed for
  the 4 channel-select values, see the hardware-confirmed note earlier in this file — not
  necessarily complete for mute/custom/mix/autogain button states).
* Authoritative protocol/SET range for the input/playback mix value (Q8.8 sign-inverted); one
  device's physical wheel produced −100.00…0.00 in the read-only Phase 7C session (§18 of
  `docs/HARDWARE_TEST_RESULTS.md`), but no SET was attempted.
* `iface_version_t` layout (no consumer).
* Windows has not been re-traced for the above (same codebase; cross-check of
  the transport only).
* **Hardware access, not RE:** Clipguard (@4/@14), preamp style (@6/@15), Mono (@33) have no
  physical control and no long-press gesture on this CONNECT 2 unit (owner-confirmed 2026-10-14) —
  CONTROL CENTER/software-only, so they cannot be physically validated on this hardware regardless
  of RE confidence. Dim (@32) likewise has no dedicated front-panel button on this unit.

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

## Progressive hardware-write control policies (Phase 6D, 2026-10-15)

Specification only — **none of these except LED brightness are wired to a CLI/GUI write command**.
Preparing a policy here does not activate it; activation is a separate, explicit step (a narrowly
scoped command plus its own hardware test) done one control at a time, same as LED brightness was in
Phase 6C. "Hardware-verified" below means the *field offset mapping* was confirmed by physically
operating the control and diffing reads (Phase 6A/6B) — **not** that a USB SET has been sent for it;
LED brightness is the only field where an actual SET has been hardware-tested (Phase 6C/6D).

| # | Control | Offset(s) | Encoding | Range | Required device state before writing | Risk | Readback verification | Rollback | Status |
|---|---|---|---|---|---|---|---|---|---|
| 1 | LED brightness | 37 | u8, raw percent | 0–100 | none | Low (cosmetic only) | byte 37 | write original value back | **ACTIVE** — hardware-verified SET (Phase 6C/6D), only enabled CLI write command |
| 2 | Custom LED colours | 38–52 (5× RGB triples: icons 38, mute 41, parameter 44, metering 47, gain-reduction 50) | 3× u8 per element | 0–255 per channel | none | Low (cosmetic only) | the 3 bytes for the changed element | restore original RGB | Mock-tested only (`setLedColor`); not hardware-tested; not CLI-enabled |
| 3 | Microphone gain | 0–1 | i16 LE Q8.8 dB | 0–72 dB | none (capture-level only, no direct output) | Low | bytes 0–1 | restore original gain | Field mapping hardware-verified (Phase 6A); SET untested; not CLI-enabled |
| 4 | Instrument gain | 10–11 | i16 LE Q8.8 dB | 0–60 dB | none | Low | bytes 10–11 | restore original gain | Field mapping hardware-verified (Phase 6B); SET untested; not CLI-enabled |
| 5 | Speaker / headphone volume | 20–21 / 27–28 | i16 LE Q8.8 dB | −60…0 dB | confirm no audio is actively playing through the channel being changed; prefer testing while muted, as done in Phase 6A/6B | **Medium** — directly drives audible output level | the relevant offset pair | restore original volume | Field mapping hardware-verified (Phase 6A/6B); SET untested; not CLI-enabled |
| 6 | Mute states (mic/instrument/speaker/headphone/loopback) | 2 / 12 / 22 / 29 / 19 | u8 boolean | 0/1 | confirm associated volume/monitor routing is safe before unmuting (see Phase 6B's speaker-mute-first precaution) | Low–Medium — unmuting can expose an existing volume/routing setting | the relevant offset | restore original mute state | Field mapping hardware-verified, all 4 primary channels (Phase 6B); SET untested; not CLI-enabled |
| 7 | Monitoring mix (source + input/playback mix) | 34 (enum 0/1/2) / 35–36 (i16 Q8.8, sign-inverted) | enum + i16 | source 0–2; current software setter accepts only ±1, but physical reads reached −100…0, so **do not use the setter for hardware** until range/mapping is resolved | output muted/safe — this changes what's audible in the monitor path | **Medium** — can introduce an unexpected monitoring/feedback path | offsets 34–36 | restore originals | Physical wheel changed only bytes 35–36 on 2026-10-17; no SET attempted or authorized (§18) |
| 8 | Preamp style + input processing (preamp 6/15, denoise 7/16, compress 9/18, Clipguard 4/14, low-cut 3/13) | see offsets | mostly u8 enum/boolean | preamp 0–2 (Clean/Warm/Vivid); denoise/compress/Clipguard/low-cut 0/1 | sample rate < 96 kHz for denoise/compress (`violatesDspRestriction`, already enforced in `commit()`) | Low (signal-character change, not loudness) | relevant offset(s) | restore originals | **Preamp style has no physical control on this unit at all** (owner-confirmed, Phase 6B) — software-only, cannot be physically cross-checked here. Denoise/compress/Clipguard/low-cut: no hardware evidence yet. Not CLI-enabled. |

**Explicitly out of scope for any policy above, now or later without separate authorization:**
firmware update, factory reset (trigger `0x106`), streaming-mode switch (`0x104`/`0x105`), any
generic/arbitrary trigger write, any settings byte without a verified setter (54–55, 58, 60–70), and
phantom power (`kPhantom1`, offset 5) — phantom power specifically requires its own separate
authorization and safety review before any write policy is even drafted for it, per instruction.
