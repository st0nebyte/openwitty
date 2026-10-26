# Desktop volume integration for the LEWITT CONNECT 2

Current reference for users and maintainers (supersedes the design parts of the Phase 8C/8D/8E notes, which remain as
history). Verified on one machine (CachyOS, Linux 7.3.0-5, KDE Plasma, PipeWire 1.6 / WirePlumber 0.5); statements marked
*observed once* still need reproduction before they are presented as general.

## What the device does (evidence)
| Fact | Evidence |
|---|---|
| The speaker volume/mute of the vendor settings message (interface 3) is honoured in every device state seen | Phase 8C/8D, verified by read-back |
| Feature Unit 2 **GET_CUR is broken**: it returns the current level and never changes, so the kernel cannot read the level back and never learns about hardware-knob changes | every boot since 8A; kernel `broken mixer GET_CUR` / `sticky mixer values` |
| Feature Unit 2 **SET_CUR is honoured after an ordinary boot** – desktop volume reaches the speaker through the kernel | observed on two ordinary boots (Phase 8E probe; Phase 8H volume keys after a reboot) |
| The device reports a level the kernel set **with a delay**: the vendor GET shows it about 0.4–0.7 s after the ALSA control changed | Phase 8H, fast volume-key presses |
| The **Mute button** is reported to the kernel (audio-control interrupt endpoint EP 3 IN): the ALSA `PCM Playback Switch` follows it in both directions **without OpenWitty**, and a desktop mute reaches the device through the kernel | Phase 8H: switch events recorded with no OpenWitty process running; KDE mute/unmute seen on the device by vendor GET |
| For a few seconds after power-up the device answers no control requests: the kernel logs `usb_set_interface failed (-110)` on every boot, with or without OpenWitty | kernel log of five consecutive boots (Phase 8H) |
| SET_CUR is **ignored** directly after a software re-enumeration of a running device (`authorized` toggle, `USBDEVFS_RESET`) | Phase 8C, measured twice |
| On Linux 7.3 the always-broken microphone control (unit 5, channels 3/4) makes every ALSA mixer load fail with ENXIO → WirePlumber "Unable to load mixer" → no hardware-volume route at all | Phase 8C; fixed by the quirk below, confirmed across a real boot (Phase 8E) |

## Architecture
* **KDE/PipeWire are authoritative for explicit desktop volume changes.** When the kernel's hardware-volume route exists,
  the slider reaches the device through ALSA → Feature Unit 2. OpenWitty does not sit in that path.
* **`openwitty-volumed` (user service, optional) keeps the desktop in step with the device.** Default *mirror* mode opens the
  CONNECT 2 read-only (vendor GET), and when the hardware knob or mute button changes the device it updates the ALSA control
  so KDE shows the real level. An ALSA control write is forwarded to the device by the kernel, so every such update re-reads
  the device first, writes only the level the device already has, rounds down to the control grid and never unmutes. A
  desktop change the device does not follow is reported, never corrected.
* **Vendor fallback (explicit opt-in).** `--vendor-fallback` additionally applies a desktop change that the device did not
  follow within the follow grace (1.5 s) through the vendor settings message: speaker bytes only, fresh snapshot, one SET, read-back verification, no retries,
  increases limited to 10 dB per step, never at startup. Intended for the verified "SET ignored" state.
* **GUI.** Shows the real speaker/headphone state (vendor GET). Speaker and headphone controls write the device only with the
  confirmed *speaker control* or *headphone control* setting respectively (Settings → Device control; scoped policy). While the GUI owns the device it hosts the same mirror when
  *desktop sync* is on (native mode only); the daemon yields the device to the GUI and takes it back afterwards.
* **One writer.** All OpenWitty processes share one per-user control lease; the GUI asks the daemon to hand over with a yield
  request. Policy and write path are the same code everywhere (`core/VolumeSync`, `desktop/DesktopVolumeLink`).
* **Startup is read-only towards the device** in every mode; a 5 s hold keeps WirePlumber's login route restore from being
  treated as a user change.

