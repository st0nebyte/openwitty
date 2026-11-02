# Phase 8A.14 — DELAY_INIT timing postmortem

Offline analysis only: installed-kernel disassembly (`objdump -d` of the build-tree `vmlinux`
matching `7.2.10-1-cachyos`), upstream source review from Phase 8A.12, and the persistent journal.
No reboot, no USB request, no tracing, no sysfs/boot/audio change. The boot-only Feature Unit 2
failure is **not** explained by this document.

## 1. Phase 8A.13 measured outcome (unchanged)
`usbcore.quirks=29c2:0004:g` was accepted (`/proc/cmdline`, and the sysfs parameter mirrors it).
On that genuine shutdown→power-on boot: FU2 sticky rejection at 11.433 s, no ALSA `PCM Playback
Volume`, `route.hw-volume=false`, one stereo sink, AudioStreaming `-110` timeout at 17.508 s.
**NEGATIVE stays negative.** Measured for CONNECT 2 (`3-2`):
`new … USB device number` 2.041779 s → `New USB device found` 2.771194 s (**+729.4 ms**) →
`New USB device strings` 2.981003 s (**+209.8 ms**).

## 2. Installed-binary control flow (certain)
Addresses are from the installed `vmlinux`; function names are from its symbol table.
1. `hub_port_connect()` is **inlined into `hub_event`** (LTO). `hub_event` has exactly one call to
   `hub_port_init` for new devices (`…8498`) and one to `usb_new_device` (`…8644`); `hub_port_init`
   is otherwise called only from `usb_reset_and_verify_device`; `usb_new_device` also from
   `register_root_hub`.
2. In `hub_port_init`, in address order: `hub_port_reset` → … set-address (`usb_control_msg`) →
   `msleep` recovery → **`_dev_info` (the `new … USB device number` line)** →
   `usb_get_device_descriptor` (18-byte control read) → **`usb_detect_quirks(udev)`** (`…ac57`, the
   *only* caller of `usb_detect_quirks` in the USB-core range) → BOS read (if bcdUSB ≥ 2.01) → return.
   (A branch before `usb_detect_quirks` exists only for SuperSpeed ports reporting bcdUSB ≤ 2.ff;
   not this high-speed device.) `udev->quirks` is therefore set (static table, then XOR with the
   dynamic `usbcore.quirks` entry) **before `hub_port_init` returns**.
3. Back in `hub_event`, straight-line, no branch targets the test: `test eax<0` → on failure jump
   away; otherwise two `mutex_unlock`s → **`testb $0x40, quirks` → `msleep(2000)`** → bus-powered-hub
   check (skipped: bDeviceClass ≠ hub) → `usb_new_device`. Struct offset: `quirks` is at +1340
   (`0x53c`) per BTF (`pahole`), matching both test sites.
4. `usb_new_device`: `announce_device_ids` (**`New USB device found`**) → … →
   `usb_get_configuration` (**`testb $0x40` → `msleep(200)` per configuration**) →
   `usb_cache_string`×3 → `announce_device_strings` (**`New USB device strings`**) → `device_add`.
5. `msleep` here is jiffies-based: `schedule_timeout(msecs)` in a loop until it returns 0, task state
   `TASK_UNINTERRUPTIBLE`; `CONFIG_HZ=1000`, so 2000 ms = 2000 jiffies.

**What the bracket means.** `new … number` is printed *before* the descriptor read and quirk
detection; `found` is printed after the 2000 ms branch. So `new→found` brackets: descriptor read,
`usb_detect_quirks`, BOS read, the lock release, **the 2000 ms branch**, and the start of
`usb_new_device`. `found→strings` brackets `usb_get_configuration` (including the 200 ms branch) and
the three string reads.

**Conditions.** The 2000 ms branch is reached whenever `hub_port_init` returns ≥ 0 and bit 6 of
`udev->quirks` is set. The 200 ms site tests the same bit of the same `udev` and runs later. A
retry within `hub_port_connect`'s loop reuses the same `udev` and recomputes `quirks` by assignment
(not accumulation), so retries cannot flip the bit between the two sites. Nothing between the
two tests modifies bit 6 (no other `usb_detect_quirks` call; `usb_detect_interface_quirks` runs
later and touches interface quirks only).

