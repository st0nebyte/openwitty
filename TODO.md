# TODO

## Needs the device (do in this order, read-only first)
1. ✔ done 2026-10-14: `openwitty-cli list`/`devices`; descriptors recorded (`docs/HARDWARE_TEST_RESULTS.md`).
2. ✔ done 2026-10-14: `probe --execute` — **access method decided: interface 3** (unbound,
   vendor-specific `ff/f0/00`), not the AudioControl interface. See `docs/USB_ACCESS.md` §3b.
3. ✔ done 2026-10-14: `read version|status|settings --interface 3` all answer read-only, over the
   same unbound interface (new `--interface N` flag on `openwitty-cli read`, no new USB call site).
   `settings` decodes cleanly into plausible real values (mic gain 27 dB, phantom on, headphone
   -19.71 dB muted, etc.) — see `docs/HARDWARE_TEST_RESULTS.md` §7b. `version`/`api` return an
   unexplained byte-identical payload despite different `wValue` — open question, not yet resolved.
4. ✔ done 2026-10-14/15: plan step 9 — physically confirmed mic/instrument gain, all four
   channels' volume/mute, and the channel-select byte (56) against real hardware. Mono, Clipguard,
   preamp style have no physical control on this unit (software/CONTROL-CENTER-only, owner-confirmed)
   — documented as a hardware-access limitation. See `docs/HARDWARE_TEST_RESULTS.md` §§10, 12.
5. ✔ done 2026-10-15: first real hardware write, LED brightness (100→60→100, one transition visually
   witnessed), via a single narrowly-scoped, explicitly-gated CLI command (`write led-brightness`,
   needs `--execute` + `LEWITT_ALLOW_HARDWARE_WRITE=1`). Found a settling delay between a SET and a
   GET reflecting it — see `docs/HARDWARE_TEST_RESULTS.md` §13, PROTOCOL.md, `docs/USB_ACCESS.md` §3c.
6. ✔ done 2026-10-15: timing experiment pinned the settling delay down precisely — convergence
   within ~1ms (not "tens-to-hundreds of ms" as first estimated), explained by the near-zero gap
   between a write and the very next same-process read, not a slow device. See
   `docs/HARDWARE_TEST_RESULTS.md` §15.1.
7. ✔ done 2026-10-15 (software only, no hardware writes): built `DeviceSession::commitAndVerify()`
   (`core/WriteVerification.hpp`) — a real `WriteState` machine that only reports success after a
   read confirms it, bounded read-only polling, no automatic SET retries. 11 new mock tests. GUI
   wired through it (`setValue`/`retryPending`) with a new composite status label. Full policy table
   for the 8 "progressive control" fields drafted in PROTOCOL.md — **none activated except LED
   brightness**; each remaining field still needs its own narrow, separately-reviewed command and its
   own hardware test before it can be written to, same as LED brightness was.
8. **Next (needs explicit per-field authorization, one at a time):** pick the next field to write-test
   against real hardware from PROTOCOL.md's policy table. Candidates ranked by risk: mic/instrument
   gain and mute states are Low/Medium risk and already have hardware-confirmed field mappings from
   physical testing (Phase 6A/6B) — good next candidates. Speaker/headphone volume and monitoring mix
   are Medium risk (directly affect audible output) and need the write-time safety precautions already
   used in Phase 6A/6B (mute first, confirm no live audio). Preamp style cannot be physically
   cross-checked on this unit at all (no hardware control exists) — lower priority for hardware
   testing specifically. Phantom power needs its own separate authorization per instruction, not
   bundled with any of the above.
9. **Correction, 2026-10-16:** Phase 6B's HARDWARE_TEST_RESULTS.md §12.7 overstated a negative
   finding as "no long-press gesture exists on any front-panel button" when only preamp/Mono/
   Clipguard cycling on the select buttons had actually been asked about. The MUTE button's hold
   gesture ("Panic Mute") was separately already documented (`docs/OFFICIAL_GUIDE.md`, phase 5-era
   static analysis) and is unaffected by that Phase 6B answer — now independently re-verified against
   the manufacturer's online guide. Wording corrected in HARDWARE_TEST_RESULTS.md, FEATURES.md,
   PROTOCOL.md, `docs/OFFICIAL_GUIDE.md`.
10. ✔ done 2026-10-16: Panic Mute hardware-confirmed, 3 read-only trials — **exactly bytes
    2/12/22/29** set simultaneously regardless of selected channel; loopback (19) and channel-select
    (56) confirmed untouched; no separate flag exists. Added `MockTransport::simulatePanicMute()`,
    read-only `mute.allFour` derived GUI state, 3 new tests (108/108 total). No software Panic Mute
    trigger implemented, per instruction. See `docs/HARDWARE_TEST_RESULTS.md` §16, PROTOCOL.md.
