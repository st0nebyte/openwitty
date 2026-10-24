#!/usr/bin/env python3
"""Offline tests for fu2_recovery_probe.py: a simulated CONNECT 2 for every competing explanation and every abort rule."""
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fu2_recovery_probe as P  # noqa: E402


def put_db(raw, db):
    v = int(round(db * 256)) & 0xFFFF
    raw[20], raw[21] = v & 0xFF, v >> 8


class World:
    """Simulated device + ALSA control. `trigger` decides when FU2 starts honouring ALSA SETs:
    healthy | time:<s> | vendor_any | vendor_speaker | never; `revert_after` makes a recovery temporary."""

    def __init__(self, trigger, speaker=-30.0, led=80, alsa=None, revert_after=None):
        self.trigger, self.led = trigger, led
        self.raw = bytearray(128)
        put_db(self.raw, speaker)
        self.raw[37] = led
        self.raw[27], self.raw[28], self.raw[29] = 0x66, 0xDF, 1       # headphone -32.4 dB muted
        self.alsa = speaker if alsa is None else alsa
        self.t = 1000.0
        self.healed_at = 0.0 if trigger == "healthy" else None
        self.revert_after = revert_after
        self.calls, self.vendor_writes, self.alsa_sets, self.envs = [], [], [], []
        self.hooks = {}                    # call index -> function(world) run before answering
        self.fail_read_at = None
        self.n_reads = 0
        self.card_index, self.usb = 3, ("3", "4")
        self._hist = []

    def speaker(self):
        return P.speaker_db(bytes(self.raw))

    @property
    def speaker_history(self):
        return self._hist + [self.speaker()]

    def honoured(self):
        if self.trigger.startswith("time:") and self.t >= 1000.0 + float(self.trigger[5:]):
            return True
        if self.healed_at is None:
            return False
        return self.revert_after is None or self.t < self.healed_at + self.revert_after

    def run(self, cmd, capture_output=True, text=True, timeout=20, env=None):
        self.calls.append(cmd)
        hook = self.hooks.pop(len(self.calls), None)
        if hook:
            hook(self)
        if cmd[0] == "amixer" and cmd[3] == "sget":
            return subprocess.CompletedProcess(cmd, 0, "  Mono: Playback %d [50%%] [%.2fdB] [on]\n"
                                               % (P.db_to_raw(self.alsa), self.alsa), "")
        if cmd[0] == "amixer" and cmd[3] == "cset":
            db = -60.0 + int(cmd[5]) * 0.5
            self.alsa = db
            self.alsa_sets.append(db)
            if self.honoured():
                self._hist.append(self.speaker())
                put_db(self.raw, db)
            return subprocess.CompletedProcess(cmd, 0, "", "")
        if cmd[1] == "read":
            self.n_reads += 1
            if self.fail_read_at == self.n_reads:
                return subprocess.CompletedProcess(cmd, 1, "", "timeout")
            out = "phantom=1  speaker=%.2f dB  headphone=-32.40 dB (muted)  dim=0\nraw=%s\n" % (
                self.speaker(), bytes(self.raw).hex())
            if self.raw[22]:
                out = out.replace("dB  headphone", "dB (muted)  headphone", 1)
            return subprocess.CompletedProcess(cmd, 0, out, "")
        if cmd[1] == "write":
            self.envs.append({k: v for k, v in (env or {}).items() if k.startswith("LEWITT_")})
            field, value = cmd[2], cmd[3]
            self.vendor_writes.append((field, value))
            if field == "led-brightness":
                self.raw[37] = int(value)
            else:
                self._hist.append(self.speaker())
                put_db(self.raw, float(value))
            if self.trigger == "vendor_any" or (self.trigger == "vendor_speaker" and field == "speaker-volume"):
                if self.healed_at is None:
                    self.healed_at = self.t
            return subprocess.CompletedProcess(cmd, 0, "write state: verified (convergence polls: 1)\n", "")
        raise AssertionError(cmd)

    def sleep(self, s):
        self.t += s
        if s >= 600 and getattr(self, "during_wait", None):
            self.during_wait(self)

    def clock(self):
        return self.t