## 3. Log correlation across all recorded boots
Pairing key: message identity + bus/port path + (same logical device identity), using monotonic
timestamps. All six boots (five baseline without the parameter, one with) show a single
`new` / `found` / `strings` triple for the Lewitt device at boot, no earlier or repeated `new` line,
no `reset`, no address change, no enumeration error, and the device number is 2 at boot every time.
So the pairs refer to one logical enumeration attempt, as far as the default-level log shows
(`dev_dbg` lines are not in the journal, so a silently retried attempt cannot be fully excluded).
The AudioStreaming `-110` timeout at ~17.5 s is a separate, later interface event (see §5).

Per-device `new→found / found→strings` in ms, boot-time only (other devices anonymised):

| device | b-5 | b-4 | b-3 | b-2 | b-1 | **test** |
|---|---|---|---|---|---|---|
| Lewitt `29c2:0004` | 0.11/0.04 | 0.11/0.10 | n/a (path) | 0.05/0.10 | 0.05/0.10 | **729.41/209.81** |
| other 1 (FS) | 259/64 | 254/64 | 254/63 | 253/64 | 251/64 | 253/64 |
| other 2 (FS) | 325/43 | 323/44 | 324/43 | 325/44 | 322/43 | 323/45 |
| other 3 (FS) | 322/41 | 322/41 | 323/40 | 324/42 | 324/41 | 321/41 |
| other 4 (hub) | 142/17 | 138/17 | 112/18 | 143/17 | 115/18 | 116/18 |
| other 5 (hub) | 31/18 | 31/18 | 30/19 | 31/18 | 30/19 | 31/18 |
| others 6–9 | 0.05–16 / 0.04–21 | similar | similar | similar | similar | similar |

(One baseline boot, b-3, had the Lewitt on a different port path and is not shown in the matcher's
by-path table; its raw triple is 0.16/0.04 ms.) Every other device is stable boot to boot, including
those enumerated in the same window as CONNECT 2, so the Lewitt change is specific to this boot's
parameter, not a general clock shift. The Lewitt baseline `new→found` of ~0.05–0.11 ms (and
`found→strings` ~0.1 ms) is shorter than a hot-plug of the same device (~123 ms and ~2.0 ms, same
code), and shorter than a synchronous USB control transfer should take; the same sub-millisecond
pattern appears for three other devices in every boot. Early-boot timestamps in this window are
therefore usable for *differences between boots* but should not be read as true elapsed time.

Net quirk effect for CONNECT 2 (test − baseline): **+729 ms** in `new→found`, **+209.7 ms** in
`found→strings`.

## 4. The 729 ms vs 2000 ms discrepancy
Demonstrated: (a) bit 6 was set (the 200 ms branch's +209.7 ms proves it for that `udev`); (b) the
2000 ms test is reached unconditionally on a successful `hub_port_init` and sits inside the
`new→found` bracket; (c) the added time inside that bracket is ~729 ms, not ~2000 ms, for this
device only. **Not demonstrated:** why. Candidate explanations, none verified, none tested:
- **H1 jiffies catch-up.** The sleep began at 2.0418 s, about 1.2 ms before `clocksource: Switched
  to clocksource tsc` (2.0429 s) and right after `tsc-early` was selected, at a point where the
  `sched_clock: Marking stable` line reports ~453 ms on its timekeeping-side value while printk
  already showed ~2.04 s (an offset of ~1.6 s between the two clock domains). `msleep` counts jiffies; a jump in the timekeeper/tick accounting during that window
  could finish 2000 jiffies in less real time (729 ms observed ⇒ ~1.27 s of jump if so).
  Supporting: coincidence of timing, jiffies-based `msleep`, the clock-domain offset. Against /
  unknown: tsc-early→tsc is a same-frequency switch; no direct jiffies measurement exists.
- **H2 the branch ran with less than 2000 ms** because of something I cannot see in the binary
  (e.g. a path where `schedule_timeout` returns 0 early). I found no such path in `msleep`.
- **H3 timestamps within this window misreport the interval** for this one device (e.g. early
  printk clock behaviour). Against: other devices in the same window show stable realistic
  intervals; the 200 ms branch measures 209.7 ms accurately just after.
- **H4 the 2000 ms branch did not run for this `udev`** although the 200 ms one did. I found no
  binary path that allows this (§2), so this is the least supported.
Conclusion: the 2000 ms branch's *effective* duration was ~0.73 s in this boot (unexplained
shortfall), the 200 ms branch ran as designed, so the total effective added enumeration delay was
~0.94 s, not ~2.2 s. This is a measurement; the mechanism is unproven. Do not claim direct proof
that the 2000 ms branch executed; the evidence is consistent with partial/shortened execution.

