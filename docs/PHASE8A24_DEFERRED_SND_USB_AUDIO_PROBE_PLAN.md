# Phase 8A.24 – Deferred `snd-usb-audio` probe: preparation plan

Status: **PREPARED, NOT EXECUTED.** Nothing in this phase rebooted, shut down, reconnected USB, loaded/unloaded a
module, wrote hardware controls or changed the bootloader. The boot experiment needs separate authorization.

## Question and rationale
Does delaying the *first* `snd-usb-audio` probe until Linux (and the desktop session) has settled prevent the FU2
failure? Evidence so far (8A.19–8A.23B): every boot-time probe fails (FU2 sticky, no `PCM Playback Volume`,
`route.hw-volume=false`, interface alt-0 `-110`), including after power-removal before boot and with S5 USB standby
power disabled; a probe after a live replug on a settled host succeeds. The FU2 sticky readback equals the real
speaker level (−4.00 dB earlier, −23.50 dB recently): FU2 reads correctly but ignores the kernel's test SETs in the
failing state. Mechanism unproven; this experiment tests *only* "probe context/timing at boot".

Difference from the live replug (8A.21): there, power-up, enumeration and probe all happen together on a settled
host. Here the device is enumerated at kernel start (≈21 ms) as usual, stays idle for minutes, and the audio probe is
triggered late by `modprobe` (task `modprobe`, not `udev-worker`). A healthy result would implicate boot-time probe
timing/concurrency; a failing one would point at the device/enumeration state itself (an enumeration-time property of
a long-idle boot-enumerated device) rather than probe timing.

## Verified module-loading facts (this installation, kernel 7.2.10-1-cachyos)
- `snd-usb-audio` is a **loadable module** (`/lib/modules/7.2.10-1-cachyos/kernel/sound/usb/snd-usb-audio.ko.zst`),
  not in `modules.builtin`. Dependencies: `snd-usbmidi-lib, snd-pcm, snd-hwdep, snd-ump, mc` (snd-pcm is already
  loaded for HDA/HDMI).
- It is **not in the initramfs** (`lsinitcpio` of the active image lists amdgpu/nvme/usb-storage/uas/… but no
  `snd-usb*`); `MODULES=()` in `/etc/mkinitcpio.conf`. xHCI/USB core and usbhid are built in. So the module is
  loaded in the root-filesystem stage by udev coldplug via its USB aliases (generic `ic01isc01`/`ic01isc03` class
  aliases plus vendor/product aliases; 273 aliases). The CONNECT 2 interface `MODALIAS` is
  `usb:v29C2p0004d0121dcEFdsc02dp01ic01isc02ip20in01`.
- There is no `/etc/modprobe.d` or `/etc/modules-load.d` entry for it; nothing else (PipeWire, KDE) loads it.
- It is the only driver for USB audio here: the only audio-class USB device is CONNECT 2 (`3-2:1.0–1.2`).
  Suppressing the module affects no other USB audio device on this machine (other devices: HID, storage, hubs, BT).
- **Mechanism choice** (not interchangeable):
  - `module_blacklist=snd_usb_audio` (kernel parameter): checked in `load_module()` (`kernel/module/main.c`,
    `blacklisted()`), rejects *every* load, including an explicit `modprobe snd-usb-audio` (`-EPERM`). **Rejected** —
    it would also block the deliberate load.
  - `/etc/modprobe.d/*.conf` `blacklist snd_usb_audio`: kmod filters alias-driven loads only; explicit `modprobe` by
    name works. But it persists on disk, would need cleanup and (for initramfs users) an image rebuild. **Not used.**
  - `modprobe.blacklist=snd_usb_audio` (kernel command line, read by libkmod from `/proc/cmdline`): same filtering
    as the config file (alias loads by udev's kmod builtin are skipped, explicit `modprobe snd-usb-audio` is
    allowed), nothing on disk, disappears with the boot entry. **Chosen.** libkmod 34 here contains the parser;
    its effect is confirmed after boot by `modprobe -c` (prints `blacklist snd_usb_audio`) and by the gate below.
  - `delayed_register` (snd-usb-audio module parameter) only delays card *registration* until a given interface
    probe; the control/stream setup and mixer probe would still run at boot. **Not suitable.**

