#!/usr/bin/env python3
"""OpenWitty volume bridge for the LEWITT CONNECT 2 (29c2:0004).

REFERENCE IMPLEMENTATION ONLY (Phase 8C), superseded by the native openwitty-volumed (Phase 8D,
docs/PHASE8D_VOLUME_INTEGRATION.md). Do not install it as a service: on start and on every card
reappearance it pushes the current ALSA level to the device (it can raise the physical level at
login), it does not track the hardware knob, it has no bounded-increase limit, and its per-write CLI
calls collide with the GUI's control lease.

Why (Phase 8C assumption, later corrected by Phase 8E): in some device states the CONNECT 2's Feature Unit 2 accepts UAC SET_CUR requests but ignores them
("broken mixer GET_CUR", usb_set_interface -110). PipeWire/KDE then move the ALSA `PCM Playback Volume` control while the
physical speaker level stays where it was. The vendor settings message on interface 3 is honoured in every state, so this
bridge mirrors the ALSA control to the device:

    ALSA `PCM Playback Volume` (dB)  ->  openwitty-cli write speaker-volume <dB>
    ALSA `PCM Playback Switch`       ->  openwitty-cli write speaker-mute on|off

It runs as the desktop user, listens to `alsactl monitor` events of the CONNECT 2 card only, debounces slider movement,
writes only when the value differs from what it last applied, and re-syncs whenever the card (re)appears. It never touches
any other card, field or interface. Changes made with the hardware knob are not mirrored back (a later slider move wins).
Requires the kernel option `snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error` so that the ALSA mixer (and the PipeWire
hardware-volume route) exists at all.
"""

import argparse
import os
import re
import subprocess
import sys
import time

USB_ID = "29c2:0004"
DEBOUNCE_S = 0.15
SETTLE_S = 3.0          # after the card appears: let the kernel probe and WirePlumber restore the route first
WATCHED = ("PCM Playback Volume", "PCM Playback Switch")

MONITOR_RE = re.compile(r"^node hw:(?P<card>\d+), #\d+ \(\d+,\d+,\d+,(?P<name>[^,]+),\d+\) (?P<mask>.*)$")
SGET_RE = re.compile(r"Playback \d+ \[\d+%\] \[(?P<db>-?\d+(?:\.\d+)?)dB\](?: \[(?P<sw>on|off)\])?")


def find_card(proc_root="/proc/asound"):
    """-> ALSA card index of the CONNECT 2, or None."""
    try:
        names = sorted(os.listdir(proc_root))
    except OSError:
        return None
    for d in names:
        if not d.startswith("card"):
            continue
        try:
            with open(os.path.join(proc_root, d, "usbid")) as f:
                if f.read().strip().lower() == USB_ID:
                    return int(d[4:])
        except (OSError, ValueError):
            continue
    return None


def parse_monitor_line(line):
    """`alsactl monitor` line -> watched control name, or None."""
    m = MONITOR_RE.match(line.strip())
    if not m or m.group("name") not in WATCHED:
        return None
    return m.group("name")


def parse_sget(text):
    """`amixer -c N sget PCM` -> (db, muted) or None. Mute is None if the control has no switch."""
    for line in (text or "").splitlines():
        m = SGET_RE.search(line)
        if m:
            sw = m.group("sw")
            return float(m.group("db")), (None if sw is None else sw == "off")
    return None


def quantize_db(db):
    """The device stores Q8.8 dB in -60..0; the ALSA control has 0.5 dB steps. Clamp and round to 0.5 dB."""
    db = max(-60.0, min(0.0, db))
    return round(db * 2.0) / 2.0


def plan_writes(state, applied):
    """state/applied: (db, muted). -> list of (field, value) CLI writes needed to make the device match `state`."""
    if state is None:
        return []
    out = []
    db, muted = state
    a_db, a_muted = applied if applied else (None, None)
    q = quantize_db(db)
    if a_db is None or q != a_db:
        out.append(("speaker-volume", "%.1f" % q))
    if muted is not None and muted != a_muted:
        out.append(("speaker-mute", "on" if muted else "off"))
    return out


class Bridge:
    def __init__(self, cli, run=subprocess.run, popen=subprocess.Popen, clock=time.monotonic, sleep=time.sleep,
                 log=lambda m: print(m, flush=True), proc_root="/proc/asound"):
        self.cli, self.run, self.popen, self.clock, self.sleep, self.log = cli, run, popen, clock, sleep, log
        self.proc_root = proc_root
        self.applied = None

    def read_state(self, card):
        try:
            r = self.run(["amixer", "-c", str(card), "sget", "PCM"], capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            return None
        return parse_sget(r.stdout) if r.returncode == 0 else None

    def write(self, field, value):
        env = dict(os.environ, LEWITT_ALLOW_HARDWARE_WRITE="1", LEWITT_ALLOW_WRITE_SPEAKER_VOLUME="1")
        try:
            r = self.run([self.cli, "write", field, value, "--execute"], capture_output=True, text=True, timeout=10, env=env)
        except (OSError, subprocess.SubprocessError) as e:
            self.log("write %s %s failed to run: %s" % (field, value, e))
            return False
        ok = r.returncode == 0 or "nothing to write" in r.stdout
        self.log("write %s %s -> %s" % (field, value, "ok" if ok else "rc %d: %s" % (r.returncode, (r.stderr or r.stdout).strip()[-200:])))
        return ok

    def sync(self, card):
        state = self.read_state(card)
        writes = plan_writes(state, self.applied)
        all_ok = True
        for field, value in writes:
            all_ok = self.write(field, value) and all_ok
        if state is not None and all_ok:
            self.applied = (quantize_db(state[0]), state[1])
        return writes

    def run_card(self, card):
        """Follow one appearance of the card until `alsactl monitor` ends (card removed)."""
        self.applied = None                      # a new device instance: always push the current state once
        self.sleep(SETTLE_S)
        self.sync(card)
        p = self.popen(["stdbuf", "-oL", "alsactl", "monitor", "hw:%d" % card], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, text=True)
        try:
            for line in p.stdout:
                if parse_monitor_line(line) is None:
                    continue
                self.sleep(DEBOUNCE_S)          # coalesce a slider drag; the read below picks up the final value
                self.sync(card)
        finally:
            p.kill()
            p.wait()

    def forever(self, poll_s=2.0):
        while True:
            card = find_card(self.proc_root)
            if card is None:
                self.sleep(poll_s)
                continue
            self.log("CONNECT 2 is ALSA card %d" % card)
            self.run_card(card)
            self.log("card %d gone" % card)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", required=True, help="path to openwitty-cli")
    ap.add_argument("--once", action="store_true", help="sync the current ALSA state to the device once and exit")
    a = ap.parse_args(argv)
    if os.geteuid() == 0:
        sys.exit("run the bridge as the desktop user")
    b = Bridge(os.path.abspath(a.cli))
    if a.once:
        card = find_card()
        if card is None:
            sys.exit("CONNECT 2 not found")
        b.sync(card)
        return 0
    b.forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
