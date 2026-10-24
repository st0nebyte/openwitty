# Phase 8D – Native speaker/headphone volume integration

Status: **implemented and tested offline; read-only (dry-run) smoke test on the real device passed.**
No hardware SET, no service activation, no audio-stack restart, no reboot or reconnect was performed
in this phase. The live write validation is proposed at the end and needs a fresh safety confirmation.

Background: `PHASE8C_CONNECT2_VOLUME_FIX.md` (why the kernel/PipeWire path fails, the kernel quirk,
and the proof that the vendor settings write on interface 3 works in every Feature-Unit-2 state).

## Architecture

```
 KDE / PipeWire ──(route hw-volume)──> ALSA "PCM Playback Volume/Switch" (card of 29c2:0004)
                                              │  events / reads / re-align
                                              ▼
                     desktop/AlsaDesktopMixer (libasound, IDesktopMixer)
                                              │
                     desktop/DesktopVolumeLink ── core/VolumeSync (pure policy)
                                              │
                     core/DeviceSession + core/OutputControl (scoped policy, stageOutput,
                     commitAndVerify) ── transport/UsbTransport (interface 3, ControlLease)
                                              │
                                        CONNECT 2 speaker
```

| Component | Role |
|---|---|
| `core/OutputControl` | speaker bytes 20-22, headphone bytes 27-29; `outputWritePolicy()` (exactly those bytes, no routing/persistent changes); `normalizeOutputDb()`, `limitIncrease()`, `stageOutput()` (rejects NaN/inf/out-of-range before staging); `kMaxOutputIncreaseDb` = 10 dB |
| `core/VolumeSync` | Qt-free, I/O-free state machine with the authority policy below; simulated clock |
| `core/Authorization` | `envAuthorized()`: an authorization variable counts only if it is exactly `1` |
| `desktop/DesktopVolumeLink` | turns decisions into I/O: fresh device read before every decision, one verified write path (`stageOutput` + `commitAndVerify`), dry-run mode |
| `desktop/AlsaDesktopMixer` | CONNECT 2 card found by `/proc/asound/card*/usbid` on every open (never by number; ambiguous = none), dB↔raw from the control's TLV range, event subscription, card-removal detection |
| `openwitty-volumed` | user daemon; **dry run by default**; writes only with `--allow-hardware-writes` **and** `LEWITT_ALLOW_HARDWARE_WRITE=1` **and** `LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1`; speaker bytes only |
| GUI (`DeviceController`) | shows the real speaker/headphone state (settings GET, as before); writes speaker **and** headphone volume/mute only when started with `OPENWITTY_OUTPUT_WRITES=1` (exactly), through `outputControlHardwareOptions()` + `outputWritePolicy(true, true)`; same 10 dB per-step increase limit on hardware |
| CLI | `write speaker-volume|speaker-mute` (Phase 8C) now rejects non-finite input explicitly and requires every authorization variable to be exactly `1` |

