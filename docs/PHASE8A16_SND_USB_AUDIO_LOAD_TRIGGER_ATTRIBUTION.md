# Phase 8A.16 — passive attribution of the `snd-usb-audio` load

Read-only inspection of recorded journals, the live udev database, installed rules, unit files and
`modules.alias`, plus upstream systemd source. No module, udev, service, USB, ALSA-mixer, BPF or
boot change occurred. The current KDE volume (40%) was not touched.

## Result in one paragraph
The load is requested by **ordinary udev handling of the CONNECT 2 audio-control interface**, begun
at about **5.50 s**, and then **takes about 5.5–6.0 s to complete**; registration at 10.9–11.45 s is the
*end* of that long operation, not the moment of a late request. The long duration is **not
explained**. Because the module directory is expected (not observed) to exist for ~5.9 s before the first
Feature Unit 2 request, the Phase 8A.15 "module still absent" ordering proof is nearly unusable. The boot trace stays NO-GO.

## 1. Evidence sources and what each can prove
| Source | Kind | Used for |
|---|---|---|
| Persistent journal, 6 boots (kernel + userspace, monotonic) | recorded | unit/driver timestamps |
| `systemctl show` of live units (this boot) | recorded by systemd | `systemd-udevd`, trigger, alsa-restore timestamps |
| `/run/udev/data` via `udevadm info` (read-only DB query) | recorded by udev | `USEC_INITIALIZED` per device |
| `/usr/lib/udev/rules.d/80-drivers.rules`, `/etc/udev/rules.d`, `modules.alias` | static config | which rule/alias matches |
| Upstream systemd source (`udev-event.c`, `udev-builtin-kmod.c`, via web fetch) | source, **not** the installed v262 binary | semantics of `USEC_INITIALIZED` and `kmod load` |

Not available: any log line naming who called `finit_module`, any timestamp for module appearance in
`/sys/module`, module `init` start, or probe start. Older boots' udev databases are gone (`/run`).

## 2. Stages and which have timestamps
| # | Stage | Timestamp? |
|---|---|---|
| 1 | Interface `add` uevent / modalias generated | not logged (device_add ~3 s after enumeration; events wait in `systemd-udevd-kernel.socket`) |
| 2 | udev worker evaluates rules for `3-2:1.0` | **5.4965 s** (`USEC_INITIALIZED`, this boot; upstream stamps it after rule evaluation, before queued `RUN{builtin}` runs) |
| 2b | `kmod load` builtin executes (synchronous in the worker) | not logged; follows stage 2 immediately in the same worker |
| 3 | module resolve/load begins | not logged |
| 4 | module appears in `/sys/module` | not logged |
| 5 | module `init` starts | not logged |
| 6 | USB audio probe starts | not logged |
| 7 | FU2 sequence → sticky line | **10.9–11.45 s** (sticky line; requests a few ms earlier) |
| 8 | "registered new interface driver snd-usb-audio" | **10.9–11.46 s**; card3 / controlC3 udev stamps 11.445 s |

`systemd-udevd` main process started at 5.4538 s; `systemd-udev-trigger` ran 5.215–5.316 s; the
udev kernel-event socket is held by systemd, so events queued before udevd ran were processed right
after it started (first interface event stamped 5.4965 s, 43 ms later). The device and interfaces
`3-2`, `3-2:1.0…1.4` carry stamps 5.496470–5.496573 s (≈20 µs apart): their first processing.

## 3. Matching alias and rule
`80-drivers.rules`: `ACTION!="add"` skipped; **`ENV{MODALIAS}=="?*", RUN{builtin}+="kmod load"`**.
Alias match of the five interface modaliases against installed `modules.alias`: only
`3-2:1.0` (`ic01 isc01 ip20`, UAC2 AudioControl) maps to **`snd_usb_audio`**; `1.1`, `1.2` (streaming),
`1.3` (vendor 0xFF/F0, used by OpenWitty) and `1.4` (DFU) map to nothing. The only local rule is
`/etc/udev/rules.d/70-openwitty.rules` (uaccess/permissions; it does not load or bind anything). No
`/run/udev/rules.d`, no `options`/`softdep`/`blacklist` for the module, no modules-load.d entry, no
udev or module kernel-command-line argument. `snd_usb_audio` depends on `snd, snd-usbmidi-lib,
snd-pcm, snd-hwdep, snd-ump, mc`. Nothing indicates a deliberate delay.

