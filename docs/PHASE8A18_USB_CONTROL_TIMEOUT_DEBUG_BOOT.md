# Phase 8A.18 — USB control-timeout debug boot (one boot)

**Status: attempt 1 INCONCLUSIVE (parameter mistyped); attempt 2 EXECUTED — POSITIVE (see "Attempt 2 results" at the end).**
Original checkpoint text (prepared 2026-10-23 ~22:00 CEST, authorization given and the six safety
confirmations recorded in chat right before the shutdown) follows; results are appended at the end.

If this chat is gone after a shutdown: read this file, `PHASE8A17_SND_USB_AUDIO_PROBE_DELAY_ANALYSIS.md`
and `PHASE8A13_DELAY_INIT_SINGLE_BOOT_TEST.md`, check `git status`, then follow "Resume after boot".
Do not start a second boot because a session was interrupted.

## 1. Purpose
Test candidate A of Phase 8A.17: does the ~5.5–6 s `snd-usb-audio` probe delay contain a **timed-out
USB control transfer**? Four `usb_set_interface()` calls in `__snd_usb_parse_audio_interface`
(`stream.c:1261–1267`) ignore their result and use a 5000 ms timeout; a timeout would be silent at the
default log level. This is a diagnostic only: it changes no USB request, no module order, no audio policy.
Unproven expectation, not a result.

## 2. Verified debug site (exact tag `cachyos-7.2.10-1`, commit `11636526…`)
- `drivers/usb/core/message.c`, `usb_start_wait_urb()`; the `dev_dbg()` spans lines 82–88; the compiler
  records the callsite line as **88**. The running kernel agrees (read-only):
  `drivers/usb/core/message.c:88 [usbcore]usb_start_wait_urb =_ "%s timed out or killed on ep%d%s len=%u/%u\n"`
  — flags `=_` (disabled). `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_DYNAMIC_DEBUG_CORE=y`.
- It is the **only** callsite in `usb_start_wait_urb`; the query below matches **exactly 1** callsite
  (`grep -c` of the control file for that file:line = 1; `message.c` exists only once in the tables).
- Parameter (verified against `lib/dynamic_debug.c` and `dynamic-debug-howto.rst` of the same tag):

      dyndbg="file drivers/usb/core/message.c line 88 +p"

  Mechanism: `dynamic_debug_init` is an `early_initcall`; it re-parses the saved command line with
  `parse_args` and applies `dyndbg` queries (built-in `usbcore` tables exist then, i.e. before any USB
  enumeration at ~2 s). A bare `dyndbg=` is processed only at boot. Words are whitespace-separated
  only, so the **double quotes are required**; the kernel strips them from the value. `file` matches
  the full path or the basename, `line 88` matches exactly line 88, flag `+p` enables printing.
  Query limit 1023 characters (ours ≈ 55).
- **Equivalent with fewer special characters** (also verified to match exactly 1 site, via the basename
  match): `dyndbg="file message.c line 88 +p"` — use it only if typing `/` in the bootloader editor is a
  problem; the cmdline captured afterwards records which form was used.
- Conflicts: `/proc/cmdline` has no `dyndbg`/`ddebug`/`udev`/`usbcore.dyndbg` argument. **The
  `usbcore.quirks=29c2:0004:g` seen in the running boot's `/proc/cmdline` came from the Phase 8A.13
  one-time edit and must NOT be present in the next boot.** `/etc/default/limine`'s stored
  `KERNEL_CMDLINE[default]` does not contain it (read-only check).
- 24 callsites are already `=p` in the running kernel (mac80211, kernel/power, init/main); expect about
  25 after the test (24 + 1). They are unrelated.
- Verbosity: this callsite prints only when a wait **times out or is killed**, so log volume is tiny;
  `quiet` affects the console only, the kernel ring buffer and journal still record it.

