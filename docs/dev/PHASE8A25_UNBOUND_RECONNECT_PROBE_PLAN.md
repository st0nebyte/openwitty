# Phase 8A.25 – Unbound physical reconnect before the first audio-driver probe: preparation plan

Status: **PREPARED (adapted for kernel 7.3.0-5 in Phase 8A.25C), NOT EXECUTED.** In this phase nothing was installed in the bootloader, nothing was rebooted,
CONNECT 2 was not touched, no module was loaded or unloaded, no UEFI setting changed, no hardware write issued.
The experiment needs its own authorization.

## Kernel change (Phase 8A.25C) – read this first
A system update replaced `linux-cachyos` 7.2.10-1 with **7.3.0-5** after 8A.24 (see `PHASE8A25B_KERNEL_73_BASELINE.md`).
8A.21–8A.24 were measured on **7.2.10**; this experiment can only run on **7.3.0-5**, so every comparison with those
phases carries a kernel-version caveat. The normal 7.3.0-5 boot still reproduces the FU2 failure, but differently:

| | kernel 7.2.10 | kernel 7.3.0-5 |
|---|---|---|
| FU2 kernel message | `2:0: sticky mixer values (… => -1024/-6016), disabling` | `2:0: broken mixer GET_CUR (… => -6016)` |
| unit 5 | separate sticky message | separate `broken mixer GET_CUR`, still not an FU2 failure |
| `usb_set_interface failed (-110)` | yes | yes |
| ALSA `PCM Playback Volume` | absent | **present** (a working control is *not* inferred; hardware writes untested) |
| PipeWire | Route present, `route.hw-volume=false` | CONNECT 2 device has **no Route/EnumRoute**; WirePlumber `Unable to load mixer` |

Gates added in code (`PROFILES["8a25"]["kernel"] = "7.3.0-5-cachyos"`): `entry install` refuses unless the ordinary
`//linux-cachyos` block says `Kernel version: 7.3.0-5-cachyos` and `modules.dep` of that kernel lists `snd-usb-audio` with all
its dependency files present; `check`/`load` (gate) require `uname -r` to equal the pinned kernel and the module set to verify.
Any future kernel change makes the gate fail (INVALID / refusal) until this plan is deliberately re-pinned.

### Current Limine baseline
The update regenerated `/boot/limine.conf` (new default kernel entry, new Snapper snapshot entry, new image hashes). The completed
8A.24 backup (`limine.conf.before`, sha256 `8017afbb…`) therefore **no longer equals** the live file (`23850fd4…`). This is expected
historical metadata: `rollover` records both hashes and `current conf identical to backup: False` in `ROLLOVER.txt`, keeps the old
backup and the load marker in the archive, and never restores the obsolete 7.2 configuration. The 8A.25 `entry install` writes a
**new** `limine.conf.before` from the *current* 7.3 file (it refuses to overwrite an existing one); `entry verify` after `entry remove`
compares only against that new backup, byte for byte. The temporary entry copies the ordinary `//linux-cachyos` block (never a
snapshot `////linux-cachyos` block) and appends only `modprobe.blacklist=snd_usb_audio`; `default_entry` and the existing/snapshot
entries are untouched. If another tool regenerates `limine.conf` between install and verify, `verify` reports a difference and
nothing is auto-restored: report it, do not overwrite.

## Question
8A.21: a physical replug while Linux was running **and `snd-usb-audio` was already loaded** produced a healthy first
probe (fast `SET_INTERFACE`, working FU2, hardware volume). 8A.24: a probe ≈317 s after the *boot-time*
enumeration failed. 8A.25 asks: does a physical re-enumeration on the settled host, **while the audio driver is NOT
loaded**, followed by the first probe, behave like 8A.21 (healthy) or like 8A.24 (failed)?
- **POSITIVE** (healthy after the load): the state produced by a fresh enumeration/power-up on a running host
  matters, not the driver being bound at that moment. The exact cause is not isolated.
