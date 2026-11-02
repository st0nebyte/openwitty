# Phase 8A.12 — USB-core `DELAY_INIT` quirk assessment

Offline research only. Nothing in this phase touched `/sys/module/usbcore/parameters/quirks`,
`/proc/cmdline`, Limine config, initramfs, USB device state, or audio policy. No reboot occurred.

## Decision

**CONDITIONAL-GO** for a future, separately approved, single-boot experiment using a
non-persistent Limine menu-time command-line edit. See gate at the end.

## 1. Source-code semantics (verified against upstream `torvalds/linux` `drivers/usb/core`,
cross-checked against the symbols actually compiled into this machine's running kernel)

The exact tag `cachyos-7.2.10-1` is not a fetchable public ref (CachyOS ships this file set
unmodified from upstream; see §3). Findings below come from current upstream
`drivers/usb/core/{quirks.c,hub.c}` and `include/linux/usb/quirks.h`, independently
cross-checked against the real binary at `/usr/src/linux-cachyos/vmlinux`
(`version` file confirms this is exactly `7.2.10-1-cachyos`, matching `uname -r`).

- `quirks_param_set()` (`drivers/usb/core/quirks.c`) parses `usbcore.quirks=VID:PID:flags`
  letter-by-letter via a `switch`. **`g` → `USB_QUIRK_DELAY_INIT`** (bit 6, `0x00000040`).
  `n` → `USB_QUIRK_DELAY_CTRL_MSG` (bit 13, `0x00002000`) — confirmed as a distinct bit,
  distinct code path, not related to this proposal.
- `include/linux/usb/quirks.h`: `#define USB_QUIRK_DELAY_INIT BIT(6)`.
- `Documentation/admin-guide/kernel-parameters.txt`, `usbcore.quirks=` entry, quoted:
  > "Each letter will change the built-in quirk; setting it if it is clear and clearing it
  > if it is set." Example given in the doc: `quirks=0781:5580:bk,0a5c:5834:gij`.
- `drivers/usb/core/hub.c`, `hub_port_connect()`:
  ```c
  status = hub_port_init(hub, udev, port1, i, NULL);
  if (status < 0)
      goto loop;
  mutex_unlock(hcd->address0_mutex);
  usb_unlock_port(port_dev);
  retry_locked = false;
  if (udev->quirks & USB_QUIRK_DELAY_INIT)
      msleep(2000);
  ```
  This runs once, after `hub_port_init()` has reset the port, assigned the device address,
  and read the device descriptor — and **before** `usb_new_device()` (the call that attaches
  drivers, including `snd-usb-audio`). No comment in-source explains the mechanism; this is a
  plain, unconditional 2000 ms sleep on the port/enumeration thread.
- **CORRECTION (second delay site).** The first version of this document stated there is no
  additional 200 ms delay. That was wrong; it came from a lossy summary of `hub.c` only, and
  `config.c` had not been inspected. `drivers/usb/core/config.c`, `usb_get_configuration()`:
  ```c
  for (cfgno = 0; cfgno < ncfg; cfgno++) {
      /* ... read 9-byte configuration header, allocate bigbuffer ... */
      if (dev->quirks & USB_QUIRK_DELAY_INIT)
          msleep(200);
      /* "skip the second read if we already got everything" check follows */
      /* ... optional full-length usb_get_descriptor() read ... */
  }
  ```
  The 200 ms sleep is inside the per-configuration loop, after the 9-byte header read and
  before the full-length read, so it runs **once per configuration descriptor** (up to
  `bNumConfigurations` × 200 ms), even when the second read is skipped. It is not per retry.
  This device reports `bNumConfigurations = 1` (read from sysfs, no USB traffic), so the expected
  extra cost is **one** 200 ms sleep. **Order (corrected):** the 200 ms site runs *after* the
  2000 ms site, not before it (see "Verified call sequence" below).