## 4. Per-boot timeline (seconds, monotonic, from recorded journals)
| event | b-5 | b-4 | b-3 | b-2 | b-1 | b0 (quirk boot) |
|---|---|---|---|---|---|---|
| real-root systemd starts | 5.238 | 5.259 | 5.378 | 5.378 | 5.376 | 5.224 |
| first real-root udevd log line | 5.299 | 5.309 | 5.428 | 5.439 | 5.447 | 5.463 |
| `btusb` registered | 5.418 | 5.365 | 5.491 | 5.548 | 5.568 | 5.586 |
| first `snd_hda_intel` enable | 5.506 | 5.513 | 5.643 | 5.646 | 5.671 | 5.674 |
| `alsa-restore` ran (b0 only: 7.237–7.249) | | | | | | 7.24 |
| Plasma Login Manager *Starting* | 8.842 | 8.940 | 9.102 | 8.895 | 8.874 | 8.797 |
| FU2 sticky line | 10.915 | 10.918 | 10.915 | 11.443 | 11.414 | 11.433 |
| `snd-usb-audio` registered | 10.928 | 10.930 | 10.928 | 11.456 | 11.427 | 11.445 |
| Plasma Login Manager *Started* | 10.935 | 10.938 | 10.938 | 11.464 | 11.436 | 11.454 |
| registration − first udevd line | 5.63 | 5.62 | 5.50 | 6.02 | 5.98 | 5.98 |
| PLM Started − registration (ms) | 6.8 | 8.4 | 9.6 | 7.7 | 8.7 | 8.9 |
| PLM wait for registration (s) | 2.09 | 1.99 | 1.83 | 2.56 | 2.55 | 2.65 |

Observations (directly in the recorded data):
- **`plasmalogin.service` has `ExecStartPre=-udevadm settle --timeout=10`.** In all six boots its
  start finishes **7–10 ms after** the module registration, after waiting 1.8–2.7 s. `udevadm settle`
  returns when udev's event queue is empty, so the `snd-usb-audio` load was the **last busy item in
  udev's queue** at that time and was already in progress (queued/running) at ≥8.8 s.
- Registration clusters at two absolute times (~10.93 s for the three boots with the device on
  port `5-1`, ~11.43–11.46 s for the three on `3-2`), and does **not** follow the variation of the
  udevd start (5.30–5.46 s) or, in the quirk boot, the +0.94 s extra enumeration time (registration
  11.445 s vs 11.427/11.456 s on the same port). So the long phase is not "udev-start plus a fixed
  duration" and not "enumeration plus a fixed duration".
- Between ~6.1 s and the sticky line, the kernel log is silent for this device (only an unrelated
  `r8169` link-up at 11.26 s in b0).
- `alsa-restore.service` ran at 7.24 s, ~4.2 s before the USB sound card existed (card3/controlC3
  stamps 11.445 s), so in this (quirk) boot the boot-time `alsactl restore` did **not** operate on card C2; other
  boots' alsa-restore times were not checked.

## 5. Comparison with `btusb`
`btusb` registers 80–90 ms after the first real-root udevd line; its interface had its first udev
stamp (2.1 s) from the initrd's udev and was replayed by the real-root coldplug. It shows that
interface-class autoload from this coldplug works promptly. It is **not** proof of an identical
path: `snd-usb-audio` additionally probes a device with class-specific requests during `init`.

## 6. Hypotheses
| | Hypothesis | Assessment |
|---|---|---|
| H1 | Delayed udev coldplug: rule/worker runs late | **Contradicted** for rule evaluation: stamp 5.4965 s, 43 ms after udevd start, 0.28 s after trigger start. (Stamp semantics from upstream source, installed v262 not read.) |
| H2 | A later userspace service requests the module at ~11 s | **Contradicted / unnecessary**: the standard rule requests it at ~5.5 s; none of the units logged between 8.8 s and registration is a sound service; registration precedes the user manager and PLM completion; the settle coupling ties it to udev's queue. |
| H3 | Request at ~5.5 s, completion at ~11.4 s | **Supported, indirect** (stamp + rule + synchronous builtin + `settle` coupling in 6/6 boots). The request/execute times and the blocking inside are **not directly logged**. |
| H3a | …because the module load/probe is blocked by the device or a USB timeout (device-side readiness) | **Plausible, unproven.** The same device reached the mixer ~180 ms after enumeration on a successful reconnect; the boot probe reaches it ~5.9 s after the request. Absolute-time clustering is compatible with a readiness/timeout process not tied to udev start or enumeration. `-110` appears again 6.1 s after the sticky line. |
| H3b | …because load is blocked by something unrelated (another module/lock) | **Not distinguishable** from H3a with current logs. |
| H4 | Kernel-side `request_module()` independent of udev | **Not distinguishable, not needed**; no evidence for or against. |
| H5 | Logs cannot identify the requester | **Partly true.** The requester process is *inferred* (udev worker running the `kmod load` builtin), never observed. |