## Validation status (real device)
| Behaviour | Status |
|---|---|
| Ordinary boot: quirk active, mixer loads, hardware-volume route present, FU2 SET reaches the device | observed once (Phase 8E, 2026-10-25) |
| Mirror mode, foreground (2026-10-25, same boot): read-only start; KDE/PipeWire volume decrease (sink 0.27 → 0.25) reached the device through the kernel (−34 → −36 dB) with no "did not follow" notice and no vendor SET (read-only transport); the owner turned the speaker volume knob up (−36 → −27 dB), mirrored to the desktop step by step; the speaker output was already selected on the CONNECT 2, and the owner pressed the Mute button (short presses) several times, deliberately more often than planned; the speaker mute was mirrored to the desktop, and after a physical unmute the desktop was reported as left muted with the notice (not reconstructable, see below; Phase 8H later showed the kernel itself follows the Mute button); no log activity for 10 s afterwards; final state after stopping: vendor GET −27.00 dB unmuted (headphone −32.40 dB muted, unchanged), ALSA −27.00 dB unmuted, sink 0.35 unmuted – consistent | passed |
| The exact mute/unmute sequence of that test | not reconstructable (extra button presses, log without timestamps); the final unmuted desktop state is not attributed to the kernel, WirePlumber or OpenWitty. Logging fixed since (millisecond timestamps outside the journal, notice clearing logged, hardware notes carry the level) |
| Settings page + installed background service (Phase 8G, 2026-10-25, same boot, user-local install): all 8 steps below, see "Settings validation" | passed |
| Mute button with the background service owning the device; start at boot/login with the service enabled (Phase 8H) | passed, see "Background service and login" |
| Suspend to RAM (S3) and resume with the service running (Phase 8I) | passed, see "Suspend/resume" |
| Vendor fallback in the "SET ignored" state; a logout/login without reboot | not yet tested on the device |

## Kernel compatibility quirk
`options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error` (example: `packaging/modprobe/openwitty-connect2.conf`).
* **What it does:** for this one USB id, failed mixer control reads/writes return 0 instead of an error, so the ALSA mixer
  loads despite the broken microphone control and PipeWire gets its hardware-volume route.
* **Limitations:** it hides genuine control errors of this device from ALSA clients (they read 0); it affects nothing else;
  kernels without the string form of `quirk_flags` ignore it; a kernel that handles the broken control itself makes it
  unnecessary. It is a device-specific compatibility workaround, not a fix of the device.
* **Policy:** OpenWitty never installs it silently. `openwitty-volumed --check` reports whether the
  mixer loads and whether the quirk is configured/active, and explains the finding; installing it is a documented manual step
  (or a packager decision with the same explanation). It takes effect at the next replug or boot; removing the file undoes it.
* **Upstream:** the right long-term fix is in the kernel (keep a per-channel control readable when only some channels stall,
  or a device quirk for 29c2:0004); worth reporting to linux-sound with the Phase 8C analysis.

### ⚠ Login screen side effect (Phase 9C)
* **Observed (confirmed):** after a logout and login the speaker read −23.50 dB; before the logout it was −9.34 dB.
  The user's own PipeWire/WirePlumber ran unchanged throughout. The change can be louder or quieter.
* **Explanation (strongly supported, not traced):** the login screen runs its own PipeWire/WirePlumber; with no saved
  state it applies WirePlumber's default sink volume 0.064 (−23.88 dB), which PipeWire rounds up to the hardware step
  −23.50 dB; the quirk lets that reach the device. No log of that write exists.
* **Optional remedy (not validated):** a login-screen-only WirePlumber rule, `51-openwitty-connect2-login-screen.conf`;
  where to find it and how to install it: `docs/INSTALLATION.md`. Evidence and the live-check plan:
  `docs/RELEASE_READINESS.md`.

## Command line (diagnostics; ordinary use is the Settings page)
Modes are mutually exclusive: no flag = follow the settings file, or exactly one of `--mirror`, `--vendor-fallback`, `--dry-run`, `--check`; conflicting or
repeated mode flags, `--max-increase-db` outside the fallback, and options with `--check` are rejected before anything runs.
`--check` makes no OpenWitty vendor USB access and no intentional hardware SET; it does open the CONNECT 2's ALSA mixer, which
can make the kernel issue USB control *reads* to the device.
```
openwitty-volumed --check                                   # setup diagnosis
systemctl --user enable --now openwitty-volumed.service     # = "Keep it running when OpenWitty is closed"
journalctl --user -u openwitty-volumed -f                   # what it does
systemctl --user disable --now openwitty-volumed.service    # undo
```

## Settings (GUI) and the shared configuration file
**OpenWitty → Settings → Desktop integration** (no environment variables, drop-ins or scripts needed):

