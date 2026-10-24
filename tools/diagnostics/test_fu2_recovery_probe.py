#!/usr/bin/env python3
"""Offline tests for fu2_recovery_probe.py: a simulated CONNECT 2 for every competing explanation."""
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fu2_recovery_probe as P  # noqa: E402


class World:
    """Simulated device + ALSA control. `trigger` decides when FU2 starts honouring ALSA SETs:
    healthy | time:<s> | vendor_any | vendor_speaker | never ; `revert_after` makes a recovery temporary."""

    def __init__(self, trigger, speaker=-30.0, led=80, revert_after=None):
        self.trigger, self.speaker, self.led, self.alsa = trigger, speaker, led, speaker
        self.t = 1000.0
        self.healed_at = 0.0 if trigger == "healthy" else None
        self.revert_after = revert_after
        self.calls, self.vendor_writes, self.alsa_sets, self.envs = [], [], [], []

    def honoured(self):
        if self.trigger.startswith("time:") and self.t >= 1000.0 + float(self.trigger[5:]):
            return True
        if self.healed_at is None:
            return False
        return self.revert_after is None or self.t < self.healed_at + self.revert_after

    def raw(self):
        b = bytearray(128)
        b[37] = self.led
        return b.hex()

    def run(self, cmd, capture_output, text, timeout, env):
        self.calls.append(cmd)
        if cmd[0] == "amixer" and cmd[3] == "sget":
            raw = P.db_to_raw(self.alsa)
            return subprocess.CompletedProcess(cmd, 0, "  Mono: Playback %d [50%%] [%.2fdB] [on]\n" % (raw, self.alsa), "")
        if cmd[0] == "amixer" and cmd[3] == "cset":
            db = -60.0 + int(cmd[5]) * 0.5
            self.alsa = db
            self.alsa_sets.append(db)
            if self.honoured():
                self.speaker = db
            return subprocess.CompletedProcess(cmd, 0, "", "")
        if cmd[1] == "read":
            out = "phantom=1  speaker=%.2f dB  headphone=-32.40 dB (muted)  dim=0\nraw=%s\n" % (self.speaker, self.raw())
            return subprocess.CompletedProcess(cmd, 0, out, "")
        if cmd[1] == "write":
            self.envs.append({k: v for k, v in env.items() if k.startswith("LEWITT_")})
            field, value = cmd[2], cmd[3]
            self.vendor_writes.append((field, value))
            if field == "led-brightness":
                self.led = int(value)
            else:
                self.speaker = float(value)
            if self.trigger == "vendor_any" or (self.trigger == "vendor_speaker" and field == "speaker-volume"):
                if self.healed_at is None:
                    self.healed_at = self.t
            return subprocess.CompletedProcess(cmd, 0, "write state: verified (convergence polls: 1)\n", "")
        raise AssertionError(cmd)

    def sleep(self, s):
        self.t += s

    def clock(self):
        return self.t


def run(world, wait=300, persist=120):
    io = P.Io("/x/openwitty-cli", run=world.run, sleep=world.sleep, clock=world.clock, proc_root="/nonexistent")
    io.card = lambda: 3
    log = []
    v, steps = P.run_sequence(io, wait, persist, log.append)
    return v, steps, log


class Helpers(unittest.TestCase):
    def test_probe_target_is_one_step_down_on_the_grid_never_louder(self):
        self.assertEqual(P.probe_target(-33.0, -33.0), -34.0)
        self.assertEqual(P.probe_target(-20.0, -33.0), -34.0)     # from the lower of the two
        self.assertEqual(P.probe_target(-33.4, -40.0), -41.0)
        self.assertEqual(P.probe_target(-33.2, -33.2), -34.5)     # -34.2 rounded down
        self.assertEqual(P.probe_target(-59.5, -60.0), -60.0)

    def test_followed_requires_movement(self):
        self.assertTrue(P.followed(-34, -33, -34))
        self.assertFalse(P.followed(-34, -33, -33))
        self.assertFalse(P.followed(-34, -34, -34))               # did not need to move: no evidence

    def test_led_step_stays_in_range(self):
        self.assertEqual(P.led_step(80), 79)
        self.assertEqual(P.led_step(0), 1)

    def test_raw_parsing(self):
        self.assertEqual(P.led_from_raw("raw=" + "00" * 37 + "50" + "00" * 90 + "\n"), 0x50)
        self.assertIsNone(P.led_from_raw("no raw line"))

    def test_verdicts(self):
        self.assertEqual(P.verdict({"P0": True}), "FU2_HEALTHY_AT_BOOT")
        self.assertEqual(P.verdict({"P0": False, "P1": False, "P2": False, "P3": False}), "NOT_RECOVERED")
        self.assertEqual(P.verdict({"P0": False}), "INCOMPLETE")


