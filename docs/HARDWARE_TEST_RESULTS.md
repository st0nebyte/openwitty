# Hardware test results — first real CONNECT 2 session

**Date:** 2026-10-14
**Machine:** CachyOS Linux (Arch-based), kernel `7.2.9-2-cachyos`, KDE Plasma (Wayland), PipeWire/PulseAudio-compat.
**Build under test:** commit `ef6143363cdd69c25e3baa8f278068d838a6ac72` (HEAD at session start), `release` preset.
**Device:** LEWITT CONNECT 2, serial `XXXXXXXXXXXX`, bus 6 device 4, path `/sys/bus/usb/devices/6-4.1`.

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
- bytes 26–45: ASCII `"XXXXXXXXXXXXXXXXXXXX"` — the last 12 characters, `XXXXXXXXXXXX`, are an exact
  match for the device's USB `iSerial` string reported by `devices`/sysfs. The leading 8 characters
  (`XXXXXXXX`) are unidentified (hex-looking, possibly a build id/date code).
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