- **Verified call sequence** (upstream `hub.c`; confirmed in the installed `vmlinux`):
  1. `hub_port_connect()` → `hub_port_init()` (reset, address, device descriptor read). The
     kernel prints `usb X-Y: new … USB device number N using …` here, before any DELAY_INIT sleep.
  2. On success: `msleep(2000)` if `DELAY_INIT` (hub.c site).
  3. `hub_port_connect()` → `usb_new_device()`.
  4. `usb_new_device()` → `announce_device_ids()`, which prints
     `New USB device found, idVendor=…` — **before** configuration descriptors are read.
  5. `usb_new_device()` → `usb_enumerate_device()` → `usb_get_configuration()`, where the
     per-configuration `msleep(200)` runs (config.c site).
  6. `usb_new_device()` → `announce_device_strings()`, which prints
     `New USB device strings: Mfr=…` — **after** configuration processing.
  7. `device_add()`; driver binding (including `snd-usb-audio`) follows.
  Installed-binary evidence for 2–7: `hub_event` has `testb $0x40` / `msleep(2000)` ahead of the
  inlined `usb_new_device` hand-off; disassembled `usb_new_device` calls `announce_device_ids`,
  then `usb_get_configuration`, then `announce_device_strings`, then `device_add`, in that
  order. The baseline journal agrees: `found` and `strings` are ~0.1 ms apart (2.069273 s vs
  2.069371 s) because no delay is present between them today. (An earlier version of this
  document wrongly put the 200 ms site before the 2000 ms site; a reviewer's suggestion that
  `New USB device found` is printed *after* configuration processing is likewise not supported —
  only the `strings` line is.)
- Other quirk-gated delay: `USB_QUIRK_HUB_SLOW_RESET` (`o`) adds 100 ms to port-reset recovery —
  unrelated bit, not part of this proposal. (A 2014 stable-3.8 backport once implemented
  DELAY_INIT as "100 ms between Get Configuration requests"; that is the ancestor of the
  `config.c` site and is **not** superseded — it still exists, now as 200 ms per configuration.)
- **Evidence tiers for the two delay sites:**
  1. *Upstream release-source evidence* (current `torvalds/linux`): both sites present as quoted.
     The exact `cachyos-7.2.10-1` tag source was not fetchable; CachyOS carries no known
     `drivers/usb/core` patches.
  2. *Installed-binary evidence* (read-only `objdump -d` of `/usr/src/linux-cachyos/vmlinux`,
     whose `version` file is `7.2.10-1-cachyos`, matching `uname -r`):
     - `usb_get_configuration`: `testb $0x40,0x53c(%r13)` / `je` / `mov $0xc8,%edi` /
       `call msleep` — i.e. test of quirk bit 6 (`0x40`, DELAY_INIT) in `udev->quirks`, then
       `msleep(200)`.
     - `hub_event` (where `hub_port_connect` is inlined, LTO): `testb $0x40,0x53c(%r12)` / `je` /
       `mov $0x7d0,%edi` / `call msleep` — i.e. `msleep(2000)` gated on the same bit and the same
       struct offset, immediately after a `mutex_unlock`, matching the source shape.
     This vmlinux is the build-tree image shipped with the installed headers package; it is
     strong evidence about the installed kernel but not a read of the memory image of the running
     kernel itself.
  3. *Unverified:* actual runtime effect on the device; no test has been performed.
- Total added enumeration delay when the flag is set on this device: 2000 ms (hub.c, first) +
  ~200 ms (config.c, later) ≈ 2.2 s, only for the quirked VID:PID.

## 2. Toggle semantics vs. existing quirk state (the critical pre-check)

`usb_detect_quirks()`:
```c
udev->quirks = usb_detect_static_quirks(udev, usb_quirk_list);
...
udev->quirks ^= usb_detect_dynamic_quirks(udev);
```
The command-line value is XORed against the built-in table, matching the documented
"set if clear, clear if set" behavior. This makes the existing built-in state the deciding
factor, not the flag letter by itself.

**Checked: `usb_quirk_list` in `drivers/usb/core/quirks.c` has no entry for `29c2:0004`
(Lewitt CONNECT 2) and no entry for vendor `0x29c2` at all.** The nearest USB-audio entries in
that table are Sound Devices USBPre2/MixPre-D (`ENDPOINT_IGNORE`), MAYA44USB and Novation
SoundControl XL (`RESET_RESUME`), Focusrite Scarlett Solo and a Lenovo audio device
(`DISCONNECT_SUSPEND`) — none share a bit with `DELAY_INIT`, none match this VID:PID.

Because the built-in flags for `29c2:0004` are all clear, `usbcore.quirks=29c2:0004:g` is a
pure **set**, not a toggle-off of something already relied upon. There is no existing quirk
state for this device to accidentally clear.

