#!/usr/bin/env python3
"""Offline tests for usb_ep0_live.py: fake tracefs/sysfs in a temp dir, real fork/signal/kill paths."""
import json
import os
import shutil
import signal
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402
import usb_ep0_live as L  # noqa: E402

TRACE_TEXT = "# tracer: nop\n# entries-in-buffer/entries-written: 3/3   #P:2\n#\n"


def fake_command(path, text):
    cur = open(path).read().splitlines()
    if text.startswith("-:"):
        name = text[2:].strip()
        cur = [l for l in cur if " " + name + " " not in " " + l + " " and not l.split(" ")[0].endswith(name)]
    else:
        cur.append(text.strip())
    open(path, "w").write("".join(l + "\n" for l in cur))


def make_tracefs(root):
    os.makedirs(os.path.join(root, "instances"))
    for n, v in (("buffer_size_kb", "1410\n"), ("tracing_on", "1\n"), ("current_tracer", "nop\n"),
                 ("trace_clock", "[local] global\n"), ("set_event", "\n"), ("kprobe_events", "\n")):
        open(os.path.join(root, n), "w").write(v)


def populate(inst):
    for d in [os.path.join(inst, "events", T.GROUP)] + [os.path.join(inst, "events", "xhci-hcd", e) for e in T.XHCI_EVENTS]:
        os.makedirs(d)
        open(os.path.join(d, "enable"), "w").write("0\n")
    open(os.path.join(inst, "tracing_on"), "w").write("0\n")
    open(os.path.join(inst, "buffer_size_kb"), "w").write("7\n")
    open(os.path.join(inst, "trace"), "w").write(TRACE_TEXT)
    os.makedirs(os.path.join(inst, "per_cpu", "cpu0"))
    open(os.path.join(inst, "per_cpu", "cpu0", "stats"), "w").write(
        "entries: 3\noverrun: 0\ncommit overrun: 0\nbytes: 100\noldest event ts: 1.5\n")


def make_sysfs(root, present):
    d = os.path.join(root, "3-1")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "idVendor"), "w").write("29c2\n")
    open(os.path.join(d, "idProduct"), "w").write("0004\n")
    if not present:
        shutil.rmtree(d)


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.tf = os.path.join(self.tmp, "tracefs")
        self.sf = os.path.join(self.tmp, "sysfs")
        os.makedirs(self.sf)
        make_tracefs(self.tf)
        self.out = os.path.join(self.tmp, "out")
        self._cmd = T.tracefs_command
        T.tracefs_command = fake_command
        self.base = T.tracefs_baseline(self.tf)

    def tearDown(self):
        T.tracefs_command = self._cmd
        shutil.rmtree(self.tmp, ignore_errors=True)

    def session(self, **kw):
        kw.setdefault("announce", lambda *_: None)
        return L.live_session(self.out, tracefs=self.tf, sysfs=self.sf, require_root=False, populate=populate,
                              rmdir=shutil.rmtree, **kw)

    def assert_clean(self):
        self.assertEqual(T.verify_clean(self.tf, self.base), [])
        self.assertFalse(os.path.exists(L.instance_dir(self.tf)))


class TestPresence(Base):
    def test_present_absent(self):
        self.assertFalse(L.target_present(self.sf))
        make_sysfs(self.sf, True)
        self.assertTrue(L.target_present(self.sf))
        self.assertFalse(L.target_present(self.sf, pid="0005"))


