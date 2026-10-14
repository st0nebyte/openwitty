# Hardware validation plan (CONNECT 2)

Not executed — no device was available when this was written. Every step is **read-only
until step 11**, and step 11 is only a *proposal*. Stop at the first unexpected result and
send the output; do not improvise. No step needs sudo unless stated.

Legend: **Expect** = what static analysis predicts · **If not** = failure modes and recovery.
Record outputs verbatim (copy the terminal).

## 0. Preconditions
* Build: `cmake --build build` (see DEVELOPMENT.md). Tools: `build/lewitt-cli`.
* Close other apps that control the interface (the official Control Center, etc.).
* Keep audio playing on another device if you need uninterrupted sound; nothing below should
  interrupt the CONNECT 2's own audio, except where marked **DISRUPTIVE** (never part of this plan).

## 1. USB enumeration
`lewitt-cli list` and `lewitt-cli devices`
* **Expect:** `29c2:0004` (normal) or `29c2:0002` (macOS streaming). Manufacturer "LEWITT".
* **If not:** no device → try another cable/port, `dmesg` is not needed. PID `29c2:000a` →
  probably bootloader/DFU: **stop**, do not continue. Other PID → record it.

## 2. Descriptor collection
Output of `devices` (from sysfs; no USB I/O). Also save `lsusb -v -d 29c2:` if available.
* **Expect:** UAC2 AudioControl interface (class 01/01, `UAC2 AudioControl`), AudioStreaming
  interfaces, possibly more. Note every interface number, alt settings, endpoints.
* **Record:** bcdDevice (matches the `≥ 0x0b` rule? PROTOCOL.md), interface numbers, whether any
  interface is **not** bound to a driver (option C in USB_ACCESS.md).

## 3. Driver ownership
Same `devices` output, `driver:` column.
* **Expect:** `snd-usb-audio` on AC and AS interfaces → interface-recipient control requests
  will fail with `EBUSY`.
* **If not:** another driver or none → note it; a different access method may apply.

## 4. User permissions
`lewitt-cli diagnose` (prints `usbfs node … read=… write=…`).
* **Expect (this machine class):** `root:root 0664`, `write=NO` → `EACCES`. This is a *file
  permission* problem, independent of driver ownership.
* **If writable:** continue. **If not:** an administrator must install
  `packaging/udev/99-lewitt-connect2.rules` (adds the `uaccess` tag) and replug; steps 5+ then
  work for the logged-in user. Alternatively stop here — descriptors/driver info are already
  gathered.

## 5. Read-only API handshake
`lewitt-cli read api`
* **Expect (best case):** `api major=1 minor=…`.
* **Predicted failure:** `[busy]` (EBUSY) because of step 3 → exit code 4 with explanation.
  That is *useful*: it confirms the kernel-ownership model. Then continue with step 5b.
* **If `[access-denied]`:** back to step 4. **If `[stall]`:** try `--request uac1`.
  **If `[timeout]`:** retry once, replug, check the PID again.

### 5b. Alternative recipient (experiment, read-only, opt-in)
`lewitt-cli read api --method device-recipient` and `lewitt-cli probe` (dry run), then
`lewitt-cli probe --execute` (only GET transfers; no detach/claim/write).
* **Expect:** most likely `[stall]` or `[busy]`; any successful 64-byte answer is a major result
  — save it.
* **Never** use `--method detach` as part of this plan (**DISRUPTIVE**: removes the sound card).

## 6. Device version
`lewitt-cli read version` → 64 raw bytes (layout unknown; capture only).
* **Expect:** 64 bytes. **If** the app-mode check fails (`Device must be opened in APP mode`
  analogue) you are in DFU/bootloader: stop.

## 7. Device status
`lewitt-cli read status` several times, e.g. with/without signal on the mic input.
* **Expect:** 64 bytes; bytes 0–1 (mic level, Q8.8 dB, ≤ 0) change with input level; output meters
  at 0x12–0x1C; byte 0x24 = 0 when idle.
* **Verify:** meter bytes respond to a tap on the microphone and to playback.

## 8. Settings read
`lewitt-cli read settings`
* **Expect:** 128 bytes; decoded gains within 0–72 / 0–60 dB, booleans 0/1, preamp 0–2,
  monitor source 0–2, mix within −1…1, LED brightness ≤ 100.
* **Record:** the raw hex and the decoded text; any value outside these ranges is a finding.

## 9. Protocol verification (compare with the device itself)
For each of: gain, mute, Clipguard, preamp, headphone volume, speaker mute, Dim, Mono, monitor
source — change it **on the hardware** (touch surface) or in the official app, re-run
`read settings`, and diff the raw hex against the previous capture.
* **Expect:** only the byte(s) in PROTOCOL.md's table change; unmapped bytes stay constant.
* **Record:** unexpected changes (new bytes, different encoding). This validates the field map
  *without* sending any write from our code.

## 10. Non-disruptive audio coexistence
While playing audio through the CONNECT 2, run steps 5–8 repeatedly (e.g. 100× `read status`).
* **Expect:** no xruns/dropouts, no new kernel messages (`dmesg` only if permitted),
  device keeps working; the sound card does not disappear (`aplay -l` unchanged).
* **If audio glitches or the card disappears:** stop; record exactly which command did it.
  Recovery: replug the device; no state is changed by read-only commands.

## 11. Proposal for a future, explicitly authorised write test (NOT part of this plan)
Only after steps 1–10 were reviewed and the owner says so in writing:
1. Preconditions: step 8 settings captured; official app closed; `UsbOptions::allowWrites` and
   `WritePolicy::allowHardwareWrites` set by a dedicated test binary, never by the GUI.
2. Single, reversible, non-persistent candidate: toggle **headphone mute** (byte 29) on, read back,
   toggle off, read back. Expected: the headphone LEDs/touch surface reflect it.
3. Abort criteria: any unexpected byte change, stall, or audio interruption → restore by
   writing the original captured settings, then stop.
4. Out of scope forever unless separately approved: factory reset (0x106), mode switching
   (0x104/0x105), firmware/DFU, LED colour reset (0x103).

## Result template
```
date / kernel / distro:
lewitt-cli devices:            (paste)
lewitt-cli diagnose:           (paste)
step 5 read api:               (paste, exit code)
step 5b (if run):              (paste)
step 6/7/8 raw hex:            (paste)
step 9 diffs:                  (paste)
step 10 notes:                 (xruns? card still present?)
```