11. **Open, not yet investigated:** the reproducible but unidentified `status` message change
    (offsets 3–4, 12–13, same values across all 3 Panic Mute trials) — not in PROTOCOL.md's
    documented status field map. Worth a dedicated follow-up once there's a hypothesis for what it
    might represent.

## Needs the user
* Approve REA MCP registration (`rea setup --dry-run` first; edits ~/.claude.json and ~/.claude/skills).
* If `ls -l /dev/bus/usb/...` shows no write access for the user, an admin must add the udev rule (not fixable without root).

## Engineering
* ✔ done: Windows MEM GET/SET verified against macOS (PROTOCOL.md).
* ✔ done (5A): custom-button enum, OutputSource usage, trigger words incl. 0x103/0x106, measurement result bits, settings schema, sync algorithm, API handshake (0x0A).
* ✔ macOS-streaming mode switch = trigger 0x104/0x105 (phase 5B); measurement bracket words 4/5.
* Still open: device-side effect of 4/5; bytes 54–56, 58, 60–70; "Background noise too loud" status source (ambient-noise panel); mix range; `iface_version_t`.
* Hardware: first read `openwitty-cli read api` (message 0x0A) — it validates the whole transport with the smallest read-only message.
* Use REA MCP session for iterative function/xref work (CLI re-imports each call, ~2 min).
* Recover `iface_version_t` layout; identify which settings groups are headphone/speaker.
* Disassemble the write path & tick cadence (`Connect2Device::tick`).
* Preset import/export (`.preset` JSON as shipped by the app).
* Visual fidelity: extract/inspect UI assets from the app (not yet done; current QML is an independent layout).
* Packaging (deb/PKGBUILD/AppImage), udev rule in `packaging/udev`.
* Kernel hwdep patch only if probe fails (docs/USB_ACCESS.md).

## GUI (phase 5B follow-ups)
* Persist the first-run onboarding flag (`general.onboardingDidNotRunYet` in the original settings file).
* Settings as a separate window? (unknown) — currently an overlay.
* Analyse remaining high-resolution references (B-main, B-mute) for knob/meter detail; replace paraphrased onboarding text only if wording is verified.
* Fonts: match the original's UI font if identifiable; own icon polish (guitar glyph).
* Re-trace on Windows: MixToSpeakers, API handshake, schema merge.

## Backend (phase 5C follow-ups)
* ✔ done 2026-10-14: hardware session, filled result template → `docs/HARDWARE_TEST_RESULTS.md`.
* Option C (`docs/USB_ACCESS.md`) confirmed working on real hardware; implement an "unbound
  interface" access method in `UsbTransport`/CLI (currently only `probe`'s hardcoded message-0x0A
  matrix uses it) so version/status/settings reads can use the same path.
* Option E (hwdep kernel driver) deprioritized now that C works from userspace — only revisit if C
  proves insufficient.
* An administrator must install `packaging/udev/70-openwitty.rules` (EACCES otherwise).
* Interruptible control transfers (libusb async + cancel) to bound shutdown latency below 500 ms.
* Decide whether to poll settings at 1–4 Hz on real hardware or only on events (no interrupt endpoint knowledge yet).
* Persist onboarding flag; log file / diagnostics export.

## Phase 5E follow-ups

- Owner decision: remove `build-asan/`/`build-tsan/` from history (commits eb2956b..91fdefd);
  see docs/AUDIT_PHASE5E.md section 6. Not done without approval.
- Run namcap and `makepkg -s` on Arch/CachyOS; clang-tidy/cppcheck when obtainable rootless.
- ~~USB mode: stop retrying after EACCES/EBUSY until the device list changes.~~ (Phase 7A, unit-tested)
- Re-examine autogain completion inference before any write test.
- Auto-select UAC1/UAC2 request variant from bcdDevice once known.
- Licence: MIT (repository LICENSE, by the owner). Review third-party notes in docs/THIRD_PARTY.md.

## Packaging (phase 5D follow-ups)

- Owner: replace placeholder homepage/maintainer in CPack, PKGBUILD, metainfo.
- Run makepkg + namcap (Arch) and lintian (Debian) on real hosts; smoke-test installs in a VM.
- Install the udev rule manually only after review (docs/USB_PERMISSIONS.md).
- AppImage/Flatpak: deferred (see docs/PACKAGING.md).
- Add `developer`/screenshots to metainfo once a licence and hosting exist.

## Phase 7B (after the Panic Mute commits are integrated)

- `SettingsMessage::selectedChannel()` + `selected.channel` + strip highlight (byte 56 is hardware-confirmed).
- Evidence badges for unverified fields in QML; `ReplayTransport` with serial-redacted real fixtures.
- Settings poll rate on USB (≥ 2 Hz) and a `mute.allFour`-aware Panic Mute indicator.
- Hardware session: GUI read-only against the real unit (`--interface auto` first), compare with CLI.
- Re-run packaging checks (`packaging_staging`) — unchanged by 7A but not re-verified on `.deb`.