- **NEGATIVE** (sticky): does not by itself show the driver must already be bound at reconnect time; the probe
  here happens much later than in 8A.21.
- Limitation, stated up front: reconnect → load will take tens of seconds to minutes (the user types/approves), not
  the ≈0.15 s of 8A.21 (reconnect → probe). The delay is recorded exactly (`reconnect_to_load_s`) and the result is
  NOT presented as an exact A/B reproduction of 8A.21. The module is deliberately **not** auto-inserted on USB
  arrival (e.g. via a udev rule): the mixer probe can change physical output level and needs fresh human safety
  confirmation, and no USB authorization/binding/reset automation is introduced.

## Tooling (extension of `deferred_probe.py`, all offline-tested; profile `--profile 8a25`)
| Item | 8A.24 profile | **8A.25 profile** |
|---|---|---|
| Limine entry name / markers | `OpenWitty deferred snd-usb-audio (temporary)` / `OPENWITTY-8A24-*` | `OpenWitty unbound-reconnect snd-usb-audio (temporary)` / `OPENWITTY-8A25-*` |
| Added kernel argument | `modprobe.blacklist=snd_usb_audio` only | same, only this |
| Root state dir | `/var/lib/openwitty-8a24` | `/var/lib/openwitty-8a25` |
| Run dir base (user) | `~/.local/state/openwitty/trace-8a24/` | `~/.local/state/openwitty/trace-8a25/` |
| Single-load guard | `load_done` | **per boot**: `load_done.<boot_id>` (O_EXCL, root, 0600) |
| Stages | `prepare`, `check`, `load`, `classify` | `prepare`, `check --stage pre`, **`watch`**, **`check --stage post`**, `load`, `classify` |
New/changed behaviour: `entry install` now refuses if a backup from an earlier run already exists in its state dir
(no silent overwrite) and, for 8A.25, if the 8A.24 state directory has not been archived; an entry of either profile
blocks adding another. New `rollover`. `watch` (user, sysfs read-only) observes exactly one disconnect + reappearance.

## Previous experiment state and the single-load marker (rollover)
`/var/lib/openwitty-8a24/` currently holds the 8A.24 `limine.conf.before` (+ sha256) and its `load_done` marker; the
8A.24 run is classified NEGATIVE (`classification.json` exists). The 8A.25 profile uses a **separate** state dir and a
per-boot marker, so the old marker can neither block nor be silently reused by the new experiment, and it is never
deleted:
`sudo python3 -I tools/diagnostics/deferred_probe.py --profile 8a24 rollover` **moves** (never deletes) the whole directory to
`/var/lib/openwitty-archive/8a24-<timestamp>/state/` and writes `ROLLOVER.txt` (classification of that experiment,
marker text, backup sha256, current `limine.conf` sha256 and whether it still equals the backup — informational, because a
kernel update may regenerate `limine.conf`). It refuses when: an experimental entry (either profile) is installed; the marker's
run directory has no `classification.json` (experiment not demonstrably complete — an active experiment can never be rolled
over); there is no marker (aborted run) unless `--allow-unloaded` is given; the archive target exists; or the state dir is
absent. The Limine backup stays recoverable inside the archive.

## Exact future procedure (every physical step is performed by the user)
0. Preconditions: clean tree at the pushed commit; running kernel `7.3.0-5-cachyos`; `limine.conf` has no entry and `default_entry: 2`; `rollover` (above)
   done; the currently running 8A.24 boot is irrelevant (the new boot replaces it).
1. `sudo python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 entry install`, `entry status`; inspect the block
   (only `modprobe.blacklist=snd_usb_audio` added; original prefix untouched; backup stored).
2. **Safety checkpoint 1** (before the reboot). Fresh confirmation: playback/recording stopped; speakers/amplifiers off,
   disconnected or independently protected; headphones/listeners protected; no reliance on KDE software volume or CONNECT 2
   mute; understanding that the reconnect **and** the kernel mixer probe may change physical output level (the failing probe
   writes −60 dB then 0 dB without restoring). No inference from earlier phases.
