# Linux access to the CONNECT 2 control protocol

**Question:** how can userspace issue the class-specific `MEM` requests (entity 0x33,
wValue `apiMajor<<8 | messageId`) while `snd-usb-audio` keeps owning the AudioControl
interface and audio keeps playing?

Evidence classes — **[src]** read in upstream kernel source (torvalds/linux master,
fetched 2026-10-14; files named below), **[sys]** observed on this development machine
(Linux 7.0, Ubuntu 24.04 userland), **[bin]** vendor binaries, **[hyp]** hypothesis that
needs the device. Nothing here is hardware-verified.

## 1. Verified facts

| # | Fact | Evidence |
|---|---|---|
| F1 | Every usbfs ioctl needs a writable fd; a read-only fd gets `EPERM`. | [src] `devio.c: usbdev_do_ioctl`: `if (!(file->f_mode & FMODE_WRITE)) return -EPERM;` |
| F2 | A class request with *interface* recipient makes usbfs claim that interface; if another driver is bound the claim fails (`EBUSY`). | [src] `devio.c`: `check_ctrlrecip` → `checkintf` → `claimintf` → `usb_driver_claim_interface` |
| F3 | A class request with *device* recipient (`bmRequestType` 0xA0/0x20) has no case in `check_ctrlrecip` → it is **not** blocked by claims. Vendor-type requests are allowed outright. | [src] `devio.c: check_ctrlrecip` |
| F4 | `snd-usb-audio` never sends UAC2 `MEM`: the constant exists, the header lists "RANGE and MEM set commands (UAC2)" as TODO. Its mixer only uses `CUR`/`RANGE`. | [src] `sound/usb/mixer.c`, `include/linux/usb/audio-v2.h` |
| F5 | The kernel has **no** entry for VID `0x29C2` (LEWITT); the only Thesycon entry is VID `0x152a`. A quirk would have to be added. | [src] `quirks-table.h`, `quirks.c`, `mixer_quirks.c` |
| F6 | There is precedent for exposing a vendor protocol of a class-audio device through ALSA `hwdep`: Focusrite Scarlett2 (`sound/usb/sc2.c: snd_hwdep_new(…"Focusrite Control"…)` + ioctls) and Creative SB remote (`mixer_quirks.c`). Hook: `snd_usb_mixer_apply_create_quirk()` keyed on USB id. | [src] |
| F7 | Detaching **any** interface of the card tears down the *whole* sound card: first disconnect does `atomic_inc_return(&chip->shutdown)==1` → `snd_card_disconnect()` + release of all PCM streams, endpoints, MIDI and mixers. Re-attaching creates a new card. | [src] `card.c: __usb_audio_disconnect` |
| F8 | `/dev/snd/*` nodes get a per-user ACL for the logged-in seat user (`70-uaccess.rules: SUBSYSTEM=="sound", TAG+="uaccess"`). `/dev/bus/usb/*` nodes are tagged only for specific classes (still-image `060101`, media players, hwdb "AV production controllers"); LEWITT's VID is not in `70-av-production.hwdb`. | [sys] udev rules + `getfacl /dev/snd/controlC0` |
| F9 | Here, 28 of 31 `/dev/bus/usb/*/*` nodes are `root:root 0664` → **no write access for the user** → `libusb_open()` fails with `EACCES` for a typical USB audio interface. | [sys] `ls -l`, `openwitty-cli`'s `access(2)` check on a real UAC2 device |
| F10 | `/sys/bus/usb/devices/*/descriptors` (raw device + configuration descriptors), `idVendor/idProduct`, and the `driver` symlink of each interface are world-readable → descriptors and driver ownership can be shown with no usbfs access and no I/O. | [sys] |
| F11 | Windows and macOS both issue exactly the request shape we use (entity 0x33, req 3 / 0x85 / 0x05, wValue = apiMajor<<8\|msg, 500 ms). On Windows the Thesycon driver chooses the interface; macOS passes `0x3300\|ifnum`. | [bin] PROTOCOL.md |

