# Hardware test results — first real CONNECT 2 session

**Date:** 2026-10-14
**Machine:** CachyOS Linux (Arch-based), kernel `7.2.9-2-cachyos`, KDE Plasma (Wayland), PipeWire/PulseAudio-compat.
**Build under test:** commit `ef6143363cdd69c25e3baa8f278068d838a6ac72` (HEAD at session start), `release` preset.
**Device:** LEWITT CONNECT 2, serial `<redacted, see note>`, bus 6 device 4, path `/sys/bus/usb/devices/6-4.1`.

*Serial numbers and other per-unit identifiers below are redacted to a fixed-length placeholder
(`XXXXXXXXXXXX`) everywhere they appear, including inside raw hex dumps — byte lengths and offsets
are preserved so the structural analysis stays reproducible; only the identifying content is removed.*

This is the **first time** OpenWitty has been run against physical hardware. Everything below is new
evidence; nothing was known before this session except by static analysis.

## 1. Build and test baseline

```
cmake --preset release && cmake --build --preset release   # 48/48 targets, clean
QT_QPA_PLATFORM=offscreen ctest --preset release            # 95/95 passed (visual_geometry skipped: no venv)
```

Matches the documented baseline exactly (HANDOFF_HARDWARE_TESTING.md §3 predicts 95 on a clean
machine without the Pillow/numpy venv). No test or protocol behaviour was changed to make the build
pass.

CLI and mock GUI smoke tests:
- `openwitty-cli --version` → `openwitty-cli 0.1.0 (full build)`
- `openwitty-cli list` → `29c2:0004` (sysfs enumeration, no USB I/O)
- `openwitty-cli read settings --mock` → plausible decoded simulated settings
- `openwitty --mock --screenshot` → GUI renders, banner clearly reads "SIMULATED DEVICE — no hardware
  is controlled; values shown are not device data."

Binaries are at `build-release/openwitty-cli` and `build-release/src/gui/openwitty` (not under
`src/cli/`/`src/gui/` as the docs' quick-start snippets suggest — flagging as a doc inaccuracy, not a
build problem).

## 2. Detected hardware (sysfs only, no USB I/O)

```
29c2:0004  bus 6 dev 4  bcdDevice 0121  [/sys/bus/usb/devices/6-4.1]
  Lewitt GmbH / CONNECT 2  serial XXXXXXXXXXXX
  cfg 1 if 0 alt 0  class 01/01/20  (UAC2 AudioControl)  driver: snd-usb-audio
      ep 0x83 attr 0x03 maxpkt 7 interval 13
  cfg 1 if 1 alt 0  class 01/02/20  driver: snd-usb-audio
  cfg 1 if 1 alt 1  class 01/02/20  driver: snd-usb-audio
      ep 0x01 attr 0x05 maxpkt 1024 interval 1
  cfg 1 if 2 alt 0  class 01/02/20  driver: snd-usb-audio
  cfg 1 if 2 alt 1  class 01/02/20  driver: snd-usb-audio
      ep 0x82 attr 0x25 maxpkt 1024 interval 1
  cfg 1 if 3 alt 0  class ff/f0/00  driver: (none/unknown)
  cfg 1 if 4 alt 0  class fe/01/01  driver: (none/unknown)
  usbfs node /dev/bus/usb/006/004: root:root mode 0664  read=yes write=NO (before udev rule)
  mode: normal (APP, PID 0x0004)
```

**New finding, not previously documented:** the device exposes two interfaces beyond the three
UAC2 audio interfaces predicted by PROTOCOL.md/ARCHITECTURE.md, and **neither is bound to a kernel
driver**:

- **Interface 3** — class `ff/f0/00` (vendor-specific), `bNumEndpoints=0` (control-transfer only,
  confirmed via raw descriptor at sysfs `descriptors` offset `0x122`: `09 04 03 00 00 ff f0 00 04`).
  This is exactly "option C" from `docs/USB_ACCESS.md` §3, which the doc flagged as hypothetical
  ("only if the device exposes one"). **It exists.**
- **Interface 4** — class `fe/01/01` (USB-IF "Application Specific" / DFU runtime, standard DFU
  protocol), also `bNumEndpoints=0`, immediately followed in the descriptor stream by a genuine DFU
  functional descriptor (`09 21 0d f0 00 40 00 10 01`: bmAttributes=0x0d, wDetachTimeOut=240ms,
  wTransferSize=64, bcdDFUVersion=1.10). This is a real DFU interface. **Treated as off-limits for
  anything beyond the single passive read below** — see §5.

## 3. USB permissions

Before the udev rule: `root:root 0664`, `write=NO` → confirmed `EACCES` class (not yet attempted,
inferred from node mode; matches F9 in USB_ACCESS.md).

Administrator action taken (user-run, not automated):
```
sudo install -m0644 packaging/udev/70-openwitty.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=29c2
```
After reload: `/dev/bus/usb/006/004` → `crw-rw-r--+ root root`, ACL `user:<user>:rw-`,
`openwitty-cli devices` reports `write=yes`. Rule worked exactly as documented; no replug needed on
this kernel/udev combination.

## 4. API handshake (message 0x0A) — read-only GET matrix

All transfers: `GET`, `wValue=0x000A`, `wLength=0x40`, 500 ms timeout, no claim, no write, no detach.

| # | Recipient / interface | bmRequestType | bRequest | wIndex | Result |
|---|---|---|---|---|---|
| 1 | interface, AC (0), kernel-owned | 0xA1 | 0x03 | 0x3300 | **`[io-error] LIBUSB_ERROR_IO`** (not the predicted EBUSY) |
| 2 | interface, AC (0), kernel-owned | 0xA1 | 0x85 | 0x3300 | `[io-error] LIBUSB_ERROR_IO` |
| 3 | device | 0xA0 | 0x03 | 0x3300 | `[stall] LIBUSB_ERROR_PIPE` (clean rejection) |
| 4 | device | 0xA0 | 0x85 | 0x3300 | `[stall] LIBUSB_ERROR_PIPE` |
| 5 | interface, unbound (3) | 0xA1 | 0x03 | 0x3303 | **SUCCESS — 64 bytes, see below** |
| 6 | interface, unbound (3) | 0xA1 | 0x85 | 0x3303 | `[timeout] LIBUSB_ERROR_TIMEOUT` |
| 7 | interface, unbound (4, DFU) | 0xA1 | 0x03 | 0x3304 | SUCCESS — **identical 64 bytes to #5** |
| 8 | interface, unbound (4, DFU) | 0xA1 | 0x85 | 0x3304 | `[timeout] LIBUSB_ERROR_TIMEOUT` |

