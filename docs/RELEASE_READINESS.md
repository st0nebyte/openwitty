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

Numbers from the Phase 9C run (2026-10-25); see "Validation run" for the commands.

**Build and tests**

| # | Check | Result |
|---|---|---|
| 1 | Release build, all CTests | PASS — 293/293 |
| 2 | GUI tests (offscreen) / tray tests / single-instance race tests | PASS — 57/57, 17/17, 7/7 (race and tray also under ASan/UBSan) |
| 3 | ASan/UBSan suite (`-DLEWITT_BUILD_GUI=OFF`) | PASS — 273/273 |
| 4 | USB call-site audit (incl. tray/desktop-UI and single-instance rules) | PASS |
| 5 | End-to-end single-instance script (second starts, two trays, unanswering owner) | PASS |

**Packaging**

| # | Check | Result |
|---|---|---|
| 6 | Packaging staging check (service, tray, man pages, licences, no `modprobe.d`, no autostart entry, unit not enabled) | PASS |
| 7 | Real rootless `makepkg` incl. `check()` | PASS — 292/292 (`packaging_staging` excluded by design) |
| 8 | Package contents: `root:root`, stripped, no RPATH/development paths, licences, udev rule, unit disabled, quirk and login-screen rule only as examples | PASS (Arch package inspected; `.deb` contents inspected in Phase 9A) |
| 9 | Desktop entry, AppStream, man pages validators | PASS |
| 10 | Uninstall: manifest complete; packages remove their files; user settings and tray entry documented | PASS |
| 11 | `namcap` | PASS — PKGBUILD clean; package: only the documented `qt6-svg` run-time plugin false positive (Phase 9D) |
| 12 | Debian: build + 292 tests, `dpkg-shlibdeps`, `lintian --pedantic`, lifecycle, real install/use/remove/purge | PASS on Debian 13 (disposable container, Phase 9D); Ubuntu 24.04 / Debian 12 **not tested** |

**Documentation and repository**

| # | Check | Result |
|---|---|---|
| 13 | Public documentation consistent with the evidence | PASS (Phase 9A–9C) |
| 14 | Unofficial-project disclaimer everywhere | PASS |
| 15 | No proprietary artefacts, captures, build outputs, device serials or secrets in the tree | PASS; Git history: see "Before going public" |

**Real device and desktop (one system)**

| # | Check | Result |
|---|---|---|
| 16 | Desktop integration: window, service, hand-over, Mute button, boot/login, suspend/resume | PASS (`docs/VOLUME_INTEGRATION.md`) |
| 17 | Tray, window activation with focus, quit, login autostart on KDE Plasma (Wayland) | PASS — "KDE Plasma acceptance (Phase 9B)" |
| 18 | Speaker level unchanged across logout/login | **FAIL (limitation)** — the login screen's own audio service sets its default level; see "Speaker level at the login screen (Phase 9C)" |
| 19 | Vendor fallback on hardware | **NOT DONE** — experimental |
| 20 | GUI *Speaker/headphone control* on hardware | **NOT DONE** — no GUI control has written to the real device yet; plan in `docs/FEATURE_COMPLETION_ROADMAP.md` |
| 20a | GUI permissions match the write policy (Phase 10A: per-control capabilities; triggers need their own authorization) | PASS (offline tests) |

**Open decisions**

| # | Item | Status |
|---|---|---|
| 21 | Version and release metadata | **DONE** (`0.1.0-alpha.1`, checked by `version_consistency`); tag and release date at release time |
| 22 | Login-screen rule (Phase 9C option B) live-validated | **OPEN** — plan below |

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
* With the kernel quirk, a graphical login screen's own audio service sets the CONNECT 2 speaker to its default
  level (≈ −23.5 dB) each time it appears; optional login-screen-only WirePlumber rule (example, not yet
  live-tested). See "Speaker level at the login screen (Phase 9C)".
* The device stalls for a few seconds after power-up (kernel `usb_set_interface failed (-110)` on every boot);
  the service recovers with one bounded reconnect.