def experiment(world):
    io = P.Io("/x/openwitty-cli", run=world.run, sleep=world.sleep, clock=world.clock)
    io.card = lambda: world.card_index
    io.usb_identity = lambda: world.usb
    log = []
    return P.Experiment(io, log.append), log


def run(world, wait=600, persist=120):
    exp, log = experiment(world)
    return exp.run(wait, persist), exp, log


class Helpers(unittest.TestCase):
    def test_grid_down_never_louder_and_floor(self):
        self.assertEqual(P.grid_down(-34.2), -34.5)
        self.assertEqual(P.grid_down(-34.0), -34.0)
        self.assertEqual(P.grid_down(-61.0), -60.0)

    def test_start_problems(self):
        self.assertEqual(P.start_problems(-30, -30, False, False), [])
        self.assertTrue(P.start_problems(-20, -30, False, False))     # disagreement > 0.5 dB
        self.assertTrue(P.start_problems(-30, -30, True, False))      # muted
        self.assertTrue(P.start_problems(-56.5, -56.5, False, False)) # too close to -60
        self.assertEqual(P.start_problems(-56.0, -56.0, False, False), [])

    def test_worst_case_is_bounded_and_documented(self):
        wc = P.worst_case(-30.0, -30.0)
        self.assertEqual(wc["device_min_db"], -33.0)
        self.assertEqual(wc["alsa_min_db"], -33.0)

    def test_probe_target_follows_the_device_and_always_changes_alsa(self):
        self.assertEqual(P.probe_target(-30.0, -30.0), -31.0)
        self.assertEqual(P.probe_target(-30.0, -31.0), -31.5)        # ALSA already there: half a step more
        self.assertEqual(P.probe_target(-30.0, -31.5), -31.0)
        self.assertEqual(P.probe_target(-30.0, -40.0), -31.0)        # drifted ALSA does not drag the device down

    def test_led_step(self):
        self.assertEqual(P.led_step(100), 99)
        self.assertEqual(P.led_step(0), 1)

    def test_classify_probe(self):
        a = bytearray(128); put_db(a, -30)
        followed = bytearray(a); put_db(followed, -31)
        self.assertTrue(P.classify_probe(-31, bytes(a), bytes(followed)))
        self.assertFalse(P.classify_probe(-31, bytes(a), bytes(a)))
        louder = bytearray(a); put_db(louder, -25)
        with self.assertRaises(P.Abort):
            P.classify_probe(-31, bytes(a), bytes(louder))            # unexpected increase
        partial = bytearray(a); put_db(partial, -30.5)
        with self.assertRaises(P.Abort):
            P.classify_probe(-31, bytes(a), bytes(partial))           # partial move
        muted = bytearray(a); muted[22] = 1
        with self.assertRaises(P.Abort):
            P.classify_probe(-31, bytes(a), bytes(muted))             # mute change
        other = bytearray(a); other[27] ^= 1
        with self.assertRaises(P.Abort):
            P.classify_probe(-31, bytes(a), bytes(other))             # headphone byte changed
        with self.assertRaises(P.Abort):
            P.classify_probe(-30, bytes(a), bytes(a))                 # no measurable decrease: inconclusive

    def test_verdicts(self):
        self.assertEqual(P.verdict({"P0": True}), "FU2_HEALTHY_AT_BOOT")
        self.assertEqual(P.verdict({"P0": False, "P1": False, "P2": False, "P3": False}), "NOT_RECOVERED")
        self.assertEqual(P.verdict({"P0": False}), "INCOMPLETE")


