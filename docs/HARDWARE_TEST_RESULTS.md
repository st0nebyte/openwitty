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

The user confirmed (2026-10-14/15): no long-press gesture exists on any front-panel button, and the
following are CONTROL CENTER (software)/overlay-only on this CONNECT 2 unit, with no hardware path to
test them:

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