| Setting | Default | What it does | Writes the device? |
|---|---|---|---|
| Show the volume knob in the desktop volume | off | mirror mode: the desktop (KDE/PipeWire) volume of the CONNECT 2 follows the speaker volume knob, which the kernel cannot see (broken volume read-back); while OpenWitty is open the window does it, otherwise the background service. The Mute button and the desktop slider/mute need no OpenWitty: the kernel handles them (validated on the development system, Phase 8H) | no (the desktop control is updated to the level the device already has; the kernel forwards that update) |
| Keep it running when OpenWitty is closed | off | `systemctl --user enable --now` / `disable --now` of `openwitty-volumed.service`, only when you flip it; shows "not installed / off / on / running" | no |
| Vendor fallback | off; needs desktop sync | if the device does not follow a desktop volume change, the background service applies it through the vendor protocol (speaker volume/mute only, ≤ 10 dB louder per step, read-back verified, never at start-up). Enabling shows an explanation that must be confirmed | **yes** |
| Show OpenWitty in the system tray | off | starts `openwitty-tray` now and at login (XDG autostart entry); off removes the entry and quits the icon. The icon shows the knob-display and service state and opens OpenWitty; it never uses the device and never changes the service (Phase 9B) | no |
| Reset desktop integration | – | after confirmation: all of the above off, the background service disabled and the tray login entry removed; the kernel quirk is not touched | no |

The page also shows the setup status (sound card present, hardware volume available, kernel quirk configured/active,
plain-language findings) and explains the kernel quirk with the exact line to install and how to remove it; OpenWitty
never installs it. The setup check runs when the page is opened (it opens the CONNECT 2's ALSA mixer, which can cause kernel
USB control reads; no OpenWitty vendor access).

**File:** `$XDG_CONFIG_HOME/openwitty/openwitty.conf` (absolute `XDG_CONFIG_HOME` only, else `~/.config/openwitty/openwitty.conf`),
written atomically by the GUI (0600, directory 0700), read by the GUI and by `openwitty-volumed`:
```
[openwitty]
desktop_volume_sync = off      # off | mirror
vendor_fallback = false        # true writes the device
speaker_control = false        # true: OpenWitty's speaker fader/mute write the device
headphone_control = false      # true: OpenWitty's headphone fader/mute write the device
led_brightness_control = false # true: OpenWitty's LED brightness control writes the device
```
The three device-control keys are set on **Settings → Device control** (each confirmed separately, applied at once by
reconnecting the session with the matching scoped policy: speaker bytes 20–22, headphone bytes 27–29, LED byte 37).
`output_control` from an older version grants **nothing**: `output_control = true` is kept only as a reminder (with a
"grants NOTHING" comment) until speaker or headphone control is set or the notice is dismissed.
Missing file = defaults. Invalid values fall back to the safe default and are shown as warnings in the page and in the
daemon log; unknown keys are ignored with a warning. Deleting the file resets everything.

**GUI ↔ daemon:** the daemon re-reads the file when it changes (checked at least every 250 ms) and switches mode at once:
idle when sync is off, mirror (device read-only), or mirror + vendor fallback; every switch starts read-only again. The
device itself is owned by one process at a time (control lease): when the window opens, the daemon hands the device over and
the window mirrors (native mode only – the vendor fallback is paused while the window is open); when the window closes, the
daemon takes it back. Explicit command-line modes (`--mirror`, `--vendor-fallback` with its two `LEWITT_ALLOW_*=1`
variables, `--dry-run`, `--check`) override the file for diagnostics.

## Packaging
`cmake --install` installs `openwitty`, `openwitty-cli`, `openwitty-volumed`, the udev rule (optional), the docs and the
systemd user unit, plus the quirk file only as a documentation example. The unit is **never enabled** by the build or
the install.
* **Unit location:** `<prefix>/share/systemd/user` by default (`LEWITT_SYSTEMD_USER_UNIT_DIR`, relative to the prefix or
  absolute). That is on systemd's user unit search path for `~/.local` (as `$XDG_DATA_HOME/systemd/user`), `/usr/local`
  and `/usr`; distribution packages may set it to `pkg-config --variable=systemduserunitdir systemd` instead. Build trees
  that cached the old `lib/systemd/user` default are migrated automatically.
* **ExecStart / Documentation** are generated at install time from the *effective* prefix
  (`packaging/systemd/InstallUserUnit.cmake`), so `cmake --install --prefix …` and `DESTDIR=… cmake --install` both
  produce the path the executable will really have; the unit is recorded in `install_manifest.txt`.
