# Phase 8A.19 — boot-time USB endpoint-0 request trace

**Status: PREPARED — boot not yet performed** (checkpoint written 2026-10-23 ~23:00 CEST).
Authorization: the user authorized USB tracing and one controlled diagnostic boot for this project; the
physical-audio safety confirmation immediately before shutdown remains required.
If this chat is gone after the shutdown: read this file, `PHASE8A17_*.md` and `PHASE8A18_*.md`, check
`git status`, then follow "Resume after boot". Do not start a second boot because a session was interrupted.

## 1. Goal
Capture the exact USB SETUP packet, submission time, completion time and status of the control request
that times out inside the first `snd-usb-audio` probe (Phase 8A.18: `(udev-worker)`, EP0 OUT, 0/0,
timeout line at 11.347 s, ≈69 ms before the FU2 sticky line), and the control traffic that follows
(FU2 SET/GET). `SET_INTERFACE` is a hypothesis, not an assumption.

## 2. Selected capture architecture
**Kernel-native kprobe events defined on the kernel command line and recorded into the in-memory ftrace
ring buffer.** No userspace reader, loader or service takes part in the capture.
- `kprobe_event=` (exact tag `cachyos-7.2.10-1`, `kernel/trace/trace_kprobe.c`): commas become spaces,
  `;` separates probes, events are created and **enabled at boot** (`setup_boot_kprobe_events`, run from
  `init_kprobe_trace`, an `fs_initcall`), i.e. before device probing; USB enumeration of the target starts at
  ≈2.0 s and the suspect request at ≈6.35 s. `dyndbg` is applied at `early_initcall`.
- Four probes (single source of truth: `PROBES` in `tools/diagnostics/usb_ep0_trace.py`):
  `uw`  entry of `usb_start_wait_urb` — the only function that waits on a control URB (called from
        `usb_control_msg`, hence from `usb_set_interface`, `snd_usb_ctl_msg`, descriptor reads, hub
        requests; one emitted copy: the single `message.c:88` dyndbg entry, and return
        addresses inside `usb_control_msg`): the 8 SETUP bytes (`urb->setup_packet`), pipe, timeout, device
        number, bus number, VID/PID (via `urb->dev`), and the first 8 bytes of the OUT buffer;
  `uwr` return of the same function: status (`$retval`), actual length, and the first 8 bytes of the IN
        buffer (entry argument `$arg1` preserved for the return probe);
  `si`/`sir` entry/return of `usb_set_interface` (device, VID/PID, interface, alternate, result) as an
        independent decode of the suspected call.
  Struct offsets come from the installed `7.2.10-1-cachyos` BTF (`pahole`): `urb.dev=64, pipe=80,
  transfer_buffer=96, transfer_buffer_length=136, setup_packet=144; usb_device.devnum=0, bus=80,
  descriptor=968 (idVendor 976, idProduct 978); usb_bus.busnum=16`.
- `trace_buf_size=8M` per CPU (16 CPUs → 128 MiB, bounded; overwrite mode; loss is detectable from the
  trace header `entries-in-buffer/entries-written`). Default `local` trace clock = `sched_clock`, the
  clock `printk` uses, so trace timestamps correlate directly with the kernel log.
- `dyndbg="file drivers/usb/core/message.c line 88 +p"` (verified in Phase 8A.18) is added so the existing
  `timed out or killed` line is logged in the same boot as an independent tie-in.
- Privacy: the probes record SETUP fields, bus/device numbers, VID/PID and ≤8 payload bytes in memory only.
  The raw buffer is read by root, stored outside Git (`~/.local/state/openwitty/trace-8a19/`, mode 0600/0700),
  and only a target-only sanitized extract (no descriptor payloads, no serial strings, no other devices) is committed.

## 3. Rejected alternatives
| Option | Why inferior here |
|---|---|
| A. xHCI tracepoints (`trace_event=xhci-hcd:…`) | Setup stage appears only inside decoded TRBs of *every* ring; no device/VID:PID context on TRB events; cannot be filtered on the command line (filters need bootconfig = initramfs change); high-rate HID/hub URBs flood the buffer. |
| B. `usbmon` | `CONFIG_USB_MON=m`: needs `modprobe` and a root reader started by a unit; real root starts at ≈5.2 s, suspect request ≈6.35 s → ≈1 s race, and a persistent unit file; bus-wide capture includes unrelated traffic. |
| C. BPF (Phase 8A.11/8A.15 line) | Loader would also start at ≈5.5 s after real-root; verifier/attach/payload reads never validated live; extra userspace lifecycle. |
| `usb_control_msg`-only probe | Would miss any inlined/internal user; `usb_start_wait_urb` is the single choke point (probe on `usb_control_msg` is kept out to avoid duplicate records; its args were validated equal on the live kernel). |