class TestStateMachine(Base):
    def test_reconnect_sequence(self):
        make_sysfs(self.sf, True)
        t = {"now": 0.0}
        steps = []

        def sleep(d):
            t["now"] += 1.0
            if t["now"] == 3:
                shutil.rmtree(os.path.join(self.sf, "3-1"))
            if t["now"] == 6:
                make_sysfs(self.sf, True)

        code, rep = self.session(deadline=100, settle=10, clock=lambda: t["now"], sleep=sleep, use_watchdog=False)
        self.assertEqual(code, 0)
        self.assertTrue(rep["disconnect_seen"] and rep["reconnect_seen"])
        self.assertEqual(rep["stop_reason"], "settled after reconnect")
        self.assertTrue(rep["t_disconnect"] < rep["t_reconnect"] < rep["t_stop"])
        self.assertEqual(rep["entries_written"], 3)
        self.assertEqual(rep["cpu_overrun_total"], 0)
        self.assertEqual(oct(os.stat(os.path.join(self.out, "raw_trace.txt")).st_mode & 0o777), "0o600")
        self.assert_clean()

    def test_deadline_no_disconnect(self):
        make_sysfs(self.sf, True)
        t = {"now": 0.0}
        def sleep(d):
            t["now"] += 5.0
        code, rep = self.session(deadline=20, clock=lambda: t["now"], sleep=sleep, use_watchdog=False)
        self.assertEqual(code, 0)
        self.assertFalse(rep["disconnect_seen"])
        self.assertIn("no disconnect", rep["stop_reason"])
        self.assert_clean()

    def test_deadline_never_returned(self):
        make_sysfs(self.sf, True)
        t = {"now": 0.0}
        def sleep(d):
            t["now"] += 5.0
            if t["now"] == 10:
                shutil.rmtree(os.path.join(self.sf, "3-1"))
        code, rep = self.session(deadline=40, clock=lambda: t["now"], sleep=sleep, use_watchdog=False)
        self.assertTrue(rep["disconnect_seen"] and not rep["reconnect_seen"])
        self.assertIn("never returned", rep["stop_reason"])
        self.assert_clean()

    def test_error_path_cleans(self):
        def bad(inst):
            populate(inst)
            os.remove(os.path.join(inst, "tracing_on"))
            os.makedirs(os.path.join(inst, "tracing_on"))  # write will fail
        code, rep = L.live_session(self.out, tracefs=self.tf, sysfs=self.sf, require_root=False, populate=bad,
                                   rmdir=shutil.rmtree, use_watchdog=False, announce=lambda *_: None)
        self.assertEqual(code, 2)
        self.assertIn("error", rep)
        self.assert_clean()

    def test_stale_instance_refused(self):
        os.makedirs(L.instance_dir(self.tf))
        with self.assertRaises(SystemExit):
            self.session(use_watchdog=False)

    def test_live_cleanup_idempotent(self):
        problems, _ = L.live_cleanup(self.tf, self.base, rmdir=shutil.rmtree)
        self.assertEqual(problems, [])
        problems, _ = L.live_cleanup(self.tf, self.base, rmdir=shutil.rmtree)
        self.assertEqual(problems, [])


class TestKillPaths(Base):
    """Real processes: the session runs in a forked child and is signalled from outside."""

    def spawn(self, **kw):
        make_sysfs(self.sf, True)
        r, w = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(r)
            try:
                code, _ = self.session(**kw)
                os.write(w, b"done")
            finally:
                os._exit(0)
        os.close(w)
        # wait until armed
        for _ in range(100):
            if os.path.exists(os.path.join(L.instance_dir(self.tf), "tracing_on")) and \
               open(os.path.join(L.instance_dir(self.tf), "tracing_on")).read().strip() == "1":
                break
            time.sleep(0.05)
        else:
            self.fail("never armed")
        return pid

    def wait_clean(self, secs=10):
        end = time.time() + secs
        while time.time() < end:
            if not os.path.exists(L.instance_dir(self.tf)):
                return True
            time.sleep(0.1)
        return False

    def test_sigterm(self):
        pid = self.spawn(deadline=60, settle=10, poll=0.05)
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
        self.assertTrue(self.wait_clean(2))
        rep = json.load(open(os.path.join(self.out, "live_report.json")))
        self.assertTrue(rep["stop_reason"].startswith("signal"))
        self.assertEqual(rep["cleanup_problems"], [])
        self.assertTrue(os.path.exists(os.path.join(self.out, "raw_trace.txt")))
        self.assert_clean()

    def test_sigkill_parent_watchdog_cleans(self):
        pid = self.spawn(deadline=60, settle=10, poll=0.05)
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        self.assertTrue(self.wait_clean(), "watchdog did not clean up")
        self.assertTrue(os.path.exists(os.path.join(self.out, "raw_trace_recovered.txt")))
        self.assertEqual(T.verify_clean(self.tf, self.base), [])

    def test_hung_parent_hard_deadline(self):
        pid = self.spawn(deadline=1, hard_extra=1, poll=0.05, sleep=lambda d: time.sleep(300))
        os.waitpid(pid, 0)  # watchdog must stop it
        self.assertTrue(self.wait_clean())
        self.assertEqual(T.verify_clean(self.tf, self.base), [])


if __name__ == "__main__":
    unittest.main()
