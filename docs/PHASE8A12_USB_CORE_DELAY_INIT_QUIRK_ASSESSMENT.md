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
- No additional 200 ms delay anywhere in `hub.c` is gated on `USB_QUIRK_DELAY_INIT`. The only
  quirk-gated delay elsewhere is `USB_QUIRK_HUB_SLOW_RESET` (`o`) adding 100 ms to port-reset
  recovery — unrelated bit, not part of this proposal. (A 2014 stable-3.8 backport once
  implemented DELAY_INIT as "100 ms between two Get Configuration requests"; that is a
  superseded, older implementation — the current mainline/CachyOS behavior is the single
  2000 ms sleep quoted above, confirmed directly from current source.)
- Local independent confirmation: `nm /usr/src/linux-cachyos/vmlinux` shows `hub_port_init`,
  `usb_detect_quirks`, and `quirks_param_set` as real, non-weak symbols in this exact kernel
  build. `hub_port_connect` itself has no standalone global symbol (CachyOS enables LTO, which
  can inline it into a caller) — so the exact compiled instruction sequence could not be
  disassembled directly by name; the confirmation here is of the surrounding, present
  machinery, not a disassembly of the inlined body itself.

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
quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=00000000-...
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

`DELAY_INIT`'s 2000 ms sleep runs once, immediately after `hub_port_init()` and before
`usb_new_device()` — i.e. in the first ~2 seconds of the device's life, well before the ~11.4 s
mixer probe. The device already receives roughly 9 additional seconds of "settling time" today
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

Limine's boot-selection menu supports pressing `e` on the highlighted entry to edit its kernel
command line for that single boot only; the edit is never written back to `limine.conf` and
disappears on the next boot. This is confirmed as intended Limine behavior per the Arch Wiki's
kernel-parameters documentation, and is architecturally safer than creating a persistent extra
entry:

- No `limine.conf` write, so nothing to roll back if the boot fails badly.
- No `limine-mkinitcpio`/initramfs regeneration needed.
- No interaction with `ENABLE_ENROLL_LIMINE_CONFIG` checksum enrollment (verified currently
  **unset/no** in `/etc/default/limine` and `/etc/limine-entry-tool.conf`, so this is moot
  today regardless, but the live-edit path would stay safe even if it were enabled later).
- The default entry's stored `KERNEL_CMDLINE[default]` (currently
  `quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=00000000-...`, read from
  `/etc/default/limine`) is never touched.

**Minimal proposed difference for the one test boot:** at the Limine menu, press `e` on the
normal/default entry and append exactly one token to the shown command line:
```
usbcore.quirks=29c2:0004:g
```
Nothing else is added, removed, or reordered. Boot that single edited line once.

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

All GET-only / observational, none side-effectful:
1. `cat /proc/cmdline` after boot — confirms the edited line was actually used.
2. `cat /sys/module/usbcore/parameters/quirks` — likely still empty (command-line quirks are
   not mirrored into this sysfs file; this file only reflects values written to it at runtime).
   Do not treat an empty value here as evidence the quirk didn't apply — see next criterion.
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
- **Positive evidence**: criterion 3 true, criterion 1 confirms the parameter was live, and
  criterion 4 finds no sticky/−110 failure this boot.
- **Negative evidence**: criteria 1 and 3 true, but criterion 4 still finds the failure.
- **Inconclusive**: criterion 1 false (edit didn't take), criterion 3 false (device didn't
  enumerate normally for an unrelated reason), or the boot wasn't a genuine shutdown→power-on
  cycle (see §7).

One successful boot is evidence, not proof of permanent reliability — this mirrors the
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
- The exact CachyOS git tag named in the task brief does not correspond to a tag this session
  could fetch; the semantic claims in §1–§2 are sourced from current upstream
  `torvalds/linux` plus a symbol-level cross-check against the actual installed
  `7.2.10-1-cachyos` `vmlinux`, not a byte-identical source diff of the CachyOS tree.
  `hub_port_connect` itself is not independently disassemblable due to LTO inlining.
  CachyOS's own patch repository shows no USB-core patches, which is the basis for treating
  upstream behavior as authoritative here.
- The timing hypothesis (§4) is plausible and testable but unproven; the 9-second existing gap
  not being sufficient is suggestive, not conclusive, of why `DELAY_INIT`'s specific placement
  might matter.
- Reproducibility was confirmed across 5 of 5 recently sampled shutdown→power-on cycles, which
  is strong but not unlimited evidence; a single future test boot is one more data point, not a
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