* A resume after which the device is re-enumerated (power lost during sleep), longer sleeps and `s2idle` as the
  default sleep mode were not tested.
* Not supported: CONNECT 6; firmware update and factory reset (deliberately not implemented); presets.
* Not a replacement for LEWITT CONTROL CENTER.

## KDE Plasma acceptance (Phase 9B, 2026-10-25)

System: CachyOS, Linux 7.3.0, KDE Plasma 6.8 on Wayland; user-local install of `a0e899e` (window, tray) with the
background service process started earlier from `39c66cf` (left running; its code is functionally unchanged
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
(`6b2f4cd`); the header indicator still said "Error" (`ca7fa0d`); "usb" shown as "USB" (`a0e899e`).

**Login (owner logged out and in, 18:29):** the tray icon came back by itself (`openwitty-tray` started by the
session's XDG autostart, parent the systemd user manager); `openwitty-volumed` was stopped with the old session
and started with the new one (new PID, `NRestarts=0`, read-only start); settings and autostart entry unchanged.

**Not performed live:** the tray status following a knob-display change made in the window (offline only); the
tray on a desktop without a Status Notifier host (offline only: exits quietly).

## Speaker level at the login screen (Phase 9C)

**Observation (Phase 9B):** before the logout the window read −9.34 dB (18:22); after the login the service read
−23.50 dB (18:29:25.79). Every start on 2026-10-25 began at −23.50 dB.

**Correction:** Phase 9B attributed this to the user's WirePlumber restoring a stale saved level at login. That
is not what happened: the user's PipeWire, WirePlumber and pipewire-pulse ran unchanged from 12:23 (same PIDs)
through the logout and login, so they restored nothing.

**Root-cause assessment (strong evidence, not proven by a trace):**

| # | Evidence (read-only) |
|---|---|
| 1 | 18:29:19: the Plasma Login screen (user `plasmalogin`, UID 958, session class `greeter`) got its own systemd user manager, which started its **own PipeWire and WirePlumber** (18:29:20) — the only audio stack started in the window between −9.34 and −23.50 dB |
| 2 | That WirePlumber's state directory (`/var/lib/plasmalogin/.local/state/wireplumber/`) is empty: no saved routes, so `apply-routes.lua` applies `device.routes.default-sink-volume` = **0.064** (WirePlumber 0.5.18 default) |
| 3 | PipeWire's ALSA layer rounds playback volumes **up** to the next hardware step (`acp/alsa-mixer.c`, `rounding = +1`, PipeWire 1.6.10): 0.064 = −23.88 dB → **−23.50 dB** — exactly the observed level (reproduced by `RouteVolumePoC.WirePlumberDefaultSinkVolumeLandsOnMinus23_5dB`) |
| 4 | With the kernel quirk, an ALSA volume write reaches the device (Feature Unit 2 SET honoured), so the login screen sets the **device** level |
| 5 | Afterwards the user's state file held 0.066836 (= −23.5 dB, rewritten 18:29) and the user's output route has `save = true`: the user's WirePlumber persisted the level the login screen had set |

Remaining uncertainty: no PipeWire/WirePlumber debug log of the login screen's route application exists; the
attribution rests on timing, elimination and the exact default-volume match. The 12:23 boot used autologin (no
login screen): its −23.50 dB is consistent with the user's WirePlumber restoring the level saved after an
earlier login-screen appearance, also not traced.

Why it matters for OpenWitty: without the kernel quirk the desktop has no hardware volume for the CONNECT 2 and
no WirePlumber can change its level; the quirk OpenWitty documents makes the device's level settable by every
audio session, including a login screen's. The change can be louder or quieter than the knob position.

**How WirePlumber persists route volumes (WirePlumber 0.5.18 / PipeWire 1.6.10 source):** `state-routes.lua`
stores a route's volume only when the device reports the route with `save = true`. PipeWire's ALSA device reports
the port's sticky save flag: set by a route request with `save = true` (pipewire-pulse `set_card_volume` — the KDE
slider and volume keys; WirePlumber's mixer API — `wpctl set-volume` on a node with a device route), cleared by
WirePlumber's own route application (`apply-routes.lua`, `save = false`, when the device appears or the profile
changes). A hardware-originated change — the kernel, or OpenWitty's ALSA mirror write — is re-announced with the
*current* flag, so it is persisted only after a desktop volume change in that WirePlumber lifetime. (The Phase 9B
statement "ALSA updates are never saved" was too strong.) `device.restore-routes` is global; even without it,
`apply-routes.lua` applies the default sink volume to a newly appearing route.

**Approaches evaluated:**

| | Approach | Fixes the login-screen reset? | Notes |
|---|---|---|---|
| A | Mirror the knob through the CONNECT 2's PipeWire route with `save = true` (identified by device name and route, never the default sink) | **No** — the login screen's WirePlumber still applies its own default | Makes the *user's* saved level follow the knob deterministically (closes the gap after a WirePlumber start). Needs a PipeWire client path (libpipewire or `wpctl`) and exact rounding: a naive linear value lands one step **louder** on 61 of 121 steps in the model; `planRouteMirror()` lands on the device's step for all 121 (round-up and nearest). Feedback, mute and fallback-to-ALSA rules as today. Deferred |
| B | Login screen only: a WirePlumber rule (`device.disabled = true` for `~alsa_card.usb-Lewitt_GmbH_CONNECT_2.*`) in the login-screen user's config | **Yes** (for Plasma Login; SDDM/GDM paths documented, untested) | Device-specific, leaves the user's session and restore untouched; no sound from the CONNECT 2 on the login screen; administrator step, never installed automatically (`examples/51-openwitty-connect2-login-screen.conf`, syntax and pattern checked offline). **Recommended** |
| C | Keep the ALSA path; document; optionally let the service remember the last level and report a change found at start | Explains, does not prevent | Never writes a remembered level back. Documentation done; the start-time notice is a small later addition |

**Safety of the recommendation:** B changes nothing in OpenWitty's write paths (no vendor SET in mirror mode,
fresh read before alignment, never louder, round down, never unmute, headphone untouched, ownership rules all
unchanged). A, if ever implemented, keeps them by construction (`planRouteMirror`) and must additionally fall back
to the ALSA path when PipeWire or the route is unavailable, and must ignore its own echo exactly as today.

