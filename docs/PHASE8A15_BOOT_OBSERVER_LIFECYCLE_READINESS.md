# Phase 8A.15 — boot observer lifecycle, startup ordering, fail-closed cleanup

> **Update (Phase 8A.16):** the `/sys/module` absence proof and the "~5.5 s margin" in this document
> are superseded: the module load is requested at ~5.5 s and completes at ~11 s, so the proof is
> usable only before ~5.5 s. The over-cap heuristic described below was replaced by a separate
> BPF `overcap` counter (lifecycle test now 68 checks). See `PHASE8A16_SND_USB_AUDIO_LOAD_TRIGGER_ATTRIBUTION.md`.

Offline research and implementation only (2026-10-23). **No reboot, USB request, BPF load or attach,
hook, trace, boot/initramfs/service/udev change, or audio change occurred.** Nothing here installs or
enables a live path: there is no loader, no service unit file and no attach code in the repository;
the new code is a pure state machine with injected side effects. Kernel-log intervals in the early
boot window are not independent physical elapsed-time measurements (Phase 8A.14); this document
makes no claim that depends on them.

## 1. Review of the existing prototype (unchanged)
`fu2_usb_core_trace.bpf.c` / `_shared.h` / `_logic.h` were re-read; they are unmodified and their 52
synthetic checks still pass.
- **Filter:** `fu2_core_fields()` (control pipe, endpoint 0, `wIndex=0x0200`, `wValue` 0x0100/0x0200, exact
  type/request/length) runs before any pointer read; `fu2_core_identity()` (VID/PID `29c2:0004`) is
  checked next. Excluded traffic reaches neither payload copy nor map.
- **Payloads:** SET bytes are copied at entry (the kernel hands `usb_control_msg` a duplicated
  buffer); GET bytes are copied at exit only for `result == size`. Live pointer reads are untested.
- **Pairing:** pending record keyed by TID, validated against device/data pointer and request fields.
  `BPF_NOEXIST` means a nested or stale record counts as loss rather than being overwritten.
- **Accounting:** `lost` counts unmatched/failed reserve/map errors/over-cap events; `emitted` caps at
  256; ring 64 KiB; pending map 128 entries; every exit path deletes its pending key. A missing
  return leaves a pending entry that only the loader can detect.
- **Finding F1 (new, handled in the lifecycle, BPF source unchanged):** events after the 256th are
  counted into `lost` kernel-side. A userspace stop at the cap races with a few more arrivals; naïvely
  that would invalidate a good truncated trace. `fu2_boot_lifecycle.h` treats `lost > 0` observed
  only after a cap-stop (and none seen while polling) as benign over-cap, recorded separately.
  Residual: a genuine loss in the poll-to-stop interval cannot be distinguished from over-cap.
- **Re-enumeration:** a device number change is classified `CORE_SECOND_DEVICE` and stops the observer.