## 3. What the message can and cannot prove
Format: `usb <bus-port>: <task name> timed out or killed on ep<N><in|out> len=<actual>/<requested>`.
It gives: device context (`usb 3-2:`), the **task name** of the caller, endpoint and direction, and
actual/requested length. It does **not** give `bRequest`, `bmRequestType`, `wValue`, `wIndex`, the start
time of the request, or the transfer history, and it is **not** specific to CONNECT 2, to USB Audio
Class, or to control transfers (any `usb_start_wait_urb` caller: control, bulk and interrupt waits).
- `ep0out len=0/0` on CONNECT 2 in the suspected interval is *consistent with* a `SET_INTERFACE`
  timeout; it does not identify which of the four calls, nor prove it is `SET_INTERFACE`.
- No message does **not** exclude other waits, or a long request that eventually succeeds (no timeout).
- A message from another device says nothing about CONNECT 2.
- A message after the FU2 sticky line may be the already-known later AudioStreaming `-110`.
- **Built-in positive control:** the known later `usb_set_interface failed (-110)` (≈17.5 s) is itself a
  5 s timeout, so an active site should print a line at ≈ that time (ep0out, task = the userspace caller,
  probably `wireplumber`/PipeWire). If the site is enabled and no line appears there, treat the
  run as inconclusive.
Timestamps: the line is printed when the wait **ends**, so a 5 s stall appears ≈5.0 s after the request
began (use `ep0out` start ≈ stamp − 5.0 s only as an estimate).

## 4. Baseline (read-only, 2026-10-23 ~21:52 CEST)
- Kernel `7.2.10-1-cachyos`; Limine 12.9.3 (package `12.9.3-2`); the editor footer in the installed
  `BOOTX64.EFI` reads `ESC Discard and Exit` and `F10/CTRL+X Boot` — there is **no save action**, which
  supports (not proves) that edits are one-boot only.
- Six recorded boots: FU2 sticky at 10.915–11.443 s; registration 10.928–11.456 s; `-110` always 6.075–6.077 s
  after the sticky line.
- Running boot: `PCM Playback Volume` **absent** (names listing), output route `route.hw-volume=false`,
  one CONNECT 2 sink, KDE volume 0.40 (software gain `0.064`; **not** protection for the hardware).
- Last physical state measured (GET-only, 20:31:40): Speaker −4.00 dB unmuted, Headphone −32.40 dB
  muted. **No later measurement; no snapshot is authorized for this test unless the user says so.**

## 5. Physical audio risk
The kernel's own mixer probe runs at boot as in every earlier boot (possible attempt at 0 dB on
Speaker, earlier unmute observations). The debug boot parameter does not change that. The uneventful
Phase 8A.13 boot is not proof of safety. KDE mute/volume and CONNECT 2 hardware mute are not protection.

## 6. One-boot procedure (do not start before separate authorization)
Immediately before shutdown the user must confirm: playback and recording stopped; speakers and
amplifiers disconnected/off/independently safe; headphones and listeners protected; no reliance on KDE
or CONNECT 2 mute; these instructions retained offline; exactly one boot experiment intended.
1. Normal full shutdown (not reboot, suspend, hibernate). Leave CONNECT 2 plugged into the **same
   port**; do not reconnect, reset, move or touch it.
2. Power on manually.
3. At the Limine menu select the usual CachyOS entry and press `e` (editor).
4. **Pre-check:** the shown `cmdline:` must be the stored original
   (`quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=… ` with **no** `usbcore.quirks=`).
   If `usbcore.quirks=29c2:0004:g` is present, the earlier edit persisted: **ESC, boot the entry
   unmodified, report, and do not run this test.**
5. Keep every existing argument unchanged. Append one space and exactly
   `dyndbg="file drivers/usb/core/message.c line 88 +p"` (straight double quotes). Nothing else.
6. Check the line visually (quotes closed, no typo, nothing else changed). Boot with **F10 or CTRL+X**.
7. Boot once. No second experimental boot.
Abort (ESC, boot the unmodified entry, report; **do not improvise** another method: no Limine file
edit, no initramfs/udev/sysfs change, no other boot parameter, no `udev.log_level`, no BPF): editor
disabled or unavailable; entry/`cmdline` unidentifiable or truncated; stored cmdline already contains the
quirk; unable to enter the exact characters; any doubt that the edit is one-time; unexpected menu behavior.
Recovery: if the edited boot fails, power-cycle once and choose the unmodified entry; that is not a
retry of the experiment.