## Temporary Limine entry (exact)
`tools/diagnostics/deferred_probe.py entry install` appends, at the end of `/boot/limine.conf`, this block (the
original normal entries and `default_entry: 2` remain untouched; the new entry is never the default):
```
# OPENWITTY-8A24-BEGIN (temporary deferred snd-usb-audio entry; remove after use)
/OpenWitty deferred snd-usb-audio (temporary)
  protocol: linux
  module_path: <same as the normal linux-cachyos entry>
  path: <same as the normal linux-cachyos entry>
  cmdline: <normal cmdline: quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=…> modprobe.blacklist=snd_usb_audio
# OPENWITTY-8A24-END
```
Only one argument is added; no tracing, ftrace, dyndbg, USB quirk, `usbcore` or host-controller argument. The tool
refuses if the source cmdline already carries a test argument. A backup (`/var/lib/openwitty-8a24/limine.conf.before`
+ sha256) is written on install; `entry remove` removes exactly the marked block and verifies byte-identity with the
backup; `entry verify` repeats that check later. The current UEFI S5 setting stays unchanged.

## Procedure (future, single experiment)
0. Pre: clean tree at the pushed commit; `deferred_probe.py entry install`; `entry status`; confirm normal entry still default.
1. **Physical audio-safety gate (mandatory, before shutdown and again before the load):** playback/recording stopped;
   speakers/amplifiers off, disconnected or independently protected; headphones/listeners protected; no reliance on
   KDE software volume or CONNECT 2 mute; the kernel mixer probe may write hardware volume (possibly 0 dB, as in the
   failing boot sequence: SET −60 then 0 dB, final 0 dB without restore); exactly one boot, one load.
2. The user performs a normal restart (or shutdown/power-on) and selects **"OpenWitty deferred snd-usb-audio
   (temporary)"** in the Limine menu.
3. After login and a settled desktop (≥ 2 minutes idle), **as the normal user** (not root), inside the desktop session:
   `RUN=$(python3 -I tools/diagnostics/deferred_probe.py prepare)` → creates the private run directory
   `~/.local/state/openwitty/trace-8a24/run-<timestamp>/` (0700, owned by the user, with `experiment.json`).
   Then `python3 -I tools/diagnostics/deferred_probe.py check --run-dir "$RUN"` (journal read falls back to
   `sudo -n journalctl`). Writes `checkpoint.json`. The **gate** must pass; every item fails closed:
   - kernel command line contains `modprobe.blacklist=snd_usb_audio` and no `module_blacklist`;
   - `/sys/module/snd_usb_audio` absent; no interface of CONNECT 2 bound to any driver;
   - **identity**: exactly one device `29c2:0004`, on port **`3-2`**, behind xHCI **`0000:0d:00.4`**. A different port,
     a different controller, a missing controller identity or several matching devices ⇒ INVALID. Nothing is reset or
     re-enumerated to "fix" a mismatch;
   - **journal evidence verified**: `journalctl -k -b 0` must succeed, be non-empty, contain the `Linux version`
     boot-start line and the CONNECT 2 enumeration line. Statuses `FAILED` (command error), `UNREADABLE`
     (permission), `EMPTY`, `INCOMPLETE` (e.g. a non-member only sees user messages) all fail the gate — an
     unreadable journal is never taken as proof that the driver has not registered. Only a verified journal without
     `registered new interface driver snd-usb-audio` passes;
   - `modprobe -c` reports `blacklist snd_usb_audio`.
   If anything fails: **INVALID** — do not load; restore and report.
4. Record: boot time, enumeration timestamp, checkpoint time and boot id (inside `checkpoint.json`, outside Git).
5. Re-confirm the physical safety gate with the user, then run exactly once, with the **same** run directory:
   `sudo python3 -I tools/diagnostics/deferred_probe.py load --run-dir "$RUN" --confirm-audio-safe`.
   Root-side checks before anything is loaded: the flag is present; `/var/lib/openwitty-8a24/load_done` does not exist;
   the run directory is an absolute, symlink-free 0700 directory owned by the user whose uid equals `SUDO_UID` and
   to the `experiment.json` uid (all later file access is `dir_fd`-relative with `O_NOFOLLOW`, files are created
   exclusively with mode 0600 and `fchown`ed to the user, so root never follows a planted link and the user can
   read the result); `checkpoint.json` is an OK gate of **this boot** (boot id) not older than 30 minutes; the gate is
   re-evaluated with root's journal access. Then the single-load marker is created atomically (`O_EXCL`, root-owned,
   0600) **before** `modprobe snd-usb-audio` runs; a second invocation, even for another run directory, is refused.
   `load.json` records wall/uptime before and after and the return code.