**Offline proof (Phase 9C):** `tests/cpp/test_route_volume.cpp` — every hardware step reached exactly with both
ACP rounding modes, off-grid device levels rounded down, never an unmute, no request when the desktop already
agrees, the WirePlumber default reproduced; `wireplumber_login_screen_rule_parses` — rule parses with
`spa-json-dump`, matches the CONNECT 2 card name pattern and not another vendor's. Not covered offline (would need
a live PipeWire with the device): WirePlumber actually persisting a route request, the rule's effect on a real
login screen, node disappearance and a different default sink under approach A.

**Optional live check of the login-screen rule (procedure; needs the owner's explicit approval; not run).**
Two logout/login cycles at most: one with the rule, and one without it only if the first result is ambiguous.

*Acoustic safety first.* The test deliberately lets the login screen change the speaker level, possibly
**upwards**. No software state (mute, volume, OpenWitty settings) is trusted to carry across the login boundary:
before each logout the owner physically secures the listening equipment — monitors/amplifier off or turned fully
down, headphones off the head. Nothing is played during the test.

*Preflight (read-only, before anything is changed):*
1. `systemctl show display-manager -p Id` → `plasmalogin.service`; `getent passwd plasmalogin` → home
   `/var/lib/plasmalogin`; `journalctl -b _UID=$(id -u plasmalogin) | grep wireplumber` shows its own WirePlumber.
2. `sudo ls -la /var/lib/plasmalogin /var/lib/plasmalogin/.config 2>&1` — note owners and whether `.config` exists.
3. The rule file exists at its source (see `docs/INSTALLATION.md`, step 1) and `spa-json-dump <file>` parses it.
4. OpenWitty service active, settings unchanged (`desktop_volume_sync = mirror`, both writing keys `false`);
   note the current device level from the service journal; vendor fallback and speaker/headphone control off.
5. Save all work (the terminal and this session end at the logout).

*Run (with the rule):*
1. Owner sets the speaker volume knob to a level clearly **quieter** than −23.5 dB, e.g. about −35 dB (the service
   journal shows it), then secures the listening equipment.
2. Administrator installs the rule as in `docs/INSTALLATION.md`, step 3; `sudo ls -l` confirms owner and mode.
3. Owner logs out, stays on the login screen about 15 s, logs in. No volume keys, no knob.
4. Read-only afterwards: service start line (`journalctl --user -u openwitty-volumed -b | grep start:`), the login
   screen's WirePlumber in `journalctl -b _UID=<uid>`, the CONNECT 2 still present in the user's session
   (`wpctl status`), the user's `default-routes` entry.

*Expected:* the service starts at the knob level (≈ −35 dB), not −23.5 dB; the CONNECT 2 is present and working in
the user's session; the login screen's WirePlumber ran but did not create the CONNECT 2.
*If it still reads −23.5 dB:* the rule did not apply or the attribution is wrong. Only then, as a control, remove
the rule and repeat once without it; −23.5 dB there and the knob level with the rule would establish the cause.

*Stop conditions:* any level **higher** than the knob setting after the login; the CONNECT 2 missing or silent in
the user's session; login problems or a login screen that does not start; unexpected USB/kernel messages for the
device; repeated service restarts. Then roll back and report; no further cycles.

**Installed state (Phase 9D, 2026-10-25, owner-approved):** the rule is installed for the login screen only —
`/var/lib/plasmalogin/.config/wireplumber/wireplumber.conf.d/51-openwitty-connect2-login-screen.conf`
(`plasmalogin:plasmalogin`, 0644; the two `wireplumber` directories created as `plasmalogin`; identical to the
repository copy; `spa-json-dump` parses it). The user's own WirePlumber configuration was not touched (no
`~/.config/wireplumber`, no `/etc/wireplumber`). Takes effect at the next login screen; **not yet live-validated**.