## 7. Evidence to collect after boot (passive only)
1. `/proc/cmdline`: contains the intended `dyndbg=…`, **no** `usbcore.quirks`.
2. `/proc/dynamic_debug/control`: `message.c:88` flag `=p` (≈25 enabled).
3. `journalctl -k -b 0 -o short-monotonic`: every `timed out or killed` line (task, device, ep, lengths).
4. Does it name CONNECT 2's path (the same `usb X-Y`) and fall **before** the FU2 sticky line?
5. Task name, endpoint, direction, lengths for each line.
6. FU2 sticky present/absent and its time; registration time; `-110` time (positive control).
7. ALSA `PCM Playback Volume` (names listing only), PipeWire `route.hw-volume`, one sink.
8. Any other timeout lines (other devices) listed but not attributed to CONNECT 2.
9. User observations (clicks, level changes, instability).
No USB request, no mixer value query, no service restart, no tracing; no other dyndbg selector.

## 8. Classification
- **Positive:** a CONNECT 2 `ep0out`-style timeout line before the FU2 sticky line, with a plausible
  duration (stamp ≈ 5 s after the preceding activity), site verified active (flag `=p`, positive control
  seen). Supports a timed-out transfer in the probe; does not identify the exact call.
- **Negative:** site demonstrably active (flag `=p` and positive control line seen), FU2 failure
  occurs as usual, no CONNECT 2 timeout before the sticky line. Weakens the 5 s-timeout hypothesis;
  does not exclude other waits or readiness problems.
- **Inconclusive:** parameter not applied, site not enabled, positive control missing, logs incomplete,
  CONNECT 2 did not enumerate normally, or lines cannot be correlated.
One boot is one data point; neither outcome fixes or characterises the bug by itself.

## 9. Offline preparation validation
Callsite identity (running control file = exact tag line 88), parameter syntax and quoting
(`dynamic_debug.c`/howto), uniqueness (1 site), no conflicting argument, editor keys (installed
`BOOTX64.EFI` strings), baseline, documentation and USB-call-site audit checks were done before the
checkpoint commit. Not verified: `limine.conf` itself (root-only), `editor_enabled`, whether a config
hash is enrolled, and keyboard-layout behavior of the editor for `"` and `/` (the `=`, `:` and `.`
of the Phase 8A.13 token worked).

## 10. Gate
Readiness: **CONDITIONAL-GO** for exactly one dyndbg-only shutdown→power-on boot after the user's
explicit approval of this experiment and the immediate safety confirmation in §6. Phase 8A.13's
authorization does **not** carry over. No shutdown has been requested or performed.

---

# Results (recorded 2026-10-23 ~22:05 CEST, after the single boot)

## Procedure record
User authorized this experiment and confirmed safety points 1–6. One normal shutdown (21:56) and one
manual power-on (21:59); CONNECT 2 stayed in its port (`3-2`); the Limine editor was available and the
one-time edit was applied (the running command line has the original arguments, **no**
`usbcore.quirks` — so the Phase 8A.13 edit was indeed one-time).