## 2. Three separate failure classes (never conflate)

| Symptom | Where | Cause | Fixable without root? |
|---|---|---|---|
| `EACCES` at `open` | node permission | usbfs node not writable by the user (F9) | **No** — needs a udev rule/ACL by an administrator |
| `EPERM` on an ioctl | read-only fd (F1) | node opened O_RDONLY | n/a (we always open O_RDWR) |
| `EBUSY` on transfer | kernel ownership (F2) | `snd-usb-audio` is bound | Not by permissions; see options below |
| `EPIPE` / stall | device | firmware rejected variant/recipient | Yes (protocol question) |

`openwitty-cli` reports each with its own code name and next step; the exit codes differ
(3 = access denied, 4 = busy, 5 = stall, 6 = timeout, 7 = disconnected/not found,
8 = unsupported, 9 = bad response).

## 3. Technical comparison

| Option | Works without disturbing audio? | Needs root? | Evidence | Verdict |
|---|---|---|---|---|
| **A. libusb, interface recipient, no claim** (default `--method interface`) | Does no harm, but **fails** while the AC interface is bound and in use (F2) — hardware-verified `LIBUSB_ERROR_IO`, not the predicted `EBUSY` | udev rule for the node (F9) | [src]; [sys] 2026-10-14 | **confirmed non-working**; useful to *prove* the failure mode |
| **B. libusb, device recipient** (`--method device-recipient`) | Not blocked by usbfs (F3). Firmware **actively rejects** it (`EPIPE`/stall) | udev rule (F9) | usbfs [src]; firmware [sys] 2026-10-14 | **confirmed non-working**; device says no |
| **C. Unbound interface** (probe lists them) | Claimable without touching audio; the CONNECT 2 exposes one (vendor-specific, interface 3, 0 endpoints) in addition to a DFU interface (4) | udev rule | [sys] 2026-10-14: hardware-verified GET success on interface 3, no audio disruption | **confirmed working** — current recommended path |
| **D. ALSA mixer / `amixer`** | n/a — MEM is not reachable (F4) | no | [src] | **not possible** |
| **E. hwdep interface in `snd-usb-audio`** (new quirk + ioctl `MEM_GET/SET`) | **Yes**: runs inside the driver that already owns the interface, no detach, no audio interruption | kernel patch + module build/install (root); once present, `/dev/snd/hwC*` is user-accessible via uaccess (F8) — no udev rule needed | precedent F6; quirk hook F5/F6 | **smallest correct solution**; recommended if B/C fail |
| **F. Detach + claim (usbfs)** (`--method detach`) | **No**: drops the whole sound card (F7) | udev rule | [src] | disruptive; isolated, opt-in, env-gated, never used by the GUI |
| **G. Unbind via sysfs / `USBDEVFS_DISCONNECT_CLAIM`** | **No**: same as F | root (sysfs) / udev rule | [src] | rejected |
| **H. usbmon / sniffing the vendor app** | Observation only | root | — | useful for *verification* on a machine with Windows/macOS, not for control |
| **I. Out-of-tree standalone kernel driver** | Competes with `snd-usb-audio` for the interface | root | — | rejected: duplicates the claim problem |

### Recommendation (updated 2026-10-14 after first hardware session)
1. On hardware, first run `openwitty-cli diagnose` (sysfs only) → learn PID, interfaces, driver
   ownership, node permissions without any I/O.
2. If the node is writable (admin installed the rule): go straight to **C** (`probe --execute`,
   the unbound-interface variants) — A and B are now confirmed non-working on this device class (see
   §3b) and are kept only as documented negative results / regression checks, not as things worth
   retrying first.
