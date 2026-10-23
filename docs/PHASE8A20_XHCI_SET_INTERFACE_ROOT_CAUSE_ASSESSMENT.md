# Phase 8A.20 — xHCI view of the stalled SET_INTERFACE, and ALSA quirk assessment

**Status: PREPARED — xHCI trace boot (attempt 3) not yet performed** (checkpoint 2026-10-23 ~23:55 CEST).
All sources are the exact tag `cachyos-7.2.10-1` (cached outside the repo); runtime facts are from the running
`7.2.10-1-cachyos`. Raw traces stay outside Git (`~/.local/state/openwitty/`). If this chat is gone after the shutdown, read this file,
`PHASE8A19_BOOT_USB_CONTROL_TRACE.md`, then follow "Resume after boot". No boot is repeated automatically.

## 1. Phase 8A.19 baseline (complete)
Probe `SET_INTERFACE(interface 1, alt 0)` (`01 0b 0000 0001 0000`, `(udev-worker)`, `snd_usb_create_stream`/`card.c:320`) submitted
6.884120 s, no completion for 5.486 s (−110, = 5.000 s + timer-wheel rounding to the 512-jiffy grid), directly after
`SET_INTERFACE(1, alt 1)` (26 ms, OK). Later `SET_INTERFACE(2, alt 0)` from WirePlumber (13.444 s, 5.070 s, −110). Complete FU2 sequence:
GET mute 0, GET_RANGE×3, GET −4.00 dB, SET −60 → GET −4.00, SET 0 → GET −4.00, final SET 0 dB, no restore. Unit-5 channel 3/4 writes
STALL. User audio observations: attempt 1 two very quiet crackles; attempt 2 none (documented).

## 2. xHCI source-level flow of an endpoint-0 control transfer (exact tag, with call sites)
1. `usb_start_wait_urb()` → `usb_submit_urb()` → `usb_hcd_submit_urb()` → `xhci_urb_enqueue()` (`xhci.c:1619`). The
   `xhci_urb_enqueue` tracepoint fires **first** (before argument checks and the `xhci->lock`), so even a URB rejected with
   `-ESHUTDOWN`/`-ENOENT` is visible.
2. `xhci_queue_ctrl_tx()` (`xhci-ring.c:3778`) builds a Setup Stage TRB (IDT: the 8 SETUP bytes are inside the TRB), an optional Data
   Stage TRB, and a Status Stage TRB, each traced by `xhci_queue_trb` (the Setup TRB decode prints `bRequestType … wLength`).
3. The ring is released to hardware by `xhci_ring_ep_doorbell()` (`xhci-ring.c:549`, tracepoint `xhci_ring_ep_doorbell`: slot, endpoint).
4. The controller executes the TD; completion arrives as a **Transfer Event** TRB handled in `handle_tx_event()` (`xhci-ring.c:2635`):
   tracepoints `xhci_handle_event` (completion code string, residual length, slot, endpoint, pointer of the completed TRB) and
   `xhci_handle_transfer` (the TRB). The URB is returned by `xhci_giveback_urb_in_irq()` (`xhci-ring.c:825`, tracepoint `xhci_urb_giveback`).
5. If USB core gives up (5 s timeout in `usb_start_wait_urb`): `usb_kill_urb()` → `xhci_urb_dequeue()` (`xhci.c:1753`; tracepoint
   `xhci_urb_dequeue`, then `xhci_dbg_cancel_urb` "Cancel URB … dev …, ep …"). A pending TD requires **Stop Endpoint**
   (`xhci_handle_cmd_stop_ep`, `xhci-ring.c:1199`), then the cancelled TD is skipped with **Set TR Dequeue Pointer**
   (`xhci_handle_cmd_set_deq`, `:1432`); a halted endpoint needs **Reset Endpoint** (`:1572`). Then the URB is given back with `-ENOENT`/error
   status, `usb_start_wait_urb` returns `-ETIMEDOUT`, `usb_set_interface()` returns −110.
6. `usb_set_interface()` always sends the SET_INTERFACE request (no same-alternate shortcut); before it,
   `usb_disable_interface()` and `usb_hcd_alloc_bandwidth()` (a Configure Endpoint command) run.

## 3. What each hypothesis would look like in the trace
| | Hypothesis | Observable signature |
|---|---|---|
| A | URB never queued | no `xhci_urb_enqueue` after the USB-core entry (or none for the task/bus), no TRBs |
| B | queued, controller not notified | `xhci_urb_enqueue` + Setup/Status `xhci_queue_trb`, **no** `xhci_ring_ep_doorbell` for the slot/ep0 |
| C | notified, no completion by timeout | doorbell present, **no** Transfer Event for the queued TRBs until `xhci_urb_dequeue` |
| D | controller error not propagated | Transfer Event with an error completion code (Stall, USB Transaction Error, Babble, …) |
| E | ordinary timeout cancelling a pending transfer | same as C, plus `xhci_urb_dequeue`, Stop Endpoint, Set TR Dequeue Pointer, giveback — C and E are the same observation |
| F | other controller/device state | unusual commands (Reset Endpoint/Device), port-status events, command-ring errors in the window |
Important limit: C/E say *the controller did not report completion*; they cannot distinguish a device that never answered the transaction
from a controller that failed to complete it (an xHCI trace is software/hardware-interface state, not the electrical bus). No firmware-defect claim is made.