**Already present on the command line?** `/proc/cmdline` (read-only check) currently contains
no `usbcore.quirks=` term at all:
```
quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=<redacted>
```
`/sys/module/usbcore/parameters/quirks` (read-only check) is currently empty. No conflicting or
pre-existing override exists to interact with the proposed one.

## 3. Early-boot applicability

- `modinfo usbcore` reports `filename: (builtin)` and `/lib/modules/$(uname -r)/modules.builtin`
  lists `kernel/drivers/usb/core/usbcore.ko` — **USB core is compiled into this kernel, not a
  loadable module.** Built-in module parameters set via `name.param=value` on the kernel
  command line are parsed during early boot parameter processing (`init/main.c`), strictly
  before any driver's `probe()` runs, and therefore strictly before `hub_port_connect()` ever
  executes for any device on any bus. The quirk table is populated before first enumeration.
- CachyOS carries no patch to `drivers/usb/core/*`: `CachyOS/kernel-patches` is organized by
  version folders (e.g. `sched/`, `misc/`) per the CachyOS wiki's own description of its
  "Base Patchset," and no USB-core-touching patch surfaced in a search of that repository or
  in CachyOS documentation. `/usr/src/linux-cachyos` (the installed headers tree, matching
  `7.2.10-1-cachyos` exactly via its `version` file) ships only `Kconfig`/`Makefile`/`include`
  for this subsystem, not the driver `.c` files, so a byte-exact local diff of `quirks.c`
  against upstream could not be done — the symbol-level cross-check in §1 is the strongest
  local confirmation available without `/boot` or root access.
- This all happens well before `snd-usb-audio` loads or probes anything, and well before the
  mixer probe this device fails at (see §5).

## 4. Timing hypothesis: plausible mechanism, not a demonstrated fix

Live `journalctl -k` evidence from this exact machine's recent boot history (5 of 5 sampled
boots, 2026-10-20 through 2026-10-23, read-only inspection of the persistent journal):

```
[   2.069s] usb 3-2: New USB device found, idVendor=29c2, idProduct=0004   (first enumeration)
[  11.414s] usb 3-2: 2:0: sticky mixer values (-15360/0/128 => -1024), disabling
[  11.418s] usb 3-2: 5:0: sticky mixer values (0/18432/256 => 6912), disabling
[  17.491s] usb 3-2: 2:0: usb_set_interface failed (-110)
```
(bus path varies boot-to-boot — seen as `3-2`, `5-1`, `6-4.1` — not tied to one physical port;
device enumerates and fails identically regardless of path.) This exactly reproduces the
~9-second enumeration-to-sticky-failure gap documented in Phase 8A.10, now confirmed across
multiple additional boots, not just the one originally sampled.

`DELAY_INIT`'s two sleeps (first 2000 ms in `hub_port_connect()` after `hub_port_init()` and
before `usb_new_device()`; then one 200 ms per configuration inside `usb_get_configuration()`
during `usb_new_device()`) add ~2.2 s in total to the first moments of the device's life, well
before the ~11.4 s mixer probe. The added time is split: ~2 s *before* `New USB device found`
and ~0.2 s *between* `New USB device found` and `New USB device strings`; it is **not**
necessarily added between `found` and audio-driver binding. The device already receives roughly 9 additional seconds of "settling time" today
between enumeration and the point of failure, through ordinary driver-load and probe sequencing
alone, and the failure still occurs.

Distinguishing plausible mechanism from demonstrated effect:
- **Plausible mechanism**: if CONNECT 2's firmware needs a fixed amount of *real time after
  descriptor read* (not time after some later event) before it reliably reports feature-unit
  state, inserting the delay at that exact point — immediately after descriptor read, before
  any further configuration or class-driver traffic — could matter in a way that an equal
  amount of *later* elapsed time (already present today) does not, if the firmware's readiness
  clock starts at descriptor read rather than at USB attach in general.
- **Not demonstrated**: there is no source-level evidence of *why* the sticky rejection happens
  (no comment, no errata reference, no other device in `usb_quirk_list` sharing this failure
  signature), so whether CONNECT 2's firmware actually behaves this way is unverified. The
  existing 9-second gap not being sufficient is suggestive that total elapsed time alone isn't
  the deciding factor, which is consistent with (but does not prove) the quirk's hypothesis.