Raw response (#5 and #7, byte-identical):
```
0100060000020503000000303030303030303001020138000000XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX130500000000000000000000000000000000
```

Decoded by offset (conservative reading; not a claim about field semantics beyond what's visible):
- byte 0 = `0x01`, byte 1 = `0x00` — matches PROTOCOL.md's "byte0 major must be 1, byte1 minor" for
  message 0x0A exactly.
- bytes 2–10: `06 00 00 02 05 03 00 00 00` — unidentified, not previously documented (PROTOCOL.md
  only specifies bytes 0–1 for this message; bytes beyond that were always opaque/unknown, not
  previously assumed to be zero).
- bytes 11–18: ASCII `"00000000"`.
- bytes 19–25: `01 02 01 38 00 00 00` — unidentified.
- bytes 26–45: ASCII, 20 characters total (redacted above as `XXXXXXXXXXXXXXXXXXXX`). The last 12
  characters were an exact match for the device's USB `iSerial` string reported by `devices`/sysfs —
  confirmed, then redacted for this document. The leading 8 characters were unidentified (hex-looking,
  possibly a build id/date code) — also redacted out of caution since their uniqueness per unit isn't
  established either way.
- byte 46 = `0x13`, byte 47 = `0x05` — unidentified.
- bytes 48–63: all zero (padding, or genuinely unused).

**This is new evidence, not a reinterpretation of an existing documented field** — PROTOCOL.md never
described bytes 2–63 of message 0x0A because nobody had a real response before. Recorded verbatim;
no change made yet to PROTOCOL.md's field table beyond noting the evidence level (see §7).

## 5. Kernel log evidence (`sudo dmesg`, user-run)

```
usb 6-4.1: usbfs: process <pid> (openwitty-cli) did not claim interface 0 before use
usb 6-4.1: usbfs: process <pid> (openwitty-cli) did not claim interface 0 before use
usb 6-4.1: usbfs: process <pid> (openwitty-cli) did not claim interface 3 before use
usb 6-4.1: usbfs: process <pid> (openwitty-cli) did not claim interface 4 before use
```

This is an **advisory warning**, not an enforced refusal, on this kernel: it was logged for
interfaces 0, 3, *and* 4 alike, yet only interface 0 failed. The IO error on interface 0 is therefore
not explained by the claim-check path described in USB_ACCESS.md F2 (which predicted a clean EBUSY);
the most likely explanation is a real bus-level transaction failure because `snd-usb-audio` is
actively using interface 0's control pipe for mixer/stream management while our competing request
lands on the same endpoint 0. This refines, rather than confirms, F2 — **flagging as a documentation
update needed**, not silently correcting it.

No `disconnect`, `reset`, `usb_set_interface failed`, or card-teardown messages appear anywhere in
the log around these events.

## 6. Audio coexistence

`cat /proc/asound/cards` before, between, and after every experiment above: `0 [C2] USB-Audio -
CONNECT 2` present and unchanged throughout. No xruns observed, no PipeWire disconnect, no replug
needed. Read-only traffic to interfaces 0, 3, and 4 (GET only, no claim, no write) did not disrupt
audio in any observed way.

## 7. What this changes about PROTOCOL.md / USB_ACCESS.md evidence levels

- **USB_ACCESS.md option C ("unbound interface") is CONFIRMED, not hypothetical**: interface 3
  (vendor-specific, `ff/f0/00`) accepts the documented UAC2 `MEM` GET (`req=0x03`) addressed with its
  own interface number in `wIndex`, with no claim, no driver conflict, no audio disruption. This is
  the first real, non-disruptive read-only control path to the device — the core unresolved problem
  in HANDOFF_HARDWARE_TESTING.md §7/§11.
- Request variant `0x85` (UAC1-style GET) is **not** accepted by this firmware on interface 3 (clean
  timeout, not stall) — only `req=0x03` (UAC2 MEM) works, at least for message 0x0A.
- Interface 0 (AC, kernel-owned) is **not usable** for interface-recipient requests while
  `snd-usb-audio` is attached and the card is in normal use: result is `EIO`, not `EBUSY` as
  predicted. Device-recipient requests to interface 0 (`wIndex=0x3300`) are cleanly rejected
  (`EPIPE`/stall) by the firmware, not blocked by the kernel.
- Message 0x0A's response is **larger and more structured** than previously documented (PROTOCOL.md
  only specified bytes 0–1). Bytes 2–63 are real, non-zero, partially-ASCII data including the
  device's own serial number. Full field semantics are **not yet determined** — do not treat the
  byte-offset reading above as authoritative; it is a verbatim capture plus the one correlation
  (serial number substring) that could be confirmed independently against `devices` output.

## 7b. Extending to version/status/settings (same session, after user approval)

`UsbTransport` already supported an explicit `interfaceNumber` override (`UsbOptions::interfaceNumber`,
previously unexposed on the CLI); the `InterfaceRecipient` read path never claims or detaches
regardless of which interface number is used — identical in shape to what `probe` already did safely
for interface 3. Added a `--interface N` flag to `openwitty-cli read` (src/cli/main.cpp) wiring
straight into that existing field; no new USB call site (`ctest`'s `audit_usb_call_sites` still
passes), no change to transport logic. Rebuilt; full suite still 95/95.

Results, all via `--interface 3`, all read-only GET, no claim, no write:

| Message | wValue | Result |
|---|---|---|
| `api` | `0x000A` | 64 bytes — identical to the earlier `probe` capture (confirms the new flag is wired correctly) |
| `version` | `0x0103` | 64 bytes — **byte-for-byte identical to `api`**, despite a genuinely different `wValue` (verified in `Types.hpp`: `wValueFor` computes `0x000A` vs `0x0103` correctly). Unexplained; flagged, not reinterpreted. Possible explanations, none confirmed: this interface serves one fixed "device identity" block regardless of requested message for these two IDs specifically; or `iface_version_t`'s real layout coincides with this blob by construction. Needs further investigation before updating `iface_version_t`'s evidence level. |
| `status` | `0x0102` | 64 bytes, **genuinely different payload**: `009c00009c...0101010101016400640a0b000c0000006f0064...` — structured, not the api/version blob, not all-zero. Not yet mapped against PROTOCOL.md's status byte offsets (that requires the step 9 comparison below). |
| `settings` | `0x0100` | **128 bytes, decodes cleanly through the existing `SettingsMessage` codec** into plausible real values: mic gain 27.00 dB (mute on, low-cut on, Clipguard on, preamp=Vivid, denoise off, comp on), instrument gain 12.00 dB (mute on, preamp=Vivid), phantom power **on**, speaker 0.00 dB, headphone −19.71 dB (muted), dim off, mono off, mix −15.91, monitor source 2 (both), custom-button value 2. |

All four reads repeated 3–5× each: byte-identical every time (device state was not touched between
reads). `aplay -l`/`/proc/asound/cards` and `amixer -c C2 controls` checked after: card and its
(pre-existing, unrelated) mixer control set unchanged — see the separate long-standing
`amixer`/volume-control memory note for this device, not something this session touched or needed to
touch.

**This is the strongest evidence yet that interface 3 is a real, working, read-only control channel**
for at least `status` and `settings` — not just a fixed identity block. The `settings` decode in
particular produces numbers in-range for every field PROTOCOL.md documents (gain 0–72/0–60 dB,
booleans 0/1, preamp 0–2, monitor 0–2, mix within ±1), which is itself a form of validation of the
settings schema, though **not yet cross-checked against the device's actual front-panel/app state**
(test plan step 9 — needs the user to change a setting on the hardware between two reads; not done
this session, proposed as the immediate next step).

## 8. What was deliberately not done

- No write of any kind (`UsbOptions::allowWrites` stayed false the whole session; the CLI has no
  write command).
- No interface claim, no detach, no reset, no mode switch, no `--method detach`.
- No further traffic to **interface 4** beyond the single GET above (see §2 and §9) — it is a real
  DFU interface and `req=0x03`/`bmRequestType=0xA1` happens to collide with the standard DFU class
  request `DFU_GETSTATUS` (same bRequest value). The byte-identical response to interface 3 strongly
  suggests this device's firmware uses one shared EP0 handler for class/interface requests regardless
  of the declared interface number (i.e., it is very likely *not* actually running the DFU state
  machine for this GET), but this was not verified further and should not be re-tested casually.
- §7b (same session, after explicit user approval) extended this to `version`/`status`/`settings` via
  a new `--interface N` flag reusing the existing, already-safe `UsbOptions::interfaceNumber` field —
  still GET-only, still no claim/detach, confirmed via `audit_usb_call_sites` that no new USB call
  site was introduced.

## 9. Recommendation going forward

1. Treat **interface 3** (vendor-specific, unclaimed) as the primary access path. Stop probing
   interface 4 except for a single confirmatory re-run if ever needed — prefer interface 3
   exclusively, since it has no declared alternate meaning that could be misread as a firmware
   command.
2. ✔ done this session: `--interface N` added to `read`; `version`/`status`/`settings` all answer
   over interface 3, read-only, GET only, same safety gates as today, no new USB call site.
3. Update PROTOCOL.md's message 0x0A field table to mark bytes 0–1 "hardware-verified" and bytes
   2–63 "hardware-captured, unidentified" (not "opaque"/unknown-and-presumed-zero as before). **Done.**
4. The hwdep kernel-driver approach (USB_ACCESS.md option E) is no longer the only path forward now
   that option C works from pure userspace with no kernel changes — deprioritize it pending option C
   proving sufficient for all five message types. **Done** (option C now confirmed sufficient for 4 of
   5 read message types; `triggers`, id 4, is write-only and untested — correctly so, per policy).
5. **Next (not done, needs the user physically present at the device):** plan step 9 — change one
   setting on the hardware itself (touch surface) or leave it as-is and simply tap the mic / play
   audio, re-run `read settings`/`read status` via `--interface 3`, and diff the raw hex against the
   captures in §7b. This is the first real cross-check of PROTOCOL.md's byte offsets against actual
   device behaviour and doesn't require any code change or write from OpenWitty.
6. Investigate the `version`/`api` identical-payload anomaly (§7b) before treating `iface_version_t`
   as understood — it is currently an open question, not a documented field layout.

## 10. Physical control / protocol offset validation (same branch, follow-up session)

Plan step 9 executed: individual hardware controls were changed by hand on the physical unit between
`read settings --interface 3` captures, diffed byte-for-byte, and checked against
`src/protocol/Messages.hpp`'s `settings_off` table. All reads only; no USB writes, no claims, no
disruption — `/proc/asound/cards` checked after every capture, card present throughout.

Tested device identity: API handshake `major=1 minor=0`, `bcdDevice 0121` (see §4). No separate
firmware-version string could be read — see the `version`/`api` anomaly (§7b/§11) — so this is the
most specific identifier available. Serial numbers redacted throughout this document (see note at
top) per instruction; the underlying analysis does not depend on the actual serial value.

### 10.1 Baseline

```
mic:        gain=27.00dB mute=1 lowcut=1 clipguard=1 preamp=2(Vivid) denoise=0 comp=1
instrument: gain=12.00dB mute=1 lowcut=0 clipguard=0 preamp=2(Vivid) denoise=0 comp=0
phantom=1(on)  speaker=0.00dB  headphone=-19.71dB (MUTED)  dim=0 mono=0 mix=-15.91 monitor=2(both) button=2
```
Stable across 3 repeated reads before any physical interaction.

### 10.2 Test 1 — headphone volume wheel (down)

Safety: headphone output was muted at baseline and headphones/amp were disconnected/turned down
beforehand as a precaution before turning the wheel.

| Offset | Field (`settings_off`) | Before | After | Verdict |
|---|---|---|---|---|
| 27–28 | `kHeadphoneVolume` (i16 LE Q8.8, −60…0 dB) | `49 ec` = −19.71 dB | `d4 e1` = −30.17 dB | **CONFIRMED** |
| 29 | `kHeadphoneMute` | `01` | `01` (unchanged) | confirms volume and mute are independent fields; turning the wheel did not auto-unmute |
| 56 | *unmapped in PROTOCOL.md* | `03` | `04` | correlated with this edit; see §10.5 |

No other byte changed.

### 10.3 Test 2 — headphone volume wheel (restore, same control)

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 27–28 | `kHeadphoneVolume` | `d4 e1` = −30.17 dB | `5f e9` = −22.63 dB | **CONFIRMED** (repeat; physical wheel has its own step granularity, landed close to but not exactly at the original −19.71 dB, as expected for an analog control) |
| 56 | *unmapped* | `04` | `04` (unchanged) | did **not** increment again on a second real edit to the same control — rules out "increments on every settings write" as the explanation; see §10.5 |

No other byte changed.

### 10.4 Test 3 — microphone gain wheel (up one notch)

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 0–1 | `kGain[0]` (mic gain, i16 LE Q8.8, 0–72 dB) | `00 1b` = 27.00 dB | `00 1c` = 28.00 dB | **CONFIRMED** — exact +1.00 dB, matches "a tiny notch" |
| 56 | *unmapped* | `04` | `01` | changed again, to a *different* value than either prior state — see §10.5 |

No other byte changed.

### 10.5 Test 4 & 5 — channel-select only (no value edit, isolating byte 56)

Motivated by the user's own observation that the device's LED ring changes meaning based on which
channel (mic/instrument/speaker/headphone) is currently selected — i.e. the firmware already tracks a
"currently focused channel" concept independent of any value change. Two select-only presses (no wheel
turn) were captured to test whether byte 56 reflects that focus state:

| Action | Byte 56 | Everything else |
|---|---|---|
| speaker-select button only | `01` → `03` | **zero other bytes changed** — decoded settings output byte-for-byte identical otherwise |
| (several more select presses: headphone, speaker, speaker again) then mic-select only | `03` → `01` | **zero other bytes changed**, intermediate presses left no trace — only the final selection is reflected |

This is a clean isolation: selecting a channel with **no value change at all** still moves byte 56,
and moves it to the *same* value (`01`) both times mic was selected, and to `03` both times speaker
was the end state (baseline *and* this explicit test). Combined with Tests 1–3 (`04` for headphone,
stable across repeated edits to the same channel), this converges on:

| Byte 56 value | Channel |
|---|---|
| `0x01` | microphone |
| `0x03` | speaker |
| `0x04` | headphone |

**Classification: partially confirmed.** The mic/speaker/headphone mapping above is well-supported —
multiple independent, repeatable observations, including two that isolate selection from any value
edit — but instrument, mute, custom-button, and mix/monitor selection states were not observed, so
this is not a complete enum. This byte is listed in PROTOCOL.md's "fields known to the schema but with
no UI id yet" (bytes 54–56) and was previously guessed (unverified) to relate to custom-button
long-press configuration; that guess is now **contradicted** by this evidence — the correlation here
is clearly with channel selection, not with the custom-button group. PROTOCOL.md updated accordingly
(§11).

**What it is *not*, based on this evidence:** a simple "number of edits" or "settings revision"
counter — it held constant (`04`→`04`) across a second real edit to the same channel (Test 2), and
changed to a *lower* value (`04`→`01`) on a later edit (Test 3), which a monotonic counter cannot
explain. Distinguishing a genuine "last selected channel" status byte from a coincidental sync/UI
artifact would benefit from one more data point (e.g. instrument-select) in a future session.

### 10.6 Summary table

| # | Control tested | Offset(s) | Field | Result |
|---|---|---|---|---|
| 1 | Headphone volume wheel (down) | 27–28 | `kHeadphoneVolume` | **CONFIRMED** |
| 2 | Headphone volume wheel (restore) | 27–28 | `kHeadphoneVolume` | **CONFIRMED** (repeat) |
| 3 | Microphone gain wheel (+1 notch) | 0–1 | `kGain[0]` | **CONFIRMED** |
| 4 | Speaker-select (no edit) | 56 | *(new: selected-channel indicator)* | **PARTIALLY CONFIRMED** |
| 5 | Mic-select (no edit, after intervening presses) | 56 | *(new: selected-channel indicator)* | **PARTIALLY CONFIRMED** (repeat, predicted in advance and matched exactly) |

No unexpected changes: in every test, only the bytes listed above moved; every other byte in the
128-byte struct (including all LED colour bytes, custom-button byte 53, external-gain-lock, etc.)
stayed constant across all five tests. `/proc/asound/cards` showed the card present and unchanged
after every single read in this session; no writes, claims, detaches, resets, or firmware operations
were performed at any point.

## 11. Open items after this session

* Byte 56 ("selected channel" status, see §10.5) needs one more confirming data point (instrument,
  mute, or custom-button selection) before being treated as fully understood; still not given a name
  in `settings_off` pending that.
* The `version`/`api` identical-payload anomaly (§7b) remains uninvestigated, as instructed —
  deliberately not explained without further evidence.
* Instrument gain, mute toggles, Dim (not present on this unit's front panel — see FEATURES.md),
  Mono, speaker mute, Clipguard, and preamp style have not yet been physically tested.
* No write test has been proposed or executed. This session stayed entirely within plan step 9.

## 12. Phase 6B — extended physical validation (follow-up session)

Same method as §10: one control changed by hand at a time, `read settings --interface 3` before and
after, diffed byte-for-byte, checked against `settings_off`. All reads only; card present and
unchanged after every single capture; no writes, claims, detaches, resets, mode switches, or
phantom-power changes at any point.

Baseline at the start of this session (continuation of §10's end state): mic gain 28.00 dB (muted),
instrument gain 12.00 dB (muted), phantom on, speaker 0.00 dB (unmuted), headphone −22.63 dB (muted),
byte 56 = `01` (mic last selected).

### 12.1 Instrument gain (wheel turn)

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 10–11 | `kGain[1]` (instrument gain, i16 LE Q8.8, 0–60 dB) | `0c 00`-equivalent = 12.00 dB | byte11 `0c`→`17` = 23.00 dB | **CONFIRMED** — exact +11 dB |
| 56 | selected-channel | `01` (mic) | `02` | **Confirms the session's open hypothesis: instrument = `0x02`** |

No other byte changed. Restored (wheel turned back down) to 16.00 dB — direction and field confirmed
again on the partial-restore read; exact dB value not expected to match given the wheel's analog step
granularity (same behaviour observed for headphone volume in §10).

### 12.2 Safety reordering — speaker mute moved earlier

Speaker was unmuted and at the 0.00 dB ceiling (loudest point in its range) at this point, with
monitor source set to "both" (direct mic+instrument monitoring). Before testing mic/instrument mute,
speaker was muted first as a precaution against unexpected monitored output. The user separately
confirmed the physical speakers' own power switch was off and headphones were not being worn,
providing additional margin.

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 22 | `kSpeakerMute` | `00` | `01` | **CONFIRMED** |
| 56 | selected-channel | `02` (instrument) | `03` | consistent (3rd observation of speaker=`0x03`, counting the §10 baseline) |

Speaker was **deliberately left muted for the remainder of the session** rather than restored to its
original unmuted state — an intentional safety choice, not an oversight.

### 12.3 Microphone mute

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 2 | `kMute[0]` (mic mute) | `01` | `00` | **CONFIRMED** |
| 56 | selected-channel | `03` (speaker) | `01` (mic) | consistent |

Bonus corroboration (not a target offset, observational only): a `read status --interface 3` capture
taken while mic was live showed the input-1 meter (bytes 0–1, PROTOCOL.md's documented offset) move
from −100 dB (floor, while muted) to −62.27 dB (near the documented [−60,0] clamp, consistent with
ambient room noise on a live mic). Two neighbouring status bytes (3–4) also moved; their meaning is
not established and is **not** claimed here — flagged as an observation only, out of scope for this
settings-focused session.

Restored: MUTE pressed again, settings re-read, only byte 2 reverted (`00`→`01`); **reversal
verified**, all other bytes unchanged including byte 56.

### 12.4 Instrument mute

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 12 | `kMute[1]` (instrument mute) | `01` | `00` | **CONFIRMED** |
| 56 | selected-channel | `01` (mic) | `02` (instrument) | consistent (2nd observation) |

Restored: MUTE pressed again, only byte 12 reverted; **reversal verified**.

### 12.5 Speaker volume (wheel turn; safe since speaker was already muted per §12.2)

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 20–21 | `kSpeakerVolume` (i16 LE Q8.8, −60…0 dB) | `00 00` = 0.00 dB | `58 f4` = −11.66 dB | **CONFIRMED** |
| 56 | selected-channel | `02` (instrument) | `03` (speaker) | consistent |

Restored: wheel turned back up; settings re-read, offset 20–21 returned to exactly `00 00` = 0.00 dB
— an **exact** match this time (unlike the headphone-volume partial restores in §10), consistent with
0.00 dB being a hard ceiling the wheel physically stops at rather than a steppable mid-range value.
**Reversal verified exactly.**

### 12.6 Headphone mute

| Offset | Field | Before | After | Verdict |
|---|---|---|---|---|
| 29 | `kHeadphoneMute` | `01` | `00` | **CONFIRMED** |
| 56 | selected-channel | `03` (speaker) | `04` (headphone) | consistent |

Restored: MUTE pressed again, only byte 29 reverted; **reversal verified**. (Headphones were off the
user's ears throughout, per their earlier confirmation in §10.)

### 12.7 Controls with no physical access on this unit — documented, not invented

**Correction, 2026-10-16**: the line below originally read "no long-press gesture exists on any
front-panel button" — that overstated what was actually asked and answered. The question at the time
was specifically whether long-pressing the **mic/instrument select buttons** cycles preamp style, and
whether any button/combo toggles **Mono or Clipguard**; the owner answered "no" to those specific
questions. It did not rule out a hold gesture on a *different* button. It has since turned out the
**MUTE button does have a documented hold gesture ("Panic Mute")** — this was already recorded in
`docs/OFFICIAL_GUIDE.md` from earlier static analysis (phase 5-era), predating and independent of this
correction, and has now been independently re-verified against the manufacturer's online guide (see
`docs/OFFICIAL_GUIDE.md`). It was simply never in scope for the Phase 6B question above, not
contradicted by it. Not yet tested against our physical unit — see the new experiment below.

The owner confirmed (2026-10-14/15): no long-press gesture that **cycles a value** (preamp style,
Mono, Clipguard) exists on the mic/instrument select buttons, and the following remain CONTROL CENTER
(software)/overlay-only on this CONNECT 2 unit, with no hardware path to test them:

* **Mono/stereo toggle** (`kMono`, offset 33)
* **Clipguard** (offsets 4/14)
* **Preamp style selection** (Clean/Warm/Vivid, offsets 6/15)

Per the session's instruction, these are recorded as a hardware-access limitation rather than tested
via an invented interaction. (Dim, offset 32, was already established in §10 as having no dedicated
front-panel button on this unit either.)

### 12.8 Byte 56 — full channel-selection enum now confirmed

Combining §10 and this session: `0x01`=microphone, `0x02`=instrument, `0x03`=speaker, `0x04`=headphone
— each observed more than once, always via a clean select-only action (or as an incidental side
effect of a MUTE/volume action on that channel) with no other byte moving unless the actual control
being exercised changed too. This is now a **confirmed mapping** for these four values (not merely a
hypothesis) — see PROTOCOL.md. Not necessarily complete: the mute, custom, mix/monitor and autogain
buttons are action-trigger buttons rather than pure channel selectors and were not tested against
byte 56, since doing so would have side effects (toggling real settings, or in autogain's case
triggering a real ~10 s measurement cycle) beyond simple selection.

### 12.9 Summary table

| # | Control tested | Offset(s) | Field | Result |
|---|---|---|---|---|
| 1 | Instrument gain wheel | 10–11 | `kGain[1]` | **CONFIRMED** |
| 2 | Speaker mute | 22 | `kSpeakerMute` | **CONFIRMED** (left muted intentionally, not reversed) |
| 3 | Microphone mute | 2 | `kMute[0]` | **CONFIRMED** + reversal verified |
| 4 | Instrument mute | 12 | `kMute[1]` | **CONFIRMED** + reversal verified |
| 5 | Speaker volume wheel | 20–21 | `kSpeakerVolume` | **CONFIRMED** + exact reversal verified |
| 6 | Headphone mute | 29 | `kHeadphoneMute` | **CONFIRMED** + reversal verified |
| 7 | Mono/stereo toggle | 33 | `kMono` | **not accessible on this hardware** — documented, not tested |
| 8 | Clipguard | 4/14 | — | **not accessible on this hardware** — documented, not tested |
| 9 | Preamp style | 6/15 | — | **not accessible on this hardware** — documented, not tested |
| 10 | Additional selected-channel values | 56 | selected-channel indicator | **instrument (`0x02`) confirmed**, completing the 4-value enum; mute/custom/mix/autogain states intentionally untested (side effects) |

No unexpected changes in any test: in every case only the bytes listed above (plus byte 56 when a
different channel was selected as part of the same action) moved; every other byte in the 128-byte
struct stayed constant throughout. Card present and unchanged after every read. No USB writes, driver
detachment, resets, firmware operations, factory reset, streaming-mode changes, or phantom-power
changes were performed at any point in this session.

## 13. Phase 6C — first real hardware write (LED brightness)

First USB write of any kind ever sent to a real CONNECT 2 by this project, after a separate explicit
authorization for exactly this operation (full proposal reviewed and approved by the owner before
execution). Implementation: `openwitty-cli write led-brightness <0-100> [--execute]` — a single,
narrowly-scoped command (settings byte 37 / `kLedBrightness` only; see the code comment above
`cmdWriteLedBrightness` in `src/cli/main.cpp` for the full safety rationale), gated by
`--execute` **and** `LEWITT_ALLOW_HARDWARE_WRITE=1` for real hardware, built entirely on
`DeviceSession`'s existing, already-tested `edit()`/`commit()` path and `WriteGuard`'s allowlist — no
new USB call site, no generic field/byte setter, no trigger write.

### 13.1 Request sent

```
bmRequestType = 0x21   (host->device, class, interface recipient)
bRequest      = 0x03   (UAC2 MEM SET)
wValue        = 0x0100 (apiMajor=1, messageId=0 / Settings)
wIndex        = 0x3303 (unit 0x33, interface 3)
wLength       = 0x80   (128 bytes, full settings struct, only byte 37 different from the
                         immediately-preceding read)
timeout       = 500 ms
```

### 13.2 Write 1: 100 -> 60

| | |
|---|---|
| USB transfer result | success, no error (exit 0, no stall/timeout/IO-error) |
| Byte 37 | `0x64` (100) -> `0x3c` (60), confirmed as the **only** byte that differed in the proposed payload |
| Command's own immediate post-write read-back | **still showed `0x64` (100)** — see §13.4 |
| Separate `read settings --interface 3` moments later (new process) | `0x3c` (60) — matches the intended write exactly, all other 127 bytes unchanged |
| Visual confirmation | inconclusive — owner was not watching the device at the moment of the switch |

### 13.3 Write 2: 60 -> 100 (rollback, same guarded mechanism, same authorization)

| | |
|---|---|
| USB transfer result | success, no error |
| Byte 37 | `0x3c` (60) -> `0x64` (100), only byte that differed |
| Command's own immediate post-write read-back | again showed the pre-write value (`0x3c`) — same pattern as §13.2 |
| Separate `read settings --interface 3` moments later | `0x64` (100) — exact match to the original pre-write-1 value; **rollback confirmed exactly** |
| Visual confirmation | **owner directly observed the LED getting brighter at that moment** — real, witnessed physical effect |

### 13.4 Finding: a GET immediately after a SET does not reflect the new value

Both writes showed the identical pattern: the write command's own internal re-read (performed by
`DeviceSession::commit()` -> `refreshSettings()`, microseconds after the SET control transfer
completes, same process) still returned the **pre-write** value, while a **separate** CLI invocation
(new process: libusb init, device open, handshake, settings read — naturally tens to a few hundred ms
of overhead) performed shortly after **did** show the new value.

This is a genuine protocol-level finding, not a write failure: the device appears to need a short
settling/propagation delay after accepting a SET before a subsequent GET reflects it. Practical
consequence for anyone building on this: **a blind immediate re-read after a settings write cannot be
trusted** to report success or failure; either add a short delay before the confirmation read, or
poll (bounded, not unbounded) until the expected byte matches or a timeout is reached. This CLI
command's own built-in read-back is therefore unreliable as currently written — flagging as a known
limitation (see §14), not fixed in this session since doing so wasn't part of the authorized scope.

### 13.5 Safety / coexistence

Card present and unchanged (`/proc/asound/cards`) before, between, and after every step. No stalls,
timeouts, unexpected byte changes, or anything resembling a disconnect/reset at any point. No retry
was performed on the ambiguous immediate-read-back result — per instruction, exactly one additional
**read** (not a write) was used to resolve the ambiguity, and it resolved cleanly.

### 13.6 Persistence across power cycles

**Not tested.** The brightness was restored to its original value before the end of the session, so
whether a SET to this field persists across a power cycle remains unverified either way.

## 14. Known limitation introduced this session

* `openwitty-cli write led-brightness`'s own printed "post-write read-back" line is unreliable (see
  §13.4) — it reflects the pre-write value due to the device's settling delay, not a confirmed
  post-write state. The *separate* read performed immediately afterward in this session is what
  actually confirmed each write. A future session should fix the command to delay or poll before
  reporting its own read-back, rather than relying on a manual follow-up read as was done here.

## 15. Phase 6D — timing experiment, then a software-only session (no further hardware writes)

### 15.1 Timing experiment (hardware; 2 writes, same approved shape as Phase 6C)

Goal: pin down the settling delay found in §13.4 more precisely. Proposed and explicitly approved
before execution: one write to a probe value, a bounded sequence of timed read-only observations, one
write back to the original value.

```
write led-brightness 50 --interface 3 --execute    (bm=0x21 req=0x03 wValue=0x0100 wIndex=0x3303)
  -> success, no error
[separate-process read, ~1ms after the write process exited]
  -> settings.byte37 = 0x32 (50) - ALREADY converged
[separate-process read, ~53ms after]
  -> settings.byte37 = 0x32 (50) - still converged; 2 consecutive matches, stopped early per plan
write led-brightness 100 --interface 3 --execute   (restore)
  -> success; confirmatory read: settings.byte37 = 0x64 (100), exact match to pre-experiment baseline
```

The script's `sleep` calls between the planned longer intervals (100/250/500/1000ms) failed locally
(`bc` not installed on this machine) and were skipped; this did not require any additional hardware
writes since the 2 writes already covered the approved shape, and the finding is unambiguous from the
data actually collected: **convergence happened within ~1ms**, far faster than §13.4's "tens to
hundreds of ms" characterization, which is hereby corrected. `status` was also read at each point for
comparison but is not directly informative for this field (LED brightness lives only in `settings`).
Card present and unchanged throughout; no disruption.

**Revised understanding**: the earlier same-process staleness (§13.4) is best explained by the
near-zero gap between `UsbTransport::write()` returning and `DeviceSession::refreshSettings()`'s
following `read()` within `commit()` — not by a genuinely long device-side settling time. Any
reasonable inter-process gap (even ~1ms) was already enough to observe the converged value.

### 15.2 Remainder of Phase 6D: software/architecture only, no hardware interaction

Everything after §15.1 was mock-tested and documentation work; **no further USB traffic of any kind**
was sent to the real device for the rest of this session. Specifically:

* `core/WriteVerification.hpp/.cpp` (new): a `WriteState` enum (`Prepared` → `Submitted` →
  `TransferCompleted` → `AwaitingConvergence` → `Verified` / `VerificationTimedOut` / `Ambiguous` /
  `Failed`) and `DeviceSession::commitAndVerify()`, which performs a fresh pre-write re-read (reusing
  the existing device-wins merge algorithm, satisfying "obtain a fresh snapshot before writing"),
  calls the existing `commit()` unchanged, and — only if the immediate readback doesn't yet match —
  polls (read-only, bounded, default ~570ms total budget informed by §15.1's measurement, never
  retrying the SET) until it converges, times out, or an out-of-band byte change makes the outcome
  `Ambiguous`. 11 new mock-backed tests (`tests/cpp/test_write_verification.cpp`) exercise every
  state transition deterministically (no wall-clock sleeps) using two small, additive `MockTransport`
  hooks: count-based stale-reads-after-write simulation, and a one-shot external-change injector.
* `src/gui`: `DeviceWorker::setValue`/`retryPending` now go through `commitAndVerify()` instead of
  `commit()`, and `publish()` exposes `write.state`, `write.verifying`, and a composite `ui.status`
  label (`Disconnected` / `Connecting` / `Hardware error` / `Mock mode` / `Connected read-only` /
  `Connected with authorized controls` / `Pending verification`). GUI write policy is **unchanged** —
  `allowHardwareWrites` still defaults off for real hardware, so "Connected with authorized controls"
  remains unreachable there by construction; only mock mode can show it. `Pending verification` is
  tracked accurately but not currently externally observable mid-flight, because `commitAndVerify()`
  runs synchronously on the worker thread — see the code comment above `DeviceWorker::retryPending`
  for why an earlier attempt at a mid-flight publish was reverted (it broke
  `GuiBackend.DspRestrictionAt96kHz` by reordering `stateChanged` ahead of `errorOccurred`; root-caused
  via a minimal standalone repro before fixing, not guessed at).
* PROTOCOL.md: a full write-policy table for all 8 controls from the task's "progressive control"
  list — offsets, encoding, risk, required state, rollback, and current evidence status for each.
  Only LED brightness is active; everything else is specification only, not wired to any command.
* Full suite: 106/106 (95 original + 11 new), confirmed stable across 4 repeated full runs and 20
  repeated runs of the specific test that briefly regressed during development.

No phantom power, firmware, factory reset, streaming-mode, or arbitrary-trigger code was touched or
enabled anywhere in this session.

## 16. Panic Mute (MUTE button hold) — hardware-confirmed, 2026-10-16

### 16.1 Background and a documentation correction

The owner pointed out LEWITT's official online guide
(https://www.lewitt-audio.com/de/support/online-guides/connect2) documents a MUTE-button hold
gesture ("Panic Mute"). Independently re-fetched and verified (quotes in `docs/OFFICIAL_GUIDE.md`):
short press toggles the selected channel; holding MUTE mutes all input and output channels
simultaneously; recovery requires unmuting each channel individually (not a second hold); a muted
channel stays dimly lit in its configured mute colour when a different channel is selected.

This was already recorded in `docs/OFFICIAL_GUIDE.md` from phase-5-era static analysis, predating
this project's hardware sessions entirely. What needed correcting was a Phase 6B line in this
document (§12.7) that over-generalized a narrower answer ("no long-press gesture exists on any
front-panel button") from a question that only asked about preamp/Mono/Clipguard cycling on the
*select* buttons — not about MUTE. §12.7 above has been corrected in place to scope that claim
properly; see it for the full correction note. No prior hardware finding was removed.

### 16.2 Read-only hardware experiment (3 trials, no USB writes)

All reads via `read settings|status --interface 3`. Card present and unchanged throughout.

**Trial 1 (inconclusive by design of the starting state).** Baseline: mic, instrument, speaker,
headphone all already muted (byte56=`04`, headphone selected). Owner held MUTE; reported the green
volume LED flashing after about a second. `settings` read back **byte-for-byte identical** to
baseline — uninformative on its own, since if Panic Mute writes `1` to already-`1` bytes there is
nothing to see. `status` changed at offsets 3–4 and 12–13 (`00 9c` → `e4 9f` each) — not in
PROTOCOL.md's documented meter map; flagged as an unidentified observation, not interpreted.

**Trial 2 (clean, single-channel).** Instrument unmuted individually (byte 12 confirmed `0`, byte56
→ `02`, everything else unchanged) as part of the documented individual-unmute recovery procedure.
Owner held MUTE again ("panic mode triggered"). Diff against the just-confirmed unmuted state:

```
offset 12: 00 -> 01
```

Only byte 12 changed. `status` changed at the *identical* offsets/values as Trial 1 (3–4, 12–13,
same `e4 9f`) — reproducible across two independent activations, which rules out ordinary metering
noise as the explanation (independent noise would not land on the same bytes twice). Still not
interpreted, but now a credible candidate for a real (if unidentified) Panic-Mute-related status
signal.

Comparing the full state after Trial 2 to the very first pre-experiment baseline (before Trial 1):
only byte 56 differs (`04`→`02`, headphone→instrument selection — a UI-focus artifact of running the
test, not a real setting). Every mute/gain/volume byte already matched exactly — Panic Mute's own
re-muting of instrument had, as a side effect, restored the original state. No further recovery
action was needed or taken (see §16.3).

**Trial 3 (clean, all four channels, resolves the selection confound).** Owner unmuted all four
channels individually first ("unmuted all 4 first") — confirmed via read: mic/instrument/speaker/
headphone all `0`, speaker at 0.00 dB (ceiling), headphone at −22.63 dB, monitor source "both".
Safety re-confirmed (speaker's own power switch off, per the owner's standing setup from Phase 6A).
Owner held MUTE ("hold mute and triggered panic"). Diff:

```
offset 2:  00 -> 01   (kMute[0], microphone)
offset 12: 00 -> 01   (kMute[1], instrument)
offset 22: 00 -> 01   (kSpeakerMute)
offset 29: 00 -> 01   (kHeadphoneMute)
```

**Exactly these four bytes changed, simultaneously, and nothing else** — not byte 19 (loopback
mute), not byte 56 (channel-select, unchanged across all three trials), and no new/separate flag
appeared anywhere in the 128-byte struct. Because the channel selected at the time (byte56
unaffected, so whichever channel was selected) was not the only one muted, this directly rules out
"mutes only the selected channel" — Panic Mute mutes all four regardless of selection, matching the
manufacturer's documentation exactly. `status` again showed the identical offsets-3-4/12-13 shift —
third independent reproduction.

Post-Trial-3 state exactly matched the original pre-experiment baseline again (all four muted, same
gain/volume values; only byte 56 differed, for the same UI-focus reason as before).

### 16.3 Recovery

Per the manufacturer's documented procedure (individual unmute, not a second hold — **not tested**,
deliberately, per instruction not to assume a hold-to-undo mechanism exists): after Trial 2, the
state already matched the original baseline, so no unmuting was performed — doing so would have
*introduced* a change rather than recovered one. The same was true after Trial 3. Both times, the
natural end state of the experiment coincided with the owner's own starting state; "preserve
intentionally muted channels" was satisfied by not touching anything further, not by additional
unmute actions.

### 16.4 Answers to the investigation questions

| Question | Answer |
|---|---|
| Exactly which bytes change? | **2, 12, 22, 29** — all four primary mute fields, confirmed via Trial 3 (clean) and consistent with Trial 2 (single-channel) |
| Is loopback (19) affected? | **No** — confirmed unchanged in every trial |
| Does byte 56 (channel-select) change? | **No** — confirmed unchanged in every trial, regardless of which channel was selected |
| Does a separate/dedicated Panic Mute flag exist? | **No evidence of one** in the 128-byte settings struct — the mechanism reuses the standard per-channel mute bytes exactly; nothing else in `settings` moved across any trial |
| How does recovery behave? | Consistent with the manufacturer's documentation: not independently re-tested (would require deliberately unmuting live channels with no new information to gain), but nothing in any trial suggested a second hold undoes it, and no code here assumes that |

An unidentified but **reproducible** `status` signal (offsets 3–4, 12–13) accompanied Panic Mute in
all three trials with identical values each time — flagged for future investigation, not interpreted.

### 16.5 Software (mock/GUI only — no new hardware writes, no software trigger implemented)

* `MockTransport::simulatePanicMute()` (new): sets exactly bytes 2/12/22/29, mirroring the confirmed
  hardware mechanism exactly — not a new invented flag.
* `DeviceWorker`/`DeviceController::simulatePanicMute()` (new, mock-only, no-op on real hardware):
  lets GUI tests simulate the hardware gesture, picked up through the **existing** poll/merge path
  (`refreshSettings()` → `mergeExternalChanges`) like any other device-side change — no new sync
  logic was needed.
  `values()["mute.allFour"]` (new): purely derived read-only display state (`true` iff all four
  mute fields happen to be set, by Panic Mute or otherwise) — not a protocol concept, matches the
  evidence that no dedicated flag exists. No software Panic Mute *trigger* was implemented, per
  instruction.
* 2 new core tests (`Session.PanicMuteSetsExactlyTheFourPrimaryMuteFields`,
  `Session.PanicMutePreservesUnrelatedPendingEditViaExistingMerge`) and 1 new GUI test
  (`GuiSync.PanicMuteUpdatesAllFourMuteFieldsAndPreservesPendingEdit`) — all confirm the exact byte
  set, confirm loopback is untouched, and confirm an unrelated pending edit survives via the
  existing merge algorithm. Full suite: 108/108 (106 + 2 core; the GUI test is counted within the
  existing `gui_integration` entry), stable across 3 repeated full runs.

## 17. Phase 7 integration — recorded prior-session real-hardware GUI validation, 2026-10-15

The observations in this section were recorded during the preceding validation session; they are
preserved here as that session's evidence and are not claims of a later independent retest.

Branch `phase7-hardware-integration` (merge of `hardware-validation-2026-10-14` @ `868331c` into
`phase7a-interface-discovery` @ `be0d3f3`, which itself sits on `phase6e-work-preparation` @
`7d8a4c0`). Full history/branch verification and merge conflict resolution recorded in the merge
commit message; not repeated here. Build: 54/54 targets. Full suite: 137/137, stable across 3
repeated runs (exceeds the expected 135 Phase-7A baseline + 3 Panic Mute tests).

### 17.1 Pre-flight (read-only, sysfs/ALSA only, no USB I/O)

Device present (`29c2:0004`, normal mode), interface 3 still class `ff/f0/00` and unbound
(`driver: none/unknown`), ALSA still shows card `C2`, usbfs node already has the `uaccess` ACL
from the earlier session (`write=yes`, no fresh udev step needed).

### 17.2 CLI auto-discovery validation (read-only)

```
openwitty-cli read api --interface auto       -> api major=1 minor=0, same payload as every prior session
openwitty-cli read status --interface auto    -> succeeds
openwitty-cli read settings --interface auto  -> succeeds, decodes to plausible values
```

`--interface auto` vs explicit `--interface 3`: **byte-for-byte identical** `read settings`
output — confirms descriptor-driven auto-discovery (`InterfaceSelection`, Phase 7A) selects
interface 3 on this real device, not interface 0. Card present and unchanged throughout.

### 17.3 First real-hardware GUI launch (read-only, no `--mock`)

`QT_QPA_PLATFORM=offscreen openwitty --screenshot …` (no `--mock` → `connectUsb()`). Launched
clean, no crash, exited cleanly (exit 0), no lingering process. Device settings read immediately
before and after the GUI session are byte-for-byte identical — the GUI session performed reads
only.

**Every visible value matched the independently-captured CLI read exactly**: mic gain 28 dB,
instrument gain 16 dB, both preamps "Vivid", mic Comp/low-cut/Clipguard/48V all correctly
highlighted on (instrument's low-cut/Clipguard correctly off), mic/instrument/headphone/loopback
MUTE shown red (muted), speaker MUTE correctly *not* highlighted (unmuted), direct-monitoring
icons for both mic and instrument lit (monitor source "both"). This is the first end-to-end
confirmation that `UsbTransport` → `DeviceSession` → `DeviceController` → QML bindings agree with
the CLI's independently-verified decode, on real hardware, with zero writes.

Banner and status text: "REAL DEVICE, READ-ONLY — CONNECT 2 · USB 29c2:0004 · API 1.0…" and
"Connected · real device · read-only" — the Phase 7A fix replacing the old, more alarming "Hardware
writes are DISABLED: the control protocol is not verified" framing, which read as an error. Sample
rate correctly shows "Unknown" (previously a hard-coded fake 48 kHz default) — confirms that Phase
7A fix too.

**UI bug found**: the top banner's trailing text ("…changing settings from this window is in[ert]")
is visually truncated/overlapped by the "Mock device"/"USB (read-only)" buttons in the top-right at
this window size. Cosmetic only — no functional or safety impact, nothing was hidden that mattered
(the read-only state itself is also shown unambiguously in the status line below). Noted for Phase
7B.

### 17.4 Known redundancy carried into this branch (not a bug)

`values()["ui.status"]` (added with the write-verification work) and `values()["ui.state"]`
(Phase 7A) are two independently-computed, overlapping status labels that both survived the merge
working correctly but duplicating each other's purpose. `Main.qml`'s banner uses `ui.state`;
`ui.status` has no QML consumer at all currently. Left for Phase 7B consolidation, not touched
here.

### 17.6 Live manual-interaction validation (read-only; relaunch-and-screenshot methodology)

Each step: physical action on the device, confirm via a fresh `read settings --interface 3`, then
relaunch the real-mode GUI (`--screenshot`, fresh `connectUsb()`) and compare. No SET was sent at
any point; Panic Mute was not triggered.

| Action | Device (CLI-confirmed) | GUI response |
|---|---|---|
| Select a different channel (speaker -> mic) | byte 56: `03` -> `01` | **No visible change whatsoever** (screenshots pixel-identical) — confirms, empirically, the gap already found in code: byte 56 has zero bindings in `SettingsMessage`, `DeviceController::publish()`, or any `.qml` file |
| Mic gain turned up | 28 dB -> 45 dB | Knob redrew to **45**, angle and level bar updated — exact match |
| Instrument unmuted (no input attached, confirmed safe by the owner; speaker was unmuted at 0 dB at the time) | `mute` `1` -> `0` | MUTE button correctly un-highlighted, meter began showing activity — exact match |

Every binding that exists in the GUI today works correctly and matches the real device exactly in
all three cases. The one gap (channel-selection display) was identified by reading the code first,
then confirmed empirically — not assumed, not fabricated. End state left as-is at the owner's
request (mic 45 dB muted, instrument unmuted no-input, speaker unmuted 0 dB, headphone muted); the
owner will re-mute manually rather than have this session restore it.

Device disconnect/reconnect handling was not tested this session (not requested).

### 17.7 Phase 7B implementation status at that time — mock/replay only

The following changes were implemented after the observations above, but were **not** exercised on
the physical device in this session: byte 56 now has a typed selected-channel decoder and a
read-only blue strip outline; the existing derived `mute.allFour` value now shows an informational
“ALL MAIN CHANNELS MUTED” banner; and USB settings polling is 2 Hz while status polling remains
4 Hz. `ui.state` is now the sole connection-state source, replacing redundant `ui.status` with
non-state `ui.detail`; write verification remains in `write.*`. A deterministic, write-refusing
`ReplayTransport` tests selected-channel and Panic-Mute-shaped settings transitions without USB or
identifying data. Release validation after this work: 140/140 CTests; these are automated/mock/replay
results only, and a separate read-only hardware session is required before claiming live GUI
confirmation. That separate session is recorded in §18 below.

## 18. Phase 7C — live read-only GUI validation, 2026-10-17

This is a new hardware session, distinct from the prior-session record in §17. The starting tree
was clean on `phase7-hardware-integration` at `087dcd1`. The CONNECT 2 (`29c2:0004`) was present
in normal mode, interface 3 was vendor-specific (`ff/f0/00`) and unbound, and the current user had
usbfs access. The ALSA `snd-usb-audio` card remained present. No kernel driver was detached, no DFU
interface was opened, and no USB SET was sent. All physical changes below were made by the owner
on the device; OpenWitty only read them.

Automatic-interface CLI API, status, and settings GETs succeeded. Automatic and explicit
interface-3 settings reads matched before launching the GUI. The real GUI connected without
`--mock`, displayed the read-only state and `Unknown` sample rate, and initially matched the CLI's
decoded gains, volumes, and mutes. The same running GUI session followed physical selection of
microphone, instrument, and speaker (independently captured) and headphone (owner-observed): the
blue strip border moved to the selected channel. No selection command was sent by software.

The owner turned the physical microphone gain up; the GUI followed almost immediately. A later
CLI read showed 55 dB, versus 45 dB in the session's initial snapshot. The owner physically
toggled microphone mute and restored it; both GUI transitions had no noticeable delay. A small
physical headphone-volume change moved the GUI slider promptly; the device has dots rather than
a numeric readout, so this was a visual comparison, not a numeric device/display equality claim.
CLI values changed from −22.63 dB initially to −20.57 dB later. No GUI control was operated to
make these changes.

With the owner's physical Panic Mute hold, all four main-channel mute indicators changed and the
derived “ALL MAIN CHANNELS MUTED” notice appeared. The blue selected-channel border remained
correct. Loopback was already muted and has no physical unmute button on this unit, so **this
session did not test loopback independence from an initially unmuted state**; §16's earlier
byte-level hardware trials remain the evidence for that behavior. The owner reported that the
notice overlapped the blue connection text at this window size; this prompted a bounded-banner
layout correction and a QML geometry regression assertion. In the reopened real GUI, the owner
confirmed that the notice and connection text were separate and readable. No channel was
auto-unmuted.

### 18.1 Mix-wheel display defect found during live validation

The owner selected the physical input/playback mix and saw the GUI mix knob appear to have only two
positions; the headphone slider also appeared to rise during an earlier uncontrolled adjustment.
With the GUI closed to avoid simultaneous USB clients, a controlled physical mix-wheel step changed
**only settings bytes 35–36**: decoded mix −32.29 → −18.57, while headphone volume stayed exactly
−20.57 dB. Reads at the owner's physical low and high stops yielded mix −100.00 and 0.00,
respectively, again with headphone volume unchanged. These are observed positions on this unit,
not a claim about every firmware or a newly authorized SET range.

The defect was the GUI's `−1…+1` mix-knob drawing range, which clamped most real values to an
endpoint. The real read-only display now uses the observed `−100…0` span, with 100:0 and 0:100
endpoint labels; mock mode retains its old `−1…+1` behavior and hardware writes remain disabled.
In the reopened real GUI, the owner confirmed that the knob reached the correct end and moved
through intermediate positions while the headphone slider stayed fixed. The owner estimated mix
updates at roughly one-second intervals; this is a visual estimate, not a timed latency
measurement, and polling was not increased.

### 18.2 USB cadence, resource use, and limitations

A local `libusb_control_transfer` counter (logging request metadata only, not payloads or device
identifiers) observed 357 status GETs over 89.03 s (**3.999/s**) and 179 settings GETs over 89.04 s
(**1.999/s**) in the unchanged GUI. It recorded one API GET, four descriptor GETs at startup,
**zero SETs, and zero failed transfers**. The corrected GUI's subsequent traced run likewise
showed no SETs or transport errors. The first desktop GUI process stayed around 176 MiB RSS and
below 1% CPU after startup; a separately instrumented run was around 173 MiB RSS after startup.
The GUI remained responsive and ALSA stayed attached.

Two CLI settings GETs attempted *concurrently* with an open GUI returned `LIBUSB_ERROR_IO` while
the GUI remained connected; CLI GETs after closing the GUI succeeded. This suggests a concurrent
client/access limitation but does not establish its precise cause. Avoid competing CLI reads
during a live GUI session until investigated. Unknown byte-56 selection values were not induced
on hardware because doing so would require an unsupported action; mock/replay tests cover them.

### 18.3 Owner-approved physical disconnect/reconnect

After explicit approval, the owner unplugged the CONNECT 2 while the real GUI continued running.
The USB device and ALSA card disappeared, the next status GET returned `LIBUSB_ERROR_NO_DEVICE`,
and the GUI displayed “No LEWITT device connected” over the cached controls. The owner also
reported that all channels still appeared muted while disconnected; it was not established
whether this referred to the four underlying indicators or the derived notice. To avoid a stale
notice regardless, the QML notice is now gated on a live connection, and a mock unplug regression
test asserts it disappears; the corrected binary was **not** re-tested by physically unplugging
again. No software reset or detach was performed.

At the owner's reconnect, the same running GUI automatically performed fresh descriptor/API,
settings, and status GETs and resumed polling. USB and ALSA enumeration returned; PipeWire again
listed a CONNECT 2 device, sink, and source. Post-reconnect CLI API/status/settings GETs succeeded,
and automatic and explicit interface-3 settings reads matched exactly. The trace had no USB SETs.
The owner observed that the GUI reported connected almost immediately while the device's lights
flashed for a few seconds before its physical controls became responsive. Thus the banner denotes
control-transport readiness, **not proven audio or front-panel readiness**. Audio nodes returned,
but audible playback/capture was not tested (PipeWire sink/source were muted during inspection).
Current hardware mute/volume/mix settings were not restored or modified by software.

### 18.4 Final validation of the corrections

The Release build completed. The full CTest suite passed **140/140** (including the new QML
mix-scale test and the extended Panic Mute/layout/disconnect test inside `gui_integration`). The
USB call-site audit passed: the GUI still uses `readOnlyHardwareOptions()`, cannot enable hardware
writes, chooses its interface automatically, and rejects DFU. A final offscreen launch of the
corrected real GUI exited cleanly; its trace contained 9 vendor GETs, zero SETs, and zero errors.
The screenshot showed a real read-only connection, `Unknown` sample rate, and current state
consistent with a subsequent CLI read. The post-reconnect stale-notice correction itself remains
mock-tested, not physically unplugged a second time.

### 19. Phase 7D source investigation and read-only stage measurement (2026-10-17)

The Phase 7C record is the hardware evidence: GUI-only reads worked; CLI-only reads worked;
two CLI settings GETs while the GUI held the device returned `LIBUSB_ERROR_IO`. Phase 7D has
**not** repeated that multi-client experiment; owner approval was requested before any new
physical concurrent GET. Source inspection found separate libusb contexts/handles in each process
and in-process-only `SerializedTransport` serialization. On this running system, passive sysfs
inspection showed usbfs bound to vendor interface 3 while the older GUI was open. Linux usbfs can
implicitly claim an interface-recipient control target; this is a plausible contribution to
contention, **not** proof that the firmware supports only one client or an explanation of EIO's
exact origin. No driver was detached and no hardware write occurred in this investigation.

Phase 7D adds a private per-user, nonblocking `flock` lease before normal OpenWitty control
sessions and executed CLI probes. It is a cooperative safeguard among new OpenWitty processes,
not a USB permission rule or protection against unrelated clients/older OpenWitty binaries.
Mock/process tests cover exclusivity and automatic release after owner exit. The older GUI found
at the start of the phase did not acquire this new lock; with owner approval it was closed and
replaced by the new read-only traced build. The GUI's banner now says **control link** and explicitly does not
assert audio or front-panel readiness; no reliable front-panel-ready bit has been identified in
the decoded API/status/settings data. This wording is code-tested, not yet visually rechecked on
the physical device.

The owner later noticed that the corrected mix knob's maximum was mislabeled `50:50` and judged
the physical high stop to correspond to playback-only. The endpoint label is corrected to
`0:100` in read-only display and mock QML tests; the low/high raw reads remain −100.00/0.00.
The acoustic ratio and intermediate mixing law were **not** measured, so the label reflects the
owner's interpretation, not a calibrated firmware percentage. No setter range was changed.

An opt-in `OPENWITTY_TRACE_LATENCY=1` trace now records monotonic poll spacing, status/settings
GET duration, decode/merge time, worker-to-GUI delivery, QML-event observation, and frame swap.
It logs no payloads or identifiers. None of these timestamps alone marks the instant of a
physical wheel/button action or photon emission; physical-to-visible latency still needs an
owner-coordinated external action timestamp. The conservative 4 Hz status / 2 Hz settings cadence
was kept.

In that new single-client GUI session, the owner physically selected speaker then headphone,
changed mic gain, selected and muted speaker, and adjusted monitoring mix. The trace classified
the expected selected-channel, gain, mute, and mix settings changes; the owner reported that the
mic gain and speaker mute displays followed. Eight successive mix changes appeared at the
half-second settings cadence, without a classified headphone-volume change. The owner confirmed
the headphone slider stayed fixed and the GUI showed `50:50` at the physical halfway mix position;
motion looked smoother than Phase 7C but not continuously smooth. These are new Phase 7D observations;
no CLI GET was run concurrently and no software setting was changed.

Representative monotonic trace ranges from those changed polls: poll spacing approximately
249.8–250.8 ms after startup; status GET 2.6–2.9 ms; settings GET 3.98–4.02 ms; worker-to-GUI
delivery 61–73 µs; GUI-to-QML callback 0.30–1.42 ms; GUI publication to next frame swap
0.86–2.29 ms. Decode/merge were below the trace's 1 µs display resolution in these samples.
These are **backend and display-pipeline stages**, not action-to-pixel latency. Physical action
could occur anywhere between 500 ms settings reads, and firmware/front-panel behavior may add
delay. The running GUI process showed approximately 0.1% cumulative CPU and 174–176 MiB RSS
after startup. `snd-usb-audio` remained bound to the audio interface. No transport error was
reported during these observed transitions.

Source review also found that a good 4 Hz meter/status read could reset the generic failure
counter between bad 2 Hz settings reads. A separate consecutive-settings failure counter now
declares an error/reconnect after three unresponsive settings refreshes (timeout/I/O/short or
malformed) even if status keeps working; stalls, busy and permission errors still do not escalate;
old status frames are cleared at reconnect. This is covered by mock tests, **not** a deliberately
induced hardware failure. After the owner's later computer restart, a new usbfs node and Wayland
session were observed; the GUI from the timing run was no longer running. The new read-only GUI
was launched again. An owner-provided screenshot confirmed the “CONTROL LINK, READ-ONLY” banner,
the explicit audio/front-panel readiness caveat, and the `Unknown` sample rate. It also exposed
an overlap between the longer header connection detail and the sample-rate label. The header
detail was shortened to “read-only”; the full readiness caveat remains in the banner, and a QML
geometry regression assertion covers the header slot. This correction is code-tested but not yet
reconfirmed in another real-device screenshot. No unplug/replug experiment was
performed in Phase 7D.

The final Release build passed **149/149 CTests**, including the GUI integration suite, lease
lifecycle/cross-process tests, timing tests, and the interleaved-settings-failure regression.
The ASan/UBSan configuration passed **148/148** (its configuration omits packaging staging).
The USB call-site audit passed without new write sites; `git diff --check` was clean. No software
USB SET, detach, reset, DFU operation, or streaming-mode change was used in Phase 7D. The old
Phase 7C concurrent EIO remains un-reproduced here pending specific approval for a physical
multi-client test.

### 20. Phase 7E — independent data freshness (2026-10-17)

The Phase 7E software work starts from `f793ab7` on `phase7-hardware-integration` with a clean
worktree. At inspection time a previously launched read-only GUI owned the control lease and the
CONNECT 2 was enumerated. **No new physical fault, concurrent-client, unplug/replug or manual
control experiment was performed for this section.** Phase 7D's physical observations and
stage timings above remain the last real-device measurements. The `f793ab7` compact-header fix
still has an automated layout check, but has not been reconfirmed by a new real-device screenshot.

Settings and status now carry independent monotonic last-success timestamps. At the production
500 ms settings / 250 ms status schedules, readings are **fresh** through 750/375 ms,
**delayed** through 1500/750 ms, then **stale**. An absent reading, disconnected session or
new connection without a successful read is **unavailable**. These boundaries allow ordinary
timer jitter before warning and flag more than three periods without success; they are policy
thresholds, not measured firmware limits. A good meter/status GET cannot refresh the settings
timestamp, or vice versa. The GUI retains last-known values but places a concise, explicit
SETTINGS/METERS DELAYED, STALE or UNAVAILABLE warning in the top banner instead of claiming
they are current. Each warning clears only after a successful read of its own message type.
Reconnect resets both timestamps, including when old settings values are retained during a
failed connection attempt. The compact `Connected · read-only` header and `Unknown` real sample
rate remain unchanged. The warning/banner and Panic Mute notice have automated geometry checks;
their appearance on the physical screen is pending direct inspection.

Mock/QML tests exercise independently failed settings and status GETs, stale-to-fresh recovery,
reconnect reset, retained Panic Mute notice, boundary timestamps and write refusal. Lease tests
exercise contention, explicit `ControlInUse` guidance, release on close and after process exit;
the lease remains cooperative among current OpenWitty processes only. No physical multi-client
test was run. Opt-in trace level 1 now samples changed settings polls plus every 20th poll
(about five seconds in USB mode, 0.8 seconds in mock mode);
level 2 logs every poll. Status/settings transport time and decode/merge stages are separately
named, with nanosecond reporting for short in-process stages. Tracing is off by default and
records no payload or identifiers. No new physical-action-to-photon latency was measured:
internal stage timing cannot supply the physical action or actual pixel-presentation timestamp.
For a later owner-approved test, record a time-synchronized view of the physical control and GUI
at high frame rate, identify the first physical motion and first changed visible frame, and report
a distribution with frame-time uncertainty alongside the monotonic internal trace.

The production polling cadence stays 2 Hz settings / 4 Hz status. A hypothetical settings
increase to 4 Hz would raise scheduled GETs from six to eight per second (+33%) and reduce
the idealized uniform-arrival wait from 250 to 125 ms on average; these are arithmetic
projections, **not** hardware traffic or latency measurements. The faster cadence needs a
single-client real-device trial and owner-visible benefit before adoption. No new USB SET,
kernel-driver detach, reset, DFU or streaming-mode operation was introduced.

Final Phase 7E validation: Release **156/156 CTests**; ASan/UBSan **155/155 CTests** (that
configuration omits packaging staging); standalone USB call-site audit **PASS**; whitespace
check (`git diff --check`) clean. The mock/replay GUI integration test is included in both suites. Passive
enumeration still found the CONNECT 2 and an older read-only GUI process; the new binary was
not run against hardware while that process owned the lease. Consequently, Phase 7E freshness
thresholds and warning presentation are software-tested, not independently real-device-tested.
An opt-in offscreen `--mock` smoke run of the new trace emitted four sampled heartbeats:
mock poll gaps 39.1–40.1 ms, publication calls 24–82 µs, and sub-microsecond mock decoding.
Mock GET timing rounds to zero at microsecond resolution; these values are trace-functionality
checks, **not** real USB or physical-control latency measurements.

### 21. Phase 7F — read-only acceptance and meter-source investigation (2026-10-17)

Starting branch `phase7-hardware-integration` at `53c24e80655270f144bb057e6ceb838f03327ee0`
was clean. Passive USB enumeration found a CONNECT 2 (`29c2:0004`); its usbfs node was
read/write-accessible to the logged-in user through the existing ACL. ALSA and PipeWire both
listed the interface. A previously running OpenWitty GUI (PID 13477) held the per-user
`openwitty-connect2-control.lock` lease. It was left running; no second hardware control
client, concurrent-client experiment, permission change, detach, reset or write was attempted.

Source inspection confirms that the current eight GUI audio levels come from the 64-byte
status GET (`DeviceSession::refreshStatus`) scheduled at 4 Hz, not the 2 Hz settings GET.
The codec reads signed Q8.8 fields and clamps to −60…0 dB; `LevelMeter.qml` binds bar geometry
directly to the latest published sample. The 1/256 dB nominal code step does **not** establish
sensor accuracy, peak/RMS semantics, internal averaging window, or firmware update frequency.
Passive sysfs descriptors showed vendor-specific interface 3 with zero non-control endpoints
in active alternate setting 0. No documented alternative read-only meter path has been found;
this does not prove none exists. The dedicated, unimplemented Phase 7G design is in
`docs/PHASE7G_AUDIO_METER_PLAN.md`.

The Phase 7E mock/QML fault-injection suite passed during Phase 7F, covering independently
delayed/stale settings and status, recovery, and reconnect without interrupting real USB.
The owner then closed the older GUI normally. PID 13477 was absent, no OpenWitty process or
control-lease holder remained, and the current Release GUI rebuilt successfully. A new
single-client read-only launch took the lease. A window-only screenshot from that build showed
“Connected · read-only”, a non-overlapping “Sample rate: Unknown” label, the readable
“CONTROL LINK, READ-ONLY” readiness caveat, and real settings/selected-channel values. No
freshness warning appeared in that screenshot. The persistent GUI subsequently produced
regular read-only status/settings traces: representative poll gaps 249.910/249.943 ms,
status GETs 2.751/2.684 ms, settings GETs 3.994/3.994 ms; no transport error was reported.
These are internal stage observations, not an action-to-screen measurement. The owner visually
confirmed that the current selected channel, gain, volume, mute and mix indications matched
the physical device, with no delayed/stale/unavailable warning. In a follow-up manual selection
test, the owner selected microphone on the device and confirmed the blue GUI outline followed;
the trace classified a settings `selection` change without any software write. This is a
read-only, owner-observed live synchronization result, not an externally timed latency sample.
The owner then adjusted microphone gain physically and confirmed the GUI followed; the trace
classified successive `gain` changes at the unchanged 500 ms settings cadence. No software
gain command was sent. The owner did not provide an external action timestamp, so “updates” is
an observation, not a measured physical-to-screen delay.
The owner next selected headphone and confirmed its blue outline, then adjusted headphone
volume physically and confirmed the GUI slider followed. The trace classified `selection` and
successive `volume` changes, again without a software write. The owner then toggled headphone
and speaker mute physically and confirmed both GUI indicators; the trace classified matching
`mute` changes. Finally, the owner selected and adjusted input/playback mix, confirmed that
the GUI mix knob followed, and saw no other control change. The trace classified successive
`mix` changes without a headphone-volume change. These owner-operated actions and their GUI
observations complete the Phase 7F live-control checks; they do not calibrate mix acoustics.
The owner confirmed at the end that the readiness banner remained readable and no
SETTINGS/METERS delayed or stale warning was visible. The single-owner GUI had run for over
eight minutes, retained the control lease, and produced regular read-only GET traces without
a reported transport error. ALSA still listed the CONNECT 2; no audio-ready claim is inferred
from the control GETs alone. No USB SET, driver detach, reset, DFU, streaming-mode switch or
software-triggered device change was performed. The GUI was left running for the owner.

No synchronized physical-action/video marker was available, so no new genuine end-to-end
latency measurement is claimed. The production 2 Hz settings / 4 Hz status schedules are
unchanged.

Phase 7F automated checks: Release **156/156 CTests**, including the mock/QML fault suite;
ASan/UBSan **155/155**; standalone USB call-site audit **PASS**. These validate the unchanged
read-only software baseline and plan documentation, not the pending real-screen acceptance.

### 22. Phase 7G — initial read-only meter rendering (2026-10-17)

Starting branch `phase7-hardware-integration` at
`2ed45798179595351bf547e65b9e2a3f0a89c097` was clean. The older GUI had been closed
normally; no OpenWitty process held the control lease. Passive enumeration found the CONNECT 2
in application mode and ALSA listed its USB-Audio card. The new eight-channel presentation
model was first exercised by deterministic unit/QML tests and an offscreen mock screenshot.
That screenshot showed all eight bars and their sampled-peak markers without a geometry shift;
offscreen rendering is **not** evidence of physical-display frame cadence.

A single instance of the new read-only GUI then acquired the lease and connected to the real
device. Its opt-in payload-free trace showed repeated ~250 ms status-poll gaps, status GETs
around 2.6–2.7 ms and alternating settings GETs around 4.0 ms. Those individual trace samples
support the unchanged 4 Hz/2 Hz schedule; they are not a statistical latency distribution.
No transport error was reported in the observed trace. ALSA still listed the device. An idle
process snapshot at roughly 33 s showed 0.4% cumulative CPU and 160,688 KiB RSS for the
whole GUI; no before/after incremental-memory or active-meter CPU conclusion follows. The
device was quiet during this measurement, so an active **real-device** meter frame-cadence distribution and
owner judgment of meter motion remain pending. No genuine physical-sound-to-screen latency
was measured. No USB SET, driver detach, reset, DFU, or polling-rate increase was performed.

Two separate seven-second **offscreen mock** runs with opt-in frame tracing produced 240-gap
summaries with median gaps 15.43–15.53 ms, p95 16.78–16.86 ms and p99 17.18–17.48 ms
(median-rate estimate 64.4–64.8 FPS). This confirms the frame clock can advance simulated
envelopes smoothly under that test setup; it is not a physical-screen or real-USB measurement.
During a separate offscreen mock run, the mock process showed 5.6% cumulative CPU and
66,524 KiB RSS at three seconds. This whole-process snapshot includes simulated 25 Hz
status generation and software offscreen rendering; it is not the meter feature's incremental
cost and must not be compared directly with the real GUI's idle snapshot.

The 4 Hz status payload has eight signed Q8.8 meter fields clamped to −60…0 dB by the
existing codec. The new 20 ms attack / 450 ms release / 750 ms sampled-peak hold / 24 dB/s
peak decay are **presentation parameters**, not measured device dynamics. A missed transient
between GETs cannot be recovered or asserted as a peak. Delayed status freezes/dims the bars;
stale/unavailable clears them until a fresh sample. Those fault transitions are mock/unit
tested, not deliberately induced on the physical USB connection.

Automated validation after the last code change: Release **163/163 CTests**; ASan/UBSan
**162/162 CTests**; standalone USB call-site audit **PASS**; `git diff --check` clean.
The full suites include the pre-existing replay tests and read-only refusal checks, plus
seven new deterministic meter-dynamics CTests and a new eight-channel QML integration check.
An active, externally observed ~60 FPS cadence and owner visual judgment were not yet
available when this note was written. The 8 Hz/16 Hz read-only acquisition trial remains
separately approval-gated.

### 23. Phase 7G.2 — bounded meter diagnostic prepared (2026-10-18)

Starting branch `phase7-hardware-integration` at `727a3da20ed54588b35561bd3126826c26bb3ac6`
was clean. Passive checks found one CONNECT 2 in normal application mode (`29c2:0004`),
vendor-specific control interface 3 (`ff/f0`, zero endpoints, unbound), a writable usbfs node
through the existing user ACL, and the card in ALSA/PipeWire. No OpenWitty process or held
control lease was found. These are enumeration/permission observations, **not** a status-GET
test. No physical USB control request or audio capture was performed in preparation.

`openwitty-meter-probe` now provides a dry-run-by-default, single-owner, read-only status
sampler. Its only real-device options use `readOnlyHardwareOptions()` (automatic safe interface,
no detach, no writes). Each bounded run performs at most one API GET plus 40 status GETs at
4 Hz or 80 at 8 Hz over a requested 10-second window. It stops on the first transport or
decode error and never sends catch-up bursts. Output contains monotonic relative time, GET
duration, eight decoded dB levels, separate changed-status/changed-meter flags, repeat runs,
summary timing/CPU/RSS, and no raw payload or serial. The software/mock checks do **not**
establish real USB stability or an 8 Hz benefit.

After implementation, the full Release suite passed **166/166 CTests** (three new probe
mock/dry-run checks), and the standalone USB call-site audit passed. The CTests made no
physical sampling GETs. ASan/UBSan was not rerun for this small diagnostic addition.

The owner then explicitly approved the two exact, sequential 10-second read-only commands in
`docs/PHASE7G_AUDIO_METER_PLAN.md` and reported a comparable steady signal was prepared. Before
execution, a fresh passive preflight reconfirmed `29c2:0004`, interface 3 `ff/f0/00` with zero
endpoints and no bound driver, read/write usbfs permission, ALSA/PipeWire card presence, no
OpenWitty process, and no held control lease. No other control client was started. The commands
were executed in order, with a lease/audio-presence check between them. No 16 Hz command,
GUI launch, software audio routing change, USB SET, driver detach, reset or DFU access occurred.

| Observation | 4 Hz baseline | 8 Hz candidate |
|---|---:|---:|
| Successful status GETs / errors | 40 / 0 | 80 / 0 |
| Actual first-to-last GET-start cadence | 4.000 Hz | 8.000 Hz |
| GET duration mean / median / p95 / max | 2.727 / 2.749 / 2.908 / 2.995 ms | 2.759 / 2.760 / 2.927 / 2.989 ms |
| Changed eight-meter bytes, excluding first sample | 39/39 | 79/79 |
| Changed complete status frames, excluding first sample | 39/39 | 79/79 |
| Consecutive identical meter snapshots | 0; max run 0 | 0; max run 0 |
| Probe CPU time / reported mean process CPU | 0.9 ms / 0.01% | 3.6 ms / 0.04% |
| Probe-reported peak RSS | 777,620 KiB | 777,620 KiB |

Only speaker L/R levels moved; both ranged from about −57 to −31 dB at 4 Hz and −60 to −30 dB
at 8 Hz. Mic, instrument, loopback L/R and headphone L/R decoded at the −60 dB display floor
throughout both runs. This is **not** an eight-channel firmware-rate finding. At 8 Hz, the
intervening 125 ms samples changed from their neighbors: 38 of the 39 interior midpoint
speaker-L readings differed from *both* adjacent 250 ms-spaced readings. Three midpoint
readings exceeded both neighbors by more than 1 dB and nine fell below both by more than
1 dB; these are observed samples, not interpolated peaks. Thus 8 Hz supplied additional
genuine status-meter observations under this signal. Because the two runs covered different
10-second signal windows, their ranges/peaks are not a controlled paired accuracy comparison;
the result does not establish the device's true peak/RMS semantics or a visual improvement.

The probe's identical 777,620 KiB `ru_maxrss` readings are anomalously high for this small
diagnostic (a separate mock run reported 4,612 KiB). They were **not** independently verified
with a concurrent process-memory sample, so no conclusion about incremental memory cost is
drawn. The reported CPU values are short-run whole-process measurements, not isolated USB
costs. After both runs, the device still enumerated at the same VID/PID, interface 3 remained
unbound, the control lease was free, and ALSA/PipeWire still listed the card. Those passive
checks demonstrate continued enumeration, not absence of audible glitches or XRUNs; owner
audio-quality confirmation was requested. Production GUI polling remains 4 Hz status / 2 Hz
settings, and any 16 Hz experiment remains unapproved.

The owner subsequently confirmed **no audible interruptions, clicks or dropouts** during
either approved probe run. This is an owner report, not instrumented XRUN evidence.

### 24. Phase 7G.3 — opt-in 8 Hz real-GUI comparison (in progress, 2026-10-18)

The branch started clean at `214a5e0e0a456e24e59f6091a9b4b24963cf18cf`. A temporary
`--experimental-status-8hz` GUI startup option changes only the real-device status timer
from 250 to 125 ms and settings from every second to every fourth status tick, preserving
2 Hz settings. Status age thresholds become 188 ms fresh / 375 ms stale in the opt-in mode;
settings stay 750 / 1500 ms. The default remains 4 Hz status / 2 Hz settings, and mock
polling is unchanged. The window title explicitly labels the experiment. The existing
single worker/serialized transport, control lease, frame animation and real-hardware
write refusal remain in place.

Before the GUI comparison, there was no OpenWitty process or held lease. The normal
4 Hz read-only GUI connected to the physical CONNECT 2 without a reported transport
error. A payload-free internal trace sample spanning 68 poll gaps had a median gap of
249.994 ms (minimum 249.635 ms; a startup maximum 261.974 ms), median status GET
2.656 ms and 34 alternating settings GETs (median 3.995 ms). This is observed GUI
polling, not a measured physical-sound-to-screen latency. ALSA and PipeWire continued
listing the card. An independent `/proc/<pid>/status` and `smaps_rollup` snapshot while
the GUI was connected measured about 161,000 KiB RSS (PSS 59,562 KiB), with 46,128 KiB
private dirty; this does **not** reproduce or explain the probe's 777,620 KiB peak-RSS
report because it is a different process and measurement. The owner judged the 4 Hz
GUI meters "way too slow" and closed that window normally. This is a qualitative
visual report, not a frame-cadence or physical-sound-to-screen latency measurement.
The 8 Hz visual assessment remains pending; no meter-smoothness acceptance is claimed.

After normal closure of the 4 Hz window and verification that no OpenWitty control
client held the lease, the opt-in 8 Hz GUI connected to the same device. Three
non-overlapping payload-free trace batches contained 414 status poll gaps in all;
their batch medians were 124.994–124.996 ms, with observed extrema 124.048–125.937 ms.
The corresponding status-GET batch medians were 2.789–2.803 ms; settings GETs
continued every fourth tick, with batch medians 3.992–3.993 ms. No USB transport
error was printed in these batches. These are acquisition/transfer timings, **not**
GUI frame cadence or physical-sound-to-screen latency. An independent process
snapshot after roughly one minute showed 161,240 KiB RSS and 0.4% cumulative CPU;
an earlier `/proc/<pid>/smaps_rollup` snapshot showed 160,016 KiB RSS, 59,011 KiB
PSS and 45,752 KiB private dirty. The memory figures are of the complete GUI, not
incremental meter cost. ALSA and PipeWire still listed the CONNECT 2 audio device.
Audible quality and meter appearance require the owner's assessment.