## 4. Instrumentation audit (running kernel, read-only tracefs)
Present events (built-in `xhci-hcd`): `xhci_urb_enqueue/giveback/dequeue`, `xhci_queue_trb`, `xhci_ring_ep_doorbell`, `xhci_handle_event`,
`xhci_handle_transfer`, `xhci_handle_command`, `xhci_dbg_cancel_urb`, `xhci_dbg_reset_ep`, `xhci_handle_cmd_stop_ep/set_deq/reset_ep/config_ep`, and more.
Field layouts were read from `events/xhci-hcd/*/format`. **Pointer printing:** the URB events carry a `urb` pointer field, but the *text*
trace prints `%p` **hashed**, so it cannot be compared with a raw kernel pointer (found by runtime validation: all 99 requests initially
failed to join). The status field of URB events exists in the binary record but is **not printed**. Therefore:
- **Join design (no pointer comparison):** USB-core entry (`uw` probe: task pid, bus, SETUP, time) → the first `xhci_urb_enqueue` by the **same task**,
  whose device name starts with `<bus>-`, within 5 ms → the Setup Stage TRB of that task must carry **identical SETUP bytes**
  (a mismatch downgrades the result to UNCERTAIN) → the *hashed* pointer printed by the xHCI events themselves is used only to follow that
  URB's dequeue/cancel/giveback events. The slot comes from the same enqueue event (`xhci_urb_enqueue` prints both device name `3-2` and
  `slot`), so no slot-from-address-number inference is needed; Transfer Events are tied to the TD by the **TRB pointer = DMA address of the
  queued Status Stage TRB** and the slot. Root-hub requests (device 1) are served in software and are classified "not applicable".
- The USB-core kprobes of Phase 8A.19 are unchanged (four probes; an `urb=` field was tried and removed because it cannot join).

## 5. Capture plan for attempt 3 (decision: GO, conditional on the safety confirmation)
Arguments: unchanged four `kprobe_event=` probes; **`trace_event=xhci-hcd:<14 events>`**; `ftrace=nop`; **`trace_buf_size=32M`**; the narrow
`dyndbg` selector; nothing else; no `usbcore.quirks`. Entry mechanism, baseline recording, `collect`, `verify-clean`, `entry remove`: as in 8A.19.
- **Visibility:** USB-core submission (kprobe), xHCI enqueue, Setup TRB, doorbell, Transfer Event or its absence, dequeue, cancel message,
  stop-endpoint/set-dequeue commands, giveback — all verified to exist and to parse on real traffic (below).
- **Volume estimate (measured):** ≈9 events per control URB (586 events for 65 URBs); idle desktop ≈1 event/s; interrupt URBs of HID devices cost ≈6
  events each, i.e. ≈6 k events/s only while a 1 kHz mouse moves. Per-event size ≈ 100–200 B. 32 MiB per CPU holds ≳ 150 k events on the
  busiest CPU (the xHCI interrupt CPU): ≳ 25 s of continuous 1 kHz mouse motion, >> the boot window. Total memory 512 MiB of 56 GB. Mitigation: the
  user avoids mouse motion until the trace is collected (keyboard login is negligible), and `collect` reports overruns.
- **Loss detection:** header `entries-in-buffer/entries-written`, per-CPU `overrun`, oldest-event timestamp (must be < 3 s), unpaired USB-core pairs,
  and `--expect-xhci`: a missing xHCI join or UNCERTAIN downgrades the verdict.
- **Cleanup:** `collect` stops/removes only our events (xHCI events are also removed from `set_event` with `!xhci-hcd:<name>`), restores the
  recorded buffer size and `tracing_on`, `verify-clean` compares against the pre-boot baseline; `collect` is re-runnable after interruption.
- **Privacy:** raw trace (hashed pointers, DMA addresses, other devices) stays in `~/.local/state/openwitty/`; committed extracts keep only the target's SETUP
  fields, times, statuses, slot/endpoint numbers and the classification — never pointers or DMA addresses.

## 6. Runtime validation done (non-target only; CONNECT 2 untouched)
Probes + all 14 xHCI events enabled on the live kernel while `lsusb -v` read **hubs** (never VID 29c2): 99 USB-core control requests captured;
after fixing the join (§4), 26 real-device requests classified NORMAL with Setup-TRB = USB-core SETUP in every case (0 mismatches),
73 root-hub requests classified not-applicable; 2 `xhci_urb_dequeue` events (interrupt URB cancellation) each followed by a giveback of the same
URB, with `Cancel URB`, `Removing canceled TD`, `Set TR Deq ptr` messages; a stop-endpoint/set-dequeue/reset-endpoint command trail was present.
End-to-end `collect` on the real tracefs (32 MiB buffer, `set_event` populated by the 14 events + 4 probes): raw trace saved 0600, `set_event` returned to empty, `kprobe_events`
empty, buffer restored to the recorded 1410 KB, `tracing_on=1`, no instances, `verify-clean` clean. The 80 offline tests cover the A/B/C-E/D/NORMAL/UNCERTAIN/root-hub
classifications, wrong-task/bus/late joins, hashed-pointer handling, set_event/xHCI cleanup, loss and completeness. The boot-time activation of
`trace_event=xhci-hcd:…` itself is unverified until the boot.

