# Release readiness — first public alpha (draft)

Status of the development branch for a first public Linux alpha. Nothing has been tagged, released, published
or merged into `main`; that needs an explicit release decision by the owner. Reviewed in Phase 9A (2026-10-25).

## What has actually been tested

| Item | Tested configuration |
|---|---|
| Hardware | **one** LEWITT CONNECT 2 (`29c2:0004`), firmware not decoded; one USB 3 port on an AMD xHCI controller |
| Distribution | **CachyOS** (Arch-based), Linux **7.3.0-5-cachyos** |
| Desktop | KDE Plasma **6.8** on Wayland, PipeWire **1.6** / WirePlumber **0.5** |
| Install route on the device | user-local CMake install into `~/.local` |
| Packages | Arch package built with a real rootless `makepkg` on that host (not installed); `.deb` contents built and inspected only (no Debian host, see `docs/PACKAGING.md`) |
| Not tested at all | other distributions, other kernels, GNOME or other desktops, PulseAudio-only or plain-ALSA systems, other CONNECT 2 units or firmware, the CONNECT 6, installing either package |

## Checklist

| # | Check | Result |
|---|---|---|
| 1 | Builds from a fresh tree (Release), all CTests pass | PASS — 282/282 |
| 2 | GUI test suite (offscreen) | PASS — 54/54 |
| 3 | ASan/UBSan suite (`-DLEWITT_BUILD_GUI=OFF`) | PASS — see "Validation run" |
| 4 | USB call-site audit (`tools/audit/check_usb_call_sites.sh`) | PASS |
| 5 | Packaging staging check (`packaging_staging`, incl. service, man pages, licences, no `modprobe.d`, unit not enabled) | PASS |
| 6 | Real `makepkg` (rootless, temporary directories) incl. `check()` | PASS — 281/281 (`packaging_staging` excluded by design) |
| 7 | `namcap` | **NOT RUN** — not installed; must be run before publishing an Arch package |
| 8 | Debian `lintian`, `dpkg-shlibdeps`, install/remove lifecycle since the desktop integration | **NOT RUN** — no Debian tooling on the host |
| 9 | Desktop entry (`desktop-file-validate`), AppStream (`appstreamcli validate --no-net`), man pages (groff warnings) | PASS — no errors, no hints |
| 10 | Package contents: `root:root`, stripped, no RPATH, no development paths, licences, udev rule, user unit installed disabled, quirk only as an example | PASS (Arch package inspected; `.deb` contents inspected) |
| 11 | Uninstall: manifest lists every installed file and link; packages remove their files; user settings documented | PASS (staging tests; user-local uninstall procedure documented) |
| 12 | Public documentation consistent with the hardware evidence | PASS after Phase 9A (README, INSTALLATION, PACKAGING, FEATURES, man pages, AppStream, package descriptions) |
| 13 | Unofficial-project disclaimer in README, desktop entry, AppStream, packages, `--version` | PASS |
| 14 | No proprietary artefacts, captures, build outputs, device serials or secrets tracked | PASS for the tree; see "Before going public" for the Git history |
| 15 | Real-device validation of the desktop integration (window, service, hand-over, Mute button, boot/login, suspend/resume) | PASS on the tested configuration (`docs/VOLUME_INTEGRATION.md`) |
| 16 | Vendor fallback validated on hardware | **NOT DONE** — documented as experimental |
| 17 | GUI *Speaker/headphone control* exercised on hardware | **NOT DONE** — documented as not exercised |
| 18 | Version and release metadata decided | **OPEN** — see "Versioning" |
| 19 | Tray icon and single window (Phase 9B): offscreen tests (tray unavailable, service disabled/running/idle/not installed, repeated opens, quit leaves the service alone, autostart on/off/reset), end-to-end single-instance script, no USB/ALSA linkage | PASS (offline) |
| 20 | Tray icon and window activation live on KDE Plasma (Wayland), incl. login autostart | PASS — see "KDE Plasma acceptance (Phase 9B)" |
| 22 | Speaker level at login follows the knob position of the previous session | **FAIL (limitation, not caused by OpenWitty)** — WirePlumber restores its saved level onto the device; see "Confirmed limitation: WirePlumber restores the speaker level at login" |
| 21 | Single-instance ownership race-free (flock), explicit failure | PASS — 7 barrier-synchronised race tests (also under ASan/UBSan), end-to-end script |

## Known limitations and experimental features

* Tested on one device and one system only (table above). The desktop integration's timing (the device reports
  a level the kernel set 0.4–0.7 s late; the follow grace is 1.5 s) was measured on that system only.
