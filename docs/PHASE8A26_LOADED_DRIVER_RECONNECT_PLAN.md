# Phase 8A.26 – Same-kernel loaded-driver reconnect comparison: preparation plan

Status: **PREPARED, NOT EXECUTED.** In this phase the system was only read: no reboot, no physical reconnect, no module
load/unload, no `/boot` or UEFI change, no USB reset, no hardware SET, no playback. The experiment needs its own authorization
and fresh physical safety confirmation.

## Question
On kernel **7.3.0-5-cachyos**, does ONE physical reconnect of CONNECT 2 while `snd-usb-audio` is **already loaded and bound**
reproduce the healthy reconnect of Phase 8A.21 (kernel 7.2.10)? 8A.25 (same kernel, driver *unloaded* at reconnect, probe 120.5 s
later) was NEGATIVE, so this isolates "driver already bound at reconnect" from "kernel version", at the price of one more
difference that stays: the reconnect is not made from a cold start of the whole bus.

## Same-boot feasibility (read-only preflight, done)
The system is still in the exact Phase 8A.25 experimental boot:

| Requirement | Observed |
|---|---|
| boot id | `d396d14b…` = the 8A.25 report's boot |
| kernel | exactly `7.3.0-5-cachyos` |
| CONNECT 2 | one device, port `3-2`, controller `0000:0d:00.4`, device number 3 |
| `snd_usb_audio` | loaded; `registered new interface driver snd-usb-audio` at 19:06:40 |
| interfaces | `3-2:1.0`, `1.1`, `1.2` bound to `snd-usb-audio`; `1.3`, `1.4` (vendor-specific) unbound – expected |
| FU2 failure | present in this boot (`2:0: broken mixer GET_CUR (… => -6016)`, `-110` at 19:06:45); unit 5 separate |
| PipeWire | CONNECT 2 device with a sink, **0 Route / 0 EnumRoute** (`DEVICE_NO_ROUTES`) |
| ALSA | `PCM Playback Volume` present (functionality untested) |
| journal | complete and readable unprivileged |
| history since 8A.25 | exactly one disconnect (device number 2, 19:04:31) and two enumerations (18:57:05, 19:04:34); no unload |
| command line | still carries the temporary `modprobe.blacklist=snd_usb_audio` (not removed, no reboot) |

Because the module is registered, a device arriving on the bus is bound by the already-registered driver; alias-autoload suppression
only affects loading by alias. This is **verified by the post gate** (interfaces bound to `snd-usb-audio`, registration count
unchanged), not assumed. If the system is rebooted or the boot differs, this plan's `--expect-boot-prefix` gate fails and the
experiment must not be run in that state.

## Tool: `tools/diagnostics/loaded_reconnect.py` (unprivileged, no root, nothing written outside the private run directory)
| Command | Purpose |
|---|---|
| `prepare` | create `~/.local/state/openwitty/trace-8a26/run-<time>` (0700) |
| `check --stage pre [--cli P] [--expect-boot-prefix X]` | baseline gate + state before: kernel pin, boot prefix, module loaded, registration in journal, exactly one device on `3-2`/`0000:0d:00.4`, audio interfaces bound, **CONNECT 2 is in the failing state** (otherwise the comparison is meaningless), event counts, ALSA, CONNECT 2 PipeWire state, optional GET-only snapshot |
| `watch [--deadline 240]` | sysfs-only observer of exactly one disconnect + reappearance, then polls (0.25 s resolution) until the audio interfaces are bound, records `reconnect_to_bound_observed_s` |
| `check --stage post` | exactly **one new** disconnect, enumeration and identity line since the baseline; device number changed; same port/controller; one device; module still loaded and **registration count unchanged**; watch status `reconnected`, no duplicates; journal verified |
| `classify [--cli P]` | passive verdict from the journal *since the reconnect*, ALSA controls, CONNECT 2-scoped PipeWire state, bound interfaces, GET-only snapshot; repeatable, every pass kept as `classification_<time>.json` |

The tool never calls `modprobe`/`rmmod`, never touches `/boot`, never resets, unbinds or authorizes anything, sends no SET and plays
nothing (a source-scan test enforces this). The only device-adjacent read is the optional GET-only
`openwitty-cli read settings --method interface --interface 3`. Timing: only the journal timestamp of the new enumeration and the
sysfs-observed bind time (0.25 s polling) are recorded; **no USB request latency is inferred** (no tracing is enabled).

