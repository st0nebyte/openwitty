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
| F9 | Here, 28 of 31 `/dev/bus/usb/*/*` nodes are `root:root 0664` → **no write access for the user** → `libusb_open()` fails with `EACCES` for a typical USB audio interface. | [sys] `ls -l`, `lewitt-cli`'s `access(2)` check on a real UAC2 device |
| F10 | `/sys/bus/usb/devices/*/descriptors` (raw device + configuration descriptors), `idVendor/idProduct`, and the `driver` symlink of each interface are world-readable → descriptors and driver ownership can be shown with no usbfs access and no I/O. | [sys] |
| F11 | Windows and macOS both issue exactly the request shape we use (entity 0x33, req 3 / 0x85 / 0x05, wValue = apiMajor<<8\|msg, 500 ms). On Windows the Thesycon driver chooses the interface; macOS passes `0x3300\|ifnum`. | [bin] PROTOCOL.md |

## 2. Three separate failure classes (never conflate)

| Symptom | Where | Cause | Fixable without root? |
|---|---|---|---|
| `EACCES` at `open` | node permission | usbfs node not writable by the user (F9) | **No** — needs a udev rule/ACL by an administrator |
| `EPERM` on an ioctl | read-only fd (F1) | node opened O_RDONLY | n/a (we always open O_RDWR) |
| `EBUSY` on transfer | kernel ownership (F2) | `snd-usb-audio` is bound | Not by permissions; see options below |
| `EPIPE` / stall | device | firmware rejected variant/recipient | Yes (protocol question) |

`lewitt-cli` reports each with its own code name and next step; the exit codes differ
(3 = access denied, 4 = busy, 5 = stall, 6 = timeout, 7 = disconnected/not found,
8 = unsupported, 9 = bad response).

## 3. Technical comparison

| Option | Works without disturbing audio? | Needs root? | Evidence | Verdict |
|---|---|---|---|---|
| **A. libusb, interface recipient, no claim** (default `--method interface`) | Does no harm, but **fails with `EBUSY`** while the AC interface is bound (F2) | udev rule for the node (F9) | [src] | baseline; useful to *prove* the failure mode |
| **B. libusb, device recipient** (`--method device-recipient`) | Not blocked by usbfs (F3). Whether the **firmware** accepts a class request addressed to the device is unknown | udev rule (F9) | usbfs [src]; firmware [hyp] | worth one read-only experiment on hardware |
| **C. Unbound interface** (probe lists them) | Claimable without touching audio, but UAC2 devices normally have **all** AC/AS interfaces bound by `snd-usb-audio`; a spare interface (HID/DFU) may not exist | udev rule | descriptors on a similar UAC2 device [sys] | only if the device exposes one; firmware routing [hyp] |
| **D. ALSA mixer / `amixer`** | n/a — MEM is not reachable (F4) | no | [src] | **not possible** |
| **E. hwdep interface in `snd-usb-audio`** (new quirk + ioctl `MEM_GET/SET`) | **Yes**: runs inside the driver that already owns the interface, no detach, no audio interruption | kernel patch + module build/install (root); once present, `/dev/snd/hwC*` is user-accessible via uaccess (F8) — no udev rule needed | precedent F6; quirk hook F5/F6 | **smallest correct solution**; recommended if B/C fail |
| **F. Detach + claim (usbfs)** (`--method detach`) | **No**: drops the whole sound card (F7) | udev rule | [src] | disruptive; isolated, opt-in, env-gated, never used by the GUI |
| **G. Unbind via sysfs / `USBDEVFS_DISCONNECT_CLAIM`** | **No**: same as F | root (sysfs) / udev rule | [src] | rejected |
| **H. usbmon / sniffing the vendor app** | Observation only | root | — | useful for *verification* on a machine with Windows/macOS, not for control |
| **I. Out-of-tree standalone kernel driver** | Competes with `snd-usb-audio` for the interface | root | — | rejected: duplicates the claim problem |

### Recommendation
1. On hardware, first run `lewitt-cli diagnose` (sysfs only) → learn PID, interfaces, driver
   ownership, node permissions without any I/O.
2. If the node is writable (admin installed the rule): `read api` (A), then B, then C —
   each read-only, each opt-in.
3. If none works (expected for A), propose **E**. Design sketch, *not implemented*:
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
