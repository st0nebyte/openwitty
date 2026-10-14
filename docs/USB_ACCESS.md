# Linux access to the CONNECT 2 control protocol

Question: how can userspace issue the `MEM` class requests on the AudioControl
interface **without** detaching `snd-usb-audio` (i.e. without dropping audio)?

Evidence levels: **[src]** read in upstream kernel source (torvalds/linux
master, fetched during this session), **[bin]** macOS app disassembly,
**[hw]** needs the device — nothing here is hardware-verified.

## Findings

1. **ALSA mixer cannot do it. [src]** `sound/usb/mixer.c` only ever issues
   `UAC2_CS_CUR` / `UAC2_CS_RANGE` (and UAC1 GET/SET_CUR…). Its header lists
   "RANGE and MEM set commands (UAC2)" under *TODO*. `UAC2_CS_MEM` is defined
   but never sent. The CONNECT 2 protocol rides on `MEM` (bRequest 3) with
   wValue = `(1<<8)|messageId`, so no ALSA control element can map to it.
   Side note: the kernel builds wIndex as `interface | (unitID << 8)` — the
   same layout the official app uses, so the app is addressing a normal UAC
   unit (0x33), just via MEM instead of CUR.
2. **usbfs requires a claim for interface-recipient requests. [src]**
   `check_ctrlrecip()` → `checkintf()` → `claimintf()` →
   `usb_driver_claim_interface()`, which fails with `-EBUSY` for an interface
   bound to `snd-usb-audio`. So a libusb control transfer to the AudioControl
   interface *fails harmlessly* unless the driver is detached (disruptive).
3. **usbfs does not check device-recipient requests. [src]** The switch in
   `check_ctrlrecip()` only handles ENDPOINT and INTERFACE recipients; a
   class request with `bmRequestType = 0xA0/0x20` (recipient *device*)
   returns 0 without any claim. Likewise an interface-recipient request
   aimed at an *unbound* interface can be claimed by usbfs without touching
   the audio driver.
4. **Precedent for the kernel route. [src]** `sound/usb/mixer_scarlett2.c`
   (Focusrite) exposes a vendor protocol through a `snd_hwdep` device
   (`snd_hwdep_new(..., "Focusrite Control", ...)` + ioctls) plus ALSA
   controls — the accepted in-tree pattern for "vendor protocol on a class
   audio interface".

## Windows confirms the protocol shape  [V-bin]
The Windows build issues the same requests (entity 0x33, request 3 / 0x85 /
0x05, selector = API major, channel = message id) through
`TUSBAUDIO_AudioControlRequestGet/Set`; the Thesycon driver chooses the
AudioControl interface itself (details in PROTOCOL.md). On Linux the
equivalent of "ask the driver to send an AC request" does not exist in
`snd-usb-audio`, hence the analysis below.

## Three separate problems (do not conflate)
| Problem | Error seen | Cause | Fixable without root? |
|---|---|---|---|
| File permission on `/dev/bus/usb/BBB/DDD` | `EACCES` / `LIBUSB_ERROR_ACCESS` at *open* | node owned by root:root 0664 without a uaccess/udev rule | **No** — needs a system udev rule (or the desktop's `uaccess` tag). Check with `ls -l` first |
| Interface owned by `snd-usb-audio` | `EBUSY` / `LIBUSB_ERROR_BUSY` at *transfer* | kernel driver binding; usbfs claim refused (verified in devio.c) | Not by permissions. Only device-recipient/unbound-interface variants (untested) or a kernel change |
| Device rejects the request | `EPIPE` / `LIBUSB_ERROR_PIPE` (stall) | wrong bRequest/recipient/wValue for this firmware | Yes — it is a protocol question |

`UsbTransport` maps these to `Errc::AccessDenied`, `Errc::Busy` and
`Errc::Stall` respectively (unit-tested). Without root, only the *second* and
*third* rows can be investigated; if the first applies the user needs a udev
rule from an administrator (`packaging/udev/99-lewitt-connect2.rules`).
On Windows none of this arises because Thesycon's driver exposes the control
requests in its own API.

## What is *not* known  [hw]
* Whether the firmware accepts the same request with *device* recipient, or
  with a different interface number in wIndex's low byte (it may route purely
  on unit ID 0x33). This decides whether a no-disruption userspace path exists.
* Whether the device exposes any unbound interface (HID/DFU) at all.
* Which of UAC2-MEM(3) / UAC1 GET_MEM(0x85) the shipping firmware wants.

## Plan
1. When the device is attached, run `lewitt-cli probe` (dry run prints the
   matrix). `probe --execute` performs only device→host GETs of the read-only
   version message: AC interface (expected EBUSY), device recipient, and any
   *unbound* interface, each with request 3 and 0x85. No detach, no claim of
   bound interfaces, no writes.
2. If any variant returns 0x40 bytes: implement it as the default
   `UsbTransport` strategy and re-verify decoded values vs. the physical unit.
3. If none does: the smallest safe kernel-level solution is a
   `snd-usb-audio` quirk patch that sends MEM requests on behalf of userspace
   via a `hwdep` ioctl (read/write of messages 0/2/3/4, size-checked), modelled
   on scarlett2. Not started; only justified after step 1 fails.
4. The detach path (`--detach` + `LEWITT_ALLOW_DISRUPTIVE=1`, `UsbOptions::
   allowDetachKernelDriver`) exists only as an isolated last resort and is
   never used by the GUI.
