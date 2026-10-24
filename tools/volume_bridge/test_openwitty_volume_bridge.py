#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import openwitty_volume_bridge as B  # noqa: E402

SGET_ON = """Simple mixer control 'PCM',0
  Capabilities: pvolume pvolume-joined pswitch pswitch-joined
  Playback channels: Mono
  Limits: Playback 0 - 120
  Mono: Playback 73 [61%] [-23.50dB] [on]
"""


class Parse(unittest.TestCase):
    def test_monitor_lines(self):
        self.assertEqual(B.parse_monitor_line("node hw:3, #4 (2,0,0,PCM Playback Volume,0) VALUE\n"), "PCM Playback Volume")
        self.assertEqual(B.parse_monitor_line("node hw:3, #3 (2,0,0,PCM Playback Switch,0) VALUE"), "PCM Playback Switch")
        self.assertIsNone(B.parse_monitor_line("node hw:3, #7 (2,0,0,Mic Capture Volume,0) VALUE"))
        self.assertIsNone(B.parse_monitor_line("garbage"))

    def test_sget(self):
        self.assertEqual(B.parse_sget(SGET_ON), (-23.5, False))
        self.assertEqual(B.parse_sget(SGET_ON.replace("[on]", "[off]")), (-23.5, True))
        self.assertEqual(B.parse_sget("  Mono: Playback 120 [100%] [0.00dB]"), (0.0, None))
        self.assertIsNone(B.parse_sget("amixer: Unable to find simple control 'PCM',0"))

    def test_quantize(self):
        self.assertEqual(B.quantize_db(-23.5), -23.5)
        self.assertEqual(B.quantize_db(-23.74), -23.5)
        self.assertEqual(B.quantize_db(-23.76), -24.0)
        self.assertEqual(B.quantize_db(-80), -60.0)
        self.assertEqual(B.quantize_db(3), 0.0)


class Plan(unittest.TestCase):
    def test_first_sync_writes_volume_and_mute(self):
        self.assertEqual(B.plan_writes((-23.5, False), None), [("speaker-volume", "-23.5"), ("speaker-mute", "off")])

    def test_only_changes_are_written(self):
        self.assertEqual(B.plan_writes((-23.5, False), (-23.5, False)), [])
        self.assertEqual(B.plan_writes((-20.0, False), (-23.5, False)), [("speaker-volume", "-20.0")])
        self.assertEqual(B.plan_writes((-23.5, True), (-23.5, False)), [("speaker-mute", "on")])
        self.assertEqual(B.plan_writes((-23.5, None), (-23.5, False)), [])
        self.assertEqual(B.plan_writes(None, None), [])


class FindCard(unittest.TestCase):
    def test_only_the_connect2_card(self):
        d = tempfile.mkdtemp()
        try:
            for n, uid in (("card1", None), ("card2", "1234:5678"), ("card3", "29c2:0004")):
                os.makedirs(os.path.join(d, n))
                if uid:
                    open(os.path.join(d, n, "usbid"), "w").write(uid + "\n")
            os.makedirs(os.path.join(d, "cards"))
            self.assertEqual(B.find_card(d), 3)
            os.unlink(os.path.join(d, "card3", "usbid"))
            self.assertIsNone(B.find_card(d))
        finally:
            shutil.rmtree(d, ignore_errors=True)


class FakeRun:
    def __init__(self, sget, cli_rc=0):
        self.sget, self.cli_rc, self.calls = sget, cli_rc, []

    def __call__(self, cmd, **kw):
        self.calls.append(cmd)
        if cmd[0] == "amixer":
            return subprocess.CompletedProcess(cmd, 0, self.sget, "")
        assert kw["env"]["LEWITT_ALLOW_HARDWARE_WRITE"] == "1"
        return subprocess.CompletedProcess(cmd, self.cli_rc, "write state: verified", "")


class FakePopen:
    def __init__(self, lines):
        self.stdout = iter(lines)

    def kill(self):
        pass

    def wait(self):
        pass


class BridgeFlow(unittest.TestCase):
    def mk(self, run, lines=()):
        return B.Bridge("/x/openwitty-cli", run=run, popen=lambda *a, **k: FakePopen(lines), sleep=lambda s: None,
                        log=lambda m: None)

    def writes(self, run):
        return [c[2:4] for c in run.calls if c[0] != "amixer"]

    def test_card_appearance_syncs_then_follows_events(self):
        run = FakeRun(SGET_ON)
        b = self.mk(run, ["node hw:3, #4 (2,0,0,PCM Playback Volume,0) VALUE\n",
                          "node hw:3, #7 (2,0,0,Mic Capture Volume,0) VALUE\n"])
        b.run_card(3)
        self.assertEqual(self.writes(run), [["speaker-volume", "-23.5"], ["speaker-mute", "off"]])  # event: no change
        run.sget = SGET_ON.replace("-23.50dB", "-30.00dB")
        b.sync(3)
        self.assertEqual(self.writes(run)[-1], ["speaker-volume", "-30.0"])

    def test_failed_write_is_retried_on_next_event(self):
        run = FakeRun(SGET_ON, cli_rc=1)
        b = self.mk(run)
        b.sync(3)
        self.assertIsNone(b.applied)
        run.cli_rc = 0
        b.sync(3)
        self.assertEqual(b.applied, (-23.5, False))

    def test_reappearing_card_pushes_state_again(self):
        run = FakeRun(SGET_ON)
        b = self.mk(run)
        b.run_card(3)
        n = len(self.writes(run))
        b.run_card(3)
        self.assertEqual(len(self.writes(run)), 2 * n)

    def test_cli_is_only_used_for_the_two_speaker_fields(self):
        run = FakeRun(SGET_ON.replace("[on]", "[off]"))
        b = self.mk(run)
        b.sync(3)
        self.assertTrue(all(c[1] == "write" and c[2] in ("speaker-volume", "speaker-mute") and c[-1] == "--execute"
                            for c in run.calls if c[0] != "amixer"))


if __name__ == "__main__":
    unittest.main()