## 5. Early enumeration vs the later mixer probe
- Enumeration/registration of CONNECT 2 finishes by ~3.0 s (test: strings 2.98 s). The mixer probe
  is run when the `snd-usb-audio` module registers its driver: `usbcore: registered new interface
  driver snd-usb-audio` at 11.445 s (test) / 11.427 s (baseline), with the FU2/FU5 sticky lines
  ~12 ms *before* that line (inside module init/probe). The module is not loaded by the initramfs
  coldplug (which finishes by ~5.3 s); something in the main-root userspace loads it at ~11.4 s,
  around when the graphical stack starts (what exactly triggers it was not determined here).
- So the probe time is driven by userspace/module-load scheduling, not by the USB core's
  enumeration thread. An enumeration delay of ≤~2.2 s that ends at ~3 s is absorbed in the
  ~8.4 s of idle time before the probe; it shifts neither the probe moment nor the data the probe sees. FU2 sticky-line times across the
  five baseline boots are 10.915, 10.918, 10.915, 11.443 and 11.414 s (spread ~0.5 s); the test
  boot's 11.433 s lies inside that range, so the +19 ms versus the immediately preceding boot is
  ordinary variation, not an effect. The value after `=>` also varies between baseline boots
  (`-6363`, `-5248`, `-5248`, `-8536`, `-1024`); the test boot's `-1024` equals the most recent
  baseline and is within that spread.
- Distinguish: enumeration completion (~3 s) ≠ device registration (`device_add`, right after) ≠
  driver/module loading (~11.4 s) ≠ mixer probe (inside module init) ≠ PipeWire/WirePlumber startup
  (later, userspace). The 200 ms branch changing a timestamp does not imply any effect on the mixer.

## 6. Revised assessment of `USB_QUIRK_DELAY_INIT`
Qualified negative: a negative test of an *accepted* parameter whose observable effect was ~+0.94 s
of enumeration time (+0.73 s, +0.21 s), not the ~2.2 s expected; one boot; sticky FU2 unchanged.
It is **not** a verified negative test of the full 2.2 s delay. Even at 2.2 s the delay would end
~8 s before the probe, which makes "the device needs more time after attach" an unlikely
explanation on its own; a quirk acting only at USB enumeration cannot help if the readiness
condition is relative to the audio-driver's first traffic or to a power/firmware state that
precedes enumeration. No further DELAY_INIT boot is recommended.

## 7. Priorities
1. **Highest information value: boot-only FU2 payload observation** (the Phase 8A.11 USB-core
   observer remains offline and NO-GO). It is the only route to the actual bytes exchanged at
   boot versus the working reconnect, which no experiment so far has seen.
2. **Power-state hypothesis** (does CONNECT 2 lose power at shutdown? VBUS/standby behaviour, BIOS
   USB standby power, or a switchable hub) — a distinct, non-software experiment.
3. Offline only: investigate H1 (early `jiffies`/timekeeping behaviour) if the shortened
   `msleep` matters for any future time-based quirk; low priority for this bug.

## 8. Gate for any future hardware experiment
Nothing in this phase authorizes a boot, reconnect, USB request, tracing, or boot-config change.
Any future experiment requires its own explicit scope, an audio-safety confirmation immediately
before it, and (for tracing) the Phase 8A.11 start/stop/cleanup gates being met first.