* On the real device most controls are display-only; only the speaker/headphone volume and mute can be changed
  from the GUI, and only after a confirmed opt-in. Low-cut, 48 V, Clipguard, Denoise, Comp, LED colours, custom
  button, autogain/autosetup and sample-rate selection work with the simulated device only.
* **Experimental:** vendor fallback (not validated on hardware), GUI speaker/headphone control (not exercised
  on hardware), CLI write commands (`instrument-mute` never executed).
* The kernel compatibility quirk is specific to the CONNECT 2 and was needed on Linux 7.3.0; other kernels have
  not been checked. It is never installed by OpenWitty or its packages.
* The device stalls for a few seconds after power-up (kernel `usb_set_interface failed (-110)` on every boot);
  the service recovers with one bounded reconnect.
* A resume after which the device is re-enumerated (power lost during sleep), longer sleeps and `s2idle` as the
  default sleep mode were not tested.
* Not supported: CONNECT 6; firmware update and factory reset (deliberately not implemented); presets.
* Not a replacement for LEWITT CONTROL CENTER.

## KDE Plasma acceptance (Phase 9B, 2026-10-25)

System: CachyOS, Linux 7.3.0, KDE Plasma 6.8 on Wayland; user-local install of `230b7dd` (window, tray) with the
background service process started earlier from `07220fb` (left running; its code is functionally unchanged
since, CMake replaces installed files without disturbing a running process). Vendor fallback and
speaker/headphone control off throughout.

**Offline (before installing):** CTest 285/285, GUI 57/57, tray 16/16, race 7/7 (5 repeats, also ASan/UBSan),
end-to-end single-instance script, ASan/UBSan 265/265, USB audit, packaging staging, privacy grep.

**Observed on the real desktop:**

| Check | Observation |
|---|---|
| Tray icon appears after "Show OpenWitty in the system tray" | yes, one icon, OpenWitty artwork; one `openwitty-tray` process |
| Tooltip | "OpenWitty / Volume knob in desktop volume: on / Background service: running" — matches the real state |
| Menu | two greyed-out status lines, Open OpenWitty, Desktop integration settings…, Quit tray icon |
| Window closed → left click on the icon | OpenWitty opens and connects (hand-over from the service, see defects) |
| Window open → tray click, launcher start, tray "Desktop integration settings…", launcher action | owner: all four bring the existing window to the front **with keyboard focus**; logs: the requests reached the running window, a launcher start printed "already open"; never two windows |
| Window closed | the GUI's socket disappears; the service takes the device back (journal "device connected", read-only start); the tray stays |
| Knob after closing the window and after "Quit tray icon" | still mirrored by the service (e.g. −18.9 → −9.1 dB) |
| "Quit tray icon" | icon gone; service same PID, `NRestarts=0`, enabled; settings unchanged; autostart entry kept |
| Tray switch off / on / off / on (monitored) | entry removed and icon stopped / entry written and icon started, each within ~1 s |

**Defects found and fixed during the acceptance test:** opening the window while the service held the device
showed a red "already in use by another OpenWitty process" for 1–2 s (the normal hand-over) — now presented as
"Connecting — taking over the CONNECT 2 from the OpenWitty background service…" with 300 ms lease retries
(`8eda3b1`); the header indicator still said "Error" (`7fc6015`); "usb" shown as "USB" (`230b7dd`).

