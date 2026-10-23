# Phase 8A.17 — offline analysis of the 5.5–6 s `snd-usb-audio` initialization delay

Offline only: exact-tag kernel source, the installed `snd-usb-audio` module and `vmlinux`, recorded
journals, live read-only sysfs/dynamic-debug *listings*. No reboot, USB access, module/udev/service
change, tracing, BPF, dynamic-debug *enabling*, ALSA mixer query, or audio change. KDE volume (40%) untouched.

## Result in one paragraph
A credible, source-reachable explanation exists but is **unproven**: during probe, `snd-usb-audio`
issues several `usb_set_interface()` calls whose return values are ignored; each sends a control
request with a **5000 ms** timeout that, on timeout, produces **no default-level log line**. A device
that does not answer such a request would stall the probe silently for ~5 s. The same stall mechanism
is *demonstrated* later in every boot (the `usb_set_interface failed (-110)` line is a 5 s timeout).
But the **absolute-time clustering** of the end of the hold (±2–3 ms across boots whose udev start
varied by 130 ms) argues against a plain "fixed 5.000 s counted from a variable start", so the end of
the hold seems gated by something anchored in the boot timeline (e.g. device/host readiness), not by
the timeout alone. The delay is in **kernel-side driver probing (inferred)**, not in plain module
loading; its exact blocking call has not been observed. A one-boot, low-perturbation test exists (§9).

## 1. Kernel provenance (corrects earlier statements)
- Installed: `7.2.10-1-cachyos`; `snd-usb-audio.ko.zst` vermagic matches; `vmlinux` in the headers
  tree matches (`version` file).
- **Correction to Phase 8A.12:** the exact tag `cachyos-7.2.10-1` (commit `11636526…`) *is* fetchable
  (`gh api repos/CachyOS/linux/contents/<path>?ref=cachyos-7.2.10-1`). Earlier documents said it was
  not; they relied on upstream `master` plus binary checks. All source below is from the exact tag.
  Re-verified at the tag: `29c2` has no entry in `drivers/usb/core/quirks.c`; `udev->quirks ^=
  usb_detect_dynamic_quirks()` (XOR) at line 782; `msleep(2000)` at `hub.c:5523`; `msleep(200)` at
  `config.c:984` (per configuration); `hub_port_init` prints `new … USB device number` (line 5015)
  *before* `usb_get_device_descriptor` and `usb_detect_quirks` (line 5222). So the Phase 8A.12–8A.14
  mechanism descriptions stand.
- `USB_CTRL_GET_TIMEOUT = USB_CTRL_SET_TIMEOUT = 5000` (`include/linux/usb.h:1936–1937`).
  `snd_usb_ctl_msg()` (`helper.c`) picks one of them and calls `usb_control_msg` once: **no retry
  loop, no sleep.** The installed module agrees: both `usb_control_msg` call paths in `snd_usb_ctl_msg`
  push `$0x1388` (= 5000) as timeout. `usb_set_interface()` (`message.c`) sends SET_INTERFACE with a
  literal **5000** ms timeout.
- Timeout path: `usb_start_wait_urb` → `wait_for_completion_timeout`; on expiry it returns
  `-ETIMEDOUT` and emits only a **`dev_dbg`** ("… timed out or killed on ep…", `message.c`), which is
  compiled in (`CONFIG_DYNAMIC_DEBUG=y`; `/proc/dynamic_debug/control` lists
  `message.c:88 [usbcore]usb_start_wait_urb =_ …` as currently disabled). `usb_set_interface` itself
  logs nothing for a timeout.

## 2. Probe call graph (source, with binary cross-checks)
`module_usb_driver()` → `usb_register` → driver core attaches to the already-present interface →
`usb_audio_probe(3-2:1.0)` **synchronously** (no `probe_type`/async flag; the registration message is
expected after `driver_register()` returns — `usb_register_driver` was not re-read here). Order inside `usb_audio_probe` (exact tag):
1. `snd_usb_apply_boot_quirk` (acts only for quirk-listed IDs; binary: `msleep` paths gated on
   `0644:xxxx` (TEAC `IFACE_DELAY`) and `07fd:0004` — **neither is `29c2:0004`**).