## Parameter verification: FAILED
`/proc/cmdline` ends with `dyndbg"file drivers/usb/core/message.c line 88 +p"` — the **`=` after
`dyndbg` is missing** (probably lost in the bootloader editor's key handling; the closing quote is present).
Consequences, all observed read-only:
- The kernel treated it as one unrelated token (no `dyndbg` parameter); `/proc/dynamic_debug/control`
  still shows `message.c:88 [usbcore]usb_start_wait_urb =_` (disabled); enabled count unchanged (24).
- No `timed out or killed` line exists in the kernel log for this boot (including the positive-control
  moment at 17.0 s), as expected for a disabled site.
- PID 1 was started with the extra argument `splash` only; the malformed token produced no kernel
  notice besides the usual "unknown parameter" for `splash`.
**Classification: INCONCLUSIVE — the debug site was never enabled, so nothing was learned about
timeouts.** The one-boot allowance is used; **no retry was or will be started automatically.** A second
attempt would be a new experiment needing fresh explicit approval and a safety re-confirmation.

## Lessons for any future attempt (not authorized)
- The user must read back the typed text character by character before F10, specifically
  `dyndbg` **`=`** `"`; the `=` followed by `"` may be the problem key sequence on this keyboard layout
  (dead-key / layout mapping under UEFI). The earlier `usbcore.quirks=29c2:0004:g` (`=` followed by a digit)
  worked.
- A pre-agreed fallback must not be improvised; abort rules in §6 stand.

## Incidental observation (a seventh normal boot, no quirk, device on `3-2`)
Elapsed times (monotonic): enumeration 2.0466 s; real-root first udevd line 5.357; `btusb` 5.479;
FU2 sticky 10.924; `snd-usb-audio` registered 10.937; PLM *Started* 10.944 (+6.8 ms); `usb_set_interface
failed (-110)` 17.000 (**+6.076 s** after sticky, same constant as all six earlier boots); sticky values
`-15360/0/128 => -1024`. ALSA `PCM Playback Volume` absent, `route.hw-volume=false`, one sink, KDE 0.40.
- **This contradicts the port inference in Phase 8A.17 §6:** on the *same* port `3-2` the sticky line was
  at 10.924 s here but 11.41–11.44 s in the three earlier `3-2` boots. Port and controller do **not**
  explain the two timing groups.
- Across all seven boots the two groups (≈10.92 s and ≈11.43 s, 0.5 s apart) separate cleanly by the
  first real-root udevd line: ≤5.428 s → ≈10.92 s; ≥5.439 s → ≈11.43 s (and by `btusb` time: ≤5.491 s vs
  ≥5.548 s); real-root systemd start does not separate them. A **step** of 0.5 s rather than a
  continuous dependence suggests the end of the hold is quantized (a periodic event or retry cadence of
  ≈0.5 s) — e.g. a stall of ≈5.0 s counted from a start ≈0.5 s after udevd, whose end is reported at the
  next ≈0.5 s tick. This is a **hypothesis from n=7**, two free parameters, not a result; it does not
  identify the mechanism or the 0.5 s source.

## User observations
Not yet collected at the time of writing (ask: clicks, level changes, USB instability).

## Next
Do not repeat the boot automatically. Options requiring fresh approval and safety confirmation: a second
dyndbg attempt with a read-back check; or the udev-debug boot (weaker). Offline: examine what has a
≈0.5 s cadence in the USB/ALSA probe path (candidates: host/xHCI polling, workqueue delays, the
device) using the exact-tag source and installed binaries.

---

# Second attempt (prepared 2026-10-23 ~22:10 CEST — authorization given in chat, safety confirmation pending)

**Status: PREPARED FOR ONE SECOND ATTEMPT — the user explicitly asked to retry ("i wanna try it again").
It is a new experiment: the safety points 1–6 of §6 must be re-confirmed immediately before shutdown, and no
third attempt follows automatically.** The first attempt's user-observation question is still unanswered.

Changes to the procedure (everything else in §2–§8 is unchanged):
- Type the parameter in this order and **read each step back** before continuing:
  1. `dyndbg` — check it is there.
  2. `=` — check the `=` is visible directly after `dyndbg` **before** typing anything else.
  3. `"` — check the `=` is still there afterwards.
  4. the query `file drivers/usb/core/message.c line 88 +p`, then the closing `"`.
- If the `=` vanishes when `"` is typed (dead-key/layout effect), type the `"` first and then move the cursor one
  step left and insert `=`; accept only if the final text reads exactly
  `dyndbg="file drivers/usb/core/message.c line 88 +p"`.
- Final read-back (character by character) before F10: `dyndbg` `=` `"` `file drivers/usb/core/message.c
  line 88 +p` `"`, preceded by the unchanged original arguments, with no `usbcore.quirks`.
- If the exact text cannot be produced after two careful tries: ESC, boot the unmodified entry, report.
  The shorter variant `dyndbg="file message.c line 88 +p"` is equally acceptable.
- Success check after boot: `/proc/cmdline` has `dyndbg="file …"` **with** `=`, and `/proc/dynamic_debug/control` shows
  `message.c:88 … =p`.

---

# Attempt 2 results (recorded 2026-10-23 ~22:15 CEST)

## Procedure record
User re-confirmed safety points 1–6 and re-ran the Limine one-time edit: normal shutdown 22:04, manual
power-on 22:05, CONNECT 2 unchanged in port `3-2`. No retry follows automatically.

## Parameter verification: PASSED
`/proc/cmdline` ends with `dyndbg="file drivers/usb/core/message.c line 88 +p"` (the `=` present), no
`usbcore.quirks`. `/proc/dynamic_debug/control`: `message.c:88 [usbcore]usb_start_wait_urb =p` (enabled);
enabled callsites 24 → 25 (exactly the one added).

## Debug output (kernel log, monotonic; CONNECT 2 = `usb 3-2`)
| time (s) | line |
|---|---|
| 11.347446 | `usb 3-2: (udev-worker) timed out or killed on ep0out len=0/0` |
| 17.491004 | `usb 3-2: wireplumber timed out or killed on ep0out len=0/0` |
No other timeout lines, from any USB device, in the whole boot.
Correlated events: enumeration 2.130; FU2 sticky 11.416; `snd-usb-audio` registered 11.429; `-110`
`usb_set_interface failed` 17.492; udev stamp of `3-2:1.0` 5.416 (rules evaluated).

## Interpretation
- **Positive control passed:** the 17.491 s line (task `wireplumber`, ep0out, 0/0) occurs 1 ms before the
  known `-110` line (17.492) — the site works and attributes the known later failure to a 5 s SET-type
  timeout from userspace.
- **New, relevant evidence:** a control-endpoint (`ep0out`) wait on **CONNECT 2** timed out at
  **11.347 s, 69 ms before the FU2 sticky line (11.416 s) and 82 ms before the driver registration**, in
  the task **`(udev-worker)`** — the context that runs the module load/probe. With the 5000 ms control
  timeout, the stalled request began at about **6.35 s** (estimate: stamp − 5.0), i.e. ~0.93 s after
  udev evaluated the rules for the interface (5.416 s). One 5 s timeout plus ~0.9 s of load/parse accounts
  for the ~5.9 s hold.
- `ep0out len=0/0` means a control request with **no data stage** — consistent with `SET_INTERFACE`
  (the ignored calls of Phase 8A.17 candidate A), but it does **not** identify which call or even that it is
  `SET_INTERFACE`; the site does not log `bRequest/wValue/wIndex` or the start time.
- The mixer decision followed 69 ms after the timeout: the device answered again immediately afterwards.

## Scoring (per the Phase 8A.18 criteria)
**POSITIVE** for "the pre-FU2 hold contains a timed-out USB control transfer": CONNECT 2 timeout, before
the FU2 sticky line, in the probing task, with duration consistent with the hold, site verified active.
| | Observation | Result |
|---|---|---|
| A | FU2 sticky warning | Present (11.416 s, `=> -1024`) |
| B | ALSA `PCM Playback Volume` | Missing |
| C | PipeWire `route.hw-volume` | False (software gain 0.064, KDE 0.40) |
| D | One CONNECT 2 sink | Yes |
| E | AudioStreaming `-110` | Present (17.492 s; timeout line 17.491 s) |

## What this does and does not establish
Established: at boot the device does not complete one (data-less) control request to endpoint 0 for the full
5 s timeout during the probe, then works; the same kind of stall occurs again later from userspace. Not
established: which request; why the device does not answer (readiness, power state, host/xHCI behavior);
whether this stall is the *cause* of the sticky FU2 verdict or merely coincident (the stall precedes it by
69 ms; the reconnect path runs the same code with no stall and accepts FU2); why the end lands at ≈10.92 s or
≈11.43 s; whether a good boot would show the stall. This run was not instrumented beyond one log line;
sticky behavior was unchanged, so no measurement interference is visible.

## User observations
Not yet collected for attempts 1 and 2 (clicks, level changes, USB instability).

## Next (offline unless separately approved)
Do not repeat this boot. The finding redirects the investigation to *why endpoint 0 stops answering for
5 s at boot*: candidates are host-side (xHCI port/endpoint state after the earlier kernel reset), device-side
readiness/power state, or the interaction of the first `SET_INTERFACE` with the audio interface. Identifying
the exact request needs wire-level or xHCI visibility, i.e. tracing or `usbmon`, which is not authorized.
