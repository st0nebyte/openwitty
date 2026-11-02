# Phase 8C – Speaker volume on Linux 7.3: root causes, quirk and vendor-write workaround

Status: fixes implemented and verified live on kernel 7.3.0-5-cachyos (2026-10-24), owner-authorized autonomous debugging.
**Corrected by Phase 8E (2026-10-25):** on an ordinary boot Feature Unit 2 *does* honour SET_CUR; "broken GET_CUR" only means
the kernel cannot read the level back. The "SET ignored" state below was measured only after software re-enumeration. With
the kernel quirk of problem 1, normal KDE/PipeWire volume works after an ordinary boot (one observation so far). The maintained
integration is the native `openwitty-volumed` (Phase 8D, `PHASE8D_VOLUME_INTEGRATION.md`); the Python bridge is a superseded
reference and must not be installed as a service.

## Two independent problems

### 1. PipeWire never got a hardware-volume route: unit 5 breaks the whole ALSA mixer (FIXED)
* Kernel 7.3 changed the "sticky GET_CUR" handling (`sound/usb/mixer.c`, `check_sticky_volume_control()`): instead of disabling a
  control ("sticky mixer values … disabling", 7.2), it marks it `get_cur_broken` and serves values from a cache only.
* CONNECT 2 unit 5 (`Mic Capture Volume`, 4 channels) is always "broken GET_CUR"; channels 3/4 STALL on SET, so they never get a
  cached value and `snd_usb_get_cur_mix_value()` returns **-ENXIO** for every read (`amixer cget numid=7` → "No such device or address").
* alsa-lib's simple mixer load aborts on that one read error (`amixer scontents` → "Mixer load error: No such device or address";
  `snd_mixer_load()` = -6). WirePlumber's `spa.alsa` therefore logs `Unable to load mixer` and creates **no Route/EnumRoute** for the
  card – on every boot and after every reconnect, independent of FU2. On 7.2 the control was removed, so the mixer loaded.
* Fix: `quirk_flags=29c2:0004:ignore_ctl_error` (per-device quirk; `filter_error()` turns read errors into 0). Verified: mixer loads,
  `Mic Capture Volume` reads `72,72,0,0`, PipeWire shows 2 routes and `analog-output` `route.hw-volume=true`.
  Persistent in `/etc/modprobe.d/openwitty-connect2.conf` (snd-usb-audio is not in the initramfs, no rebuild needed).

### 2. Feature Unit 2: GET_CUR is broken; SET_CUR is honoured except in some states (WORKAROUND KEPT AS FALLBACK)
Two different properties were mixed up during the investigation; they are separated here.
* **GET_CUR (read-back):** the kernel's sanity test sees a constant value and logs `broken mixer GET_CUR` (7.3) /
  `sticky mixer values` (7.2). The value is the device's real current level. Consequence: the kernel never learns about
  hardware-knob changes.
* **SET_CUR (write):** measured directly only where a vendor GET was taken after an ALSA/PipeWire volume change:

| Device state | FU2 SET measured with vendor read-back |
|---|---|
| ordinary boot, autoload (Phase 8E, 2026-10-25) | **followed** (−33 → −34 dB) |
| physical replug with the driver loaded (8A.26, 8A.26B, 8C) | followed |
| software re-enumeration via `authorized` 0→1 (8C, 21:14) | **ignored** (sink 0.30, device stayed −20 dB) |
| `USBDEVFS_RESET` port reset (8C, 21:16) | **ignored** (device stayed −23.5 dB) |
| same instance later, after several vendor writes (8D live test) | followed |
| late load after boot (8A.24), unbound replug + late load (8A.25) | not measured (only the GET classification) |

Earlier versions of this table called the ordinary boot "SET ignored"; that was an inference from the GET classification and
is wrong for the boot measured in Phase 8E. Whether ordinary users can reach the "ignored" state (e.g. through suspend/resume,
which re-initializes USB) is open. The controller has no per-port power switching (HCCPARAMS PPC=0).

**The vendor settings message on interface 3 is honoured in every state.** One `speaker-volume` vendor write in the FU2-broken state
was verified (−23.5 → −30.0 dB, read back by GET). Hence the workaround:

* `openwitty-cli write speaker-volume <-60..0>` / `speaker-mute <on|off>`: same gated single-field path as `led-brightness`
  (exact byte-diff check: bytes 20-21 resp. 22 only; `LEWITT_ALLOW_HARDWARE_WRITE=1` + `LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1`;
  fresh snapshot, one SET, read-back verification).
* `tools/volume_bridge/openwitty_volume_bridge.py` (user service): listens to `alsactl monitor` of the CONNECT 2 card, debounces,
  and mirrors ALSA `PCM Playback Volume` (dB, 0.5 dB steps) and `PCM Playback Switch` to the device via the CLI; pushes the state
  once whenever the card (re)appears. Verified live in the FU2-broken state: sink 0.30 → −31.0 dB, 0.50 → −18.0 dB, 0.40 → −23.5 dB,
  mute/unmute followed. In the healthy state it writes the value FU2 already applied (consistent, idempotent).

## Installation
```
# kernel quirk (already installed here):
#   /etc/modprobe.d/openwitty-connect2.conf  ->  options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error
# bridge as user service:
mkdir -p ~/.config/systemd/user
cp tools/volume_bridge/openwitty-volume-bridge.service ~/.config/systemd/user/
systemctl --user daemon-reload && systemctl --user enable --now openwitty-volume-bridge.service
```
Rollback: `systemctl --user disable --now openwitty-volume-bridge.service`, delete the unit, `sudo rm
/etc/modprobe.d/openwitty-connect2.conf`.

## Limits
* Hardware-knob changes are not mirrored back to PipeWire; the next slider move wins.
* Each CLI write logs a harmless `usbfs: … did not claim interface 3 before use` kernel line.
* In the FU2-broken state PipeWire still issues FU2 SET_CUR requests (ignored by the device); the vendor write is what takes effect.
* Headphone volume is not bridged (no ALSA control for it).