*Rollback:* `sudo rm /var/lib/plasmalogin/.config/wireplumber/wireplumber.conf.d/51-openwitty-connect2-login-screen.conf`
(optionally also `sudo -u plasmalogin rmdir /var/lib/plasmalogin/.config/wireplumber/wireplumber.conf.d
/var/lib/plasmalogin/.config/wireplumber`)
(effective at the next login screen). If the CONNECT 2 were missing in the user's own session, check that the rule
was not placed in `~/.config/wireplumber` or `/etc/wireplumber` and remove it there. The speaker level is restored by
the owner with the knob, never by OpenWitty.

**Alpha impact:** not a code blocker. It must be documented next to the kernel quirk (done in
`docs/INSTALLATION.md` and `docs/VOLUME_INTEGRATION.md`); validating option B live is recommended before a public
alpha.

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

## Before going public (owner decisions and outstanding work)

### Git author identity

Phase 9D: the owner approved sanitizing the history. Before the rewrite, 26 commits carried a personal e-mail
address and 13 a first name in the author/committer fields, and a few old file revisions contained the handle or
the name (an early `LICENSE`, the first Debian `changelog`/`copyright`, `docs/AUDIT_PHASE5E.md`, two ACL examples in
the hardware docs). Procedure and results: "History rewrite (Phase 9D)" below. New commits use the GitHub no-reply
address (`git config user.email`).

### History rewrite (Phase 9D, 2026-10-25)

* **Backups** (private, `~/Backups/`, mode 0700): `openwitty-pre-history-rewrite-20261025-2000.{git,bundle}` and
  `…-worktree.tar.gz` (local `.git`), and `openwitty-pre-history-rewrite-20261025-2031.{git,bundle}` (the exact
  remote state that was rewritten, all 8 refs incl. `refs/pull/*`; bundle verified).
* **Rewrite** (`git filter-repo` in a disposable mirror, `--prune-empty never --prune-degenerate never`, so no
  commit is dropped — the old "Update LICENSE" commit stays as an empty commit): author/committer identities mapped
  to `st0nebyte <133608037+st0nebyte@users.noreply.github.com>`; in file revisions the handle and first name replaced
  by `st0nebyte`, the two ACL/ownership examples by `<user>`; hash references in commit messages updated by filter-repo.