## 4. Runtime validation done (this machine, non-target)
Probes were defined through tracefs with the exact strings, enabled in a private instance and in the top
buffer while the system was idle; traffic seen was the kernel's own root-hub status polling (`1d6b:0003`).
No request was sent to CONNECT 2. Verified: probe syntax accepted incl. stack arguments and return-probe
entry arguments; the decoded SETUP of ≈100 requests agrees with the independent `usb_control_msg` argument
probe (`bmRequestType=0xa3, bRequest=0, wValue=0, wIndex=1, wLength=4`); entry/return pairing by task;
IN data appears at return; end-to-end `collect` stored the raw trace with mode 0600, removed only the four
`owtrace` probes and the group directory, and `verify-clean` reported clean (`kprobe_events` empty, no instances,
no `set_event`). A bug found and fixed: Python's `open(...,"a")` made the kernel reject probe removal (EINVAL);
the tool now uses a plain `O_APPEND` write. Offline tests: `tools/diagnostics/test_usb_ep0_trace.py`
(40 checks: SETUP/`SET_INTERFACE` decode, direction, pairing, five-second timeout, killed URB, missing completion,
malformed setup/line, buffer overwrite vs normal, second device, descriptor payload never exported, FU2 timeline,
debug-message tie, Limine entry add/remove round-trip, collect/cleanup on a fake tracefs). Not testable offline:
the boot-time `kprobe_event=` creation itself (will be confirmed by the boot).

## 5. Lifecycle and bounds
Memory is fixed by `trace_buf_size`; tracing ends at reboot; nothing persists in the kernel. No userspace
process exists during capture, so no reader or watchdog can hang. `collect` is idempotent and re-runnable:
disable only `events/owtrace` → write raw trace → remove only our four probes → restore buffer size → verify.
If it is killed midway the state is still `owtrace` events/probes (re-run `collect`, then `verify-clean`).
No BPF link, pin, or service exists. The cost is a few microseconds per control request on two functions.

## 6. Boot entry (temporary, reversible)
Typing a ≈560-character argument in the bootloader editor is unreliable (the earlier `=` failure), so a temporary
second entry is added to `/boot/limine.conf` (root, atomic replace). It copies the `linux-cachyos` entry's
`protocol/path/module_path` (including their BLAKE2 hashes) and `cmdline`, **appends only** the three
arguments above, and contains no `usbcore.quirks`. Verified read-only beforehand: the installed Limine EFI file
is byte-identical to the package's pristine binary (no config hash enrolled); `default_entry: 2` still selects
`linux-cachyos` (the new entry is last); `remember_last_entry: yes` means the *last booted* entry is remembered,
so the entry is **removed after the run**. The block is delimited by `# OPENWITTY-8A19-BEGIN/END`; removal
deletes exactly that block (round-trip on the real file is byte-exact; later edits by `limine-snapper-sync` survive).
A pre-change copy is kept at `/var/lib/openwitty-8a19/limine.conf.before`.
Commands: `sudo python3 tools/diagnostics/usb_ep0_trace.py entry install|status|remove`.

## 7. Procedure (user)
Safety confirmation required immediately before shutdown: playback/recording stopped; speakers/amplifiers disconnected, off
or independently safe; headphones/listeners protected; no reliance on KDE (40%) or CONNECT 2 mute; these instructions
retained; exactly one trace boot intended. The kernel's own mixer probe can still write FU2 volume (possible 0 dB fallback).
1. Normal full shutdown (not reboot). CONNECT 2 stays in its port.
2. Power on manually. At the Limine menu press a key to stop the countdown, select **"OpenWitty USB trace (temporary)"**
   with the arrow keys (no typing, no editor) and press Enter. If it is missing or anything is unclear: boot the normal
   entry and report. If the trace boot fails: power-cycle once and choose the normal `linux-cachyos` entry.
3. After login say "continue Phase 8A.19".

## 8. Resume after boot (agent)
Check `git status`; confirm `/proc/cmdline` has the three arguments and no `usbcore.quirks`; `dmesg` shows no
"Failed to add event"; `sudo ... usb_ep0_trace.py collect --out-dir ~/.local/state/openwitty/trace-8a19`;
`analyze` the raw file with the saved `timed out or killed` lines (`--dmesg`); read FU2/ALSA/PipeWire state passively;
`entry remove` + `entry status`; `verify-clean`; sanitized extract into this document; commit/push.
Evidence required: exact SETUP of the first timed-out EP0 OUT request, submit/complete times and status, the tie to
the 11.347 s debug line, what follows the timeout, the FU2 SET/GET sequence (stale/changing readback, extrema,
restore, mute), whether the WirePlumber timeout is the same request type, any loss.

## 9. Interpretation rules
Transport failure = which request timed out. Device readiness = a host-visible request with no completion for the full
timeout cannot by itself distinguish a device-side non-response from a host-controller problem. FU2 behavior = the actual
boot-time SET/GET values. Causality = timeout *precedes* the sticky verdict; causation is not shown by order alone.
A trace that identifies the first timed-out request is a successful result even if the root cause stays unknown.
Missing records, overwritten buffer, unpaired events or a failed `kprobe_event` creation make the run inconclusive.

## 10. Status table
| Item | State |
|---|---|
| Probe syntax on live kernel | verified |
| Decode/pairing/timeout/cleanup logic | 40 offline checks pass |
| Boot-time creation of `kprobe_event=` | **unverified until the boot** |
| Start before first USB enumeration / before ≈6.35 s | by initcall order (`fs_initcall`); confirmed by trace content after boot |
| Temporary Limine entry | installed only after this checkpoint is pushed |
| Boot performed | **no** |
