#!/usr/bin/env python3
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deferred_probe as D  # noqa: E402
import post_boot_health as H  # noqa: E402
import usb_ep0_trace as T  # noqa: E402

CONF = """timeout: 5
default_entry: 2

comment: machine-id=00000000000000000000000000000000
/+CachyOS
  //linux-cachyos
  ### auto-generated
  comment: Kernel version: 7.2.10-1-cachyos
  comment: kernel-id=linux-cachyos
  protocol: linux
  module_path: boot():/00/linux-cachyos/initramfs#abc
  path: boot():/00/linux-cachyos/vmlinuz#def
  cmdline: quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=00000000

  //linux-cachyos-lts
  protocol: linux
  module_path: boot():/00/lts/initramfs#abc
  path: boot():/00/lts/vmlinuz#def
  cmdline: quiet rw root=UUID=00000000

/EFI fallback
  protocol: efi
  path: boot():/EFI/BOOT/BOOTX64.EFI
"""


class EntryText(unittest.TestCase):
    def test_entry_adds_only_the_blacklist_argument(self):
        leaf = T.parse_leaf(CONF)
        new = D.add_entry(CONF)
        self.assertIn(leaf["cmdline"] + " " + D.BLACKLIST_ARG, new)
        for bad in ("ftrace", "trace_event", "kprobe", "dyndbg", "quirk", "module_blacklist="):
            self.assertNotIn(bad, new[len(CONF):])
        self.assertTrue(new.startswith(CONF))

    def test_remove_restores_bytes(self):
        self.assertEqual(D.remove_entry(D.add_entry(CONF)), CONF)

    def test_double_install_and_missing_remove_refused(self):
        with self.assertRaises(ValueError):
            D.add_entry(D.add_entry(CONF))
        with self.assertRaises(ValueError):
            D.remove_entry(CONF)

    def test_refuses_polluted_source(self):
        for bad in ("module_blacklist=snd_usb_audio", "modprobe.blacklist=x", "usbcore.quirks=1:2:g"):
            with self.assertRaises(ValueError):
                D.add_entry(CONF.replace("quiet nowatchdog", "quiet nowatchdog " + bad, 1))

    def test_default_entry_and_normal_entries_untouched(self):
        new = D.add_entry(CONF)
        self.assertIn("default_entry: 2", new)
        self.assertEqual(new.count("kernel-id=linux-cachyos"), 1)