### Single writer and arbitration
There is exactly one hardware writer at any time: the process that holds the per-user
`ControlLease` (flock in `$XDG_RUNTIME_DIR`, taken by `UsbTransport::open()`). GUI, daemon and CLI all
use the same `DeviceSession`/`OutputControl` code, so they cannot apply different rules.
* Daemon holds the device while it runs (one open per session, so the kernel's "did not claim
  interface 3" notice appears once per session, not per poll).
* GUI starts while the daemon holds it → the GUI's open fails with `ControlInUse` → it writes a
  **yield request** (`openwitty-connect2-yield.request`, private 0600, refreshed on every retry) and retries
  every second. The daemon checks the request every loop (≤ 250 ms), releases the device, and stays away
  while the request is fresh (30 s) or the lease is held. The GUI removes the request once it owns the
  device. A stale request (crashed GUI) expires by itself.
* GUI closed → the lease is free → the daemon reconnects and starts **read-only** again.
* While the GUI owns the device it hosts the **same** `DesktopVolumeLink` in its worker thread (with the
  `OPENWITTY_OUTPUT_WRITES=1` opt-in on a real device): desktop-slider changes keep reaching the device, GUI
  speaker moves are mirrored to KDE, and the GUI's settings refresh (62.5 ms) is reused as the hardware sample, so
  the link adds no extra polling. The link never stages on top of an unconfirmed GUI edit (it writes nothing
  while one is pending). It is dropped and restarted read-only on every disconnect, reconnect or session change,
  and it is never active in simulation. State is published as `desktop.volumeLink` (`off|waiting|active`).
* CLI writes while the daemon or GUI runs fail cleanly with `ControlInUse` (no competing writer).

## Review findings addressed (after f52b24c)

**1. "Read-only start" means no *vendor* write – an ALSA control write is not side-effect free.**
Aligning the desktop writes the card's `PCM Playback Volume/Switch`. With a working Feature Unit 2 the kernel
forwards that to the device as a UAC `SET_CUR`; with the broken FU2 the device ignores it. The evidence says FU2
and the vendor speaker level are the same physical parameter (the FU2 readback always equalled the vendor level),
so writing exactly the device's level is physically a no-op – except for rounding and a stale value. Whether the
ALSA switch and the vendor *mute* are the same physical mute is **not** established. Therefore every desktop
alignment now:
* re-reads the device immediately before writing and skips (then adopts the newer state, bounded) when the device
  changed meanwhile – a knob that just went down is never "restored" upwards;
* rounds **down** to the control's 0.5 dB grid (never louder than the device);
* **never unmutes** the desktop (a muted desktop next to an unmuted device is left as is and explained);
* is skipped when the control already shows the level (no needless control writes);
* happens after, not before, a device write that it accompanies.
In the dry run nothing is written at all (neither vendor nor ALSA).

**2. Desktop change while a GUI edit is unconfirmed.** The link still never stages on top of a pending GUI edit, and
it never commits it. Instead of silently leaving KDE at a value the speaker does not have, the rejected desktop change
now re-aligns the desktop to the device (under the rules above) and publishes a notice
(`desktop.volumeNotice` in the GUI, `notice:` in the daemon log): *"A desktop volume change was not applied because
another OpenWitty change is still unconfirmed (retry or discard it)…"*. The notice disappears with the pending edit.

## Authority and synchronization rules (core/VolumeSync)

| Transition | Rule |
|---|---|
| service start, login, reconnect, service restart, card renumbering | **read-only**: hardware is the truth; the desktop control is re-aligned to the hardware level (no physical change); the device is never written |
| desktop events during the 5 s startup hold (WirePlumber route restore at login) | not authoritative: desktop re-aligned to hardware, no device write → **no volume increase merely because the service started** |
| KDE slider / desktop mute after the hold | authoritative; written to the device. Increases limited to **+10 dB above the current hardware level per step**; when limited, the desktop is set to what was actually applied (a far jump becomes visible bounded steps). Decreases and mute are applied as requested; unmute keeps the hardware level |
| device already at the desktop value (healthy FU2 followed by itself) | no write (fresh read before every decision) |
| hardware knob / other tool | adopted as truth, mirrored to the desktop; an older queued desktop request is dropped (the newer hardware change wins); a desktop move after it wins again |
| our own desktop update echoing back (within 2 s, ±0.3 dB) | ignored → no feedback loop |
| slider drag | rate limit 150 ms; while a write is in flight or rate-limited only the newest desktop value is kept |
| write timed out / ambiguous / failed | **never retried**; state Uncertain, desktop events ignored until the next hardware read re-establishes the truth and re-aligns the desktop |
| device gone, permission error, missing control, ambiguous card | no writes, no guesses; wait and re-baseline read-only |
| defaults | never 0 dB; nothing is ever written as a "default" or "recovery" level |

Headphone volume/mute has no desktop (ALSA) control: it is changed only explicitly (GUI with the opt-in,
same bounded-increase rule) and is never touched by the daemon.

## Kernel quirk (unchanged, preserved)
`/etc/modprobe.d/openwitty-connect2.conf` (installed in Phase 8C, not modified here; a byte-identical copy
is `packaging/modprobe/openwitty-connect2.conf`, installed only as a documentation example):
```
options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error
```
* Effect: for this one device, failed mixer control reads/writes return 0 instead of an error. Without it
  the broken unit-5 microphone control (channels 3/4) makes every alsa-lib mixer load fail on kernel 7.3,
  WirePlumber creates no routes and there is no `PCM Playback` path for the desktop.
* Scope: only VID:PID 29c2:0004; other USB audio devices are unaffected. The quirk string syntax and the
  `IGNORE_CTL_ERROR` flag exist in the installed 7.3 module (checked in Phase 8C); older kernels without the
  string parser ignore the entry with a log message.
* Side effect: genuine control errors of this device are hidden from ALSA clients (values read as 0).
* Applied when `snd-usb-audio` binds the device (boot or replug); `snd-usb-audio` is not in the initramfs, so no
  rebuild is needed. **No module reload or reboot was done in this phase.**
* Rollback: `sudo rm /etc/modprobe.d/openwitty-connect2.conf`, effective at the next boot/replug.
* Upstream: a targeted kernel fix (keep partially stalling per-channel controls readable, or a device
  quirk for 29c2:0004) would be preferable later; not a prerequisite.

## Tests (all offline)
`lewitt_tests` (GoogleTest) adds 34 cases, and CTest adds 9 command refusal cases:
* `OutputControl.*` – exact byte scope, NaN/inf/out-of-range rejected before staging, boundary encoding
  (-60, 0, -30 = 0xE200), increase limiting incl. broken config, speaker/headphone independence, speaker policy
  cannot write headphone/LED on hardware, ignored write → `VerificationTimedOut` with exactly one SET.
* `Authorization.OnlyExactlyOneAuthorizes`.
* `VolumeSync.*` – read-only start for louder/quieter/muted/unreadable desktops, login restore during the hold,
  decreases, bounded increases + echo, max-increase 0, mute/unmute, knob mirroring without loops, knob drops an older
  queued request, later desktop wins, slider coalescing + rate limit, no retry after timeout/ambiguous/failed,
  no baseline/reset/reconnect, non-finite desktop, dry-run abandon.
* `DesktopLink.*` – with a simulated device and ALSA control: no device write on startup even when KDE is at 100 %,
  dry run writes nothing anywhere, verified write leaves the headphone untouched, a device that already followed is not
  written, knob → desktop without write or loop, ignored write not retried and desktop corrected (this test found and
  fixed a stale-read retry bug), mute/unmute, lost device, restart after reconnect read-only.
* `DesktopMixerHelpers.*` – card found by USB id only, renumbering after reconnect, two devices → none, absent; raw↔dB
  mapping of the 0..120 / -60..0 dB control, NaN → minimum (never louder).
* `ControlLease.Yield*` – fresh/private/clearable request, stale request ignored, symlink and non-private directory refused.
* CTest `cli_*` / `volumed_*` – NaN/−inf/out-of-range volume, `true`/`"1 "`/`yes` instead of `1`, missing field variable;
  all refused before a transport exists.

Totals (after the GUI-hosted link): Release CTest **242/242** (the GUI suite runs as one CTest entry), USB call-site audit **PASS** (extended: one gated GUI output site, output policy byte scope,
daemon speaker-only/double opt-in/dry-run default/single verified write path), `git diff --check` clean, privacy grep clean.

Live, read-only: `openwitty-volumed` (dry run) for 8 s: connected, found card 3, read speaker −23.50 dB unmuted, desktop
already aligned, no write of any kind, clean stop on SIGINT.

## Installation (nothing is enabled automatically)
```
cmake -S . -B build-release && cmake --build build-release && ctest --test-dir build-release
sudo cmake --install build-release              # /usr/local: binaries + lib/systemd/user/openwitty-volumed.service
# or per-user: cmake --install build-release --prefix ~/.local, then
#   cp ~/.local/lib/systemd/user/openwitty-volumed.service ~/.config/systemd/user/
systemctl --user daemon-reload
openwitty-volumed                               # dry run in a terminal: watch its decisions, Ctrl+C
systemctl --user enable --now openwitty-volumed.service    # only after the live validation below
```
The unit runs `openwitty-volumed --allow-hardware-writes` with the two authorization variables, as the user,
`Restart=on-failure` with a start limit; no hard-coded development paths (`@CMAKE_INSTALL_FULL_BINDIR@`).
GUI output control: start `OPENWITTY_OUTPUT_WRITES=1 openwitty`.

## Uninstall / rollback
```
systemctl --user disable --now openwitty-volumed.service
rm -f ~/.config/systemd/user/openwitty-volumed.service && systemctl --user daemon-reload
sudo xargs rm -f < build-release/install_manifest.txt          # files installed by cmake --install
sudo rm /etc/modprobe.d/openwitty-connect2.conf                 # optional: remove the kernel quirk (next boot/replug)
```
Nothing else is changed by OpenWitty (no udev, PipeWire or WirePlumber configuration). Device settings are only changed
by explicit volume/mute moves while the service or the opted-in GUI runs.

## Proven offline vs. still to be shown live
| Proven offline (simulation / unit tests) | Needs a live test |
|---|---|
| alignment re-reads before writing, rounds down, never unmutes, skips no-ops; pending-edit rejection with notice; startup/reconnect never write; login hold; bounded increases; mute; knob vs. desktop ordering; echo suppression; coalescing; no retry; byte scope; authorization exactness; card lookup; lease yield file | the daemon's writes on the real device and the KDE slider round trip in the FU2-broken state |
| daemon dry run reads the real device and ALSA control correctly (live, read-only) | knob → KDE mirroring with the real PipeWire (does WirePlumber accept the ALSA re-align without its own counter-write?) |
| GUI-hosted link: pending-edit guard, host-provided samples without extra reads, off in simulation | GUI ↔ daemon hand-over with the real lease, GUI-hosted link on the real device |
| | behaviour across a real boot with the quirk (routes present, daemon start read-only) |

## Proposed first live validation (needs fresh physical audio-safety confirmation)
One session, no reboot, no replug, no playback:
1. Safety confirmation: speakers/amplifiers off or independently protected, headphones off, nothing playing/recording,
   KDE volume and device mute not relied upon.
2. `openwitty-volumed` dry run in a terminal for one minute while moving the KDE slider a little: the log must show only
   "dry run: would write speaker …" lines with bounded values.
3. Start the writing daemon in the foreground: `LEWITT_ALLOW_HARDWARE_WRITE=1 LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1
   openwitty-volumed --allow-hardware-writes`; confirm the start line says read-only start and **no write** happens.
4. Lower the KDE volume by a few steps, mute, unmute, raise by one step; after each step one GET
   (`openwitty-cli read settings --method interface --interface 3` while the daemon is stopped, or the daemon log) must show the
   device following, increases ≤ 10 dB.
5. Turn the hardware knob once: the KDE slider must follow without a device write in the log.
6. Ctrl+C. Only then decide whether to enable the user service.