- **Equally plausible null result**: the delay could do nothing (the firmware readiness problem
  may depend on something `DELAY_INIT` doesn't touch, e.g. config-descriptor fetch order,
  endpoint claim order, or something downstream of `snd-usb-audio`'s own probe sequence that
  this device-independent core delay cannot influence).

This hypothesis is **meaningfully testable** with one boot: the quirk changes nothing about USB
Audio Class requests themselves (§1 — it only delays when `usb_new_device()` runs), so a test
result is unambiguous — either the sticky rejection at FU2 still occurs or it does not, with
nothing else different in the request sequence.

## 5. Comparison with prior/other approaches

| Approach | Mechanism | Status |
|---|---|---|
| `snd_usb_audio quirk_flags=...MIXER_GET_CUR_BROKEN` | `snd-usb-audio`-internal, masks GET_CUR stickiness after the fact | Tried Aug 2026; made the control exist but not function — rejected as fake |
| Kernel mixer sticky-readback patch | Would require a custom kernel patch | Not attempted; out of scope (no patch, per this phase's own prohibition) |
| Physical USB reconnect | Real VBUS-level or full re-enumeration event | Confirmed reliable fix; the only mechanism proven to work so far |
| USB reset / power cycling (`USBDEVFS_RESET`, `uhubctl`, port de-authorize) | Re-enumeration without power-level control | Tried Aug 2026; `USBDEVFS_RESET` doesn't fix it, de-authorize restores control *existence* without restoring real function |
| PipeWire/WirePlumber changes | Userspace policy only | Tried Aug 2026; irrelevant once the kernel control is genuinely missing |
| BIOS USB standby-power config | Firmware-level VBUS/power policy | Not attempted; out of scope for this phase |
| USB-core BPF boot observer (Phase 8A.11) | Passive kernel tracing, no behavior change | Prototype only, explicitly NO-GO for live execution; not superseded or regressed by this phase |
| **`usbcore.quirks=29c2:0004:g` (this phase)** | Generic `usbcore` enumeration-timing quirk, changes real device init sequencing before any audio-specific code runs | **New and untested.** No equivalent test exists in Phase 8A documentation or the August investigation. |

This is categorically different from every `snd_usb_audio`-specific or reset/power mechanism
above: it is a standard, widely-used, upstream `usbcore` quirk (shipped for Logitech
C920/C922/C925e/C930e webcams, a Corsair K70 RGB keyboard, and a SanDisk flash drive with
similar enumeration-reliability symptoms), it changes *when* `usb_new_device()` runs relative to
descriptor read, and it never touches `snd-usb-audio`, PipeWire, WirePlumber, or any Audio Class
request directly.

## 6. Early-boot BPF observer (Phase 8A.11)

Not modified, not regressed, not superseded. Its own NO-GO rating for live execution stands
unchanged; this phase did not expand that work.

## 7. Reversible single-boot experiment design

**Boot method that reproduces the failure (verified, not assumed):** `last -x` and
`journalctl --list-boots` show every sampled failing boot followed a genuine **shutdown, then
a later separate power-on** (gaps of 5h14m, 8h15m, 1 day 18h51m, 17h49m between `shutdown` and
the next `reboot` record) — not a `systemctl reboot` warm restart. All 5 sampled
shutdown→power-on cycles in the last 4 days reproduced the identical sticky-mixer failure at the
identical ~11.4 s mark. **The future test must be a genuine full shutdown followed by a separate
power-on**, not a warm reboot, to match the method that has actually been observed to fail.

**Preferred mechanism — Limine's non-persistent, menu-time command-line edit (not a new boot
entry at all):**

Evidence for this mechanism: (a) the installed Limine 12.9.3 docs
(`/usr/share/doc/limine/CONFIG.md`) describe a boot-menu *config editor* controlled by
`editor_enabled` and list the per-entry `cmdline` option (`kernel_cmdline` is an alias) that is
"passed to the kernel"; (b) the Arch Wiki states that pressing `e` at the entry menu edits the
command line for the current boot only. The installed docs do **not** explicitly state that edits
are never persisted; the single-boot property rests on the Arch Wiki plus the fact that the
menu-time editor has no documented save-to-disk facility. It must be visually confirmed at the
menu, not assumed. Note the editor edits the entry's config text, so the operator must locate the
`cmdline:` line within it. Compared with a persistent extra entry this is expected to be safer:

