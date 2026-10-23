# Phase 8A.19 — boot-time USB endpoint-0 request trace

**Status: attempt 1 EXECUTED — PARTIAL (ring buffer too small; see "Attempt 1 results"). No automatic retry.**
(Original preparation checkpoint written 2026-10-23 ~22:25 CEST.)
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

---

# Attempt 1 results (boot 2026-10-23 22:31 CEST; recorded ~22:50 CEST)

## Procedure record
User re-confirmed safety points 1–6, did one normal shutdown (22:30) and one manual power-on (22:31),
selected the temporary entry from the menu (no typing). `/proc/cmdline` carried the three added arguments and no
`usbcore.quirks`; `kprobe_event=` creation at boot **worked**: all four events existed and were enabled at login,
no "Failed to add event" in the kernel log. The boot showed the usual failure (FU2 sticky at 11.417 s, no ALSA
`PCM Playback Volume`, `route.hw-volume=false`, one sink, `usb_set_interface failed (-110)` at 17.493 s).
After the run: `collect` saved the raw trace outside Git (mode 0600 in `~/.local/state/openwitty/trace-8a19/`),
removed only the four probes, `verify-clean` = clean, `tracing_on` restored to 1; the temporary Limine entry was
removed and `limine.conf` is byte-identical to the pre-change backup (default entry unchanged).

## What went wrong: the ring buffer was never expanded
After login `buffer_size_kb` read `7 (expanded: 8192)` and the header `entries-in-buffer/entries-written` was
885/4136 (later 1034/4670): `trace_buf_size=8M` alone does **not** enlarge the global buffer at boot; it stays at
its minimum (≈7 KB per CPU, ≈100 events) until something expands it. `kernel/trace/trace.c` (exact tag):
`set_cmdline_ftrace()` — "We are using ftrace early, expand it" — i.e. the `ftrace=` parameter (e.g. `ftrace=nop`)
expands the buffer. Consequence: most records between ≈5.8 s and ≈11.4 s were overwritten per CPU. The tool now adds
`ftrace=nop`, and `collect` warns when the buffer was never expanded. Classification of this run: **partial** — start
coverage by the probes was sufficient (events exist from 0.78 s) but retention was not, so the submission of the first
timed-out request was lost.

## Captured evidence (sanitized; timestamps are the trace clock = printk clock; target = CONNECT 2 only)
1. **The later WirePlumber timeout — complete.** Submit 12.407676 s, complete 17.493069 s, elapsed 5.085 s, task
   `wireplumber`, status −110 (ETIMEDOUT). SETUP: `bmRequestType=0x01, bRequest=0x0b, wValue=0x0000, wIndex=0x0002,
   wLength=0` = **standard `SET_INTERFACE`, interface 2, alternate 0**. The independent `usb_set_interface` probe
   returned −110 from `endpoint_set_interface` at 17.4957 s. The dynamic-debug line (17.491078) ties to it within 2 ms.
   17.5277 s: the same `SET_INTERFACE(2, alt 0)` completes in 0.123 ms.
2. **The first (udev-worker) timeout — `usb_set_interface` call fully captured, control-URB records lost.**
   (Corrected after re-analysis with the new `usb_set_interface` pairing; the first write-up of this run wrongly said
   the entry was lost and the interface only inferred.) The `si`/`sir` probes of task `(udev-worker)`-563 on one CPU
   survived: `si iface=1 alt=1` at 5.798512 s → `sir ret=0` at 5.842917 s (caller
   `__snd_usb_parse_audio_interface+0xdb6`, 44 ms); then **`si iface=1 alt=0` at 5.842918 s → `sir ret=−110` at
   11.351722 s** with return address `snd_usb_create_stream+0x189`, i.e. the `card.c:320` "reset the current interface"
   call (binary: `xor %edx,%edx; call usb_set_interface` right after `snd_usb_parse_audio_interface` returns 0).
   So the first stalled request is **SET_INTERFACE, interface 1, alternate 0, measured** (call duration 5.509 s).
   Its SETUP follows from the arguments: `bmRequestType=0x01, bRequest=0x0b, wValue=0x0000, wIndex=0x0001,
   wLength=0` — **derived, not observed**: the `uw`/`uwr` records of the control URB were overwritten. The
   dynamic-debug "timed out" line at 11.347443 puts the URB submission at ≈6.347 s (11.347 − 5.000), i.e. about
   **0.504 s after the call began** (5.843 s) — time spent inside `usb_set_interface` before the control request
   (LPM handling and the xHCI bandwidth/endpoint reconfiguration step); the equivalent prefix before WirePlumber's
   stalled call was 29.7 ms (12.378 → 12.408 s). This ≈0.5 s prefix is a candidate for the ≈0.5 s step between the two
   sticky-time groups of Phase 8A.18 (Group A ≈10.92 s vs B ≈11.43 s): 5.843 + prefix + 5.0 + ≈0.07 s reproduces
   11.42 s for a 0.5 s prefix and ≈10.9 s for none — a hypothesis from one measured case, not shown.
   The dynamic-debug line (11.347443) is 4.3 ms before the `sir` return at 11.351722.
   **This corrects Phase 8A.17's candidate list:** the stalled call is the `card.c:320` reset call, not one of the
   four `stream.c:1261–1267` calls (those completed in 0.6 ms and 44 ms).