A dry run of `prepare` + `check --stage pre` on the live system returned gate OK (this validated the tool; that run directory is
not reused – the real experiment creates a fresh one).

## Exact single-reconnect procedure (for separate approval; every physical step by the user)
Preconditions: no reboot since 8A.25; clean tree at the pushed commit; nothing playing or recording.

1. **Safety checkpoint (fresh):** playback and recording stopped; powered speakers/amplifiers switched off, physically disconnected
   or independently protected; headphones/listeners protected; KDE software volume and the CONNECT 2 mute are *not* protection;
   the user understands the kernel mixer initialization on the new enumeration can change physical output level and mute state.
2. `RUN=$(python3 -I tools/diagnostics/loaded_reconnect.py prepare)`
3. `python3 -I tools/diagnostics/loaded_reconnect.py check --run-dir "$RUN" --stage pre --cli build-release/openwitty-cli --expect-boot-prefix d396d14b`
   – must print `"gate": "OK"`; otherwise stop (INVALID, nothing is attempted to repair).
4. **Safety checkpoint (fresh, again).**
5. `python3 -I tools/diagnostics/loaded_reconnect.py watch --run-dir "$RUN" --deadline 240` – wait for `ARMED`.
6. The user unplugs CONNECT 2 and plugs it back into the **same port exactly once** and leaves it connected. Nothing else is done
   (no second attempt even if observation fails).
7. `python3 -I tools/diagnostics/loaded_reconnect.py check --run-dir "$RUN" --stage post` – must be `OK`, else INVALID.
8. After at least 60 s without any audio activity:
   `python3 -I tools/diagnostics/loaded_reconnect.py classify --run-dir "$RUN" --cli build-release/openwitty-cli`
   (may be repeated later; the reconnect never).
9. The user reports any audible effect (silence does not exclude electrical transients). Result document
   `docs/PHASE8A26_LOADED_DRIVER_RECONNECT_RESULT.md`; no repetition.

No reboot, Limine change, module load/unload, volume/mute SET or playback is part of this procedure. Rollback: nothing to roll back
(the device is simply left enumerated and bound); the temporary kernel argument disappears at the next normal boot.

## Interpretation
| Verdict | Condition | Meaning |
|---|---|---|
| **POSITIVE** | no FU2 failure after the reconnect, ALSA `PCM Playback Volume` present, CONNECT 2 PipeWire route with `route.hw-volume=true`, audio interfaces bound | a loaded-driver hotplug path is healthy on kernel 7.3.0-5 even though the previous state was failing. Does not isolate whether driver registration, immediate probing, device state or a combination is decisive; no hardware SET was validated |
| **NEGATIVE** | FU2 failure repeats, CONNECT 2 has no usable hardware-volume route (`DEVICE_NO_ROUTES` or `hw-volume=false`), stack settled (≥ 60 s) | the 8A.21 result does not necessarily reproduce under these conditions; kernel version and differences in the exact reconnect circumstances remain relevant |
| **INCONCLUSIVE** | missing/contradictory evidence, interfaces not (yet) bound, route without `route.hw-volume` field, PipeWire not yet initialised, unsettled stack | repeat only the passive `classify` later |
| **INVALID** | any pre/post gate failed (boot, kernel, identity, counts, device number, module state) | not interpretable; no repetition |

Unit-5-only messages are never counted as an FU2 failure. A present ALSA volume control alone is never success.

## Comparison caveats (state in the result document)
8A.21 was kernel 7.2.10, used a private tracefs capture, and the new driver bind followed the enumeration within ≈0.15 s; here only
journal timestamps and 0.25 s sysfs polling are available. The pre-state in 8A.21 was a failing boot state, as here. One observation
cannot establish causation.

## Validation of the preparation
`test_loaded_reconnect.py` (21 offline tests, CTest `loaded_reconnect_offline`): fake-sysfs interface binding, pre-gate failure matrix
(wrong kernel/boot, unloaded module, journal gaps, missing registration, duplicate/wrong-port/wrong-controller device, unbound
interfaces, non-failing pre-state), exactly-one-new-cycle delta analysis (none/two cycles, unchanged number, module reload), verdict
matrix (POSITIVE/NEGATIVE/INCONCLUSIVE/INVALID incl. unit 5 alone, unsettled, partial binding), bind-time polling, full
prepare→pre→post→classify flows, and a source scan forbidding module/bootloader/reset/SET/playback operations.
