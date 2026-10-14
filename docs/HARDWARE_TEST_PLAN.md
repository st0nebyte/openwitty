# Hardware validation plan (CONNECT 2)

Not executed — no device was available when this was written. Every step is **read-only
until step 11**, and step 11 is only a *proposal*. Stop at the first unexpected result and
send the output; do not improvise. No step needs sudo unless stated.

Legend: **Expect** = what static analysis predicts · **If not** = failure modes and recovery.
Record outputs verbatim (copy the terminal).

## 0. Preconditions
* Build: `cmake --build build` (see DEVELOPMENT.md). Tools: `build/openwitty-cli`.
* Close other apps that control the interface (the official Control Center, etc.).
* Keep audio playing on another device if you need uninterrupted sound; nothing below should
  interrupt the CONNECT 2's own audio, except where marked **DISRUPTIVE** (never part of this plan).

## 1. USB enumeration
`openwitty-cli list` and `openwitty-cli devices`
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
`openwitty-cli diagnose` (prints `usbfs node … read=… write=…`).
* **Expect (this machine class):** `root:root 0664`, `write=NO` → `EACCES`. This is a *file
  permission* problem, independent of driver ownership.
* **If writable:** continue. **If not:** an administrator must install
  `packaging/udev/70-openwitty.rules` (adds the `uaccess` tag) and replug; steps 5+ then
  work for the logged-in user. Alternatively stop here — descriptors/driver info are already
  gathered.

## 5. Read-only API handshake
`openwitty-cli read api`
* **Expect (best case):** `api major=1 minor=…`.
* **Predicted failure:** `[busy]` (EBUSY) because of step 3 → exit code 4 with explanation.
  That is *useful*: it confirms the kernel-ownership model. Then continue with step 5b.
* **If `[access-denied]`:** back to step 4. **If `[stall]`:** try `--request uac1`.
  **If `[timeout]`:** retry once, replug, check the PID again.

### 5b. Alternative recipient (experiment, read-only, opt-in)
`openwitty-cli read api --method device-recipient` and `openwitty-cli probe` (dry run), then
`openwitty-cli probe --execute` (only GET transfers; no detach/claim/write).
* **Expect:** most likely `[stall]` or `[busy]`; any successful 64-byte answer is a major result
  — save it.
* **Never** use `--method detach` as part of this plan (**DISRUPTIVE**: removes the sound card).

## 6. Device version
`openwitty-cli read version` → 64 raw bytes (layout unknown; capture only).
* **Expect:** 64 bytes. **If** the app-mode check fails (`Device must be opened in APP mode`
  analogue) you are in DFU/bootloader: stop.

## 7. Device status
`openwitty-cli read status` several times, e.g. with/without signal on the mic input.
* **Expect:** 64 bytes; bytes 0–1 (mic level, Q8.8 dB, ≤ 0) change with input level; output meters
  at 0x12–0x1C; byte 0x24 = 0 when idle.
* **Verify:** meter bytes respond to a tap on the microphone and to playback.

## 8. Settings read
`openwitty-cli read settings`
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
openwitty-cli devices:            (paste)
openwitty-cli diagnose:           (paste)
step 5 read api:               (paste, exit code)
step 5b (if run):              (paste)
step 6/7/8 raw hex:            (paste)
step 9 diffs:                  (paste)
step 10 notes:                 (xruns? card still present?)
```


---

## Appendix A (Phase 5E): pre-flight checklist and privilege matrix

### A.1 Pre-flight (do all before touching the device)

| # | Check | How | Pass |
|---|-------|-----|------|
| 1 | Build is the audited one | `openwitty-cli --version` prints `0.1.0 (full build)`; `git rev-parse HEAD` recorded | yes |
| 2 | Test suite green on the build under test | `ctest --test-dir <build> --output-on-failure` (96 entries incl. `audit_usb_call_sites`) | 0 failures |
| 3 | Writes are off | `openwitty-cli` has no write command; the GUI shows the device read-only (`writable=false`); do **not** set any write option | by construction |
| 4 | Simulation is not mistaken for hardware | GUI footer says "SIMULATED"; hardware sessions show backend `usb` | visual |
| 5 | Kernel audio is working before the test | play a sound through the device; `aplay -l` shows the card | yes |
| 6 | Results sheet ready | copy the "Result template" below; record outputs verbatim | yes |

### A.2 Which steps need which privileges

| Step | Needs root / admin? | Notes |
|------|--------------------|-------|
| 1 enumeration (`list`, `devices`) | **No** | sysfs / libusb enumeration only |
| 2 descriptors | **No** | `devices` reads sysfs; `lsusb -v` may need root for string descriptors on some systems (optional) |
| 3 driver ownership | **No** | sysfs `driver` links |
| 4 permissions | **No** to inspect; **Yes (admin, once)** to install `70-openwitty.rules` | rule is user-reviewed; nothing in this repo installs it automatically |
| 5 API handshake (`read api`) | **No** after the udev rule; otherwise EACCES (exit 3) | expect EBUSY (exit 4) while `snd-usb-audio` owns the interface; this is a *finding*, not an error to "fix" |
| 5b device-recipient variant | **No** (same as 5) | opt-in `--method device-recipient`; read-only GET |
| 6 version, 7 status, 8 settings | **No** (same as 5) | reads only, 500 ms timeouts |
| 9 compare with the device | **No** | |
| 10 audio coexistence | **No** | observe `aplay`/PipeWire during steps 5-8; no detach |
| `--method detach` | **Admin-equivalent effect (drops the sound card)** | **not part of this plan**; needs `LEWITT_ALLOW_DISRUPTIVE=1` *and* a transport-level `allowDisruptive` |
| 11 write test | **Not authorised** | separate written approval, separate plan |

### A.3 Abort criteria (stop immediately, record, do not retry in a loop)

* PID `29c2:000a` (probable bootloader/DFU) or any PID other than `0002`/`0004`.
* The sound card disappears from `aplay -l` or audio glitches during a read.
* Any response whose size differs from the message size (`short-transfer`/`malformed`), or an
  API major other than 1 (`Message API version not supported`).
* Three consecutive timeouts: the session declares the link dead; unplug/replug before continuing.

### A.4 What the first session must produce

1. `openwitty-cli devices` (full), `lsusb -v -d 29c2:` if available.
2. `openwitty-cli read api` for each of: default; `--request uac1`; `--method device-recipient`
   (each is a single GET; stop at the first successful variant).
3. If a variant succeeds: `read version`, `read status`, `read settings` (raw hex kept).
4. `getfacl` of the `/dev/bus/usb/BBB/DDD` node, `id`, and the udev rule that was installed.
5. Whether audio kept playing throughout.

These results decide the access method (see USB_ACCESS.md) and are the first real evidence
for the layouts in PROTOCOL.md; until then nothing in this project is hardware-verified.
