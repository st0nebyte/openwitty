#!/usr/bin/env python3
"""Phase 8E: does a vendor settings SET make the CONNECT 2 honour Feature Unit 2 (kernel/ALSA volume) again?

Question: after an ordinary boot the device ignores UAC SET_CUR on Feature Unit 2 (the ALSA "PCM Playback Volume"
that PipeWire/KDE use). In one earlier session it later accepted them, after several vendor settings writes. Is the
vendor settings SET the trigger, or time, or something else?

Measurement (a "probe"): set the ALSA control 1 dB below the current level, wait, read the complete 128-byte device
settings with the vendor GET, and classify: followed (speaker reached the target), not followed (nothing changed), or
inconclusive (no measurable decrease possible). The kernel serves ALSA reads from its cache, so only a SET can show
whether FU2 is honoured.

Sequence (stops as soon as the question is answered):
  P0  probe right away                         -> followed: FU2 healthy at boot, STOP (no failure is manufactured)
  wait --wait seconds, no intervention (time control)
  P1  probe                                    -> followed: recovered with time (or by the P0 SET), STOP
  A   vendor settings SET intended to change LED brightness only (one step). The message carries all 128 bytes, so
      every speaker/headphone byte is retransmitted unchanged; device-side effects of that retransmission are not
      established beyond what the settings read-back shows.
  P2  probe                                    -> followed: recovered after a vendor settings SET
  B   only if P2 did not follow: vendor speaker volume 1 dB down
  P3  probe                                    -> followed: recovered after a speaker-field vendor write
  P4  after --persist seconds, if anything recovered: probe again (does it last?)
  On normal completion LED brightness is restored to its original value (guarded like every other write).

Safety rules (any violation aborts at once; after an abort NOTHING more is written, a pending LED restore is reported):
  * start only if ALSA and device agree (<= 0.5 dB), the device is unmuted, and the level is >= -56 dB;
    every probe targets 1 dB (1.5 dB when ALSA already sits 1 dB down) below the DEVICE level, B is 1 dB down;
    lowest reachable levels from a start level L: device L - 3 dB, ALSA L - 3 dB (offline-verified for every branch);
    a probe that cannot produce a measurable decrease aborts as inconclusive before writing;
  * before every write: the same single CONNECT 2 (bus/device number) and the same ALSA card (re-found by USB id);
  * after every write and between steps: the full settings read-back may differ only in the bytes the step may change,
    and only to the expected value (probe: speaker level to the target or unchanged; A: LED byte; B: speaker level);
    louder, mute changes, partial moves and changes made outside the experiment abort;
  * no retries; any failed read or unverified write aborts.

Usage:  fu2_recovery_probe.py plan --cli build-release/openwitty-cli        (read-only: current state + bounds)
        fu2_recovery_probe.py run  --cli ... --confirm-audio-safe [--wait 600] [--persist 120] [--out DIR]
No reboot, replug, USB reset, module or service action. Run as the desktop user with no other OpenWitty process.
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
import time

USB_VID, USB_PID = "29c2", "0004"
USB_ID = USB_VID + ":" + USB_PID
STEP_DB = 1.0
TOL_DB = 0.3
START_AGREE_DB = 0.5
SETTLE_S = 1.5
FLOOR_DB = -60.0
SPEAKER = (20, 21)                    # i16 Q8.8 speaker volume
SPEAKER_MUTE = 22
LED = 37                              # LED brightness 0..100
MAX_DEVICE_DROP_DB = 3.0              # lowest reachable device level: start - 3 dB (offline-verified for every branch)
MAX_ALSA_DROP_DB = 3.0                # lowest ALSA probe target: start - 3 dB (offline-verified)
HEADROOM_DB = 4.0                     # start level must be >= -60 + HEADROOM_DB
PHANTOM_RE = re.compile(r"^phantom=\d\s+speaker=(?P<sv>-?\d+(?:\.\d+)?) dB(?P<sm> \(muted\))?", re.M)
RAW_RE = re.compile(r"^raw=(?P<hex>[0-9a-f]{256})\s*$", re.M)
SGET_RE = re.compile(r"Playback (?P<raw>\d+) \[\d+%\] \[(?P<db>-?\d+(?:\.\d+)?)dB\](?: \[(?P<sw>on|off)\])?")


class Abort(Exception):
    """Stop now; no further hardware writes."""


# ------------------------------------------------------------------ pure helpers (offline-tested)

def grid_down(db):
    return max(FLOOR_DB, math.floor(db * 2.0 + 1e-9) / 2.0)


def speaker_db(raw):
    v = int.from_bytes(raw[SPEAKER[0]:SPEAKER[1] + 1], "little", signed=True)
    return v / 256.0


def led_of(raw):
    return raw[LED]


def diff_offsets(a, b):
    return [i for i in range(len(a)) if a[i] != b[i]]


def start_problems(alsa_db, device_db, device_muted, alsa_muted):
    p = []
    if abs(alsa_db - device_db) > START_AGREE_DB:
        p.append("ALSA (%.2f dB) and device (%.2f dB) disagree: align them first (no write by this tool)" % (alsa_db, device_db))
    if device_muted or alsa_muted:
        p.append("speaker or ALSA control muted: a probe could not show a level change")
    need = FLOOR_DB + HEADROOM_DB
    if min(alsa_db, device_db) < need:
        p.append("level %.2f dB too close to the -60 dB floor (need >= %.1f dB for every step)" % (min(alsa_db, device_db), need))
    return p


def worst_case(alsa_db, device_db):
    """Lowest levels the sequence can reach from a valid start (bounds verified by the offline tests)."""
    return {"device_min_db": device_db - MAX_DEVICE_DROP_DB, "alsa_min_db": device_db - MAX_ALSA_DROP_DB}


def probe_target(device_db, alsa_db):
    """One step below the DEVICE level (not below ALSA: after unfollowed probes ALSA has drifted down, and a later
    recovery must not make the device jump several steps). The kernel sends no USB request when an ALSA write
    repeats the cached value, so if ALSA already sits at that level the target is half a step lower."""
    t = grid_down(device_db - STEP_DB)
    if abs(alsa_db - t) <= TOL_DB:
        t = grid_down(device_db - STEP_DB - 0.5)
    return t


def classify_probe(target_db, before_raw, after_raw):
    """-> True (followed) | False (not followed). Raises Abort for anything else (inconclusive or unexpected)."""
    before, after = speaker_db(before_raw), speaker_db(after_raw)
    if not target_db < before - TOL_DB:
        raise Abort("inconclusive probe: target %.2f dB is not measurably below the device level %.2f dB" % (target_db, before))
    changed = diff_offsets(before_raw, after_raw)
    if any(o not in SPEAKER for o in changed):
        raise Abort("unexpected settings change during an ALSA probe: bytes %s" % changed)
    if abs(after - target_db) <= TOL_DB:
        return True
    if not changed:
        return False
    raise Abort("unexpected speaker level %.2f dB after a probe to %.2f dB (from %.2f dB)" % (after, target_db, before))


def check_vendor_write(before_raw, after_raw, allowed, expect):
    """After a vendor SET: only `allowed` offsets may differ and `expect(after_raw)` must hold."""
    changed = diff_offsets(before_raw, after_raw)
    if any(o not in allowed for o in changed):
        raise Abort("vendor write changed unexpected settings bytes %s" % changed)
    if not expect(after_raw):
        raise Abort("vendor write did not produce the expected value")


def verdict(results):
    if results.get("P0"):
        return "FU2_HEALTHY_AT_BOOT"
    if results.get("P1"):
        return "RECOVERED_WITHOUT_VENDOR_WRITE"
    for k, name in (("P2", "RECOVERED_AFTER_VENDOR_SETTINGS_SET"), ("P3", "RECOVERED_AFTER_SPEAKER_VENDOR_WRITE")):
        if results.get(k):
            return name + ("_PERSISTENT" if results.get("P4") else "_NOT_PERSISTENT" if "P4" in results else "")
    if "P3" in results:
        return "NOT_RECOVERED"
    return "INCOMPLETE"


def led_step(value):
    return value - 1 if value > 0 else value + 1


def db_to_raw(db, raw_min=0, raw_max=120, db_min=-60.0, db_max=0.0):
    db = max(db_min, min(db_max, db))
    return raw_min + int((db - db_min) / (db_max - db_min) * (raw_max - raw_min) + 1e-9)   # rounds down


# ------------------------------------------------------------------ I/O (injectable)

class Io:
    def __init__(self, cli, run=subprocess.run, sleep=time.sleep, clock=time.time, proc_root="/proc/asound",
                 sys_usb="/sys/bus/usb/devices"):
        self.cli, self._run, self.sleep, self.clock = cli, run, sleep, clock
        self.proc_root, self.sys_usb = proc_root, sys_usb

    def run(self, cmd, env=None, timeout=20):
        e = dict(os.environ)
        for k in ("LEWITT_ALLOW_HARDWARE_WRITE", "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"):
            e.pop(k, None)
        if env:
            e.update(env)
        r = self._run(cmd, capture_output=True, text=True, timeout=timeout, env=e)
        return r.returncode, r.stdout, r.stderr

    def _read(self, path):
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return None

    def usb_identity(self):
        """(busnum, devnum) of the single CONNECT 2, or None if absent/ambiguous."""
        found = []
        try:
            names = os.listdir(self.sys_usb)
        except OSError:
            return None
        for n in names:
            base = os.path.join(self.sys_usb, n)
            if self._read(os.path.join(base, "idVendor")) == USB_VID and self._read(os.path.join(base, "idProduct")) == USB_PID:
                found.append((self._read(os.path.join(base, "busnum")), self._read(os.path.join(base, "devnum"))))
        return found[0] if len(found) == 1 else None

    def card(self):
        try:
            names = sorted(os.listdir(self.proc_root))
        except OSError:
            return None
        found = [int(d[4:]) for d in names if d.startswith("card") and d[4:].isdigit()
                 and (self._read(os.path.join(self.proc_root, d, "usbid")) or "").lower() == USB_ID]
        return found[0] if len(found) == 1 else None

    def settings(self):
        rc, out, err = self.run([self.cli, "read", "settings", "--method", "interface", "--interface", "3"])
        m = RAW_RE.search(out or "")
        if rc != 0 or not m:
            raise Abort("vendor GET failed: %s" % (err or out).strip()[-200:])
        raw = bytes.fromhex(m.group("hex"))
        ph = PHANTOM_RE.search(out)
        if not ph or abs(float(ph.group("sv")) - speaker_db(raw)) > 0.01 or bool(ph.group("sm")) != bool(raw[SPEAKER_MUTE]):
            raise Abort("vendor GET output inconsistent (decoded vs raw)")
        return raw

    def alsa(self, card):
        rc, out, _ = self.run(["amixer", "-c", str(card), "sget", "PCM"])
        m = SGET_RE.search(out or "")
        if rc != 0 or not m:
            raise Abort("ALSA PCM Playback Volume not readable (is the kernel quirk installed?)")
        return float(m.group("db")), m.group("sw") == "off"

    def alsa_set(self, card, db):
        rc, _, err = self.run(["amixer", "-c", str(card), "cset", "name=PCM Playback Volume", str(db_to_raw(db))])
        if rc != 0:
            raise Abort("ALSA write failed: %s" % err.strip()[-200:])

    def vendor_write(self, field, value):
        env = {"LEWITT_ALLOW_HARDWARE_WRITE": "1"}
        if field.startswith("speaker"):
            env["LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"] = "1"
        rc, out, err = self.run([self.cli, "write", field, str(value), "--execute"], env=env)
        if rc != 0 or "write state: verified" not in out:
            raise Abort("vendor write %s %s not verified (rc %d): %s" % (field, value, rc, (err or out).strip()[-200:]))


# ------------------------------------------------------------------ the experiment

class Experiment:
    def __init__(self, io, log):
        self.io, self.log = io, log
        self.identity = self.card = self.last = self.led0 = None
        self.led_attempted = None          # last LED value sent, even if its read-back never came
        self.results, self.steps = {}, []

    def snapshot(self, why):
        """Read the device; anything that changed since the last known state (outside a step) aborts."""
        raw = self.io.settings()
        if self.last is not None and raw != self.last:
            raise Abort("device settings changed outside the experiment (%s): bytes %s" % (why, diff_offsets(self.last, raw)))
        return raw

    def revalidate(self):
        if self.io.usb_identity() != self.identity:
            raise Abort("CONNECT 2 USB identity changed or ambiguous: not writing")
        if self.io.card() != self.card:
            raise Abort("CONNECT 2 ALSA card changed or ambiguous: not writing")

    def start(self):
        self.identity, self.card = self.io.usb_identity(), self.io.card()
        if self.identity is None or self.card is None:
            raise Abort("CONNECT 2 not found exactly once (USB and ALSA)")
        raw = self.io.settings()
        alsa_db, alsa_muted = self.io.alsa(self.card)
        dev = speaker_db(raw)
        problems = start_problems(alsa_db, dev, bool(raw[SPEAKER_MUTE]), alsa_muted)
        self.log({"name": "start", "card": self.card, "usb": self.identity, "device_db": dev, "alsa_db": alsa_db,
                  "led": led_of(raw), "worst_case": worst_case(alsa_db, dev), "problems": problems})
        if problems:
            raise Abort("; ".join(problems))
        self.last, self.led0 = raw, led_of(raw)

    def probe(self, name):
        before = self.snapshot("before " + name)
        alsa_db, _ = self.io.alsa(self.card)
        target = probe_target(speaker_db(before), alsa_db)
        if not target < speaker_db(before) - TOL_DB or abs(alsa_db - target) <= TOL_DB:
            raise Abort("inconclusive probe %s: no measurable decrease possible (device %.2f dB, ALSA %.2f dB)"
                        % (name, speaker_db(before), alsa_db))
        self.revalidate()
        t = self.io.clock()
        self.io.alsa_set(self.card, target)
        self.io.sleep(SETTLE_S)
        after = self.io.settings()
        ok = classify_probe(target, before, after)
        self.last = after
        rec = {"name": name, "t_set": t, "alsa_before_db": alsa_db, "target_db": target,
               "device_before_db": speaker_db(before), "device_after_db": speaker_db(after), "followed": ok}
        self.log(rec)
        self.results[name] = ok
        self.steps.append(rec)
        return ok

    def vendor(self, name, field, value, allowed, expect):
        before = self.snapshot("before " + name)
        self.revalidate()
        t = self.io.clock()
        if field == "led-brightness":
            self.led_attempted = int(value)
        self.io.vendor_write(field, value)
        after = self.io.settings()
        check_vendor_write(before, after, allowed, expect)
        self.last = after
        self.log({"name": name, "t": t, "field": field, "value": value})

    def run(self, wait_s, persist_s):
        self.start()
        if self.probe("P0"):
            return self.finish()
        self.log({"name": "time-control wait", "seconds": wait_s})
        self.io.sleep(wait_s)
        if self.probe("P1"):
            return self.finish()
        target_led = led_step(self.led0)
        self.vendor("A settings SET (LED brightness)", "led-brightness", target_led, (LED,),
                    lambda r: led_of(r) == target_led)
        recovered = self.probe("P2")
        if not recovered:
            dev = speaker_db(self.last)
            target = grid_down(dev - STEP_DB)
            self.vendor("B speaker vendor write (1 dB down)", "speaker-volume", "%.1f" % target, SPEAKER,
                        lambda r: abs(speaker_db(r) - target) <= TOL_DB)
            recovered = self.probe("P3")
        if recovered:
            self.log({"name": "persistence wait", "seconds": persist_s})
            self.io.sleep(persist_s)
            self.probe("P4")
        return self.finish()

    def finish(self):
        """Normal completion only: restore the LED under the same guards."""
        if self.last is not None and self.led0 is not None and led_of(self.last) != self.led0:
            self.vendor("LED brightness restored", "led-brightness", self.led0, (LED,), lambda r: led_of(r) == self.led0)
        return verdict(self.results)

    def pending_restore(self):
        """After an abort: what a later, explicitly verified LED restore would have to undo (None = nothing)."""
        known = led_of(self.last) if self.last is not None else None
        if self.led0 is None or (known == self.led0 and self.led_attempted in (None, self.led0)):
            return None
        return {"led_original": self.led0, "led_last_confirmed": known, "led_last_sent": self.led_attempted}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["plan", "run"])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--confirm-audio-safe", action="store_true")
    ap.add_argument("--wait", type=float, default=600.0)
    ap.add_argument("--persist", type=float, default=120.0)
    ap.add_argument("--out", default=os.path.expanduser("~/.local/state/openwitty/fu2-recovery"))
    a = ap.parse_args(argv)
    if os.geteuid() == 0:
        sys.exit("run as the desktop user")
    if not (30 <= a.wait <= 3600 and 30 <= a.persist <= 3600):
        sys.exit("--wait and --persist must be 30..3600 s")
    io = Io(os.path.abspath(a.cli))
    if a.cmd == "plan":
        card, ident = io.card(), io.usb_identity()
        raw = io.settings()
        alsa_db, alsa_muted = io.alsa(card) if card is not None else (None, None)
        dev = speaker_db(raw)
        print(json.dumps({"card": card, "usb": ident, "device_db": dev, "device_muted": bool(raw[SPEAKER_MUTE]),
                          "alsa_db": alsa_db, "alsa_muted": alsa_muted, "led": led_of(raw),
                          "start_problems": start_problems(alsa_db, dev, bool(raw[SPEAKER_MUTE]), alsa_muted)
                          if alsa_db is not None else ["ALSA card not found"],
                          "worst_case": worst_case(alsa_db, dev) if alsa_db is not None else None,
                          "wait_s": a.wait, "persist_s": a.persist}, indent=1))
        return 0
    if not a.confirm_audio_safe:
        sys.exit("refused: pass --confirm-audio-safe only after the physical audio-safety confirmation")
    os.makedirs(a.out, mode=0o700, exist_ok=True)
    path = os.path.join(a.out, "run-%s.jsonl" % time.strftime("%Y%m%d-%H%M%S"))
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        def log(rec):
            rec = dict(rec, wall=time.strftime("%H:%M:%S"))
            f.write(json.dumps(rec) + "\n")
            f.flush()
            print(json.dumps(rec), flush=True)
        exp = Experiment(io, log)
        try:
            v = exp.run(a.wait, a.persist)
        except (Abort, KeyboardInterrupt, OSError, subprocess.SubprocessError) as e:
            log({"name": "ABORTED", "error": str(e) or type(e).__name__, "no_further_writes": True,
                 "pending_led_restore": exp.pending_restore()})
            print("log:", path)
            return 1
        log({"name": "verdict", "verdict": v})
    print("log:", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