2. `guard(mutex)(&register_mutex)`, `snd_usb_apply_boot_quirk_once`, `snd_usb_audio_create`
   (`snd_card_new`).
3. `snd_usb_create_streams` → for each AudioStreaming interface `snd_usb_create_stream` →
   `snd_usb_parse_audio_interface` → per altsetting with endpoints (CONNECT 2: interface 1 alt 1, EP
   0x01 OUT; interface 2 alt 1, EP 0x82 IN; read from the sysfs-cached descriptors, no USB traffic):
   `snd_usb_parse_audio_format` (class GET requests, **errors are logged**, e.g. "unable to retrieve
   number of sample rates" as seen on a failing reconnect) → add stream/endpoints → **`usb_set_interface(iface,0)`
   → `snd_usb_init_pitch` → `snd_usb_init_sample_rate` → `usb_set_interface(iface, alt)`**
   (`stream.c:1261–1267`, UAC2 → `set_iface_first = false`). **The `usb_set_interface` return values
   are not checked.** Clock-rate failures *are* logged (`dev_warn/err`), except the SET_CUR write
   result in `snd_usb_set_sample_rate_v2v3`.
4. `snd_usb_create_mixer` → descriptor-order unit parse; Feature Unit 2 → `get_min_max_with_quirks` /
   `check_sticky_volume_control`; `get_ctl_value_v2` has **no retry loop** and logs failures only via
   `usb_audio_dbg`.
5. `try_to_register_card` → `snd_card_register` (card3/controlC3 udev stamp 11.445 s).
Not reached for this device: `snd_usb_midi_v2_create` (no MIDI interfaces).
Source-level inference vs binary: the binary confirmed the single-call `snd_usb_ctl_msg`, the 5000
timeout constant, the gated `msleep` sites and which functions reach `usb_set_interface`/`snd_usb_ctl_msg`;
indirect calls through the quirk table and inlined helpers are not visible in a static graph, so the
order above is **source**, not a traced execution.

## 3. Candidate blockers before the Feature Unit 2 check
| # | Candidate | Pre-FU2? | Max wait | Logged on failure? | Boot logs | Reconnect |
|---|---|---|---|---|---|---|
| A | Ignored `usb_set_interface` ×4 (`stream.c:1261–1267`: iface 1 and 2, each alt 0 then alt 1) | yes | 5000 ms each | **no** (`dev_dbg` only) | **no contradiction**, silent by design | same code ran in ~0.18 s total |
| B | `snd_usb_init_sample_rate` / clock selector/validity requests | yes | 5000 ms per request | **yes** (`dev_warn/err`) | **contradicted**: no such messages in any boot | n/a |
| C | `snd_usb_parse_audio_format` GET_RANGE | yes | 5000 ms | **yes** | **contradicted**: message absent at boot (present only in one failing reconnect) | n/a |
| D | Boot quirk waits (`msleep(50)`, 50×100 ms) | yes | up to 5 s+ | partly | **not applicable**: gated on 0644/07fd IDs | — |
| E | `register_mutex` / probe serialization | yes | unbounded | no | no other probe contends; nothing observed | — |
| F | Module load: zstd-decompress 1.4 MB, relocate, dependencies (`snd, snd-usbmidi-lib, snd-pcm, snd-hwdep, snd-ump, mc`) | before probe | expected ms–hundreds of ms | no | cannot be 5.9 s without a blocker; concurrent module loads (HDA, GPU) were not examined for contention | — |
| G | Runtime-PM resume wait (`snd_usb_lock_shutdown`/autopm) | at mixer | bounded by device | no | not distinguishable | — |
| H | FU2 requests themselves (`get_ctl_value_v2`) | no (they *are* FU2) | 5000 ms each | debug only | they succeeded (sticky decision made) | — |

Only **A** is simultaneously reachable before FU2, capable of 5 s, silent, and consistent with the
recorded logs. B and C are contradicted by the absence of their error messages. This is an argument
by elimination within the reviewed code, **not an observation**; E, F, G and unknown workers remain
open.

## 4. Timeout hypothesis — evidence
For: (1) A is source-reachable (above). (2) `-110` is exactly what a 5 s control timeout returns.
(3) A SET_INTERFACE timeout is *shown to occur on this device at boot*: `usb_set_interface failed
(-110)` (the only emitter is `endpoint_set_interface` in `endpoint.c:940`, called from PCM/endpoint
open/close paths — i.e. a userspace-triggered stream operation, not the probe) appears **6.075–6.077 s
after the FU2 sticky line in all six boots**. WirePlumber's first log line follows it by ~0.5–0.58 s in every
boot, consistent with it being blocked in that call. A 6.076 s offset = ~1.07 s userspace chain after
registration + a 5.000 s timeout. (4) A 5.9 s hold ≈ one 5.000 s timeout + ~0.4–0.9 s load/parse.
Against / limits: (a) **Tightness.** Within the three `5-1` boots the sticky line is at 10.915026,
10.918043, 10.915026 s (3 ms spread) while the first real-root udevd log line was at 5.299, 5.309,
5.428 s (129 ms spread) and `found` at 2.049–2.078 s (29 ms). A fixed 5.000 s timeout counted from a
start that follows udev start would inherit ≥ ~0.1 s of that spread. Three `3-2` boots: 11.443,
11.414, 11.433 s (29 ms) although the quirk boot shifted enumeration by +0.94 s. So the *end* of the
hold is anchored near an absolute boot time, not to udev start or enumeration. (b) Pure timeout
accounting would need the request to be issued at X ≈ sticky − 5.0 − ~0.2 s ≈ 5.7–6.3 s, i.e.
0.33–0.85 s after udevd's start, with a compensating spread — a coincidence unless something anchors X too.
(c) No timeout is directly observed before FU2; the dyndbg site that would show it is disabled.
(d) A timeout is not required: a NAKing device completes the request as soon as it becomes ready, so a
request issued early can end at the device's readiness time R, not X+5.0. Mixed case (timeout, then
the next request waits for R) fits the anchoring too.
Conclusion: **timeouts (A) are plausible and likely part of it; the precise gate is not identified.**
Whether the stalled request is an independent problem or the boot bug itself is unknown: the bug is
"sticky FU2 at boot", the stall is *before* it, and both disappear after a physical replug (the
reconnect path ran the same code in ~0.18 s).