6. **As the normal user, inside the desktop session** (PipeWire is per-user; the tool refuses to run as root):
   `python3 -I tools/diagnostics/deferred_probe.py classify --run-dir "$RUN" --cli build-release/openwitty-cli`.
   It reads `checkpoint.json` and `load.json` from the same directory and writes `classification.json` there.
   It collects, restricted to events after the load time: sticky verdicts (unit 2 vs unit 5 separately),
   `usb_set_interface failed (-110)`, `PCM Playback Volume`, PipeWire analog-output `route.hw-volume`, a journal
   status for the after-load slice (a non-OK status makes the result INCONCLUSIVE), and the GET-only hardware
   snapshot. The snapshot is parsed from the real CLI format (`src/cli/main.cpp`): Speaker and Headphone volume and
   mute are on the `phantom=` line (`phantom=1  speaker=-23.50 dB  headphone=-32.40 dB (muted) …`); the parser
   stores `speaker_db`, `speaker_muted`, `headphone_db`, `headphone_muted` plus the microphone/instrument/phantom
   lines (never `raw=`) and a `complete` flag; a missing or garbled line yields `complete: false`. Only the existing
   GET-only `read settings` is used. No playback, no volume change, no reconnect, no reload.
7. Cleanup: `sudo … entry remove`, `entry verify`; next boot uses the normal entry; delete `/var/lib/openwitty-8a24`. The run directory stays outside Git.

## Evidence and what it can and cannot show
- Distinguishes **USB enumeration** (journal `New USB device found`, kernel start + ≈21 ms), **probe start**
  (the `modprobe` time recorded by the tool; the probe is synchronous with the load), and **registration**
  (`registered new interface driver snd-usb-audio` and card creation after the load).
- No tracing is enabled, so `SET_INTERFACE` latencies are NOT measured and are not inferred from journal
  timestamps; a missing "timed out or killed" line is not evidence of no timeout (dynamic debug is off). The
  pass/fail criteria use only the sticky verdicts, `usb_set_interface failed (-110)` and the ALSA/PipeWire state.
  (An optional later variant could add the private-instance tracer of 8A.21 around the load; deliberately not
  part of this first, uninstrumented experiment.)

## Classification
- **INVALID**: gate fails (module loaded/registered before the deliberate action, blacklist ineffective, wrong entry,
  device not enumerated or already bound).
- **POSITIVE**: `PCM Playback Volume` present AND output `route.hw-volume` true AND no unit-2 sticky verdict after the
  load. Supports "probe context/timing at boot matters"; not a proof of the mechanism, and the same state could
  be reached by a replug (8A.21).
- **NEGATIVE**: unit-2 sticky present, `PCM Playback Volume` absent, hw-volume false. A late, quiet probe of a
  boot-enumerated device still fails → the problem is tied to the device/enumeration state, not probe timing at boot.
- **INCONCLUSIVE**: `modprobe` failed, mixed or incomplete evidence. A unit-5-only sticky message never counts as
  an FU2 failure. No automatic repetition in any case.

## Risks and rollback
- The load triggers the same mixer probe as a normal boot, so physical output level/mute can change (earlier replug:
  −33.34 dB muted → −4.00 dB unmuted; speaker restore by the kernel is not guaranteed). Safety gate above.
- If `modprobe.blacklist` fails to stop udev's autoload the test is INVALID (detected by the gate; the situation is
  then equal to an ordinary boot, which already happens daily).
- No audio device exists in the desktop until the load (expected; HDMI/other sound cards unaffected).
- Rollback at any time: reboot and choose the normal "linux-cachyos" entry (default; the experimental entry is never
  default) — or, in the experimental session, simply do nothing (no module is loaded, no hardware is touched).
  Cleanup of the bootloader: `sudo python3 -I tools/diagnostics/deferred_probe.py entry remove && … entry verify`.
  If the tool is unavailable, remove the block between the two `OPENWITTY-8A24` marker lines or restore
  `/var/lib/openwitty-8a24/limine.conf.before` (sha256 stored alongside).

## Resume instructions after the boot
Tell Claude "8A.24 resume". Claude will run `check` first and stop on any gate failure; it will not run `load`
until the user re-confirms the physical safety gate in that session.

## Validation of the preparation
`test_deferred_probe.py` (32 offline tests, CTest `deferred_probe_offline`): entry text/round-trip/refusals; temp-file
install–remove–verify; gate invalidators incl. wrong port/controller/missing controller/ambiguity; journal status
matrix and sudo fallback; classification matrix; hardware-snapshot parsing of the real CLI format; run-directory
handoff (private user-owned directory, user and simulated-root resolve the same directory, symlink/relative/`..`/
wrong-mode/`SUDO_UID` mismatch rejected, symlinked file not followed, root-written files chowned to the user, 0600);
full check → load → classify flow on one directory with single-load protection, stale/other-boot checkpoint refusal,
failed-gate and failed-`modprobe` paths. In addition a real `sudo` run (with a stubbed gate and a stubbed `modprobe`
that loads nothing, a throw-away marker directory and run directory, all deleted afterwards) confirmed on this machine
that root-created `load.json`/`checkpoint.json` come out `<user>:<user> 0600`, the marker is root-only, and the
unprivileged `classify` reads them, while `classify` as root is refused.
