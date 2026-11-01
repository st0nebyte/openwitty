# Product mode, private beta and final review (plan, 2026-11-01)

Owner decision (2026-11-01): OpenWitty must be fully usable without switching permissions on and off; the
evaluation features become a separate debug mode. The owner uses it privately for one week and reports every issue;
then a final review decides about publication.

## 1. Product mode (before the beta week)

**What changes for the user**

* Every control that was verified on the real CONNECT 2 works immediately: inputs (gain, mute, gain lock), processing
  (low-cut, Clipguard, preamp, Denoise, Comp), monitoring (source, mix, Dim, Mono, loopback mute), outputs (speaker
  and headphone volume and mute, output modes), LEDs (brightness, colours, custom button), 48 V, routing (mix to
  speakers, mobile streaming, volume-key mapping), follow active output, Autosetup and Denoise measurements, presets.
* Settings ▸ **Device control** (eleven permission switches, legacy notice, "Currently allowed") is removed. The
  window's write rules no longer depend on the settings file; the permission keys in `openwitty.conf` are ignored
  without a warning and dropped at the next save.
* Disabled placeholders are removed: "Check for updates", "Manual update", "Factory reset" (shown only as a note
  that firmware update and factory reset are not part of OpenWitty).

**What stays (invisible safety, no switches)**

* Only known bytes are ever written (the byte list of all verified controls); unknown bytes, persistent changes,
  firmware update and factory reset stay impossible. macOS streaming mode (restarts the device) stays simulation-only.
* Every write is read back; after an unconfirmed write the window stops writing and shows why, with a **Reconnect**
  button (instead of a notice hidden in Device control).
* Large increases are applied in bounded steps (outputs 10 dB, gain 6 dB per SET) — the end value is the same.
* One-click confirmations only where hardware or hearing is at risk: 48 V on, high-volume speakers on,
  high-impedance headphones on, mobile streaming (changes what the inputs carry).
* The background service checks every client write against the same byte list (unchanged mechanism, no
  configuration).

**Debug mode** (`openwitty --debug`; `--hw-check` implies it)

* A "Debug" section in Settings, only in debug mode: hardware check (baseline / compare, was `--hw-check`), vendor
  fallback for desktop volume (experimental), "Allow 44.1 and 96 kHz" (experimental, hung the device once), last
  write outcomes, log location.
* Developer start options stay but are hidden from `--help` unless `--debug`: `--mock`, `--screenshot`, `--size`,
  `--show`, `--quit-after-seconds`, `--diagnostic-legacy-polling`.
* `openwitty-cli` is the developer tool and stays as it is: its four write commands keep `--execute` and the
  `LEWITT_ALLOW_*` variables (its tests rely on those refusals never reaching a real device); `--method detach` and
  `probe --execute` stay behind `LEWITT_ALLOW_DISRUPTIVE=1`. Everyday control is the window.

**Done when:** all tests pass (normal and sanitizer build), the USB audit is updated and passes, every control is
tried once on the real device in product mode with read-back (owner present, safety confirmed), docs updated.

## 2. Private beta week (owner, about 2026-11-02 to 11-09)

* Normal daily use: desktop volume keys, calls, recording, DAW, suspend/resume, replug, reboot, login.
* Report each issue in one line or more: *what happened, when (time), what you expected, what you did before*. Screens
  or `openwitty-cli diagnose` output help. I keep them in `docs/BETA_ISSUES.md` (numbered, status, fix commit).
* Severity: **S1** sound/hardware at risk or data lost · **S2** a feature does not work · **S3** wrong text, layout,
  annoyance. S1 is fixed immediately; S2/S3 in batches, each fix with a regression test where possible.

## 3. Final review (after the beta week)

Each step ends with a written result in `docs/FINAL_REVIEW_RESULTS.md`; failures go back into the issue list.

1. **Correctness review of the whole code base** (not only diffs): the code-review tool at high effort per module
   (protocol, transport, core, daemon, gui, cli, desktop); manual review of the threads (DeviceWorker, DeviceServer,
   follower), timeouts and error paths; every `TODO`/`unverified` comment resolved or documented.
2. **Static and dynamic analysis:** compiler warnings at `-Wall -Wextra -Wpedantic` clean; clang-tidy and cppcheck;
   ASan/UBSan full test run; TSan run of the service and worker tests; fuzzing (≥ 10 min each) of the parsers:
   settings/status messages, the service socket protocol, `openwitty.conf`, preset JSON, `pw-cli`/`pw-metadata`
   output.
3. **Security review:** service socket (permissions, SO_PEERCRED, message size limits, one controller), files written
   (config, presets, gui.conf: atomic, modes), external programs (no shell, fixed argv), udev rule (only the
   CONNECT 2, user access only), no root at runtime, no network access.
4. **Hardware regression on the real device** (owner present, safety confirmed): every control once with read-back;
   presets save/load; Autosetup; replug during idle and during playback; suspend/resume; reboot; logout/login;
   service restart; window + CLI at the same time; DAW (Ardour JACK) recording; 2-hour run with meters open.
5. **UX review:** screenshots of every page and dialog in the default size and minimum size; texts (English, no
   internal phase names or byte numbers in the product UI), tooltips, keyboard focus, error states (no device,
   unplugged during a change, service missing, PipeWire restarted).
6. **Packaging and install:** Arch package and Debian package built in clean environments; install, upgrade,
   uninstall leaves nothing behind; user-local CMake install; autostart, tray, service; the optional system files
   (kernel quirk, login-screen rule) documented with rollback.
7. **Documentation:** README, INSTALLATION, FEATURES, PROTOCOL, CHANGELOG consistent with the product; the phase and
   test documents moved to `docs/dev/` so users see only user documents; known limitations listed honestly.
8. **Publication readiness:** licence and third-party notices; wording about LEWITT (unofficial, not affiliated, no
   vendor code or binaries in the repository); final privacy scan of every revision and message of the repository
   that would be published; version 0.1.0 and changelog.
9. **Owner decisions:** merge to `main`, tag, release, visibility. Nothing of this happens without them.

**Exit criteria:** no open S1/S2 issue; every step above has a result; the owner signs off.
