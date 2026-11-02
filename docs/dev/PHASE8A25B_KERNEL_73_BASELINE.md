# Phase 8A.25B – Linux 7.3.0-5 normal-boot baseline

Status: passive observation of ONE ordinary boot. No experiment was run: no experimental boot entry, no module
operation, no reconnect, no USB reset, no hardware write, no test playback. Phase 8A.25 (unbound reconnect) is
**not** executed.

## Why this baseline exists

After Phase 8A.24 a system update replaced `linux-cachyos` 7.2.10-1 with 7.3.0-5 (13:54, 2026-10-24) and
regenerated `/boot/limine.conf` (new default kernel entry, new Snapper snapshot entry). The Phase 8A.24 backup of
`limine.conf` therefore no longer equals the live file. This is expected; the old backup was not touched. The
`rollover` code records both hashes and the "identical" flag without requiring equality, and a later
`entry install` writes a fresh backup of the then-current configuration (it refuses to overwrite an existing one).

Preflight (read-only, before the reboot): module tree for 7.3.0-5 present including `snd-usb-audio.ko.zst`;
default entry 2 = `linux-cachyos` 7.3.0-5; its image hashes match the files in `/boot`; all `limine_history` files
referenced by the snapshot entry exist; no experimental entry; CONNECT 2 on port `3-2` behind controller
`0000:0d:00.4`; 8A.19 and 8A.24 state preserved.

## Safety

Fresh confirmation was obtained from the user before the reboot (playback/recording stopped, speakers/amplifiers
protected, headphones/listeners protected, KDE volume and device mute not relied upon, understanding that the
normal probe may write volume values). The user performed one manual restart into the ordinary default entry.

## Observed (boot started 14:39:00, kernel 7.3.0-5-cachyos, no experimental arguments)

| Item | Result |
|---|---|
| Boot entry | ordinary default; cmdline contains no OpenWitty/blacklist arguments |
| `snd_usb_audio` | autoloaded normally |
| USB identity | `29c2:0004`, port `3-2`, bus 3 device 2, controller `0000:0d:00.4`, bcdDevice 1.21 |
| Enumeration | 14:39:02 |
| Mixer failure + driver registration | 14:39:11 (about 9 s after enumeration; request latencies not observed) |
| FU2 (unit 2) | `broken mixer GET_CUR (-15360/0/128 => -6016)` – **same value as the earlier sticky readbacks** |
| Unit 5 | `broken mixer GET_CUR (0/18432/256 => 6912)` – reported separately, not counted as FU2 failure |
| `usb_set_interface failed (-110)` | present, unit 2, 14:39:17 |
| ALSA `PCM Playback Volume` | **present** (differs from all earlier failing boots) |
| WirePlumber | `spa.alsa: Unable to load mixer` (x2) at 14:39:17 |
| PipeWire routes / `route.hw-volume` | none: Device has 0 Route / 0 EnumRoute entries, so no hardware-volume route |
| Sink | analog stereo, software volume 30 % |
| GET-only snapshot (`read settings --method interface --interface 3`) | Speaker −23.50 dB (unmuted), Headphone −32.40 dB (muted); identical to the pre-reboot values |
| Journal | readable and complete for this boot |
| Audio observations by user | no pops or other audible anomalies noticed (silence does not exclude electrical transients) |

Kernel wording change: kernels up to 7.2 log `sticky mixer values (...), disabling`; 7.3 logs
`broken mixer GET_CUR (...)` for the same condition. `tools/diagnostics/post_boot_health.py` only knew the old
wording and initially reported `fu2_sticky=false`; it now accepts both (offline tests added). The tool's own
classifier is unchanged and returns INCONCLUSIVE for this mixed picture (control present, no hw-volume route).

## Classification: FAILED

The original FU2 failure reproduces on 7.3.0-5 at the kernel level: unit-2 GET_CUR returns the same −6016
(= the physical speaker level), `usb_set_interface` for unit 2 fails with −110, and WirePlumber cannot load the
mixer, so there is no PipeWire hardware-volume route.

Caveats:

* The kernel no longer drops the control: `PCM Playback Volume` exists. Whether that control actually works was
  not tested (no writes were allowed), so "volume works" must not be inferred.
* This is one boot; one observation. The kernel change is not shown to be a cause or a fix of anything.
* The mixer failure timestamp (14:39:11) is a log time, not a measured request latency.

## Consequences

* Phase 8A.25 comparison with 8A.21–8A.24 (kernel 7.2.10) is no longer clean. Because the failure still
  reproduces on 7.3.0-5, running 8A.25 on 7.3.0-5 is possible but would carry a kernel-version caveat. **It is not
  authorized yet** and needs renewed approval.
* Before any 8A.25 install the plan must be updated for the 7.3 baseline: the profile's kernel gate and the
  documentation of "backup != live config" after the update; the old 8A.24 backup must be archived by `rollover`,
  never overwritten.
* Open question for OpenWitty: with the control present but non-functional-or-unknown, a read-only health
  diagnostic must not rely on `PCM Playback Volume` alone; `route.hw-volume` and the FU2 kernel signature are the
  better indicators.

Raw logs and JSON stay outside Git (`~/.local/state/openwitty/baseline-73/`, mode 0700).