class Sequences(unittest.TestCase):
    def assert_bounds(self, w, start_db=-30.0):
        self.assertGreaterEqual(min(w.speaker_history), start_db - P.MAX_DEVICE_DROP_DB - 1e-9)
        self.assertLessEqual(max(w.speaker_history), start_db + 1e-9)            # never louder than at start
        self.assertGreaterEqual(min(w.alsa_sets), start_db - P.MAX_ALSA_DROP_DB - 1e-9)
        self.assertTrue(all(db < start_db for db in w.alsa_sets))
        steps = [b - a for a, b in zip(w.speaker_history, w.speaker_history[1:])]
        self.assertTrue(all(-1.5 - 1e-9 <= d <= 1e-9 for d in steps), steps)   # each device change: down, <= 1.5 dB
        for field, value in w.vendor_writes:
            if field == "speaker-volume":
                self.assertLess(float(value), start_db)

    def test_bounds_hold_for_every_branch(self):
        for trig in ("healthy", "time:200", "time:900", "vendor_any", "vendor_speaker", "never"):
            for revert in (None, 60):
                w = World(trig, revert_after=revert)
                run(w)
                self.assert_bounds(w)

    def test_healthy_at_boot_stops_after_one_probe_without_any_vendor_write(self):
        w = World("healthy")
        v, _, _ = run(w)
        self.assertEqual(v, "FU2_HEALTHY_AT_BOOT")
        self.assertEqual(w.vendor_writes, [])
        self.assertEqual(w.alsa_sets, [-31.0])

    def test_recovery_with_time_is_not_attributed_to_a_vendor_write(self):
        w = World("time:200")
        v, _, _ = run(w, wait=600)
        self.assertEqual(v, "RECOVERED_WITHOUT_VENDOR_WRITE")
        self.assertEqual(w.vendor_writes, [])

    def test_any_vendor_settings_set_recovers_and_persists(self):
        w = World("vendor_any")
        v, exp, _ = run(w)
        self.assertEqual(v, "RECOVERED_AFTER_VENDOR_SETTINGS_SET_PERSISTENT")
        self.assertEqual(w.vendor_writes, [("led-brightness", "79"), ("led-brightness", "80")])
        self.assertEqual(w.raw[37], 80)
        self.assertIsNone(exp.pending_restore())
        self.assert_bounds(w)

    def test_only_a_speaker_write_recovers(self):
        w = World("vendor_speaker")
        v, _, _ = run(w)
        self.assertEqual(v, "RECOVERED_AFTER_SPEAKER_VENDOR_WRITE_PERSISTENT")
        self.assertEqual([f for f, _ in w.vendor_writes], ["led-brightness", "speaker-volume", "led-brightness"])
        self.assertAlmostEqual(w.speaker(), -33.0)                  # the documented worst case
        self.assert_bounds(w)

    def test_temporary_recovery_is_reported_as_not_persistent(self):
        w = World("vendor_any", revert_after=60)
        v, _, _ = run(w, persist=120)
        self.assertEqual(v, "RECOVERED_AFTER_VENDOR_SETTINGS_SET_NOT_PERSISTENT")

    def test_no_recovery_completes_and_restores_led(self):
        w = World("never")
        v, _, _ = run(w)
        self.assertEqual(v, "NOT_RECOVERED")
        self.assertEqual(w.raw[37], 80)
        self.assert_bounds(w)

    def test_write_authorization_is_exactly_what_each_field_needs(self):
        w = World("vendor_speaker")
        run(w)
        self.assertEqual(w.envs[0], {"LEWITT_ALLOW_HARDWARE_WRITE": "1"})
        self.assertEqual(w.envs[1], {"LEWITT_ALLOW_HARDWARE_WRITE": "1", "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME": "1"})