class EntryFile(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.conf = os.path.join(self.d, "limine.conf")
        open(self.conf, "w").write(CONF)
        os.chmod(self.conf, 0o600)
        self.st = os.path.join(self.d, "state")

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def test_install_remove_verify_cycle(self):
        self.assertEqual(D.entry_cmd("install", self.conf, self.st, False), 0)
        self.assertTrue(D.has_entry(open(self.conf).read()))
        self.assertEqual(D.entry_cmd("remove", self.conf, self.st, False), 0)
        self.assertEqual(open(self.conf).read(), CONF)
        self.assertEqual(D.entry_cmd("verify", self.conf, self.st, False), 0)
        self.assertEqual(oct(os.stat(self.conf).st_mode & 0o777), "0o600")

    def test_verify_fails_if_conf_changed_meanwhile(self):
        D.entry_cmd("install", self.conf, self.st, False)
        D.entry_cmd("remove", self.conf, self.st, False)
        open(self.conf, "a").write("# changed\n")
        self.assertEqual(D.entry_cmd("verify", self.conf, self.st, False), 1)


GOOD_DEV = [{"port": "3-2", "devnum": "2", "busnum": "3", "controller_pci": "0000:0d:00.4"}]
GOOD_JOURNAL = "kernel: Linux version 7.2.10\nkernel: usb 3-2: New USB device found, idVendor=29c2, idProduct=0004\n"


def gate_args(**kw):
    a = dict(cmdline="quiet rw " + D.BLACKLIST_ARG, module_loaded=False, devices=GOOD_DEV, bound_interfaces=[],
             journal_status="OK", journal_text=GOOD_JOURNAL, blacklist_in_config=True)
    a.update(kw)
    return a


class Gate(unittest.TestCase):
    def test_ok(self):
        self.assertEqual(D.gate(**gate_args()), [])

    def test_each_invalidator(self):
        for k, v in (("cmdline", "quiet rw"), ("module_loaded", True), ("devices", []),
                     ("bound_interfaces", ["3-2:1.0"]),
                     ("journal_text", GOOD_JOURNAL + "usbcore: registered new interface driver snd-usb-audio\n"),
                     ("blacklist_in_config", False),
                     ("cmdline", "quiet module_blacklist=snd_usb_audio " + D.BLACKLIST_ARG)):
            self.assertTrue(D.gate(**gate_args(**{k: v})), k)

    def test_wrong_port_controller_or_ambiguity_is_invalid(self):
        wrong_port = [dict(GOOD_DEV[0], port="3-3")]
        wrong_ctl = [dict(GOOD_DEV[0], controller_pci="0000:0d:00.3")]
        no_ctl = [dict(GOOD_DEV[0], controller_pci=None)]
        for devs in (wrong_port, wrong_ctl, no_ctl, GOOD_DEV + GOOD_DEV):
            p = D.gate(**gate_args(devices=devs))
            self.assertTrue(p, devs)
        self.assertIn("expected 3-2", " ".join(D.gate(**gate_args(devices=wrong_port))))
        self.assertIn("ambiguous", " ".join(D.gate(**gate_args(devices=GOOD_DEV + GOOD_DEV))))

    def test_find_devices_reads_sysfs_layout(self):
        d = tempfile.mkdtemp()
        try:
            real = os.path.join(d, "pci0000:00", "0000:00:08.1", "0000:0d:00.4", "usb3", "3-2")
            os.makedirs(real)
            for n, v in (("idVendor", "29c2"), ("idProduct", "0004"), ("devnum", "2"), ("busnum", "3")):
                open(os.path.join(real, n), "w").write(v + "\n")
            base = os.path.join(d, "devices")
            os.makedirs(base)
            os.symlink(real, os.path.join(base, "3-2"))
            devs = D.find_devices(base)
            self.assertEqual(devs, [{"port": "3-2", "devnum": "2", "busnum": "3", "controller_pci": "0000:0d:00.4"}])
        finally:
            shutil.rmtree(d, ignore_errors=True)


class JournalFailClosed(unittest.TestCase):
    def test_statuses(self):
        j = D.judge_journal
        self.assertEqual(j(0, GOOD_JOURNAL, "")[0], "OK")
        self.assertEqual(j(None, "", "no such file")[0], "FAILED")
        self.assertEqual(j(1, "", "boom")[0], "FAILED")
        self.assertEqual(j(1, "", "Permission denied")[0], "UNREADABLE")
        self.assertEqual(j(0, "", "")[0], "EMPTY")
        self.assertEqual(j(0, "-- No entries --\n", "")[0], "EMPTY")
        # a non-member sees only user messages and exit code 0: no boot start line -> not trusted
        self.assertEqual(j(0, "Okt 24 systemd[1]: something\n", "")[0], "INCOMPLETE")
        self.assertEqual(j(0, "kernel: Linux version 7.2.10\n", "")[0], "INCOMPLETE")

    def test_gate_refuses_unless_journal_ok(self):
        for st in ("FAILED", "UNREADABLE", "EMPTY", "INCOMPLETE"):
            p = D.gate(**gate_args(journal_status=st, journal_text=""))
            self.assertTrue(any("journal evidence unavailable" in x for x in p), st)

    def test_empty_text_is_never_proof_of_non_registration(self):
        self.assertTrue(D.gate(**gate_args(journal_status="EMPTY", journal_text="")))

    def test_sudo_fallback_then_fail(self):
        calls = []

        def runner(cmd, timeout=20):
            calls.append(cmd[0])
            return (1, "", "Permission denied") if cmd[0] == "journalctl" else (0, GOOD_JOURNAL, "")
        st, _, text = D.read_kernel_journal(runner=runner)
        self.assertEqual((st, calls), ("OK", ["journalctl", "sudo"]))
        st, detail, text = D.read_kernel_journal(runner=lambda c, timeout=20: (1, "", "Permission denied"))
        self.assertEqual((st, text), ("UNREADABLE", ""))


class Classify(unittest.TestCase):
    def f(self, fu2):
        return {"fu2_sticky": fu2, "unit5_sticky": True}

    def test_matrix(self):
        self.assertEqual(D.classify(True, 0, self.f(False), True, True), "POSITIVE")
        self.assertEqual(D.classify(True, 0, self.f(True), False, False), "NEGATIVE")
        self.assertEqual(D.classify(False, 0, self.f(False), True, True), "INVALID")
        self.assertEqual(D.classify(True, 1, self.f(False), True, True), "INCONCLUSIVE")
        self.assertEqual(D.classify(True, 0, self.f(False), True, False), "INCONCLUSIVE")
        self.assertEqual(D.classify(True, 0, self.f(False), True, True, "UNREADABLE"), "INCONCLUSIVE")


class HardwareSnapshot(unittest.TestCase):
    REAL = ("microphone: gain=27.00 dB mute=1 lowcut=1 clipguard=1 preamp=2 denoise=0 comp=1\n"
            "instrument: gain=14.00 dB mute=1 lowcut=0 clipguard=0 preamp=2 denoise=0 comp=0\n"
            "phantom=1  speaker=-23.50 dB  headphone=-32.40 dB (muted)  dim=0 mono=0 mix=0.00 monitor=2 button=2\n"
            "raw=001b01\n")

    def test_real_cli_format(self):
        h = H.parse_hw_snapshot(self.REAL)
        self.assertTrue(h["complete"])
        self.assertEqual((h["speaker_db"], h["speaker_muted"], h["headphone_db"], h["headphone_muted"]),
                         (-23.5, False, -32.4, True))
        self.assertEqual(len(h["lines"]), 3)
        self.assertFalse(any(l.startswith("raw=") for l in h["lines"]))

    def test_speaker_muted_and_headphone_unmuted(self):
        h = H.parse_hw_snapshot("phantom=0  speaker=-33.34 dB (muted)  headphone=-10.00 dB  dim=0 mono=0 mix=0.00 monitor=2 button=2\n")
        self.assertEqual((h["speaker_muted"], h["headphone_muted"]), (True, False))

    def test_missing_or_garbled_is_incomplete(self):
        for t in ("", "microphone: gain=1\n", "phantom=1 speaker=oops\n", None):
            self.assertFalse(H.parse_hw_snapshot(t)["complete"], t)


class RunDirHandoff(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.home = os.path.join(self.d, "home")
        os.makedirs(self.home, mode=0o700)
        self.uid = os.getuid()
        self.run = self._prepare()

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def _prepare(self):
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            D.cmd_prepare(home=self.home)
        return buf.getvalue().strip()

    def test_prepare_is_private_and_user_owned(self):
        st = os.stat(self.run)
        self.assertEqual((st.st_uid, st.st_mode & 0o777), (self.uid, 0o700))
        self.assertEqual(oct(os.stat(os.path.join(self.run, "experiment.json")).st_mode & 0o777), "0o600")

    def test_prepare_refuses_root(self):
        with self.assertRaises(SystemExit):
            D.cmd_prepare(home=self.home, euid=0)

    def test_user_and_root_resolve_the_same_directory(self):
        fd_u, uid_u, info_u = D.open_run_dir(self.run)
        fd_r, uid_r, info_r = D.open_run_dir(self.run, euid=0, sudo_uid=str(self.uid))
        try:
            self.assertEqual((uid_u, info_u["dir"]), (uid_r, info_r["dir"]))
            self.assertEqual(os.fstat(fd_u).st_ino, os.fstat(fd_r).st_ino)
        finally:
            os.close(fd_u)
            os.close(fd_r)

    def test_rejects_symlink_wrong_mode_wrong_sudo_uid_and_relative(self):
        link = os.path.join(self.d, "link")
        os.symlink(self.run, link)
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(link)
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(os.path.relpath(self.run))
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(self.run + "/../" + os.path.basename(self.run))
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(self.run, euid=0, sudo_uid=str(self.uid + 1))
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(self.run, euid=0, sudo_uid=None)
        os.chmod(self.run, 0o750)
        with self.assertRaises(D.RunDirError):
            D.open_run_dir(self.run)

    def test_symlinked_file_inside_is_not_followed(self):
        fd, uid, _ = D.open_run_dir(self.run)
        try:
            victim = os.path.join(self.d, "victim")
            open(victim, "w").write("keep")
            os.symlink(victim, os.path.join(self.run, "load.json"))
            with self.assertRaises(OSError):
                D.write_json(fd, "load.json", {"x": 1}, uid, replace=False)
            with self.assertRaises(D.RunDirError):
                D.read_json(fd, "load.json", uid)
            self.assertEqual(open(victim).read(), "keep")
        finally:
            os.close(fd)

    def test_root_writes_are_chowned_to_the_user(self):
        calls = []
        real_fchown, real_geteuid = os.fchown, os.geteuid
        os.fchown = lambda fd, u, g: calls.append((u, g))
        os.geteuid = lambda: 0
        try:
            fd, uid, _ = D.open_run_dir(self.run, euid=0, sudo_uid=str(self.uid))
            D.write_json(fd, "load.json", {"rc": 0}, uid, replace=False)
            os.close(fd)
        finally:
            os.fchown, os.geteuid = real_fchown, real_geteuid
        self.assertEqual(calls[0][0], self.uid)
        self.assertEqual(oct(os.stat(os.path.join(self.run, "load.json")).st_mode & 0o777), "0o600")


class LoadAndClassifyFlow(unittest.TestCase):
    """check (user) -> load (root, simulated) -> classify (user) all on one run directory."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.home = os.path.join(self.d, "home")
        os.makedirs(self.home, mode=0o700)
        self.uid = os.getuid()
        import io
        import contextlib
        with contextlib.redirect_stdout(io.StringIO()) as b:
            D.cmd_prepare(home=self.home)
        self.run = b.getvalue().strip()
        self.marker_dir = os.path.join(self.d, "state")
        self._gate = D.run_gate
        self.gate_ok = ([], {"journal_status": "OK", "journal_detail": "", "devices": GOOD_DEV, "text": GOOD_JOURNAL})
        D.run_gate = lambda runner=D.default_runner: self.gate_ok

    def tearDown(self):
        D.run_gate = self._gate
        shutil.rmtree(self.d, ignore_errors=True)

    def check(self):
        import io
        import contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            return D.cmd_check(self.run)

    def load(self, confirmed=True, rc=0, modprobe_calls=None):
        import io
        import contextlib
        calls = modprobe_calls if modprobe_calls is not None else []

        def fake():
            calls.append(1)
            return rc, ""
        with contextlib.redirect_stdout(io.StringIO()):
            return D.cmd_load(self.run, confirmed, marker_dir=self.marker_dir, euid=0, sudo_uid=str(self.uid), modprobe=fake)

    def test_requires_flag_and_checkpoint(self):
        with self.assertRaises(SystemExit):
            self.load(confirmed=False)
        with self.assertRaises(SystemExit):
            self.load()  # no checkpoint yet

    def test_needs_root(self):
        with self.assertRaises(SystemExit):
            D.cmd_load(self.run, True, euid=1000)

    def test_failed_checkpoint_or_gate_does_not_load(self):
        self.gate_ok = (["module already loaded"], self.gate_ok[1])
        self.assertEqual(self.check(), 1)
        calls = []
        with self.assertRaises(SystemExit):
            self.load(modprobe_calls=calls)
        self.assertEqual(calls, [])
        self.gate_ok = ([], self.gate_ok[1])
        self.check()
        self.gate_ok = (["blacklist ineffective"], self.gate_ok[1])
        self.assertEqual(self.load(modprobe_calls=calls), 1)  # fresh gate at load time fails closed
        self.assertEqual(calls, [])
        self.assertFalse(os.path.exists(os.path.join(self.marker_dir, "load_done")))

    def test_checkpoint_from_another_boot_or_stale_is_refused(self):
        self.check()
        cp = os.path.join(self.run, "checkpoint.json")
        import json
        data = json.load(open(cp))
        data["boot_id"] = "other"
        json.dump(data, open(cp, "w"))
        with self.assertRaises(SystemExit):
            self.load()
        data["boot_id"] = D.boot_id()
        data["time"]["wall_epoch"] -= D.CHECKPOINT_MAX_AGE_S + 5
        json.dump(data, open(cp, "w"))
        with self.assertRaises(SystemExit):
            self.load()

    def test_single_load_and_shared_report(self):
        self.check()
        calls = []
        self.assertEqual(self.load(modprobe_calls=calls), 0)
        self.assertEqual(len(calls), 1)
        with self.assertRaises(SystemExit):
            self.load(modprobe_calls=calls)
        self.assertEqual(len(calls), 1)
        self.assertTrue(os.path.exists(os.path.join(self.marker_dir, "load_done")))
        # the user-context classifier reads load.json + checkpoint.json from the same directory
        import io
        import contextlib
        D.read_kernel_journal = lambda **kw: ("OK", "", "")
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(D.cmd_classify(self.run, None, runner=lambda c, timeout=20: (1, "", ""), euid=1000), 0)
        finally:
            import importlib
            importlib.reload(D)
        self.assertTrue(os.path.exists(os.path.join(self.run, "classification.json")))

    def test_failed_modprobe_is_inconclusive_and_marker_stays(self):
        self.check()
        self.assertEqual(self.load(rc=1), 1)
        with self.assertRaises(SystemExit):
            self.load()

    def test_classify_refuses_root(self):
        with self.assertRaises(SystemExit):
            D.cmd_classify(self.run, None, euid=0)


if __name__ == "__main__":
    unittest.main()