## 7. ALSA quirk assessment (exact tag; no quirk is enabled by this phase)
Module parameter syntax (`card.c`/`quirks.c`): `snd_usb_audio quirk_flags=<vid>:<pid>:<FLAG>[|<FLAG>…]` (hex IDs, flag names or hex masks, `!` clears);
`module_param_array(…, 0644)`, i.e. a `modprobe.d` option or the writable sysfs parameter works without recompiling; there is no built-in entry for `29c2:0004`.
Key fact for all four: **`card.c:320` — the call that actually stalled — has no quirk condition**, and `usb_set_interface()` has no same-alternate shortcut.
| flag (bit, mask) | code location | what it changes | affects the stalled `card.c:320` call? | applicable / helps? |
|---|---|---|---|---|
| `IFACE_DELAY` (11, 0x800) | `endpoint.c:945` (after a *successful* `usb_set_interface` in `endpoint_set_interface`), `clock.c:656` (TEAC vendor 0x0644 only) | adds `msleep(50)` **after** stream-open-time interface setup | **No.** Not in the probe path; adds delay only after success, so neither stall (both occur before any delay) is touched | does not address the known failures; adverse: 50 ms per interface change at stream open |
| `SKIP_IFACE_SETUP` (26, 0x4000000) | `stream.c:1258` (`continue` in the per-altsetting loop) | skips the probe-time block `SI(iface,0)`, [`SI(alt)` if set-first], `init_pitch`, `init_sample_rate`, `SI(iface,alt)` | **No — the call still runs** (unconditional after a successful parse). It *would* run with the interface still in alternate 0 (never moved to alt 1), a state in which the same request type completed in 0.2 ms in all traces | only candidate with a plausible *indirect* effect; does not touch WirePlumber's stream-open `SI(iface,0)` (`endpoint_prepare`); adverse: no probe-time sample-rate/pitch init (clock state first set at stream open), possible change in what FU2 sees |
| `SET_IFACE_FIRST` (16, 0x10000) | `stream.c:1254`, `endpoint.c:1496` | reorders: `SI(0)`, `SI(alt)`, then pitch/rate (no trailing `SI(alt)`); same for prepare | **No** (the alt-1 → alt-0 reset at `card.c:320` still follows) | unlikely to help; changes ordering only |
| `FORCE_IFACE_RESET` (20, 0x100000) | `endpoint.c:1701` | on stopping an *output* stream marks the interface for re-setup | **No** (stream stop/restart only) | irrelevant to probe; not applicable |
Evidence check: successful `SET_INTERFACE(1,1)` (26 ms) followed 32 ms later by the stalled `(1,0)` is consistent with a state-transition problem;
the same transition on interface 2 (`SI(2,1)` then `SI(2,0)` 31 ms later) completed in 0.1 ms in attempt 2, and WirePlumber's stalled `SI(2,0)` came when no alt-1
had just been selected — so the pattern is **not** simply "alt 1 → alt 0", and a generic delay would not address a missing completion. Verdict: only
`SKIP_IFACE_SETUP` could justify a future single-flag experiment (indirect, uncertain); IFACE_DELAY, SET_IFACE_FIRST and FORCE_IFACE_RESET are not justified.
Do not combine flags; none was enabled.

## 8. Procedure for attempt 3 (requires the user's physical-audio safety confirmation immediately before shutdown)
Playback/recording stopped; speakers and amplifiers disconnected/off/independently safe; headphones/listeners protected; no reliance on KDE (40%) or
CONNECT 2 mute; instructions retained; exactly one boot. Normal full shutdown; manual power-on; at the Limine menu stop the countdown and select
**"OpenWitty USB trace (temporary)"** (no typing); do not touch the mouse until collection; after login say "continue Phase 8A.20 attempt". Abort/rollback as in
Phase 8A.19 (normal `linux-cachyos` entry if the entry is missing or the boot fails).

## 9. Resume after boot
`git status`; `/proc/cmdline` (arguments present, no `usbcore.quirks`); `buffer_size_kb` (≈32768, expanded) and header; `sudo python3 -I tools/diagnostics/usb_ep0_trace.py collect
--out-dir ~/.local/state/openwitty/trace-8a20`; `analyze … --report …/collect_report.json --dmesg <timeout lines> --expect-xhci`; one GET-only snapshot; `entry remove` + `entry status` + `verify-clean`;
write §10 (results) here; commit/push.

## 10. Results
*(to be filled after the boot)*