**Login (owner logged out and in, 18:29):** the tray icon came back by itself (`openwitty-tray` started by the
session's XDG autostart, parent the systemd user manager); `openwitty-volumed` was stopped with the old session
and started with the new one (new PID, `NRestarts=0`, read-only start); settings and autostart entry unchanged.

**Not performed live:** the tray status following a knob-display change made in the window (offline only); the
tray on a desktop without a Status Notifier host (offline only: exits quietly).

## Confirmed limitation: WirePlumber restores the speaker level at login

Observed (Phase 9B login, and consistent with every start in the logs of 2026-10-25): the device speaker was at
−9.34 dB when the owner logged out (last read by the window); the service started after login read −23.50 dB.
WirePlumber's `~/.local/state/wireplumber/default-routes` holds `channelVolumes 0.066836` for the CONNECT 2 output
= −23.5 dB. Because the kernel forwards desktop volume changes to the device (Feature Unit 2 SET honoured),
WirePlumber's route restore at login sets the **device** to that saved level. WirePlumber saves levels changed
through PipeWire (desktop slider, volume keys), but not the ALSA updates OpenWitty makes to mirror the knob, so the
saved level is the last *desktop-set* one. The startup hold keeps OpenWitty from treating the restore as a user
change, but the restore itself happens before OpenWitty starts and is outside its control.

Effect: after a login the speaker returns to the last level set on the desktop, which can be quieter **or louder**
than the knob position of the previous session. Not caused by OpenWitty; not seen as a problem by the owner yet.
Not attributed by a kernel trace (inferred from the saved state, the start levels and the timing).

Fix options (design decision, not implemented): (a) mirror the knob through PipeWire (route volume via the
PipeWire/WirePlumber API instead of the raw ALSA control) so WirePlumber saves it; (b) document how to stop
WirePlumber from restoring this route; (c) let the service record the last device level and report a login
change. (a) is the clean one; it touches the synchronization path and needs its own validation.

## Installation and rollback

`docs/INSTALLATION.md` (packages, user-local install, USB permission rule, kernel quirk, desktop integration,
service, uninstall). Rollback of a user-local install:

```sh
systemctl --user disable --now openwitty-volumed.service      # if it was enabled
xargs -d '\n' rm -f < build-local/install_manifest.txt
systemctl --user daemon-reload
openwitty-tray --quit; rm -f ~/.config/autostart/io.github.st0nebyte.openwitty-tray.desktop   # if the tray was on
rm -r ~/.config/openwitty                                     # optional: settings
```

Packages: `pacman -R openwitty` / `apt remove openwitty`; the user service and settings as above.

## Versioning (recommendation, not applied)

The tree advertises `0.1.0` (CMake `project()`, PKGBUILD, Debian changelog) and AppStream lists a `0.1.0` release
dated 2026-10-14 that never happened. Recommendation for the first public alpha:

* Keep the CMake version `0.1.0` and add a pre-release label in one place, e.g. a `LEWITT_VERSION_SUFFIX` of
  `alpha.1`, shown by `--version` as `0.1.0-alpha.1`.
* Git tag `v0.1.0-alpha.1`; Arch `pkgver=0.1.0alpha1` (no hyphen allowed; `0.1.0alpha1` sorts before `0.1.0`);
  Debian `0.1.0~alpha1-1` (the tilde sorts before `0.1.0-1`).
* AppStream: replace the 2026-10-14 entry with `<release version="0.1.0~alpha1" date="<release date>"
  type="development">` and a short description.
* Subsequent alphas increment the label; `0.1.0` is reserved for the first non-alpha release.

## Before going public (owner decisions)

* **Git history** contains a personal e-mail address as author identity of 26 commits (and a first name in 13).
  Rewriting history is outside this branch's rules; decide whether to publish the history as is, rewrite it,
  or publish a squashed tree.
* Replace the packaging contact (`noreply@users.noreply.github.com` placeholder) if wanted.
* `docs/` contains many dated engineering notes (`PHASE*`, `CLAUDE_SESSION_START.md`, handoff/test-plan
  documents, marked historical where outdated). They are not installed by packages; decide whether they stay in
  the public repository.
* Run `namcap`, a Debian build with `lintian`, and a clean-VM install of both packages.
* `docs/THIRD_PARTY.md` notes a possible visual similarity of the GUI with the vendor's application.

## Release blockers (current assessment)

None in the code or packaging for a **private alpha review**. For a **public** release, the open items above
(history/identity decision, `namcap`, Debian re-validation, version metadata) must be resolved first.

## Validation run (Phase 9A)

Final run on the Phase 9A tree (CachyOS host; nothing installed, the running user-local installation untouched):

| Check | Result |
|---|---|
| Fresh Release build in a temporary directory: CTest / GUI tests | 282/282, 54/54 |
| `build-release`: CTest / GUI tests / USB audit | 282/282, 54/54, PASS |
| ASan/UBSan (`build-asan`, GUI off, `halt_on_error=1`, LSan suppressions) | 265/265 |
| `packaging_staging`, `install_staging_user_unit` | PASS |
| Real rootless `makepkg` from a working-tree tarball, temporary BUILDDIR/PKGDEST/SRCDEST | built; `check()` 281/281 |
| `desktop-file-validate`, `appstreamcli validate --no-net` on the packaged files | clean |
| `man --warnings` on the three man pages | clean |
| `.deb` via CPack (`SHLIBDEPS=OFF`, contents only) | built and inspected |
| Privacy grep (device serial, root UUID), `git diff --check` | clean |

Defects found and fixed in Phase 9A (packaging/docs only): the PKGBUILD lacked `alsa-lib`; the
`install_staging_user_unit` test failed for packages that set an absolute user unit directory (seen only in the
real `makepkg` run); the `.deb` did not declare the Qt SVG plugin; `openwitty-volumed` had no man page; the CLI
usage text omitted the speaker write commands; outdated "not validated on hardware" statements in README,
INSTALLATION, man pages, AppStream, package descriptions and `--version`.