## 2. Startup ordering — what the system shows
Evidence (all read-only): `usbcore` built in; `snd-usb-audio` is a module (`CONFIG_SND_USB_AUDIO=m`);
mkinitcpio `HOOKS=(base systemd autodetect microcode kms modconf block keyboard sd-vconsole plymouth
filesystems)`, no sound hook, and 8A.10 found the module absent from the inspected image; real-root
systemd starts at ~5.22 s, `snd-usb-audio` registers (`usbcore: registered new interface driver
snd-usb-audio`) at **10.9–11.45 s** across six boots, with FU2 sticky lines ~12 ms earlier (inside that
module's init/probe). `btusb` registers at 5.586 s on the same boot, so USB-interface module
autoload from coldplug works at ~5.5 s for other drivers, yet `snd-usb-audio` is ~5.9 s later.
**What triggers or delays that load was not determined offline** (no udev log lines, rule, or
modules-load entry explains it; neither `alsa-restore.service` nor `systemd-modules-load` shows a
corresponding log line). That is the central unknown for startup ordering.

### A. Initramfs observer — NO-GO
- Payload: needs `libbpf`, `libelf`, `libz`, `libzstd` (the existing loader's actual dependencies), the
  BPF object, BTF at `/sys/kernel/btf`, root. This means rebuilding the initramfs (mkinitcpio) and,
  because Limine runs with `ENABLE_VERIFICATION=yes`, regenerating the boot entry hashes — a persistent
  boot-chain change, outside this phase and risky to roll back.
- Lifetime (local `bootup(7)`, `systemd.unit(5)`): `initrd-cleanup.service` isolates
  `initrd-switch-root.target`; `IgnoreOnIsolate=` defaults to **false** for services, so initrd
  services are stopped at the switch. Even if a unit opted out, the real-root manager would not be
  supervising the old cgroup, so **no service-manager runtime limit would exist** and killing the old
  tree is not guaranteed. A process that survives the switch is an unsupervised orphan.
- Pinned links would outlive the reader; their cleanup then depends on a second component that does
  not exist yet. Pinning is rejected.
- Net: the independent-watchdog and guaranteed-cleanup requirements cannot be met.

### B. Early real-root observer — CONDITIONAL, preferred
- No initramfs change; the loader runs from the installed system under a transient or one-shot unit
  early in the real root (real root begins ~5.2 s; observed module registration ≥10.9 s, a
  ~5.5–6 s margin in 6/6 boots). The margin is observational, not guaranteed, because the trigger is
  unknown. **A normal `Before=` is not an ordering proof** and none is relied on.
- **Arm, then verify (implemented in the lifecycle):** attach the hooks, *then* check that
  `/sys/module/snd_usb_audio` is still absent. The sysfs module directory appears before module
  `init` runs, and the first mixer request happens inside init, so absence *after* attach proves
  attach preceded the first request. Presence, or any matching traffic before the proof, aborts the
  run as `MISSED_WINDOW` (fail closed; a module caught in init is conservatively treated as missed).
  The module's loading is never held, delayed, blacklisted or preloaded.
- Residual risk: the window may be missed on a given boot; then the boot carries safety risk and
  yields no data. Needs live confirmation of the trigger and margin before any approval.

## 3. Lifecycle and supervision design (implemented offline)
`tools/diagnostics/fu2_boot_lifecycle.h` (pure, header-only, no I/O) plus
`test_fu2_boot_lifecycle.c`. States: `IDLE → ATTACHED → ARMED → OBSERVING → DONE | CLEANUP_FAILED`.
- **Application limits (defaults):** ≤256 matching events; first matching event within 90 s of attach;
  120 s from first event; 180 s total; 10 s quiet tail after the last event ends a normal run;
  immediate stop on target error/short/bad data/bad time, second device, polled loss, missed window,
  attach failure, signal, reader fault, supervisor stop. A backwards clock is a deadline.
- **Cleanup (exactly once, never retried automatically):** detach only registered, experiment-owned
  links → read lost/pending after detach (missing return = `INCOMPLETE`) → release reader →
  destroy maps → verify nothing of the experiment remains. Each failing step sets a flag;
  any flag puts the run in `CLEANUP_FAILED` (outcome `INVALID`). Repeated stop requests are counted;
  the first reason wins. A finished lifecycle refuses to attach or register links again — no
  automatic retry, no replacement observer.
- **Outcomes:** `COMPLETE` (quiet tail, clean), `TRUNCATED` (cap/deadline/signal with data), `NO_DATA`,
  `INVALID` (integrity, cleanup, error, loss, second device, missed window, attach failure).
- **Independent supervisory stop:** `fu2_lc_supervisor_limit_ns()` = longest application limit +
  15 s grace, always later than every application limit. Per the local `systemd.service(5)`,
  **`RuntimeMaxSec=` has no effect on `Type=oneshot`**, so a supervising unit must be `Type=exec`
  (or `simple`) with `RuntimeMaxSec=` set to that limit, `KillMode=control-group`,
  `SendSIGKILL=yes`, a short `TimeoutStopSec=`, `Restart=no`, and a consume-once enable token removed
  by `ExecStartPre=` so a later boot does not trace again. This is a design, not an installed unit.
- **Link ownership:** links are non-pinned (`bpf_link` file descriptors), so the kernel removes them
  when the last descriptor closes, including on SIGKILL/crash. The lifecycle can only detach ids it
  registered; it never enumerates or touches other programs.
- **What the offline tests prove vs. what stays unproven**
  - Proven (66 checks, no BPF/USB): state machine, limits, loss accounting, no retry, idempotent stop,
    only-owned detach on a fake kernel with unrelated programs present, cleanup-failure handling,
    zero residual fake state after success; real child processes show that an ordinary
    descriptor (a stand-in for a link fd) is closed on clean exit, on abrupt SIGKILL/crash, on
    cooperative SIGTERM, and that a hung reader ignoring SIGTERM is removed by an external SIGKILL.
  - Not proven (needs privileged runtime validation in a non-target setting): that real fentry/fexit
    `bpf_link`s detach on fd closure under this kernel, verifier acceptance, attach to the static
    `usb_control_msg`, `bpf_probe_read_kernel` of SET/GET buffers, `RuntimeMaxSec` actually firing,
    behaviour with a task in uninterruptible sleep.

## 4. Timing interference
The kernel's own SET→GET sequence is made of synchronous transfers measured at ~0.36–0.38 ms each
in 8A.9 (with an observer present). Per-call cost of the hooks is a few integer compares for
non-matching traffic (filter before any pointer read), plus a short map insert, two short reads and a
ring submit for matching traffic; consumption is asynchronous in userspace. The sticky decision is a
value comparison, not a timing threshold, so only firmware settling time would make microsecond
shifts matter; this is unmeasured. A boot that is clean *only while instrumented* must be
reported as inconclusive for the underlying bug, never as a fix. No stack capture, function-graph or
unrelated payload logging is proposed.

## 5. Physical safety boundary
The kernel's probe can still write FU2 volume, including an attempted 0 dB fallback, and an observer
changes none of that. The uneventful Phase 8A.13 boot is not evidence that another boot is safe.
Any future run needs independent protection of speakers, amplifiers, headphones and listeners, and no
reliance on software or hardware mute. The observer must never write volume or mute. The later KDE
40% setting is untouched.

## 6. Readiness
| Requirement | Rating | Basis |
|---|---|---|
| Built-in hook source/BTF compatibility | **GO** (source) | 8A.11: 9-arg BTF match; unchanged here |
| Live attachability | **CONDITIONAL** | never attached; static symbol/LTO untested |
| Capture actual SET payload | **CONDITIONAL** | entry copy implemented; live read untested |
| Capture actual GET payload | **CONDITIONAL** | return copy implemented; live read untested |
| Startup before first mixer request (initramfs) | **NO-GO** | §2A |
| Startup before first mixer request (early real root) | **CONDITIONAL** | arm-then-verify implemented offline; module-load trigger unknown; ~5.5 s margin observed |
| Preservation across switch-root | **NO-GO** | §2A; not needed for B |
| Event collection and integrity | **CONDITIONAL** | lifecycle accounting tested synthetically; no reader exists |
| Independent watchdog | **CONDITIONAL** | design (Type=exec + RuntimeMaxSec + SIGKILL) and limit relation tested; unit not built/tested live |
| Link ownership and cleanup | **CONDITIONAL** | owned-id/idempotent/no-retry tested; real link teardown unverified |
| Bounded execution | **CONDITIONAL** | limits tested; hard enforcement unverified live |
| Privacy | **GO** (design) | fixed filter, no serial/descriptors/audio payload; local data short-lived |
| Physical audio safety | **NO-GO** | requires an explicit safety procedure and user confirmation per run |
| **Live boot trace overall** | **NO-GO** | no loader/unit exists; trigger unknown; no live validation |

## 7. Remaining authorization requirements and next action
Not authorized or done: any loader, unit, reboot, attach, or trace. Before any CONNECT 2 boot trace:
(1) identify what actually loads `snd-usb-audio` at ~11 s (privileged, passive, non-target evidence);
(2) build the disabled loader and validate verifier/attach/payload/teardown on a non-target kernel
run, including `RuntimeMaxSec` firing and the arm-then-verify check; (3) separately authorize exact
files/commands, one boot, and audio protection. **Recommended next action:** step (1) as a read-only
or passive study, because the unknown trigger decides whether approach B is viable at all.