- No `limine.conf` write, so nothing to roll back if the boot fails badly.
- No `limine-mkinitcpio`/initramfs regeneration needed.
- No `limine.conf` modification, so no hash re-enrollment is triggered. **However**, per
  `CONFIG.md`/`USAGE.md`, the editor is *unavailable* when a config hash is enrolled and Secure
  Boot is active, and defaults to disabled-capable when a hash is enrolled. Read-only checks:
  `SecureBoot` EFI variable = 0 (inactive), `SetupMode` = 1; `ENABLE_ENROLL_LIMINE_CONFIG` is not
  set in `/etc/default/limine` (tool default `no`). Whether a hash is actually enrolled in the
  Limine EFI binary could not be read (`/boot` is root-only), and `editor_enabled` in the
  generated `/boot/limine.conf` could not be read either. So editor availability is
  **unverified** until seen at the menu.
- The default entry's stored `KERNEL_CMDLINE[default]` (currently
  `quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=<redacted>`, read from
  `/etc/default/limine`) is never touched.

**Minimal proposed difference for the one test boot:** at the Limine menu, press `e` on the
normal/default entry, **preserve every existing argument unchanged**, and append exactly one
token to the end of the existing `cmdline` value:
```
usbcore.quirks=29c2:0004:g
```
Nothing else is added, removed, or reordered. Boot that single edited entry once.

**Abort conditions (no improvising another method):** if the editor does not open or is disabled;
if the normal known-good entry cannot be positively identified; if the existing `cmdline` is
ambiguous, truncated, or does not match the baseline `rw rootflags=subvol=/@ root=UUID=...`
form recorded in this document; or if it is unclear whether the edit will persist — then boot
the unmodified entry and report back. Do not fall back to editing `limine.conf`,
`/etc/default/limine`, modprobe files, or sysfs.

**Could not be fully verified:** the live `/boot/limine.conf` file itself is unreadable by the
current user (`/boot` is mounted `fmask=0077,dmask=0077` — root-only); this assessment relies on
`/etc/default/limine`, `/etc/limine-entry-tool.conf`, and the installed `limine` /
`limine-mkinitcpio-hook` / `limine-snapper-sync` package versions (12.9.3-2 / 1.40.0-1 /
1.32.1-1) rather than a direct read of the generated config. The menu-edit mechanism does not
depend on the content of that file, so this gap does not block the proposed design, but it does
mean the exact current entry list/count was not independently confirmed in this phase.

**Explicitly not combined with:** a kernel update, a new `snd-usb-audio` quirk, WirePlumber
changes, a new eBPF boot trace, driver blacklisting, module-load timing changes, USB resets, or
any other boot parameter. Confirmed: no such change is staged anywhere in this diff.

## 8. Passive success/failure criteria for the future test

All GET-only / observational, none side-effectful. Outcomes are scored **separately**:

- **A.** Feature Unit 2 sticky rejection absent or present (criterion 4).
- **B.** ALSA `PCM Playback Volume` present or missing (criterion 5).
- **C.** PipeWire `route.hw-volume` true or false (criterion 6).
- **D.** The single stereo playback sink still present (criterion 7).
- **E.** The separate AudioStreaming `usb_set_interface … (-110)` timeout occurs or not (criterion 8).

A missing sticky warning (A) is **not sufficient** if B or C show the hardware mixer is still
unavailable. A good A/B/C result is **not** a failure merely because E persists: E is a separate
interface-level timeout. `/proc/cmdline` (criterion 1) proves only that the parameter string was
passed to the kernel; it is **not** proof the USB delay executed. Direct evidence of execution is
indirect at best. Optional corroborating markers in `journalctl -k -o short-monotonic` for
`29c2:0004`: (i) `new … USB device number` → `New USB device found` gap of ~2.0 s (the hub.c
sleep; the earliest bracketing pair), and (ii) `New USB device found` → `New USB device
strings` gap of ~0.2 s (the config.c sleep) versus ~0.1 ms in earlier boots. These are not
prerequisites: other enumeration latency can blur them, and the outcome of A–E is interpreted on
its own. Neither marker is a measurement of the quirk bit itself.