## 5. Module-dependency and synchronization alternatives
- Zstd-compressed module (`CONFIG_MODULE_COMPRESS_ZSTD`, all modules) and signature checking add
  ms-scale cost; they cannot by themselves explain 5 s unless storage/CPU is saturated, and `btusb`
  (also compressed) loads within ~0.1 s on the same boots.
- `snd_usb_audio` init is `module_usb_driver`: only `usb_register`; no firmware load, no workqueue wait.
- Probing is synchronous under `register_mutex`; no second sound-USB device exists to contend.
- Module loading by the udev worker is a plain blocking `finit_module` (builtin `kmod load`); several
  interface events share one alias hit (only `1.0`), so no cross-worker serialization is expected.
These are **not excluded** by direct evidence — only unsupported.

## 6. Port-dependent timing
Sticky/registration cluster at ≈10.93 s (3 boots, device on `5-1`, controller `0000:0e:00.0`,
Oct 20 and 22) and ≈11.43–11.46 s (3 boots, `3-2`, controller `0000:0d:00.4`, Oct 23). Same kernel,
no package changes between those boots (`pacman.log`: nothing from Oct 17 to Oct 23 20:19 beyond the
Oct 17 updates). The device was evidently moved between ports once; port and date are therefore
**confounded**. Enumeration times (2.04–2.08 s), udevd start and `btusb`/HDA timings do not explain
the 0.5 s difference. Correlation with the controller/port is real in this sample (n=3+3) but
**not shown to be causal**; per-controller init order or per-port power sequencing are candidate
mechanisms, untested.