3. **Earliest udev-worker probe traffic that survived (5.7958–5.7978 s):** clock-source `GET_RANGE`
   (entity 0x29, `wValue 0x0100`, `wLength 2`), `SET_INTERFACE(1, alt 0)` (0.6 ms, OK), two `GET_CUR` of the
   sample-rate control — each 0.56–0.75 ms and successful. Between 5.798 s and the timeout nothing survived.
   The timed-out request was submitted about 6.35 s (11.347 − 5.0).
4. **FU2 tail (11.418–11.419 s), after the stall:** `SET_CUR volume 0 dB` (ret 0, 0.379 ms) → `GET_CUR volume` =
   **−4.00 dB** (0.371 ms) → `SET_CUR volume 0 dB` (ret 0, 0.372 ms). I.e. the readback stayed at the earlier value
   after a 0 dB write (the sticky pattern), and the last attempted write is 0 dB with no restore in the surviving
   records (earlier records of the sequence are lost). The probe then walks unit 5: mute/volume `GET_CUR`, three
   `GET_RANGE`, several `SET_CUR`; then **20 consecutive `SET_CUR` volume writes to unit 5 channels 3 and 4 fail with
   status −32 (EPIPE/STALL)**, each ≈0.37 ms (10 tries per channel). FU2 requests take 0.37–0.38 ms each, so after
   the stall the device answers at normal speed.
5. **Later userspace traffic:** `wireplumber` (12.378 s mute `GET_CUR`, then the stalled `SET_INTERFACE(2,0)`), then at
   17.53–17.85 s a series of fast `SET_INTERFACE` (2/1, alt 0/1) and sample-rate/volume `GET_CUR`; `pipewire` at
   22–27 s likewise. No further timeouts.

## Physical audio observations
User: "some minor speaker crackle, nothing bad, twice, almost inaudible, might have been the volume unmute or so".
GET-only snapshot at 22:35:40 CEST (after the run): Speaker −4.00 dB, unmuted; Headphone −32.40 dB, muted
(identical to the earlier snapshots). The kernel's two `SET_CUR 0 dB` writes (item 4) were accepted at USB level but
the hardware volume did not change, so the crackle cannot be attributed to a level change; a coincidence in time with
those two writes is possible but unmeasured and subjective.

## Interpretation
- **Transport failure:** the stall is a **`SET_INTERFACE` (alternate 0) control transfer that receives no completion
  for the full 5 s timeout**, once in the probe (udev-worker, `usb_set_interface(1, 0)`, measured by the call-entry probe) and
  once from userspace (WirePlumber, interface 2). Observable at the host-controller wait; it is not an
  error response (a STALL would return −32 quickly).
- **Device vs host:** a request that is neither completed nor stalled is consistent with a device that does not
  answer (or a controller that does not complete it); this data cannot separate the two. The same request type
  later completes in 0.12 ms, so it is not a permanent inability.
- **Causality:** the timeout precedes the FU2 sticky verdict by ≈66 ms; no evidence here shows that it causes it.
  The reconnect path runs the same code without a stall and accepts FU2.
- **Not established:** the SETUP bytes of the first stall as observed (derived only); the FU2 prefix; what the ≈0.5 s before the control URB consists of.

## Next
No automatic retry. A second attempt would be the same entry plus `ftrace=nop` (already in the tool) — it would
capture the submission and the SETUP of the first stalled request and the complete FU2 sequence; it needs a new
physical shutdown/power-on and the same safety confirmation.

---

# Attempt 2 — PREPARED (2026-10-23 ~23:35 CEST)

