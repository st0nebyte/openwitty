# Phase 8A.25 – Unbound reconnect before the first audio-driver probe: result

Classification: **NEGATIVE** (kernel 7.3.0-5-cachyos). One experimental boot, one physical reconnect, one guarded
module load. Nothing was repeated. Raw run data stays outside Git (`~/.local/state/openwitty/trace-8a25/`, mode 0700).

## Question
Does a physical re-enumeration on a settled host **while `snd-usb-audio` is not loaded**, followed by the first probe,
behave like Phase 8A.21 (healthy) or like Phase 8A.24 / a normal boot (FU2 failure)?

## Preparation and safety
* Phase 8A.24 state archived with `rollover` to `/var/lib/openwitty-archive/8a24-<timestamp>/` (old `limine.conf.before`
  sha256 `8017afbb…`, original load marker, NEGATIVE classification, run reference). The old 7.2 backup does **not** match the
  regenerated 7.3 `limine.conf` (`23850fd4…`); recorded in `ROLLOVER.txt` as historical metadata, never restored or rebased.
* Temporary entry `OpenWitty unbound-reconnect snd-usb-audio (temporary)` installed from the ordinary 7.3.0-5 entry (never a
  snapshot), adding only `modprobe.blacklist=snd_usb_audio`; a **new** backup of the current 7.3 config was written.
* Fresh physical audio-safety confirmations from the user at three checkpoints: before the reboot, before the reconnect, before the
  load (playback/recording stopped; speakers/amplifiers and headphones/listeners protected; KDE volume and device mute not relied
  upon; understanding of possible level changes and the −60 dB / 0 dB probe writes).

## Sequence (all physical steps by the user)
| Time (2026-10-24) | Event |
|---|---|
| 18:57:02 | boot of the temporary entry, kernel 7.3.0-5-cachyos, cmdline = normal + `modprobe.blacklist=snd_usb_audio` |
| 18:57:05 | CONNECT 2 enumerated, port 3-2, device number 2 |
| 18:59:30 | baseline gate OK: driver absent and unbound, one device on 3-2 / controller `0000:0d:00.4`, journal verified (boot id prefix `d396d14b`) |
| ≈19:04:31 | user unplugs CONNECT 2 (observer: disconnect) |
| 19:04:34 | user replugs into the same port; new enumeration, **device number 3** (observer + journal: exactly 1 disconnect, 2 enumerations) |
| 19:04:52 | post-reconnect gate OK: driver still absent and unbound, identity unchanged |
| 19:06:34 | the single guarded `modprobe snd-usb-audio` starts (rc 0, finished 19:06:40) |
| 19:06:40 | FU2 and unit-5 `broken mixer GET_CUR`; driver registered |
| 19:06:45 | `usb_set_interface failed (-110)` (unit 2) |

**Reconnect → load delay: 120.5 s** (Phase 8A.21 probed ≈0.15 s after reconnect). Boot-enumeration → load is not applicable here;
the device had been re-enumerated 2 min earlier.

## Hardware-volume state
| | before the load | after the load |
|---|---|---|
| driver | absent, interfaces unbound | bound |
| ALSA `PCM Playback Volume` | n/a | present (works or not: **untested**, no writes allowed) |
| PipeWire CONNECT 2 | n/a | device + sink + source, **0 Route / 0 EnumRoute** (`DEVICE_NO_ROUTES`) → no `route.hw-volume` |
| Sink | | analog stereo, software volume 30 %, profile `output:analog-stereo+input:analog-surround-40` |
| GET-only Speaker / Headphone | −23.50 dB unmuted / −32.40 dB muted (before the experiment) | −23.50 dB unmuted / −32.40 dB muted (unchanged) |

## FU2 / unit 5
* Unit 2 (FU2): `broken mixer GET_CUR (-15360/0/128 => -6016)` — the same value as in the 7.2 sticky readbacks and the 7.3 normal boot.
* Unit 5: separate `broken mixer GET_CUR (0/18432/256 => 6912)`; not counted as an FU2 failure.
* `usb_set_interface failed (-110)` present, about 5 s after driver registration. No ep0 timeout lines (dynamic debug not enabled; absence
  of the log line is not absence of a timeout).
* The first passive classify ran 104 s after the load (stack settled); journal after load complete (13 lines).

## Classification and limitations
Rule applied (`classify_evidence`): FU2 failure + CONNECT 2 `DEVICE_NO_ROUTES` + settled stack → **NEGATIVE**, although ALSA exposes a
volume control.

* NEGATIVE means: the sequence tested here (re-enumeration without a bound audio driver, then a probe 120.5 s later) did **not** produce the
  healthy 8A.21 state. It does **not** prove that the audio driver must already be loaded at reconnect time: the reconnect → probe
  interval is about 800× longer than in 8A.21, the experiment ran on kernel 7.3.0-5 whereas 8A.21–8A.24 ran on 7.2.10, and the user
  physically unplugged for a few seconds rather than the 8A.21 sequence.
* It does not isolate firmware readiness, power cycling, bus reset or controller state, and it validated no hardware SET behaviour.
* One observation; no repetition (single-load marker `load_done.<boot_id>` stays in `/var/lib/openwitty-8a25/`).

## User audio observation
Nothing audible at the reconnect or at the driver load (no pop, click or level jump noticed). Silence does not exclude electrical transients.

## Rollback verification
`entry remove` + `entry verify`: `/boot/limine.conf` is byte-identical to the 8A.25 pre-install backup (sha256 `23850fd4…`,
89651 bytes). `default_entry: 2`, `timeout: 5`, the normal and snapshot entries are unchanged; no OpenWitty entry remains. The running
kernel still carries the temporary argument until the next boot (no reboot was performed to clear it).
No USB reset, port power cycling, authorization change, rebind, firmware or volume/mute SET, and no playback occurred.

## Implications for the next investigation
* The failure was not avoided by a *late probe after boot* (8A.24), by an *unbound reconnect followed by a late probe* (this phase), and it also appears on the newer kernel (7.2.10 → 7.3.0-5); none of this isolates a cause.
  What remains from the evidence: only the live replug **with the driver already bound** (8A.21) went healthy, i.e. a probe shortly after
  enumeration while the host is in the same moment, or driver-side activity during enumeration, may matter.
* Candidate next steps (each needs its own approval): an 8A.21-style replug on 7.3.0-5 with the driver bound to check whether that healthy
  path still exists on the new kernel; a Windows-side startup-timing comparison (handoff in `PHASE8A22_WINDOWS_CODEX_HANDOFF.md`);
  a design review of the `SKIP_IFACE_SETUP` quirk as a single-flag trial.
* OpenWitty health diagnostics must not treat `PCM Playback Volume` as success; use the FU2 signature (both kernel wordings) and the
  CONNECT 2 route state.
