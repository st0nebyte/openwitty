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
6. **Next:** fix `write led-brightness`'s own post-write read-back to account for the settling delay
   (delay or bounded poll instead of an immediate re-read) before proposing any further write targets.
   Any additional writable field needs its own narrow, separately-reviewed command — no generic
   field/byte write command is planned.

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
- USB mode: stop retrying after EACCES/EBUSY until the device list changes.
- Re-examine autogain completion inference before any write test.
- Auto-select UAC1/UAC2 request variant from bcdDevice once known.
- Licence: MIT (repository LICENSE, by the owner). Review third-party notes in docs/THIRD_PARTY.md.

## Packaging (phase 5D follow-ups)

- Owner: replace placeholder homepage/maintainer in CPack, PKGBUILD, metainfo.
- Run makepkg + namcap (Arch) and lintian (Debian) on real hosts; smoke-test installs in a VM.
- Install the udev rule manually only after review (docs/USB_PERMISSIONS.md).
- AppImage/Flatpak: deferred (see docs/PACKAGING.md).
- Add `developer`/screenshots to metainfo once a licence and hosting exist.