No instance of the loader process, its start time, or the blocking location is directly observed.
Never read the nearby `PLM Started` timestamp as the cause: it shows only that udev's queue emptied.

## 7. Implications for the early real-root observer (Phase 8A.15 design)
- The planned ordering proof (attach, then check `/sys/module/snd_usb_audio` is absent) can hold
  only **before ≈5.5 s** — a window of at most ~0.1–0.3 s after the real root starts (5.22–5.38 s)
  and a race with the very udev event that loads the module (first interface stamp ≈33–43 ms after
  udevd's start). A loader that must read BTF, load, verify and attach in that time cannot be
  promised to win. Under this predicate: **NO-GO**.
- The first FU2 request is instead ~5.9 s later (a few ms before the sticky line). A wider attach
  window of several seconds exists *in these six boots*, but: (a) the cause of the 5.5–6 s hold is
  unknown, so it may not recur or may change with timing; (b) absence of the module is no proof of
  anything in that window, so a different completeness proof is needed — e.g. require the first
  matching event after attach to be the expected opening FU2 request, which is known only from the
  reconnect trace, not from a boot; (c) a hold that is itself part of the bug means a "good" boot
  may be probed immediately at ~5.5 s, before any observer.
- `Before=` ordering would not help: the load is requested by a udev worker that no unit orders
  against, and holding or reordering it is prohibited because it could change the phenomenon.
- Attaching during an in-flight probe is architecturally possible (the hooks are on a built-in
  function), but its safety and the verifier/attach behaviour remain untested.
Revised ratings: *Startup before first mixer request* — **NO-GO** under the module-absence proof;
**CONDITIONAL-but-unproven** under a sequence-based proof that does not yet exist. Overall live boot
trace: **NO-GO**.

## 8. BPF overflow-versus-loss (resolved offline)
Finding from 8A.15: the BPF side counted events beyond the 256 cap in `lost`, so a benign over-cap
arrival and a genuine ring/map loss were indistinguishable after a cap stop, and 8A.15 had to
treat them heuristically (an ambiguity that must not be treated as harmless).
Offline fix, no live test: `fu2_usb_core_trace.bpf.c` now counts over-cap events in a new `overcap`
array map and leaves `lost` for genuine loss only (if the `overcap` lookup fails it falls back to
`lost`, i.e. fails closed). The lifecycle reads `(lost, pending, overcap)`; `lost > 0` always
invalidates the trace, even at the cap, and `overcap` is only recorded. Verified: clang compiles the
object (`-Wall -Wextra -Werror`, sections `fentry/fexit/.maps/license`), `bpftool gen skeleton`
(file operation only) exposes maps `pending, events, lost, emitted, overcap`; the lifecycle test
now has 68 synthetic checks (cap with over-cap only → truncated and valid; cap with genuine loss →
invalid; cap with pending → incomplete). **Not verified:** verifier acceptance and live counter
behaviour. Incomplete (missing return) and malformed records remain separate (`pending`,
`CORE_BAD_DATA`). The FU2 core logic test (52 checks) is unchanged.

## 9. Minimum missing evidence
1. Direct timestamps of the request and of module appearance/init/probe — e.g. one boot with udev
   debug logging (`udev.log_level=debug`, a one-time bootloader edit like Phase 8A.13) would record
   when the worker starts `kmod load` and when it returns, with no USB access, but it **is a boot
   run** and carries the same physical-audio risk of the kernel's own probe.
2. What the probe waits on during 5.5–11.4 s (stack sampling or tracing; not authorized).
3. Whether the hold appears on a *good* boot (after the reconnect the device mixes fine).
Without these, no startup-ordering claim beyond the table above is justified.

## 10. Recommendation
Do not build a loader or schedule a trace yet. Safest next step: decide whether to authorize the
single udev-debug boot (item 1) under the same safety gate as Phase 8A.13, or keep the study
offline. The most valuable open question is **what the ~5.9 s hold is** — it may be the failing
phenomenon itself.