* **Verified before pushing:** commit count identical on all 8 refs; only the no-reply identity (and GitHub's own
  committer for 7 web merges) remains; 0 personal strings in identities, messages and every blob of every ref; branch
  tips differ from the originals only in the replaced lines (1–2 doc lines).
* **Pushed** with exact leases: `phase7-hardware-integration` 9615d05→`d39c727`, `hardware-validation-2026-10-14`
  868331c→`171990e`, `phase6e-work-preparation` 7d8a4c0→`94b89a3`, `phase7a-interface-discovery` be0d3f3→`820412d`.
  Local branches reset to them; hash references in the docs translated with filter-repo's commit map (34 in 15 files).
* **`main`** (owner-approved, second step): remote re-checked immediately before, pushed with an exact lease
  `ef61433` → `9da2276`. Afterwards: 29 commits as before, all subjects identical and in order, tree differs in one
  doc line. The old development branch had carried byte-identical **copies** of `main`'s 15 own commits that
  differed only in the identity fields; after the mapping they converged, so `main` is now an ancestor of the
  development branch (no merge was made; 130 development commits are not in `main`). Across all branches: 173
  distinct old commits → 158 rewritten (15 duplicates absorbed), every old commit maps to an existing new one.
* **Final check from a fresh mirror of GitHub** (each field on its own line — an earlier check joined the fields,
  which can hide a name at a word boundary): all branches 0 personal strings in identities, messages and file
  revisions; identity names only `st0nebyte` and `GitHub`.
* **Not rewritten — unresolved privacy blocker:** GitHub's `refs/pull/1..3/head` (`66bc210`, `d1d5e4e`, `6cef96f`;
  read-only, GitHub-managed) still point at the old commits: the **first name** is in the author/committer name of
  18 commits (the e-mail address is not; their file revisions are clean). GitHub may also keep old commits reachable
  by hash until its garbage collection. Options: see "Pull-request refs" below.

### Pull-request refs (open)