class Aborts(unittest.TestCase):
    def writes_after(self, w, n_before):
        return [c for c in w.calls[n_before:] if c[0] == "amixer" and c[3] == "cset" or (c[0] != "amixer" and c[1] == "write")]

    def test_refuses_to_start_on_disagreement_mute_or_floor(self):
        for w in (World("never", alsa=-20.0), World("never", speaker=-56.5)):
            with self.assertRaises(P.Abort):
                run(w)
            self.assertEqual(w.alsa_sets, [])
            self.assertEqual(w.vendor_writes, [])
        m = World("never")
        m.raw[22] = 1
        with self.assertRaises(P.Abort):
            run(m)
        self.assertEqual(m.alsa_sets, [])

    def test_failed_read_after_the_led_set_aborts_without_any_restore(self):
        w = World("never")
        exp, _ = experiment(w)
        reads_before_a = 6                                           # start, P0 (2), P1 (2), snapshot before A
        w.fail_read_at = reads_before_a + 1                          # the read-back right after the LED SET
        with self.assertRaises(P.Abort):
            exp.run(600, 120)
        self.assertEqual(w.vendor_writes, [("led-brightness", "79")])   # no restore attempted
        self.assertEqual(w.raw[37], 79)
        self.assertEqual(exp.pending_restore(), {"led_original": 80, "led_last_confirmed": 80, "led_last_sent": 79})

    def test_knob_turned_during_the_wait_aborts_before_any_further_write(self):
        w = World("never")
        exp, _ = experiment(w)
        w.during_wait = lambda world: put_db(world.raw, -20.0)        # knob turned up during the time-control wait
        with self.assertRaises(P.Abort) as ctx:
            exp.run(600, 120)
        self.assertIn("outside the experiment", str(ctx.exception))
        self.assertEqual(w.vendor_writes, [])
        self.assertEqual(len(w.alsa_sets), 1)                         # only P0

    def test_unexpected_increase_after_a_probe_aborts(self):
        w = World("never")
        exp, _ = experiment(w)

        def louder_on_set(world):
            world.trigger = "healthy"
            world.healed_at = 0.0
        orig = w.run

        def run_with_jump(cmd, **kw):
            r = orig(cmd, **kw)
            if cmd[0] == "amixer" and cmd[3] == "cset":
                put_db(w.raw, -25.0)                                   # device jumps up instead of following
            return r
        w.run = run_with_jump
        exp.io._run = run_with_jump
        with self.assertRaises(P.Abort):
            exp.run(600, 120)
        self.assertEqual(w.vendor_writes, [])
        self.assertEqual(len(w.alsa_sets), 1)

    def test_vendor_write_with_side_effects_aborts(self):
        w = World("never")
        exp, _ = experiment(w)
        orig = w.run

        def led_also_changes_headphone(cmd, **kw):
            r = orig(cmd, **kw)
            if cmd[0] != "amixer" and cmd[1] == "write" and cmd[2] == "led-brightness":
                w.raw[29] = 0                                          # headphone unmuted as a side effect
            return r
        exp.io._run = led_also_changes_headphone
        with self.assertRaises(P.Abort) as ctx:
            exp.run(600, 120)
        self.assertIn("unexpected settings bytes", str(ctx.exception))
        self.assertEqual(w.vendor_writes, [("led-brightness", "79")])  # nothing after it, no restore

    def test_card_or_usb_identity_change_aborts_before_writing(self):
        for change in ("card", "usb"):
            w = World("never")
            exp, _ = experiment(w)
            w.during_wait = (lambda world: setattr(world, "card_index", 5)) if change == "card" \
                else (lambda world: setattr(world, "usb", ("3", "5")))
            with self.assertRaises(P.Abort):
                exp.run(600, 120)
            self.assertEqual(len(w.alsa_sets), 1, change)              # P1 never written
            self.assertEqual(w.vendor_writes, [])

    def test_inconclusive_probe_at_the_floor_aborts(self):
        w = World("never", speaker=-55.0)
        exp, _ = experiment(w)
        exp.start()
        put_db(w.raw, -60.0)                                           # simulated: device already at the floor
        exp.last = bytes(w.raw)
        w.alsa = -60.0
        with self.assertRaises(P.Abort) as ctx:
            exp.probe("P0")
        self.assertIn("inconclusive", str(ctx.exception))
        self.assertEqual(w.alsa_sets, [])

    def test_main_abort_path_writes_nothing_more_and_reports_pending_led(self):
        w = World("never")
        exp, log = experiment(w)
        w.fail_read_at = 7
        try:
            exp.run(600, 120)
        except P.Abort:
            pass
        self.assertEqual(sum(1 for c in w.calls if c[0] != "amixer" and c[1] == "write"), 1)
        self.assertIsNotNone(exp.pending_restore())


if __name__ == "__main__":
    unittest.main()
