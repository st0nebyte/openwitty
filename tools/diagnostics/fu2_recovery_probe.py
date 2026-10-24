#!/usr/bin/env python3
"""Phase 8E: does a vendor settings SET make the CONNECT 2 honour Feature Unit 2 (kernel/ALSA volume) again?

Question: after an ordinary boot the device ignores UAC SET_CUR on Feature Unit 2 (the ALSA "PCM Playback Volume"
that PipeWire/KDE use). In one earlier session it later accepted them, after several vendor settings writes. Is the
vendor settings SET the trigger, or time, or something else?

Measurement (a "probe"): move the ALSA control 1 dB DOWN from the lower of the ALSA and device levels, wait, read
the device level with the vendor GET (`openwitty-cli read settings`), and record whether the device followed. The kernel
serves ALSA reads from its cache, so only a SET can show whether FU2 is honoured. Every probe makes the target
quieter; nothing in this tool ever raises a level.

Sequence (stops as soon as the question is answered; minimal interventions):
  P0  probe right away                         -> followed: FU2 healthy at boot, STOP (no failure is manufactured)
  wait --wait seconds, no intervention (time control)
  P1  probe                                    -> followed: recovered with time (or by the P0 SET), STOP
  A   vendor settings SET that cannot change audio: LED brightness one step (speaker bytes unchanged)
  P2  probe                                    -> followed: recovered after a vendor settings SET
  B   only if P2 did not follow: vendor speaker volume 1 dB down
  P3  probe                                    -> followed: recovered after a speaker-field vendor write
  P4  after --persist seconds, if anything recovered: probe again (does it last?)
  LED brightness is restored to its original value at the end (vendor SET, audio-neutral).

Usage:  fu2_recovery_probe.py plan --cli build-release/openwitty-cli        (read-only: current state + plan)
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

USB_ID = "29c2:0004"
STEP_DB = 1.0
FOLLOW_TOL_DB = 0.3
SETTLE_S = 1.5
PHANTOM_RE = re.compile(r"^phantom=\d\s+speaker=(?P<sv>-?\d+(?:\.\d+)?) dB(?P<sm> \(muted\))?", re.M)
RAW_RE = re.compile(r"^raw=(?P<hex>[0-9a-f]{256})\s*$", re.M)
LED_OFFSET = 37                      # settings byte: LED brightness 0..100
SGET_RE = re.compile(r"Playback (?P<raw>\d+) \[\d+%\] \[(?P<db>-?\d+(?:\.\d+)?)dB\]")


# ------------------------------------------------------------------ pure decision helpers (offline-tested)

def probe_target(alsa_db, device_db, step=STEP_DB):
    """Next ALSA probe level: one step below the lower of the two levels, never below -60 dB, rounded DOWN to the
    0.5 dB grid (never louder than intended)."""
    t = max(-60.0, min(alsa_db, device_db) - step)
    return max(-60.0, math.floor(t * 2.0 + 1e-9) / 2.0)


def led_from_raw(text):
    m = RAW_RE.search(text or "")
    return int(m.group("hex")[2 * LED_OFFSET:2 * LED_OFFSET + 2], 16) if m else None


def followed(target_db, device_before_db, device_after_db, tol=FOLLOW_TOL_DB):
    """The device followed the ALSA SET if it ended at the target and actually moved (target differs from before)."""
    return abs(device_after_db - target_db) <= tol and abs(device_before_db - target_db) > tol


def db_to_raw(db, raw_min=0, raw_max=120, db_min=-60.0, db_max=0.0):
    db = max(db_min, min(db_max, db))
    return raw_min + int((db - db_min) / (db_max - db_min) * (raw_max - raw_min) + 1e-9)   # rounds down


def led_step(value):
    """An LED brightness change of one step that stays in 0..100."""
    return value - 1 if value > 0 else value + 1


def verdict(results):
    """Summarize the probes. results: dict name -> bool (followed) for P0..P4 that ran."""
    if results.get("P0"):
        return "FU2_HEALTHY_AT_BOOT"
    if results.get("P1"):
        return "RECOVERED_WITHOUT_VENDOR_WRITE"
    if results.get("P2"):
        return "RECOVERED_AFTER_VENDOR_SETTINGS_SET" + ("_PERSISTENT" if results.get("P4") else
                                                       "_NOT_PERSISTENT" if "P4" in results else "")
    if results.get("P3"):
        return "RECOVERED_AFTER_SPEAKER_VENDOR_WRITE" + ("_PERSISTENT" if results.get("P4") else
                                                        "_NOT_PERSISTENT" if "P4" in results else "")
    if "P3" in results:
        return "NOT_RECOVERED"
    return "INCOMPLETE"


# ------------------------------------------------------------------ I/O (all through injectable functions)

class Io:
    def __init__(self, cli, run=subprocess.run, sleep=time.sleep, clock=time.time, proc_root="/proc/asound"):
        self.cli, self._run, self.sleep, self.clock, self.proc_root = cli, run, sleep, clock, proc_root

    def run(self, cmd, env=None, timeout=20):
        e = dict(os.environ)
        for k in ("LEWITT_ALLOW_HARDWARE_WRITE", "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"):
            e.pop(k, None)
        if env:
            e.update(env)
        r = self._run(cmd, capture_output=True, text=True, timeout=timeout, env=e)
        return r.returncode, r.stdout, r.stderr

    def card(self):
        try:
            names = sorted(os.listdir(self.proc_root))
        except OSError:
            return None
        found = []
        for d in names:
            if d.startswith("card") and d[4:].isdigit():
                try:
                    with open(os.path.join(self.proc_root, d, "usbid")) as f:
                        if f.read().strip().lower() == USB_ID:
                            found.append(int(d[4:]))
                except OSError:
                    pass
        return found[0] if len(found) == 1 else None

    def device(self):
        rc, out, err = self.run([self.cli, "read", "settings", "--method", "interface", "--interface", "3"])
        m = PHANTOM_RE.search(out or "")
        if rc != 0 or not m:
            raise RuntimeError("vendor GET failed: %s" % (err or out).strip()[-200:])
        return {"speaker_db": float(m.group("sv")), "speaker_muted": bool(m.group("sm")),
                "led": led_from_raw(out), "t": self.clock()}

    def alsa(self, card):
        rc, out, _ = self.run(["amixer", "-c", str(card), "sget", "PCM"])
        m = SGET_RE.search(out or "")
        if rc != 0 or not m:
            raise RuntimeError("ALSA PCM Playback Volume not readable (is the kernel quirk installed?)")
        return float(m.group("db"))

    def alsa_set(self, card, db):
        rc, _, err = self.run(["amixer", "-c", str(card), "cset", "name=PCM Playback Volume", str(db_to_raw(db))])
        if rc != 0:
            raise RuntimeError("ALSA write failed: %s" % err.strip()[-200:])

    def vendor_write(self, field, value):
        env = {"LEWITT_ALLOW_HARDWARE_WRITE": "1"}
        if field.startswith("speaker"):
            env["LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"] = "1"
        rc, out, err = self.run([self.cli, "write", field, str(value), "--execute"], env=env)
        if rc != 0 or "write state: verified" not in out:
            raise RuntimeError("vendor write %s %s not verified (rc %d): %s" % (field, value, rc, (err or out).strip()[-200:]))
        return self.clock()


def probe(io, card, name, log):
    before = io.device()
    alsa_before = io.alsa(card)
    target = probe_target(alsa_before, before["speaker_db"])
    t_set = io.clock()
    io.alsa_set(card, target)
    io.sleep(SETTLE_S)
    after = io.device()
    ok = followed(target, before["speaker_db"], after["speaker_db"])
    rec = {"name": name, "t_set": t_set, "alsa_before_db": alsa_before, "target_db": target,
           "device_before_db": before["speaker_db"], "device_after_db": after["speaker_db"], "followed": ok}
    log(rec)
    return ok, rec


def run_sequence(io, wait_s, persist_s, log):
    card = io.card()
    if card is None:
        raise RuntimeError("CONNECT 2 ALSA card not found (or not unique)")
    start = io.device()
    log({"name": "start", "card": card, "device": start, "alsa_db": io.alsa(card)})
    led0 = start["led"]
    results, steps = {}, []

    def do_probe(name):
        ok, rec = probe(io, card, name, log)
        results[name] = ok
        steps.append(rec)
        return ok

    try:
        if do_probe("P0"):
            return verdict(results), steps
        log({"name": "time-control wait", "seconds": wait_s})
        io.sleep(wait_s)
        if do_probe("P1"):
            return verdict(results), steps
        if led0 is None:
            raise RuntimeError("LED brightness not reported by the CLI; intervention A not possible")
        t_a = io.vendor_write("led-brightness", led_step(led0))
        log({"name": "A vendor settings SET (LED brightness)", "t": t_a, "from": led0, "to": led_step(led0)})
        recovered = do_probe("P2")
        if not recovered:
            dev = io.device()["speaker_db"]
            t_b = io.vendor_write("speaker-volume", "%.1f" % max(-60.0, dev - STEP_DB))
            log({"name": "B vendor speaker write (1 dB down)", "t": t_b, "from": dev})
            recovered = do_probe("P3")
        if recovered:
            log({"name": "persistence wait", "seconds": persist_s})
            io.sleep(persist_s)
            do_probe("P4")
        return verdict(results), steps
    finally:
        try:
            cur = io.device().get("led")
            if led0 is not None and cur is not None and cur != led0:
                io.vendor_write("led-brightness", led0)
                log({"name": "LED brightness restored", "to": led0})
        except RuntimeError as e:
            log({"name": "LED restore failed", "error": str(e)})


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
        card = io.card()
        dev = io.device()
        alsa = io.alsa(card) if card is not None else None
        print(json.dumps({"card": card, "device": dev, "alsa_db": alsa,
                          "first_probe_target_db": probe_target(alsa, dev["speaker_db"]) if alsa is not None else None,
                          "wait_s": a.wait, "persist_s": a.persist,
                          "worst_case_speaker_decrease_db": 5 * STEP_DB}, indent=1))
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
        try:
            v, _ = run_sequence(io, a.wait, a.persist, log)
        except RuntimeError as e:
            log({"name": "aborted", "error": str(e)})
            return 1
        log({"name": "verdict", "verdict": v})
    print("log:", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