**Status: prepared, not yet executed. The user authorized one additional controlled trace boot; the physical
audio-safety confirmation immediately before shutdown is still required. No third attempt follows automatically.**

## Why attempt 1 was partial
The probes and boot-time events worked, but the global ring buffer stayed at its ≈7 KB/CPU minimum
(`buffer_size_kb` read `7 (expanded: 8192)`), so roughly 100 events per CPU survived. Retained by luck: the complete
`usb_set_interface` timeline of the udev worker (its CPU was quiet) and the tail of the FU2 sequence; lost: the
control-URB records of the first stall (observed SETUP and exact submit time), the mute/range/first-GET prefix of FU2.

## New boot arguments (exactly, appended to the unchanged normal arguments of the `linux-cachyos` entry)
`kprobe_event=<four probes, unchanged>` `ftrace=nop` `trace_buf_size=8M` `dyndbg="file drivers/usb/core/message.c line 88 +p"`.
No `usbcore.quirks`. `ftrace=nop` appears exactly once (offline test). Mechanism (exact tag, `kernel/trace/trace.c`):
`set_cmdline_ftrace()` runs `trace_set_ring_buffer_expanded(NULL)` ("We are using ftrace early, expand it") and
`tracer_alloc_buffers()` then allocates `trace_buf_size` instead of the 1-page minimum. `nop` is the do-nothing tracer.
**Expected after boot:** `buffer_size_kb` = `8192` per CPU (not `7 (expanded: …)`), 16 CPUs → 128 MiB.

## Tool changes (offline, 61 tests)
- `collect` records the pre-collect buffer text, `expanded` verdict, header counts and **per-CPU `stats`** (entries,
  overrun, commit overrun, oldest event ts) into `collect_report.json` (mode 0600), restores `buffer_size_kb` to the
  **expanded size recorded before the boot** (baseline saved by `entry install` in
  `/var/lib/openwitty-8a19/tracefs_baseline.json`, not a hard-coded value), restores `tracing_on`, and `verify-clean`
  compares tracing_on/tracer/set_event/kprobe_events/instances/buffer with that baseline.
- `analyze --report collect_report.json` gives a strict **COMPLETENESS** verdict: INVALID if the buffer was not
  expanded, any CPU overrun, header loss, or coverage starts after 3 s; PARTIAL if the first timed-out request's
  `uw`/`uwr` records, any entry/return pairing, or the FU2 prefix (must start with a GET and contain a volume SET) is
  missing; COMPLETE only when none of these applies. A decoded request list alone is never accepted as complete.
- `usb_set_interface` entry/return pairing (interface, alternate, caller, duration); the earliest timeout is chosen
  across requests and calls; a SETUP derived from call arguments is labelled "derived, not observed".

## Verification and abort conditions after boot
Proceed only if `/proc/cmdline` has the arguments and no `usbcore.quirks`; four `owtrace` events exist and are enabled;
`buffer_size_kb` is expanded. If the buffer is still unexpanded: save what remains, mark PARTIAL, do **not** retry.
Abort the whole plan (boot the normal entry) if the temporary entry is missing or the menu behaves unexpectedly.

## Cleanup and rollback
`collect` (saves raw outside Git, removes only the four probes, restores recorded buffer/tracing state, verifies),
then `entry remove` (exact block removal) and `entry status`; `limine.conf` must equal the pre-change backup unless
`limine-snapper-sync` changed it meanwhile. Fallbacks if the trace boot fails: power-cycle once and choose the normal
`linux-cachyos` entry (the LTS entry and snapshot entries exist but are not claimed to have been tested).

## Audio safety
The kernel's probe can write FU2 volume (0 dB fallback seen); attempt 1 produced a very quiet, subjective crackle with an
unchanged GET-only snapshot afterwards (Speaker −4.00 dB unmuted, Headphone −32.40 dB muted), which cannot exclude a
transient. Playback and recording stopped; speakers/amplifiers disconnected, off or independently safe; headphones and
listeners protected; no reliance on KDE (40%) or CONNECT 2 mute; no deliberate level/mute change.

## Resume after boot
`git status`; `/proc/cmdline`; `buffer_size_kb`; `sudo python3 -I tools/diagnostics/usb_ep0_trace.py collect --out-dir
~/.local/state/openwitty/trace-8a19-attempt2`; `analyze … --report …/collect_report.json --dmesg <timeout lines>`; passive
FU2/ALSA/PipeWire state; one GET-only snapshot; `entry remove` + `entry status` + `verify-clean`; document; commit/push.
