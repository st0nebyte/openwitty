# Phase 8A.11 — USB-core early-boot observer prototype

Research and offline prototype, 2026-10-23. **No CONNECT 2 request, ALSA mixer query, BPF load/attach, live trace, reboot, reconnect, boot-file edit, or audio-policy change occurred.** The prototype has no installed loader, boot service or automatic launch path. Read with [Phase 8A.10](PHASE8A10_EARLY_BOOT_TRACE_READINESS.md) and the [8A.9 reconnect record](PHASE8A9_RECONNECT_WRITER_ATTRIBUTION.md).

## Decision

**Prototype compiled; early-boot execution is NO-GO.** The source and installed BTF support `usb_control_msg()` as a promising built-in boundary that the Linux USB-audio mixer reaches. The prototype filters and copies only short Feature Unit 2 control values and has offline regression coverage. Compilation does not test fentry/fexit attachment, the kernel verifier, first-request coverage, the early-start ordering, or automatic detachment. No future boot trace should be attempted until those missing gates and independent physical-audio safeguards are separately approved and validated.

## Exact source path and installed evidence

The reviewed files are from CachyOS tag [`cachyos-7.2.10-1` `sound/usb/mixer.c`](https://github.com/CachyOS/linux/blob/cachyos-7.2.10-1/sound/usb/mixer.c), [`sound/usb/helper.c`](https://github.com/CachyOS/linux/blob/cachyos-7.2.10-1/sound/usb/helper.c), and [`drivers/usb/core/message.c`](https://github.com/CachyOS/linux/blob/cachyos-7.2.10-1/drivers/usb/core/message.c). Their source was inspected outside the tracked repository. As established in [8A.5](PHASE8A5_KERNEL_BUILD_PROVENANCE.md), the installed module matches its signed package, while the historical source-to-running-binary link remains high confidence, not byte-proven.

| Layer | Source observation |
|---|---|
| UAC2 mixer read, `mixer.c` `get_ctl_value_v2()` | `GET_CUR` or `GET_RANGE` calls `snd_usb_ctl_msg()` with `usb_rcvctrlpipe(dev,0)`, class/interface/IN, selector/channel as `wValue`, entity/interface as `wIndex`, and stack-buffer sizes 2 or 8 for the relevant master volume control. |
| UAC2 mixer write, `mixer.c` `snd_usb_mixer_set_ctl_value()` | UAC2 `SET_CUR` calls `snd_usb_ctl_msg()` with `usb_sndctrlpipe(dev,0)`, class/interface/OUT and a 1–4 byte local buffer; Feature Unit 2 master volume is 2 bytes. Failed non-timeout transfers may be retried. |
| Audio wrapper, `helper.c` `snd_usb_ctl_msg()` | Checks pipe type, allocates a temporary buffer with `kmemdup(data,size)`, calls `usb_control_msg(dev,pipe,request,requesttype,value,index,buf,size,timeout)`, copies that buffer back to caller and frees it, then calls a quirk helper. This confirms the mixer request reaches the USB-core candidate on the inspected path. It also means a USB-core hook sees the temporary heap buffer, not the mixer's original stack buffer. |
| USB core, `message.c` `usb_control_msg()` | Nine arguments match the candidate BTF signature. It builds a control setup packet, calls `usb_internal_control_msg()`, then frees the setup packet and returns its result. The internal helper uses a synchronous URB wait and returns a negative error or the actual byte count. Thus the data buffer supplied by `snd_usb_ctl_msg()` remains allocated through both function entry and return. USB-core fexit occurs before the wrapper copies to its caller or runs its later quirk hook. Other USB-core callers may use the same boundary; strict filtering is essential. |

Read-only installed metadata: `CONFIG_USB=y`, `CONFIG_SND_USB_AUDIO=m`, `CONFIG_LTO_CLANG_THIN=y`. `/proc/kallsyms` lists `usb_control_msg` as an unexported `t` symbol in the kernel image and `snd_usb_ctl_msg` in `[snd_usb_audio]`; addresses are zeroed by existing pointer policy. `bpftool btf dump file /sys/kernel/btf/vmlinux format raw` (BTF inspection only; **not** `feature probe`) reports `FUNC 'usb_control_msg'` BTF ID **145197**, `linkage=static`, prototype ID **65101**, returning `int` with arguments `struct usb_device *`, `unsigned int`, `u8`, `u8`, `u16`, `u16`, `void *`, `u16`, `int`. These types and order match the tagged source. BTF and symbol presence do **not** prove fentry attachment to this local symbol will be accepted by the running kernel. The installed `usb_device` BTF exposes descriptor VID/PID and bus/device numbers; the prototype reads only those fields.

The prior [8A.10 boot timeline](PHASE8A10_EARLY_BOOT_TRACE_READINESS.md#installed-boot-path-and-attachment-window) places initramfs switch-root at 4.477 s and the sticky warning at 11.414 s. The inspected initramfs omits `snd-usb-audio`. A built-in USB-core hook *could* exist before audio-module load; the USB-core component itself is built into this kernel. Startup of any observer before the first mixer request remains unproven.

## Prototype files and behavior

- [`fu2_usb_core_trace.bpf.c`](../tools/diagnostics/fu2_usb_core_trace.bpf.c): fentry/fexit hooks at `usb_control_msg`; fixed 128-entry inflight hash keyed by current PID/TID; 64 KiB ring; 256 matching-event production cap and a loss counter. No USB or ALSA sending API. The BPF source is not auto-loaded or attached.
- [`fu2_usb_core_shared.h`](../tools/diagnostics/fu2_usb_core_shared.h): fixed-size short record, with entry/exit monotonic timestamps, transient correlation ID, setup fields, result, requested length, bus/address, data-validity flags and at most eight data bytes. Device and data pointers remain map-only, never emitted. No serial, descriptor body, stack or task name is retained.
- [`fu2_usb_core_logic.h`](../tools/diagnostics/fu2_usb_core_logic.h): filter, Q8.8 decode, pending-record comparison, payload/result classification, time/event/loss policy, shared with synthetic tests.
- [`test_fu2_usb_core_offline.c`](../tools/diagnostics/test_fu2_usb_core_offline.c): deterministic host-only regression; CMake registers `fu2_usb_core_offline` CTest. Existing reconnect observer files remain untouched.

The filter checks control pipe endpoint zero, **VID:PID `29c2:0004`**, class/interface `0xa1` IN or `0x21` OUT, `wIndex=0x0200` (Feature Unit 2 on AudioControl interface 0), `wValue=0x0200` volume or `0x0100` mute (master channel 0), and exact expected UAC2 CUR/RANGE request/length. It rejects other channels by design; a later capture would need separate review before broadening. It rejects entity `0x33`, other interfaces, request types, endpoint types and USB devices **before payload copying**. No isochronous audio path reaches this control-message hook. Volume SET is copied at entry; volume/mute GET and volume RANGE are copied at return **only when the returned byte count equals the requested length**. A negative or short transfer emits status with no invented return payload. A successful SET reports an attempted value plus USB completion, not verified analogue level.

The synchronous source path makes buffer lifetime plausible. It does not guarantee that BPF can read the pointer: live verifier/attach success and `bpf_probe_read_kernel` result remain untested. Constant 1-, 2- and 8-byte copies limit verifier complexity. The fexit hook checks the original device/data pointers and request fields against the matching entry record. A duplicate TID entry, missing/mismatched exit, map exhaustion or ring overflow increments the loss counter rather than silently overwriting a record. Different TIDs can coexist; nested same-TID calls intentionally fail closed. A loader must inspect remaining map entries at stop to detect unmatched entries. Kernel worker identity would still not identify a userspace policy originator.

### Offline evidence

Using the installed `vmlinux.h` and Clang BPF target, `fu2_usb_core_trace.bpf.c` compiled with `-O2 -g -Wall -Wextra -Werror`; `readelf` identified a Linux BPF ELF64 object with both expected fentry/fexit sections. Offline `bpftool gen skeleton` succeeded and found the intended maps and programs; it did **not load anything into the kernel**. `fu2_usb_core_offline` passed **55/55 synthetic assertions**; both FU2 CTests passed **2/2**. Cases include target/wrong VID-PID, interface/entity/endpoint/request filtering; −4, −60, −3.5 and 0 dB values; hypothetical stale versus measured changed readback; saved-value restore and failed restore; a two-read sticky pattern and max fallback; volume RANGE; mute GET/SET; negative and short transfers; bad payload; pointer correlation, two concurrent pending records, missing return/loss policy, device-address change, timestamp checks and 120 s/256-event boundaries. These test pure logic and record classification, **not** kernel map behavior, BPF verifier acceptance or USB timing. The earlier reconnect trace supplies real reference values but is not replayed through the live hook here.

Reproducible offline commands (outputs belong in an ignored build directory):

```sh
clang -O2 -g -Wall -Wextra -Werror -target bpf -D__TARGET_ARCH_x86 \
  -Wno-missing-declarations -I/usr/lib/modules/$(uname -r)/build \
  -c tools/diagnostics/fu2_usb_core_trace.bpf.c \
  -o build-release/fu2_usb_core_trace.bpf.o
bpftool gen skeleton build-release/fu2_usb_core_trace.bpf.o \
  > build-release/fu2_usb_core_trace.skel.h
cmake --build build-release --target fu2_usb_core_offline
ctest --test-dir build-release -R 'fu2_(observer|usb_core)_offline' --output-on-failure
```

These commands compile or inspect files only. Do **not** substitute `bpftool feature probe kernel`, `bpftool prog load`, or an attaching loader.

## Early startup and stop design

| Approach | Coverage, required work and timing risk |
|---|---|
| Bootconfig/ftrace dynamic probe at built-in `usb_control_msg` | [Kernel boot-time tracing](https://docs.kernel.org/trace/boottime-trace.html) can start after `core_initcall`, before the observed audio-module load. However, this eBPF fentry object is **not** a bootconfig ftrace event. A separate kprobe/fprobe specification must prove it can fetch a short OUT buffer at entry and the correct IN bytes at return, correlate pairs, filter to the target and stop safely. That has not been designed/tested to the required standard. Treat this option as insufficient today, not a substitute for the BPF prototype. |
| Minimal initramfs or early real-root eBPF loader at built-in USB core | The strongest *candidate* for this exact prototype. An initramfs loader might attach before switch-root, while the audio module is absent from the inspected image. It needs a reviewed handoff that keeps links/reader/watchdog alive across switch-root. An early real-root loader might start before the observed 11.414 s probe, but a normal systemd ordering declaration alone cannot guarantee it beats udev's module auto-load. **Do not hold back, blacklist or reorder `snd-usb-audio`** merely to make the race easier: that could change the bug. Neither loader nor startup unit is implemented or installed here. |

The BPF map's 256-event limit bounds retained matching records; it does **not detach hooks**. The ring bounds memory at 64 KiB; the pending map at 128 entries. A future privileged loader must poll ring and loss counter, check pending-map emptiness, stop on target errors or malformed data, enforce a monotonic **120 s hard deadline**, and destroy only its own BPF links on exit. A supervisory watchdog (for example a separately approved transient systemd unit with `RuntimeMaxSec` and `KillMode=control-group`) must enforce process termination if the reader hangs; file-descriptor closure then removes non-pinned links. If an initramfs handoff pins links, that crash-cleanup assumption no longer holds and a separately proven unpin watchdog is required. No such loader/watchdog is built or active: **the automatic-stop and cleanup gate is presently NO-GO**. No bootconfig, Limine entry, initramfs or service was changed.

The fixed event filter minimizes privacy exposure; BPF still executes at every `usb_control_msg` entry/exit to reject unrelated requests. This can alter scheduling and perhaps the critical SET-to-GET gap. Minimal value copying is preferable to stack capture or global function graph tracing, but only a controlled measurement can quantify interference. An instrumented boot without a sticky warning would not prove that the uninstrumented bug disappeared.

## Execution-readiness gate

“GO” below means source/build evidence and offline tests support that *part* of the design, not approval for a live trace.

| Requirement | Rating | Evidence or missing gate |
|---|---|---|
| USB-core boundary exists before audio-module loading | **GO** for code location | `CONFIG_USB=y`, `CONFIG_SND_USB_AUDIO=m`, installed BTF and inspected initramfs. Its exact early attachability remains conditional. |
| Installed signature compatible | **GO** for types; **CONDITIONAL** for attach | Nine-argument BTF/source match; no live verifier/fentry test. Static/local symbol and ThinLTO can matter. |
| Exact submitted SET bytes | **CONDITIONAL** | Entry copy implemented, source buffer lifetime supported, BPF compiled; live pointer-read/verifier result untested. |
| Exact returned GET bytes | **CONDITIONAL** | Return copy after full-length synchronous completion implemented; live pointer-read/verifier result untested. |
| Target filter before payload copy | **GO** in source | Shared field/VID-PID checks precede every short copy; 52 synthetic assertions include excluded traffic. |
| Pair requests without ambiguity | **CONDITIONAL** | TID-keyed map plus full request/pointer match; actual nesting, lost events and final map inspection need a loader/live validation. |
| Start before first mixer request | **NO-GO today** | No approved installed early loader; no guaranteed ordering without changing driver loading. |
| Stop automatically | **NO-GO today** | In-kernel event/memory cap exists, but no live timeout, reader, watchdog or link cleanup implementation. |
| Pointer restrictions preserve essential evidence | **CONDITIONAL** | Payload design uses BTF and short kernel reads, not symbolized stacks; live access remains untested. |
| Avoid delaying driver initialization | **CONDITIONAL** | No module-load order change is proposed; any observer itself adds overhead and startup must be proven without gating udev. |
| Remove tracing cleanly | **NO-GO today** | No experiment-owned loader/link lifecycle or post-stop verification exists. |
| Physical audio safety | **NO-GO today** | Kernel sticky path may attempt 0 dB. A separate approved safety procedure must protect listeners/amplification without relying on software or hardware mute. |

**Recommended next action:** design and offline-review an early loader/watchdog and prove its ordering without delaying the existing audio-module trigger, then validate attachability, payload reads, caps and cleanup in a separately approved non-target runtime environment if available. If a guarantee cannot be obtained without altering module timing, retain NO-GO for a normal-boot experiment and reassess a different narrow mechanism. A future CONNECT 2 boot observation requires its own explicit approval for privileged boot configuration, one reboot, trace activation, physical audio protection, rollback and verification. No Phase 8B hardware writes are enabled or justified by this prototype.