3. **C is the current working path** for message 0x0A. Extending it (new CLI/transport method) to
   the other read-only message types (version/status/settings) is the proposed next step — not yet
   implemented. Option **E** (hwdep) is deprioritized unless C proves insufficient. Design sketch for
   E, kept for reference, *still not implemented*:
   * quirk entry for `USB_ID(0x29c2, 0x0004)` and `0x0002` in `mixer_quirks.c`
     (`snd_usb_mixer_apply_create_quirk`);
   * `snd_hwdep_new(card, "LEWITT Control", 0, &hw)`, ioctls `GET_VERSION`, `MEM_GET`,
     `MEM_SET`; arguments `{u8 message_id; u8 size; u8 data[0x80]}`;
   * the kernel validates `message_id ∈ {0,2,3,4,0x0A}`, size == table, and **refuses
     trigger words 0x104/0x105/0x106**; issues `usb_control_msg()` to the AC interface it
     already owns (entity 0x33, request 3 or 0x85/0x05, 500 ms), serialised by
     `snd_usb_lock_shutdown()`;
   * userspace adds `HwdepTransport : IDeviceTransport` — no protocol/GUI changes.
   Upstream acceptance is not assumed; the patch would be small and class-compliant.

## 3b. Hardware session 2026-10-14 — option C confirmed

First real hardware session (`docs/HARDWARE_TEST_RESULTS.md`). Summary of what changed:

* **Option C is no longer hypothetical.** The CONNECT 2 (serial redacted, bcdDevice `0121`)
  exposes interface 3, class `ff/f0/00` (vendor-specific, 0 endpoints), unbound to any driver. A
  read-only GET (`req=0x03`, `wValue=0x000A`, `wIndex=0x3303`) addressed to it succeeded: 64 bytes
  back, byte0/1 matching the documented API-handshake shape (major=1), with additional structured
  data in bytes 2–63 including the device's own serial number in ASCII. No claim, no driver
  conflict, no audio disruption (card stayed enumerated throughout; no kernel teardown messages).
* **Option A (interface 0, kernel-owned) fails, but not the way F2 predicted.** Both GET variants
  (`req=0x03` and `req=0x85`) returned `LIBUSB_ERROR_IO`, not `EBUSY`. `dmesg` showed only an
  advisory `usbfs: ... did not claim interface 0 before use` warning — the same advisory also fired
  for interfaces 3 and 4, which both succeeded, so the warning does not explain the failure. Most
  likely cause: a genuine bus-level collision with `snd-usb-audio`'s own active use of interface 0's
  control pipe, not a usbfs-level claim refusal on this kernel. F2 should be read as "interface 0 is
  not usable while the card is attached", not "fails with EBUSY" — the exact errno is kernel/timing
  dependent.
* **Option B (device recipient) got a clean, well-defined answer**: `EPIPE`/stall on both request
  variants — the firmware actively rejects device-recipient class requests. This rules B out
  definitively; it is not a permissions or kernel issue, the device itself says no.
* The device also has an **interface 4** (class `fe/01/01`, real DFU functional descriptor) that
  answered the identical 64 bytes to the same GET. Likely a shared EP0 handler rather than a second
  real channel; not re-tested beyond the one capture because `req=0x03`/`bmRequestType=0xA1` aliases
  the standard DFU `DFU_GETSTATUS` request. **Use interface 3, not interface 4, going forward.**
* Consequence for the recommendation below: **Option E (hwdep kernel driver) is no longer the
  default next step.** Option C works today, from userspace, with no kernel patch. Revisit E only if
  C turns out insufficient (e.g. if settings/status reads over interface 3 misbehave).

### 3c. Hardware session 2026-10-15 — option C confirmed for writes too

`docs/HARDWARE_TEST_RESULTS.md` §13. A real `bm=0x21 req=0x03` MEM SET (the write counterpart of the
already-verified GET) against interface 3, changing exactly one byte of the full 128-byte settings
struct (LED brightness), was accepted by the device and produced a visible, confirmed effect — then
reversed. No driver conflict, no claim needed, no audio disruption (card present throughout). This is
the first real-hardware write on this project and confirms option C is not read-only: the same
userspace, no-kernel-patch access path works for SET as well as GET, at least for this one field.