class Sequences(unittest.TestCase):
    def assert_never_louder(self, w, steps):
        for s in steps:
            self.assertLess(s["target_db"], min(s["alsa_before_db"], s["device_before_db"]) + 1e-9)
        for field, value in w.vendor_writes:
            if field == "speaker-volume":
                self.assertLess(float(value), -30.0 + 1e-9)

    def test_healthy_at_boot_stops_after_one_probe_without_any_vendor_write(self):
        w = World("healthy")
        v, steps, _ = run(w)
        self.assertEqual(v, "FU2_HEALTHY_AT_BOOT")
        self.assertEqual(len(steps), 1)
        self.assertEqual(w.vendor_writes, [])

    def test_recovery_with_time_is_not_attributed_to_a_vendor_write(self):
        w = World("time:200")
        v, steps, _ = run(w, wait=300)
        self.assertEqual(v, "RECOVERED_WITHOUT_VENDOR_WRITE")
        self.assertEqual(w.vendor_writes, [])
        self.assertEqual([s["followed"] for s in steps], [False, True])

    def test_any_vendor_settings_set_recovers_and_persists(self):
        w = World("vendor_any")
        v, steps, _ = run(w)
        self.assertEqual(v, "RECOVERED_AFTER_VENDOR_SETTINGS_SET_PERSISTENT")
        self.assertEqual(w.vendor_writes, [("led-brightness", "79"), ("led-brightness", "80")])   # A, then restore
        self.assertEqual([s["name"] for s in steps], ["P0", "P1", "P2", "P4"])
        self.assertEqual(w.led, 80)
        self.assert_never_louder(w, steps)

    def test_only_a_speaker_write_recovers(self):
        w = World("vendor_speaker")
        v, steps, _ = run(w)
        self.assertEqual(v, "RECOVERED_AFTER_SPEAKER_VENDOR_WRITE_PERSISTENT")
        self.assertEqual([f for f, _ in w.vendor_writes], ["led-brightness", "speaker-volume", "led-brightness"])
        self.assertEqual(w.led, 80)
        self.assert_never_louder(w, steps)

    def test_temporary_recovery_is_reported_as_not_persistent(self):
        w = World("vendor_any", revert_after=60)
        v, _, _ = run(w, persist=120)
        self.assertEqual(v, "RECOVERED_AFTER_VENDOR_SETTINGS_SET_NOT_PERSISTENT")

    def test_no_recovery_and_led_restored(self):
        w = World("never")
        v, steps, _ = run(w)
        self.assertEqual(v, "NOT_RECOVERED")
        self.assertEqual(w.led, 80)
        self.assertEqual(len(w.alsa_sets), 4)                      # P0..P3, no P4
        self.assert_never_louder(w, steps)

    def test_write_authorization_is_exactly_what_each_field_needs(self):
        w = World("vendor_speaker")
        run(w)
        led_env, speaker_env = w.envs[0], w.envs[1]
        self.assertEqual(led_env, {"LEWITT_ALLOW_HARDWARE_WRITE": "1"})
        self.assertEqual(speaker_env, {"LEWITT_ALLOW_HARDWARE_WRITE": "1", "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME": "1"})
        for c in w.calls:
            self.assertNotIn("--mock", c)

    def test_failed_read_aborts_and_still_restores_led(self):
        w = World("never")
        orig = w.run
        state = {"n": 0}

        def flaky(cmd, **kw):
            if cmd[0] != "amixer" and cmd[1] == "read":
                state["n"] += 1
                if state["n"] == 9:                                  # fails after intervention A
                    return subprocess.CompletedProcess(cmd, 1, "", "timeout")
            return orig(cmd, **kw)
        w.run = flaky
        with self.assertRaises(RuntimeError):
            run(w)
        self.assertEqual(w.led, 80)


if __name__ == "__main__":
    unittest.main()