## 7. Boot versus reconnect
Reconnect (8A.9 and the recorded reconnects at monotonic ≈2691–2697 s in b-2 and ≈4460 s in b-1): module and ALSA already loaded, dependencies
present, no module-load or card-creation cost; enumeration → `strings` → FU2 sticky/accept within
~0.18 s, i.e. the same probe code, including the four `usb_set_interface` calls and sample-rate
initialization, completes in <180 ms. So **device-probe overhead is ≤~0.18 s when the device
answers**, and the extra ~5.7 s at boot is a stall or wait, not work. Module-load overhead at boot is
independent and expected to be hundreds of ms at most (`btusb` ≈ 0.1 s). The physical replug
power-cycles the device; a boot leaves the host resetting a device that may have been continuously powered,
which is consistent with (but does not demonstrate) a device-state difference.
Separately: the observation that at boot the late `-110` hits `SET_INTERFACE(2, 0)` shows the stall
is not confined to the probe.

## 8. What the udev-debug boot could and could not show
`udev.log_level=debug` (systemd 262 reads it from the kernel command line) can add journal lines for:
event arrival/dispatch to a worker, rule processing, "Running … command" for the queued `kmod load`
builtin and its return, and worker finish — each with a monotonic timestamp. It **cannot** show:
module appearance in sysfs, `init` start, probe entry, the first FU2 request, or *where* inside the
kernel the worker was blocked. It would bound the hold to "inside the builtin/kernel" vs "before it",
which is already inferred. It adds log volume and some timing change. It does not identify the
blocking function. (Exact debug-line wording was not verified against v262.)

## 9. Better discriminator for a future, separately authorized boot
`dyndbg` can be set on the kernel command line (`CONFIG_DYNAMIC_DEBUG=y`; usbcore is built in, so it
applies from early boot): enabling only the `message.c:88` site (`usb_start_wait_urb`'s "timed out or
killed" line) makes the kernel log **one line per timed-out control transfer**, with the calling
process name, the endpoint (ep0in/ep0out) and `len=`. A SET_INTERFACE timeout in the probe would show
as `ep0out len=0/0` with the udev worker/modprobe task name and a timestamp 5 s before the stall's end. No tracing,
no BPF, no USB request is added, log volume is tiny, and perturbation is minimal. It would
directly confirm or refute candidate A for the pre-FU2 phase. It is **still a boot run** with the same
physical-audio risk as any boot, and its own authorization/safety gate (as in Phase 8A.13) applies;
this phase does not enable it.

## 10. Implications for the boot observer
Identifying a 5 s stall would not make the window dependable: it exists only while the device stalls
and its end is anchored to something not yet understood. Absent a stall the probe may reach FU2
within ~0.2 s of module load (~5.7 s), earlier than any realistic observer start. The previous
"module absent" proof remains unusable (module is loaded ~5.5 s; first FU2 request is ~5.5–6 s later),
so the live trace is **not** promoted.

## 11. Remaining unknowns
Where the pre-FU2 time is spent (no observed blocker); whether candidate A occurs before FU2; what
anchors the absolute end time; whether the hold exists on a "good" boot; why ports/controllers differ;
the exact udev-debug wording in v262.

## 12. Next investigations (ranked by information per risk)
1. **dyndbg boot** (§9): highest information (directly tests A); low perturbation; same boot/audio risk.
2. udev-debug boot (§8): confirms request/return timing; weaker, more verbose.
3. Offline only: nothing further discriminates; a pure-logic accounting test would add no evidence.
4. Not recommended: any observer/loader work until 1 clarifies the mechanism.

## 13. Gate for a future live experiment
| Item | Status |
|---|---|
| Boot-time FU2 trace (BPF) | **NO-GO** (unchanged; ordering proof unusable; no loader; trigger/hold unexplained) |
| dyndbg-only boot (message.c timeout line) | **CONDITIONAL** — needs explicit scope, Limine one-time edit procedure, audio-safety confirmation immediately before shutdown, and a rule that no second boot follows automatically |
| udev-debug boot | **CONDITIONAL** — same gates; lower value |
| Any hardware access / module reload / reconnect | **NO-GO** |