**Settling delay, measured more precisely 2026-10-15** (`docs/HARDWARE_TEST_RESULTS.md` §15.1): a GET
issued immediately after the SET (same process, microseconds later) returned the pre-write value; a
GET from a separate process ~1ms later already returned the correct post-write value — convergence is
far faster than the original "tens-to-hundreds of ms" characterization suggested; that estimate is
corrected here. The staleness is best explained by the near-zero gap between the write call returning
and the very next read call in the same process, not by a genuinely slow device-side settling time.
Anything built on this access path must still not treat an immediate same-process post-write read as
authoritative — add even a short delay or a bounded poll (see `core/WriteVerification.hpp`'s
`commitAndVerify()`, added 2026-10-15, default ~570ms total budget) instead of trusting it blindly.
This is a property of the device's firmware/USB stack interaction, not of interface 3 specifically,
but was only observed via this path.

Only LED brightness (one byte, one message type) has been write-tested. This does **not** confirm SET
works uniformly for every settings field, for `status`/`version` (read-only messages — SET isn't even
meaningful there), or for `triggers` (write-only; still untested and still gated by the full guard
stack in `WriteGuard.cpp`/`DeviceSession.cpp`, which refuses factory reset and mode-switch regardless).

## 4. What stays unknown until hardware exists
* The product id/mode of the attached device and its interface list (F9/F10 give the
  tooling to find out safely).
* Whether any non-AC interface exists (option C).
* Whether the firmware accepts device-recipient class requests (option B) and which of
  request 3 / 0x85 / 0x05 it wants (`--request uac1`).
* Whether reading before the audio stream is open behaves differently.

## 5. Implemented safeguards
* `UsbOptions::method` defaults to `InterfaceRecipient`; `DetachAndClaim` requires the
  explicit option **and** `LEWITT_ALLOW_DISRUPTIVE=1` in the CLI; the GUI never sets it.
* `UsbTransport::write` refuses unless `allowWrites`, and independently of that never
  passes factory reset / mode switching (`checkWriteAllowed`).
* Reads are bounded (500 ms), serialised, cancellable between requests; a request that is
  already in the kernel completes within its timeout (libusb sync transfers cannot be
  interrupted — documented limit).
* Discovery and diagnostics use sysfs only.

## 3d. Phase 7A: descriptor-driven control-interface selection (not yet run on hardware)

The GUI previously opened the first class 01/01 interface (interface 0), which fails with EIO on a
real CONNECT 2 (§3b). `selectControlInterface()` (`src/transport/InterfaceSelection.cpp`) now
chooses the control interface from the descriptors, as a pure function:

1. VID/PID must be 29c2:0004 or 29c2:0002 (0x000A is named as a probable bootloader and refused).
2. Candidates are altsetting-0 interfaces with class `0xFF`, subclass `0xF0`, **no endpoints** —
   the signature recorded on the real unit (`ff/f0/00`). A generic vendor-specific interface is
   not assumed safe.
3. Class `0x01` (audio) and class `0xFE` (DFU) are always rejected.
4. Exactly one candidate must remain (zero → `NotFound` with the list of interfaces and reasons;
   several → refuses to guess).
5. The candidate must be *known* to be unbound (`libusb_kernel_driver_active == 0`, a read-only
   query); bound or unknown → `Busy`. No detach, no claim, no reset.

An explicit `--interface N` (CLI experiments) is still honoured, except that an interface whose
descriptor class is `0xFE` is refused. `openwitty-cli read … --interface auto` runs the same
selection as the GUI, so the selection can be checked read-only on the next hardware session.
Interface numbers are never assumed (3 is what the descriptors yield on the tested unit).
