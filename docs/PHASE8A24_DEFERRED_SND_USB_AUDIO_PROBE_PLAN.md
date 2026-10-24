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
3. After login and a settled desktop (≥ 2 minutes idle): `python3 -I tools/diagnostics/deferred_probe.py check`
   (root for the journal). The **gate** must pass: command line contains the blacklist and no `module_blacklist`;
   `/sys/module/snd_usb_audio` absent; CONNECT 2 (29c2:0004) enumerated on port `3-2`; no interface of it bound to a
   driver; no `registered new interface driver snd-usb-audio` in the journal; `modprobe -c` reports the blacklist.
   If the module is already loaded or registered: **INVALID** — do not load; restore and report.
4. Record: boot time, enumeration timestamp, checkpoint time (`checkpoint.json`, outside Git).
5. Re-confirm the safety gate with the user, then run exactly once:
   `sudo python3 -I tools/diagnostics/deferred_probe.py load --confirm-audio-safe`
   (refuses without the flag, refuses if the gate fails, refuses a second run via `/var/lib/openwitty-8a24/load_done`,
   records wall/uptime before and after and the `modprobe` return code).
6. `python3 -I tools/diagnostics/deferred_probe.py classify --cli build-release/openwitty-cli`: collects, restricted to
   events after the load time: sticky verdicts (unit 2 vs unit 5 reported separately), `usb_set_interface failed
   (-110)`, `PCM Playback Volume`, PipeWire analog-output `route.hw-volume`, GET-only Speaker/Headphone state, and the
   deliberate-load timestamps. No playback, no volume change, no reconnect, no reload.
7. Cleanup: `entry remove`, `entry verify`; next boot uses the normal entry; delete `/var/lib/openwitty-8a24`.

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
`test_deferred_probe.py` (13 offline tests: entry text/round-trip/refusals, install–remove–verify on a temp file,
gate invalidators, classification matrix, load guards) is registered as CTest `deferred_probe_offline`.