* CTest `install_staging_user_unit` checks both cases in a disposable directory (user-local prefix override; `DESTDIR`
  package staging with `--prefix /usr`), including `systemd-analyze --user verify`.
* Distribution packages should keep the unit disabled and the quirk opt-in, and point to `openwitty-volumed --check`.

## Local installation (user-local, reversible)
Into `~/.local` (no root; the udev rule is already installed system-wide on the development machine):
```
cmake -S . -B build-local -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DLEWITT_INSTALL_UDEV_RULES=OFF
cmake --build build-local -j"$(nproc)" && ctest --test-dir build-local
cmake --install build-local                       # binaries, docs, ~/.local/share/systemd/user/openwitty-volumed.service
systemctl --user daemon-reload
systemctl --user status openwitty-volumed.service # expect: loaded, disabled, inactive
```
Rollback:
```
systemctl --user disable --now openwitty-volumed.service   # only if it was enabled
xargs -d '\n' rm -f < build-local/install_manifest.txt
systemctl --user daemon-reload
rm -f ~/.config/openwitty/openwitty.conf                   # optional: forget the settings
```

## Settings validation (real device)
Scope: the Settings page, mirror on/off from the page, the background-service switch, the GUI ↔ daemon hand-over.
**Vendor fallback and speaker/headphone control stay off throughout.** Nothing changes the speaker level except the owner's
knob/Mute button and one small desktop decrease. No reboot, replug, USB reset or audio-service restart.
1. Install as above; `systemctl --user status` shows the unit loaded and disabled. Settings file absent (defaults).
2. Start `openwitty`, open Settings → Desktop integration: expect "Desktop hardware volume: available", quirk active, all
   switches off, background service "off".
3. Turn on "Show the volume knob in the desktop volume" (called "Show knob and Mute button in desktop volume" until Phase 8H). Check the settings file (`desktop_volume_sync = mirror`, both
   device-writing keys `false`). Owner turns the speaker volume knob a little: the KDE volume follows (GUI-hosted mirror).
4. Turn "Keep it running when OpenWitty is closed" on: `systemctl --user status` shows active; `journalctl --user -u
   openwitty-volumed` shows "waiting: device handed over" / "device in use by another OpenWitty client" while the window is open.
5. Close the window: the service log shows "device connected" and a read-only start. Owner turns the knob: KDE follows
   (service-hosted mirror). One small desktop decrease (`wpctl set-volume <node-id> <current−0.02>`, after checking with
   `wpctl inspect` that the node is the CONNECT 2 – never the default sink blindly): the device
   follows through the kernel, no "did not follow" notice.
6. Reopen the window: the service releases the device ("another OpenWitty client asked for the device"), the window connects
   and mirrors; close it again: the service takes the device back.
7. In the page, turn desktop sync off with the service still enabled: the service log says "idle"; the page shows the
   "only idles" hint and the service switch can be turned off. Turn it off: unit disabled and inactive.
8. "Reset desktop integration": settings file back to defaults, service disabled; independent vendor GET and ALSA/PipeWire
   agree. Uninstall with the rollback above if wanted.
Evidence: service journal (journald timestamps), the settings file, `systemctl --user status`, vendor GET
(`openwitty-cli read settings --method interface --interface 3`, only while no other OpenWitty process holds the device),
`amixer -c <card> sget PCM`, `wpctl get-volume`.

**Result (2026-10-25, Phase 8G, one machine, one boot; vendor fallback and Speaker/headphone control off throughout):**
| Step | Observed |
|---|---|
| 1 | Install into `~/.local` (35 manifest entries incl. compatibility links); unit loaded, disabled, inactive; `systemd-analyze --user verify` clean; no settings file |
| 2 | Page: hardware volume available, quirk active, all switches off, "Keep it running" disabled while sync is off, service "off" |
| 3 | File written with `desktop_volume_sync = mirror`, both writing keys `false`; knob turns mirrored by the window (−50.9 → −20.0 dB); ALSA −20.00 dB (rounded down), sink 0.46; one "alignment skipped: the device changed meanwhile" during a fast turn (expected); no "did not follow" |
| 4 | Unit enabled and active; journal: mirror mode, "waiting: device in use by another OpenWitty client" |
| 5 | Window closed: "device connected", read-only start; knob mirrored over the full range (−60 → 0 dB); desktop decrease 1.00 → 0.98 reached the device through the kernel (device −0.50 dB, ALSA −0.50 dB); no vendor SET (read-only transport), no "did not follow" |
| 6 | Reopen: "another OpenWitty client asked for the device: releasing it", window mirrored; close: same service process (no restart) took the device back read-only and mirrored |
| 7 | Sync off: journal "settings changed: idle", page showed the "only idles" hint; service switch off: systemd stopped it cleanly, disabled/inactive |
| 8 | Reset: file back to defaults, service disabled; window closed; vendor GET speaker −39.50 dB unmuted (headphone −32.40 dB muted, unchanged since the baseline), ALSA −39.50 dB on, sink 0.22 – consistent |