1. `cat /proc/cmdline` after boot — confirms the edited line was passed to the kernel (not that
   the delay ran).
2. `cat /sys/module/usbcore/parameters/quirks` — (Phase 8A.13 correction: this DOES mirror the
   boot argument; it read `29c2:0004:g` on the test boot. Earlier expectation of "empty" was wrong.)
3. `journalctl -k -b 0 | grep -i 29c2` — confirms CONNECT 2 enumerated normally.
4. `journalctl -k -b 0 | grep -iE 'sticky mixer|usb_set_interface failed'` — presence/absence is
   the primary result signal.
5. `amixer -c <C2 card index> controls | grep -i 'PCM Playback Volume'` — passive control-list
   read (not a GET_CUR/SET_CUR), matching the established project diagnostic.
6. `wpctl status` / `pw-dump` grep for `route.hw-volume` on the CONNECT 2 node — passive read.
7. `pactl list sinks short` / `wpctl status` — confirms the single stereo playback sink is still
   exactly one sink, unchanged.
8. `journalctl -k -b 0 | grep -i 'AudioStreaming'` (or equivalent) — confirms whether the
   separate, previously-documented AudioStreaming interface timeout still occurs.
9. Do **not** query live Speaker/Headphone hardware level via any mechanism that could itself
   write — an independent OpenWitty GET-only snapshot (interface 3, the already-established
   safe read path) is the right tool if this is wanted, but it is **a separate authorization
   item for that later, separately approved test**, not something to run now or assume into the
   test design.
10. Mute-state safety is inferred from (9) being GET-only and from §9 below's pre-boot
    requirements, not measured by any new mechanism introduced here.

**Interpretation:**
- **Positive evidence**: criterion 1 shows the parameter, criterion 3 shows normal enumeration,
  A is absent **and** B and C show a working hardware volume. A timing shift (markers above) is
  supporting evidence only, and its absence does not invalidate an otherwise clear A–C result. E may persist without changing this classification.
- **Negative evidence**: same preconditions, but A (sticky rejection) still occurs, or A is
  absent while B/C show the hardware volume is still missing.
- **Inconclusive**: criterion 1 false (edit didn't take), criterion 3 false (device didn't
  enumerate normally for an unrelated reason), or the boot wasn't a genuine shutdown→power-on
  cycle (see §7).

One successful boot is promising evidence, not proof of permanent reliability — this mirrors the
project's existing position on the one Phase 8A.9 reconnect trace.

## 9. Physical audio safety

The DELAY_INIT quirk itself issues no USB Audio Class request and no volume/mute SET of any
kind (confirmed in §1 — it is a plain `msleep()`, nothing else). But the real risk, exactly as
in every prior boot in this investigation, is the kernel's **own** mixer-probe sequence, which
on a sticky-failure path has previously been associated with unexpected hardware-volume
excursions and an unmute-on-reconnect event (per Phase 8A.9 and the August findings). The quirk
does not change that probe's behavior if it still happens — it only changes *when* driver
attachment begins.

Before any future test boot:
- Playback and recording stopped.
- External amplification disconnected, powered off, or otherwise made physically safe.
- Listeners protected / not in the room at the moment of boot.
- No reliance on software (KDE) mute or on a previously stored hardware mute state — both are
  exactly the state this bug can scramble.
- Starting volume must not be deliberately pushed toward an extreme to "help" reproduce the
  issue.
- Explicit user confirmation immediately before the shutdown step, separate from this phase's
  authorization.
- A clear, pre-agreed manual rollback path (see §10) must exist before the shutdown happens.

## 10. Rollback and failure handling

- **Select the known-good entry:** at the Limine menu, simply select the normal/default entry
  without pressing `e` — it boots with its stored, unmodified `KERNEL_CMDLINE[default]`.
- **Confirm the experimental parameter is absent there:** `cat /proc/cmdline` after that boot
  should not contain `usbcore.quirks=`, matching the current baseline captured in this phase.
- **If the edited boot fails to come up:** power off, reboot again, and simply do not press `e`
  / do not re-apply the edit. Because nothing was written to `limine.conf` or the initramfs,
  there is no file to restore — the "rollback" is just "boot normally," which was already true
  before the test.
- **Removing the temporary entry:** not applicable — no persistent entry is created by this
  design, so there is nothing to delete afterward.
