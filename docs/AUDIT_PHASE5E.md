# Phase 5E audit: pre-hardware review and release readiness

Status of the project after this audit: **Experimental — not hardware validated.** Nothing in
this audit touched real hardware; every statement about device behaviour is inherited from
static analysis (PROTOCOL.md) and is unverified. Simulated results are never presented as real.

## 1. Method

* Manual review of all production sources (core, transport, protocol, CLI, GUI backend, QML
  bindings) for ownership, lifetimes, threading, error propagation, cancellation, reconnection,
  state consistency and every path that can reach USB hardware.
* Static guard (`tools/audit/check_usb_call_sites.sh`, ctest `audit_usb_call_sites`).
* Compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnull-dereference -Wcast-qual -Wformat=2 -Woverloaded-virtual -Wnon-virtual-dtor`).
* ASan+UBSan (+ `_GLIBCXX_ASSERTIONS`) over the full suite including the Qt GUI; TSan over the
  Qt-free core. clang-tidy / cppcheck / clang are not available rootless on this host and were
  **not** run.
* Packaging: lintian 2.117 (obtained rootless), genuine Arch `makepkg` 7.1.0, rootless dpkg
  lifecycle simulation, clean-clone rebuild.

## 2. Findings and fixes

| # | Severity | Finding | Fix | Test |
|---|----------|---------|-----|------|
| F1 | High (safety) | The "disruptive" detach/claim path was gated only in the CLI (`LEWITT_ALLOW_DISRUPTIVE`); any internal caller could select `DetachAndClaim` / `allowDetachKernelDriver` and detach `snd-usb-audio` | Transport-level `UsbOptions::allowDisruptive`; `read()` refuses (`NotSupported`) before any state/USB I/O; CLI sets it only when the env var is present | `Audit.DetachPathRefusedAtTransportWithoutExplicitGate` |
| F2 | High (UI correctness) | Replacing a session (connect mock <-> USB) cleared the worker's diff cache, so keys that no longer exist (`pending.*`, stale values) were never removed from the GUI's value map: stale "pending" flags / values survived | diff cache is kept across sessions; removed keys are emitted as cleared | `GuiAudit.PendingFlagsAndWorkflowResetWhenSessionIsReplaced` |
| F3 | Medium | `DeviceWorker` kept workflow state (pending keys, autogain state, open measurement bracket, restart flag) across session replacement/disconnect; an open measurement bracket was never closed | `resetWorkflowState()`; `endMeasurementSessionBestEffort()` closes the bracket while connected/writable; explicit disconnect clears pending, cancels re-scan and stops auto-reconnect | `GuiAudit.ExplicitDisconnectClearsPendingAndDoesNotReconnect` |
| F4 | Medium (race) | `cancelAll()` (GUI thread) loaded a raw transport pointer that the worker could destroy during session replacement (use-after-free window) | pointer guarded by a mutex, cleared before the old transport is destroyed | TSan core run; GUI shutdown tests |
| F5 | Medium | `DeviceSession::commit()` dereferenced `current_` when no snapshot existed | explicit refusal | `Audit.CommitWithoutDeviceSnapshotIsRefused` |
| F6 | Low | `SettingsMessage::setCustomButtonFunction` accepted values the UI never offers (guard was GUI-only) | validated in the setter (backend) | `Audit.CustomButtonSetterRejectsUnknownValues` |
| F7 | Low | USB string descriptors relied on NUL termination of the buffer | use the returned length | warning build |
| F8 | Low | mock-only build's stub transport returned a different error order than the libusb transport for forbidden writes | same guard order | `WriteGuard.UsbTransportRefusesEvenWithWritesEnabled` (mock-only) |
| F9 | Low | `-Wconversion`/`-Wshadow` issues | cleaned | warning build |
| F10 | Packaging | `.deb`: no changelog, non-755 directories (0775), no man pages, over-long description, no copyright notice, no AppStream modalias | all fixed; lintian reports only `initial-upload-closes-no-bugs` (Debian-archive-specific) | lintian run |
| F11 | Reproducibility | installed binaries depended on the build directory length (build RPATH rewritten in place at install) and Qt `rcc` embeds file mtimes | `CMAKE_SKIP_BUILD_RPATH` under `LEWITT_REPRODUCIBLE`; document `SOURCE_DATE_EPOCH` (set automatically by makepkg/dpkg-buildpackage) | two source/build directories -> identical payload with `SOURCE_DATE_EPOCH` |
| F12 | Repo hygiene | build directories `build-asan/`, `build-tsan/` were committed in Phase 5C (`.gitignore` only listed `build/`) | removed from HEAD (commit of this phase) and `build-*/` ignored. **They remain in history** (commits eb2956b, 40f1f49, 91fdefd): rewriting local history was not performed because it needs explicit approval — see section 6 | `git ls-files` shows none |

### Reviewed and found sound (no change)

* Transport serialisation (`SerializedTransport`): one request in flight, cooperative
  cancellation, `close()` waits for the in-flight request, bounded by the 500 ms timeout.
* Write path: `DeviceSession` (policy) -> `checkWriteAllowed` (size, trigger whitelist,
  FactoryReset/mode-switch refusal on hardware) -> `verifyOnlyKnownFieldsChanged`
  (unknown bytes cannot change) -> `UsbTransport::write` (re-checks, `allowWrites` default
  off). On real hardware `writesEnabled()` is false, so the GUI is read-only and `commit()`
  / `sendTrigger()` are refused before reaching the transport.
* Failure classification: stalls never count as a lost link; timeouts/IO/short/malformed
  escalate after 3; EACCES/EBUSY never escalate and are reported distinctly.
* Unknown-byte preservation and enum preservation (`setBoolByte`, merge semantics,
  `UnsupportedEnumValuesAreNotRewritten`).
* Q8.8 helpers (saturating, round-to-nearest), little-endian helpers, message sizes and
  `wValue`/`wIndex` constants (static_asserts).

### Known limitations (not fixed; documented)

* **USB retries**: in USB mode a present-but-unusable device (EACCES/EBUSY/EPIPE) is retried
  with backoff 1 s -> 8 s forever. The retries are reads only (EACCES/EBUSY are refused by
  usbfs before the device sees anything) but are noise; a later improvement is to wait for
  a change in the device list after a permission/busy error.
* **Autogain completion on hardware**: completion is inferred from status progress >= 100.
  If the device keeps a stale 100 from a previous measurement the UI would mark a new
  measurement finished immediately. Writes are disabled on hardware, so this cannot occur yet;
  it must be re-examined before any write test.
* **commit() read-back**: if the settings read-back after a successful write fails, `commit()`
  reports an error while the write was applied; a retry re-sends an idempotent write.
* **Request variant** (UAC2 `0x03` vs UAC1 `0x85/0x05`) is not auto-selected from `bcdDevice`.
* **GUI thread safety**: the TSan run covers the Qt-free core only. It says nothing about the
  Qt GUI, the worker/controller signal flow or QML; those are covered by ASan/UBSan and
  integration tests, not by a race detector.
* **QML AOT compilation is disabled** (Qt 6.4 singleton issue); the app loads QML at run time.

## 3. Test and tool results (this host: Ubuntu 24.04, Qt 6.4.2, GCC 13)

| Suite | Result |
|-------|--------|
| Release, full (`--preset release`) | 96/96 ctest entries pass |
| Mock-only (no libusb) | all but the libusb-only entries pass (93/93 run) |
| ASan+UBSan, GUI included | 0 failures (packaging test excluded for sanitizer binaries) |
| TSan, core only (`setarch -R`) | 93/93 pass, no reports |
| Strict-warning build | no warnings in project code except benign `Status` enumerator shadowing |
| Clean clone (fresh `git clone`, sysroot re-created by `tools/setup-sysroot.sh`) | release 95/95, mock-only 93/93 |
| `audit_usb_call_sites` | pass |
| `packaging_staging` | pass |

## 4. Protocol cross-check (against PROTOCOL.md and the recovered evidence)

Verified by test and review: message ids 0/2/3/4/0x0A and sizes 0x80/0x40; settings schema
(62 records, bytes 0-70, 71-127 untouched); little-endian; Q8.8 encoding, input-gain clamp
72/60 dB, volume -60..0, mix stored sign-inverted; handshake major must be 1; PID -> mode
mapping (0x0004 normal, 0x0002 macOS streaming); custom-button enum 0,1,2,8,9,0xC,0xD,0xE;
trigger words 1,2,3,6,7,8 / 4,5 / 0x103 / 0x104,0x105 / 0x106 and their guard treatment.
All are *static-analysis* facts; none is hardware-verified.

Full-structure writes: the 0x80-byte settings message is always written as a whole;
bytes outside the verified setter set are checked equal to the device snapshot before any
write (`verifyOnlyKnownFieldsChanged`), and the snapshot is re-read after the write.

## 5. Hardware-access safety matrix

| Property | Mechanism | Evidence |
|----------|-----------|----------|
| No automatic kernel-driver detach | gate at transport + CLI; GUI never sets it | `Audit.DetachPath…`, call-site audit |
| No unauthorized writes | `allowWrites=false` default, session policy, GUI read-only on hardware | WriteGuard tests, `GuiBackend.UsbBackendIsReadOnly…` |
| No device reset / set-configuration / alt-setting / bulk / ioctl / sysfs writes | not present in the source | `audit_usb_call_sites` |
| FactoryReset, firmware, mode switch never on hardware | guard in session and transport; no firmware code exists | WriteGuard tests |
| EACCES / EBUSY / EPIPE reported distinctly | `mapLibusbError` + exit codes + suggestions | hardening tests |
| Safe timeouts / serialization / cancel | 500 ms, `SerializedTransport` | Serialized.* tests |
| Not bypassable via CLI | CLI has no write command; `--method detach` needs env + transport gate | call-site audit, CLI review |

## 6. Source and Git-history audit

* **Tracked files (HEAD)**: ~130 files; sources, docs, own artwork (4 screenshots of the
  simulated GUI, 4 icon PNGs + SVG), scripts. No installers, firmware, decompiled code,
  LEWITT images, captures, credentials or personal data. `docs/OFFICIAL_GUIDE.md` is a
  paraphrased summary (4.5 kB) of the public guide with the source URL; the guide's own
  text/screenshots are kept only in the git-ignored `research/guide/`.
* **History (12 commits, single author identity `st0nebyte`)**: scanned for private keys,
  tokens, passwords, API keys, e-mail addresses and home paths: none, other than the
  placeholder `noreply@example.invalid`. No `research/`, `.tools/`, `.sysroot/` or `.venv/`
  paths were ever tracked.
* **Finding (F12)**: commits `eb2956b`..`91fdefd` contain `build-asan/` and `build-tsan/`
  (own build artefacts, ~110 MB uncompressed). Not proprietary, not sensitive, but they bloat
  the repository (`.git` is ~29 MB after pruning the duplicate this phase created).
  Removing them from history requires rewriting local, unpublished commits
  (`git filter-branch`/`git filter-repo`), which was **not done without your approval**.
  A bundle backup of the pre-audit state exists at `.tools/pre-filter-history.bundle`
  (git-ignored). Suggested command if approved:
  `git filter-branch --index-filter 'git rm -r -q --cached --ignore-unmatch build-asan build-tsan' -- --all`
  followed by `git reflog expire --expire=now --all && git gc --prune=now`.
* Nothing was pushed; no GitHub repository was created.

## 7. Remaining blockers before the first real-device session

1. **Hardware**: nothing is verified. The first session must follow
   `docs/HARDWARE_TEST_PLAN.md` (Appendix A) and is read-only.
2. **USB permissions**: the udev rule needs an administrator to install it (documented, not
   installed). EBUSY from `snd-usb-audio` is expected and is a research outcome, not a bug.
3. **Licence** not chosen (docs/LICENSE_ASSESSMENT.md).
4. **namcap** could not be run (needs libalpm/pyalpm); `makepkg` was run rootless with
   `--nodeps` on Ubuntu, not on Arch. Real `makepkg -s` + `namcap` on Arch/CachyOS remains.
5. **Package installation** was simulated into a private root with a stubbed `udevadm`; no
   package was installed on the host.
6. Optional: remove build artefacts from history (F12).