Not covered by this run: the vendor fallback, the speaker/headphone control, the Mute button through the service, a second
boot, suspend/resume, login with the service already enabled (the last three: Phase 8H below).

**Why the desktop shows 10 % at the knob's minimum.** The CONNECT 2's speaker range ends at −60 dB, which is quiet but not
silent. PipeWire/KDE display volume on a cubic scale, and −60 dB is exactly 10 % there (∛10⁻³). The mirror therefore shows
10 %, the truth about the device. Dragging the desktop slider below 10 % sets the device to −60 dB and PipeWire attenuates the
rest in software, which the knob cannot do. Silence from the device itself is the Mute button (mirrored as desktop mute).

## Background service and login (Phase 8H, 2026-10-25)
Settings via the page: `desktop_volume_sync = mirror`, `vendor_fallback = false`, `output_control = false`; service enabled.
Vendor fallback and speaker/headphone control off throughout.

**Mute button, service owns the device (window closed).** Two short presses (speaker selected): device, ALSA switch and KDE
went muted → unmuted, one journal line each, level unchanged (−19.50 dB), no loop. With the service *stopped*, an ALSA event
monitor recorded the same two `PCM Playback Switch` changes: the kernel follows the Mute button by itself (the device
reports it on its interrupt endpoint). The service's "mirrored to the desktop" then finds the desktop already in step;
OpenWitty never unmuted anything. A KDE mute/unmute reached the device through the kernel (service journal: device
speaker muted / unmuted, read by vendor GET). Only the volume knob depends on OpenWitty, because the volume read-back is broken.

**Start at boot/login.** The owner rebooted (instead of a logout). systemd started the service with the user session, about
10 s after boot, no manual start (`NRestarts=0`). Quirk active, no "Unable to load mixer", output route with
`route.hw-volume`/`route.hw-mute`. The first seconds coincide with the device's power-up stall: two vendor-read timeouts,
one bounded reconnect after 6 s, read-only start again; the kernel's `usb_set_interface failed (-110)` occurs on every boot
without OpenWitty as well. The knob turned after login was mirrored by that same process (device −17.14 dB, ALSA −17.50 dB,
KDE 0.51). The PipeWire node ids changed after the reboot (sink 65 → 64): always select the sink by name.

**Defect found and fixed: false "did not follow" during fast volume-key presses.** In native mode the verdict was taken on
the single device read that follows a desktop event; because the device reports a kernel-set level with a delay, a burst of
key presses produced a "did not follow" notice although the device followed (it cleared itself 0.7 s later). The verdict
now waits for a grace period per target (`LinkOptions::followGrace`, 1.5 s; a newer desktop change starts a new one); a
change still not followed afterwards is reported as before. The vendor fallback path is unchanged. Re-tested on the device:
three steps up and three down, device followed each with ≈0.7 s delay, no notice, final state consistent (−17.50 dB).

### Follow-up review (offline, after Phase 8H)
Both findings below were **reproduced by deterministic tests** against the code of `33a41b1` (they failed before the fix);
neither was observed on the device. Fixed without changing the architecture, the fresh read before every decision, the
no-retry rule or the never-unmute rule.

1. **Stale intermediate hardware report overwrote the latest desktop request.** Desktop asks for A, then B; the device
   reports A late. The link treated A as a knob change and mirrored it to the ALSA control, which the kernel forwards to
   the device: the user's B would be undone (louder than requested when pressing *down*). Now a hardware report that equals
   a desktop level requested within the follow window (1.5 s) while the desktop already asks for something else is an
   intermediate step: adopted as the device state, not mirrored, any newer queued request kept. A knob change to any other
   level, or to a level requested longer ago, is mirrored at once as before
   (`StaleIntermediateReportDoesNotOverwriteTheLatestDesktopRequest`, `KnobTurnedDuringAPendingDesktopRequestIsStillMirrored`,
   `AnOldRequestNoLongerMasksAKnobChange`).