3. The user does ONE manual restart and selects `OpenWitty unbound-reconnect snd-usb-audio (temporary)`; logs in; lets the
   desktop idle ≥ 2 minutes; no playback; says "8A.25 resume".
4. (user) `RUN=$(python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 prepare)`; then
   `python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 check --run-dir "$RUN"` (stage pre). Gate: argument
   active, no `module_blacklist`; `snd_usb_audio` absent; no CONNECT 2 interface bound; exactly one `29c2:0004` on **3-2** behind
   **`0000:0d:00.4`**; verified current-boot kernel journal (boot start + enumeration line, no snd-usb-audio registration);
   `modprobe -c` shows the blacklist; `uname -r` is exactly `7.3.0-5-cachyos` with a complete `snd-usb-audio` module set. The baseline device number is recorded. Any failure ⇒ INVALID, stop.
   Optional GET-only snapshot before the reconnect.
5. **Safety checkpoint 2** (before the physical action): same confirmation again, in the current session.
6. Arm the observer: `python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 watch --run-dir "$RUN" --deadline 240`
   (prints ARMED; reads sysfs only; SIGTERM-safe; hard deadline). Immediately the user unplugs CONNECT 2 and plugs it
   back into the **same port**, **once**. No further touching. `reconnect.json` records the disconnect/reappearance
   wall times and the device numbers before/after. No USB reset, power-cycle, authorization toggle or driver bind is performed
   by any tool.
7. `python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 check --run-dir "$RUN" --stage post`. Gate (all must hold):
   the reconnect was observed (status `reconnected`, one device throughout, no duplicates); the journal shows **exactly one**
   `USB disconnect` of port 3-2 and **exactly two** enumerations (boot + this one) with two `New USB device found` identity lines;
   the device number changed (or, if equal, INVALID: a new generation cannot be confirmed); identity/port/controller still
   expected; `snd_usb_audio` still absent; all CONNECT 2 interfaces unbound; no `registered new interface driver snd-usb-audio`;
   baseline checkpoint OK for this boot; blacklist active. Failure ⇒ INVALID, no corrective action, no second attempt.
   Optional GET-only snapshot (to see whether the reconnect itself changed the output state).
8. **Safety checkpoint 3** immediately before the load: protection still in place, nothing playing or recording; the probe may write
   −60 dB and 0 dB. Not inferred from a flag.
9. `sudo python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 load --run-dir "$RUN" --confirm-audio-safe`: requires the OK
   **post** checkpoint of this boot (< 30 min old), `reconnect.json`, a re-run of the whole gate including the re-enumeration
   count, the run-directory ownership checks of 8A.24A, and creates the per-boot marker before `modprobe`. `load.json` records the
   load time and **`reconnect_to_load_s`**. No manual `modprobe`; a failure or timeout is not retried.
10. (user, in the desktop session) `python3 -I tools/diagnostics/deferred_probe.py --profile 8a25 classify --run-dir "$RUN" --cli
    build-release/openwitty-cli`; if events were still arriving, only the passive classify may be re-run later (never the load).
11. Cleanup: `sudo … --profile 8a25 entry remove`, `entry verify` (byte-identical to the backup); next boot uses the normal
    entry; optionally `rollover` the 8A.25 state afterwards; result document `PHASE8A25_…_RESULT.md`; no repetition.

## Evidence recorded
Initial enumeration timestamp (journal) and device number; reconnect timestamps (watch + journal) and new device number;
module state before/after reconnect; load timestamp and reconnect→load delay; FU2 (unit 2) sticky verdict; independent unit-5
message; `usb_set_interface failed (-110)`; ALSA `PCM Playback Volume`; PipeWire analog-output `route.hw-volume`; GET-only Speaker
and Headphone volume/mute (structured, from the `phantom=` line); journal completeness status. No tracing is enabled, so no request
latency is inferred.