| Option | Effect | Needs |
|---|---|---|
| GitHub Support removes the PR refs / closed PRs and purges cached views and unreachable objects | the existing repository is clean | a support request by the owner (GitHub's "remove sensitive data" procedure) |
| Publish a **new, separate repository** pushed only from the sanitized branches (no `refs/pull/*`); keep this repository private or archive it | clean public history at once; PR discussion of the 3 old asset PRs stays private | creating a repository; deciding what happens to the old one |
| Keep this repository private and never publish it | no exposure | nothing |

Facts (GitHub's "Removing sensitive data from a repository", checked 2026-10-25): `refs/pull/*` are read-only and
cannot be force-pushed; only GitHub Support can "dereference or delete any affected PRs", run a garbage collection
and remove cached views — and only after all other references are clean and no fork holds the data; Support "won't
remove non-sensitive data" (whether a personal address qualifies is Support's decision). The repository has **no
forks**; the three PRs are the merged branding/asset PRs #1–#3. Nothing has been changed or requested (no
visibility change, no support contact).

### Arch

Done in Phase 9D: `namcap` (installed for this: `namcap` + 3 Python libraries, no upgrades) — see the checklist.
Optional: a clean-chroot build (`extra-x86_64-build`, devtools).

### Debian / Ubuntu

Done in Phase 9D for **Debian 13.7** in a disposable `debootstrap` root under `/var/tmp/openwitty-debian-test`
(tools installed on the host: `debootstrap`, `debian-archive-keyring`, `distro-info`, `distro-info-data`; no
upgrades; `debootstrap` needed a private copy of its data directory with an `arch` file because CachyOS's
`pacman-conf` reports several architectures). Found and fixed: the SVG image plugin dependency
(`qt6-svg-plugins | libqt6svg6 (<< 6.5~)`) and the upstream changelog name (`changelog.gz`). Outstanding: Ubuntu
24.04 and Debian 12 (Qt 6.4.2) build and install test.

### Clean installation and removal test plan (both package formats)

In a fresh VM or container per format, nothing else installed:
1. Record `find / -xdev -newer <stamp>` baseline; install the package; list `pacman -Ql` / `dpkg -L`.
2. Check: binaries run (`--version`, `--mock --screenshot`), unit present and **disabled**
   (`systemctl --user is-enabled openwitty-volumed` → disabled), no autostart entry, udev rule present,
   no `modprobe.d` or WirePlumber file outside `share/doc/openwitty/examples`, man pages, licences.
3. As a user: enable mirroring and the tray in Settings (offscreen or a desktop session), confirm the files OpenWitty
   writes are only `~/.config/openwitty/openwitty.conf` and `~/.config/autostart/io.github.st0nebyte.openwitty-tray.desktop`.
4. Reset desktop integration; remove the package; check that no package file remains (`pacman -Qkk`/`dpkg -L`
   against the filesystem) and that only the user's own settings directory is left, as documented.

### Version identifiers: `0.1.0-alpha.1` (applied in Phase 9D, no tag yet)

Applied as in the table below; `version_consistency` (CTest) checks that all sites agree. Still to do at release:
the annotated tag `v0.1.0-alpha.1` and the release date in AppStream/CHANGELOG.


| Place | Now | Proposed |
|---|---|---|
| `CMakeLists.txt` `project(... VERSION 0.1.0)` | 0.1.0 | unchanged (CMake allows no suffix) + new `set(LEWITT_VERSION_SUFFIX "alpha.1")` |
| `src/version.hpp.in` `LEWITT_VERSION_STRING` | `@PROJECT_VERSION@` | `@PROJECT_VERSION@-alpha.1` (from the suffix) → `--version` prints `0.1.0-alpha.1` |
| `src/gui/qml/SettingsView.qml` "OpenWitty 0.1.0" | hard-coded | `Qt.application.version` (one source of truth) |
| `packaging/arch/PKGBUILD` `pkgver` | 0.1.0 | `0.1.0alpha1` (no hyphen allowed; sorts before 0.1.0) |
| `packaging/arch/make-source-tarball.sh` | reads `project()` | read the full version so the tarball is `openwitty-0.1.0alpha1.tar.gz` |
| `packaging/debian/changelog`, CPack `CPACK_PACKAGE_VERSION` | 0.1.0-1 | `0.1.0~alpha1-1` (tilde sorts before 0.1.0) |
| `packaging/linux/…metainfo.xml` | `<release version="0.1.0" date="2026-10-14"/>` (never released) | `<release version="0.1.0~alpha1" date="<release date>" type="development">` with a short description |
| man pages `.TH … "openwitty 0.1.0"` | 0.1.0 | `openwitty 0.1.0-alpha.1` |
| Git tag | none | `v0.1.0-alpha.1`, annotated, on the reviewed release commit |
| `CHANGELOG.md` | "Unreleased" | `0.1.0-alpha.1 — <date>` |

### Other

* Replace the packaging contact (`noreply@users.noreply.github.com` placeholder) if wanted.
* `docs/` keeps many dated engineering notes (`PHASE*`, handoff/test-plan documents marked historical); not
  installed by packages; decide whether they stay public.
* `docs/THIRD_PARTY.md` notes a possible visual similarity of the GUI with the vendor's application.

## Release blockers (current assessment)

None in the code or packaging for a **private alpha review**. For a **public** release: the pull-request refs (privacy, see above),
`namcap`, the Debian validation, the clean install/removal test and the version change above; recommended in
addition: the live check of the login-screen rule.

## Validation run (Phase 9C, latest)

CachyOS host; nothing installed, the running user-local installation, service, tray and settings untouched.
Release build: CTest 292/292, GUI 57/57, tray 17/17, race 7/7 (5 repeats in 9B; race and tray also under
ASan/UBSan); `build-asan`: 272/272; USB audit PASS; `packaging_staging` and `install_staging_user_unit` PASS;
real rootless `makepkg`: `check()` 291/291 — its first run exposed the single-instance socket-path defect fixed in
`67f866f`; privacy grep and `git diff --check` clean.

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