- **Verify default boot config unchanged:** re-read `/etc/default/limine` and (if root access is
  used for this one verification step) `/boot/limine.conf`, and confirm `KERNEL_CMDLINE[default]`
  matches what this phase recorded as the baseline.
- **Verify no permanent quirk configuration remains:** confirm no `/etc/modprobe.d/*.conf` or
  `/etc/default/limine` edit was made as part of the test (this phase made none; the future test
  should make none either, by design — the whole point of the menu-edit approach).
- **Preserve sanitized results:** record the four observational outcomes from §8 in a follow-up
  `PHASE8A13`-style doc, with the device serial number redacted to a placeholder per the
  project's existing privacy pattern, exactly as done throughout Phase 8A.

**Not done in this phase, correctly:** no rollback modification was made, because no boot
configuration was changed in this phase in the first place.

## 11. Known limitations

- `/boot/limine.conf` could not be read directly (permission-restricted ESP mount); the design
  relies on `/etc/default/limine`, `/etc/limine-entry-tool.conf`, and documented Limine/Arch
  Wiki menu-edit behavior rather than a direct read of the live generated config.
- **(Corrected in Phase 8A.17: the exact tag `cachyos-7.2.10-1` IS fetchable via `gh api`, and the
  DELAY_INIT/toggle/no-`29c2`-entry facts below were re-verified against it.)** The exact CachyOS git
  tag named in the task brief was not fetchable by this session's web tool at the time; the semantic claims in §1–§2 are sourced from current upstream
  `torvalds/linux` plus a symbol-level cross-check against the actual installed
  `7.2.10-1-cachyos` `vmlinux`, not a byte-identical source diff of the CachyOS tree.
  `hub_port_connect` is inlined into `hub_event` (LTO); the binary check in §1 was therefore
  done on `hub_event` and `usb_get_configuration`.
  CachyOS's own patch repository shows no USB-core patches, which is the basis for treating
  upstream behavior as authoritative here.
- The timing hypothesis (§4) is plausible and testable but unproven; the 9-second existing gap
  not being sufficient is suggestive, not conclusive, of why `DELAY_INIT`'s specific placement
  might matter.
- Reproducibility was confirmed across 5 of 5 recently sampled shutdown→power-on cycles, which
  is strong but not unlimited evidence. Note that a long shutdown gap does **not** by itself
  prove CONNECT 2 lost USB standby power (the host port may keep VBUS powered while "off"; that
  is unmeasured here), so "shutdown→power-on" identifies the observed startup *method*, not a
  proven device power state; a single future test boot is one more data point, not a
  statistical guarantee in either direction.

## 12. GO / CONDITIONAL / NO-GO

| Gate | Status |
|---|---|
| Quirk letter mapping (`g` = `DELAY_INIT`) correct | **GO** — confirmed in two independent primary sources (`quirks.c` switch, kernel-parameters.txt) |
| No existing built-in quirk conflict for `29c2:0004` | **GO** — confirmed absent from `usb_quirk_list` |
| Not already present/conflicting on current command line | **GO** — confirmed via `/proc/cmdline` and sysfs read |
| Applied early enough to matter | **GO** — usbcore built-in, parsed before any driver probe |
| Demonstrated to fix the actual failure | **NO** — untested; this phase is assessment only |
| Reversible without persistent config change | **GO** — Limine menu-time edit, no file write |
| Known failing boot method identified | **GO** — genuine shutdown→power-on, confirmed 5/5 recent occurrences |
| Physical audio safety plan defined | **GO** — pre-boot checklist in §9 |
| **Overall** | **CONDITIONAL-GO** for a future, separately approved single test boot |

## 13. Next execution approval gate

Separately and explicitly required before any boot happens:
1. User authorization to perform one genuine shutdown + power-on cycle (not a warm reboot).
2. User confirmation that playback/recording is stopped and external amplification is safe,
   immediately before that shutdown (§9).
3. Explicit agreement that only `usbcore.quirks=29c2:0004:g` is added, via the Limine menu-time
   `e` edit, with no other change.
4. If an independent OpenWitty GET-only hardware snapshot is wanted as part of that test, that
   is its own additional authorization item, not implied by the above.

No boot, shutdown, power-cycle, USB access, sysfs write, or Limine config change was performed
in this phase.