2. **Vendor fallback wrote before a healthy kernel change became visible.** The fallback acted on the first read after the
   desktop event, i.e. within the device's 0.4–0.7 s reporting delay, so a working kernel path got a needless vendor SET.
   The fallback now waits for the same per-target grace period as the native verdict and writes only if the device still
   has not followed, once, for the latest target, read back, never retried; the 10 dB increase limit still applies
   (`HealthyButLaggingKernelPathGetsNoVendorWrite`, `BurstWithAnIgnoringDeviceWritesOnlyTheLatestTargetOnce`,
   `StaleIntermediateReportOfALaggingKernelPathCausesNoWrite`, `IncreaseLimitStillAppliesAfterTheGracePeriod`). As a
   consequence a desktop change in the "SET ignored" state reaches the device 1.5–2.5 s late (daemon poll: 1 s).

**Residual risks, not confirmed by tests or on the device:**
* The mirror only knows desktop levels it read. If ALSA events of a fast burst are coalesced so that an intermediate level
  is never read, a late report of that level still looks like a knob change. Not observed (the Phase 8H burst was read step
  by step); not reproducible without a model of WirePlumber's event timing.
* A genuine knob turn that lands exactly on a level the desktop requested within the last 1.5 s, while a different desktop
  request is pending, is not mirrored; native mode then reports "did not follow", the fallback would apply the desktop's
  latest level. Requires knob and volume keys within 1.5 s of each other.
* The fallback grace is based on reporting delays measured on one machine (0.4–0.7 s); the fallback itself remains
  unvalidated on hardware.

**Settings wording.** The switch is now "Show the volume knob in the desktop volume" and explains that Linux cannot see the
knob, while the desktop slider and the Mute button work without OpenWitty.

## Suspend/resume (Phase 8I, 2026-10-25, installed `39c66cf`)
Service enabled in mirror mode (fallback and speaker/headphone control off). Before: service PID unchanged since start,
0 restarts; device −20.00 dB unmuted (service's vendor GET), ALSA −20.00 dB on, sink 0.46; quirk active; output route
with `route.hw-volume`/`route.hw-mute`; `mem_sleep` = deep, USB `power/control` = on, `persist` = 1.

The owner suspended through KDE. The kernel log shows one S3 cycle of ≈14 s and two short follow-up cycles (the last one
s2idle) requested by KDE's lock screen (`kscreenlocker_greet`) after waking to the lock screen without unlocking – KDE
power management, unrelated to OpenWitty. After resume:
* the same service process kept running (no restart, `NRestarts=0`), no read errors, timeouts, reconnects or ownership
  conflicts in its journal;
* the CONNECT 2 was not re-enumerated (no kernel message for the device at all); WirePlumber/PipeWire logged nothing;
  the sink, re-identified by name, kept its id; ALSA control and hardware-volume route unchanged; levels as before;
* speaker volume knob turned up to 0 dB: mirrored step by step (−18.8 → 0.00 dB), ALSA 0.00 dB, sink 1.00;
* one desktop decrease (1.00 → 0.98): the device followed through the kernel within milliseconds (service read −0.50 dB),
  no vendor SET, no "did not follow"; ALSA −0.50 dB.

Result: both directions survive suspend/resume on this system; Feature Unit 2 SET stays honoured (the "SET ignored" state
was not reached). No recovery behaviour was exercised or needed. Not covered: a resume after which the device is
re-enumerated (e.g. USB power lost during sleep), longer sleep, s2idle as the configured default.

## Prepared hardware checkpoints (optional, each needs the owner's go-ahead)
1. **Reproduce the ordinary-boot result.** Done informally in Phase 8H (volume keys reached the device after a reboot). For a
   formal record: owner reboots normally, then
   `tools/diagnostics/fu2_recovery_probe.py run --cli build-release/openwitty-cli --confirm-audio-safe` – stops after P0 if FU2
   is honoured (≈1 s, speaker −1 dB).
2. **Suspend/resume.** Done in Phase 8I with the service running (kernel path honoured after resume). Formal probe: owner
   suspends and resumes the machine once; then the same tool run. `FU2_HEALTHY_AT_BOOT` (P0 followed)
   means suspend/resume keeps the kernel path working; anything else is the first evidence that ordinary users can reach the
   "SET ignored" state, which would justify offering the vendor fallback more prominently.
Neither checkpoint involves reboots, resets, reloads or replugs by OpenWitty.