## Classification (version-aware, evidence-based, no hardware write)
`post_boot_health.classify_evidence` (used by `deferred_probe classify` for 8a25; the 8a24/7.2 rule `H.classify` is unchanged and
regression-tested). Both FU2 wordings are recognised; unit 5 is never an FU2 failure; PipeWire is assessed for **CONNECT 2 only**
(vendor/product `0x29c2`/`0x0004`), never for other sound cards; an absent route is its own state (`DEVICE_NO_ROUTES`), distinct from
`route.hw-volume=false`.

| FU2 kernel failure | CONNECT 2 PipeWire state | ALSA `PCM Playback Volume` | stack age since load | verdict |
|---|---|---|---|---|
| no | `route.hw-volume=true` | present | any | **POSITIVE** (no hardware SET was validated) |
| no | other / no routes / not a device | any | any | INCONCLUSIVE (health not consistent) |
| yes (7.2 or 7.3 wording) | no routes, or hw-volume false | present **or** absent | ≥ 60 s | **NEGATIVE** |
| yes | no routes / hw-volume false | any | < 60 s or unknown | INCONCLUSIVE (not settled; classify again later) |
| yes | CONNECT 2 not a PipeWire device | any | any | INCONCLUSIVE (initialisation incomplete) |
| yes | `route.hw-volume=true` | any | any | INCONCLUSIVE (contradiction) |
| yes | routes without a `route.hw-volume` field | any | any | INCONCLUSIVE (not shown to be unusable) |
| unit 5 only | any | any | any | never NEGATIVE on its own |
| ALSA or PipeWire evidence missing | | | | INCONCLUSIVE |

INVALID (any gate failed, wrong kernel, module loaded early, wrong entry, identity/port/controller differs, reconnect count ≠ 1,
device number unchanged) and INCONCLUSIVE for `modprobe` failure or an unreadable post-load journal dominate, as before.
The first passive `classify` may come too early (as in 8A.24): it then reports INCONCLUSIVE with the reason; it may be **repeated**
after the stack has settled (every pass is kept as `classification_<time>.json`, the latest as `classification.json`). The module
load and the physical reconnect are never repeated; the per-boot marker enforces this.

## Risks and rollback
- The reconnect itself may change the device's physical output state (earlier: −33.34 dB muted → −4.00 dB unmuted); the later probe
  may write −60 dB/0 dB. Safety checkpoints 1–3 are mandatory and not satisfiable by software state.
- If the gate fails after the reconnect the device is simply left enumerated and unbound; nothing needs repair. Rollback at any time:
  reboot into the normal entry (default); bootloader cleanup `entry remove` + `entry verify`, or restore
  `/var/lib/openwitty-8a25/limine.conf.before` (sha256 beside it; after rollover it is under `/var/lib/openwitty-archive/`).
- No USB reset, port power cycling, authorization change, driver rebind, firmware or volume/mute SET is part of this plan.

## Validation of the 7.3 adaptation (Phase 8A.25C)
`test_deferred_probe.py` now has 82 offline tests (new: kernel pin in gate/install, module-set verification via `modules.dep`, wrong-kernel
and snapshot-source refusals, CONNECT 2-scoped PipeWire states with an unrelated card advertising hw-volume, the classification matrix
above incl. unsettled/incomplete/contradictory evidence and legacy 7.2 regression, repeated passive classify without a second load, Limine
regeneration with the historical backup mismatch, new-backup install and exact install/remove/verify round trip with the snapshot entry
preserved). `test_post_boot_health.py` has both kernel wordings.

## Validation of the original preparation
`test_deferred_probe.py` had 56 offline tests (new: profile 8a25 entry text/blocking/stale-backup/unarchived-state refusals;
rollover archive/refusal matrix incl. incomplete experiment, installed entry, aborted run, conf drift, per-boot markers;
re-enumeration journal analysis; passive watch state machine incl. no-disconnect, never-returned, fast replug, duplicates;
full prepare→pre→watch→post→load flow with per-boot marker, delay recording, and every post-gate failure blocking the load).
