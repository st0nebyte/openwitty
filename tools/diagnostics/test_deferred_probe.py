#!/usr/bin/env python3
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deferred_probe as D  # noqa: E402
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


class Gate(unittest.TestCase):
    OK = dict(cmdline="quiet rw " + D.BLACKLIST_ARG, module_loaded=False, device_present=True,
              bound_interfaces=[], driver_registered_in_log=False, blacklist_in_config=True)

    def test_ok(self):
        self.assertEqual(D.gate(**self.OK), [])

    def test_each_invalidator(self):
        for k, v in (("cmdline", "quiet rw"), ("module_loaded", True), ("device_present", False),
                     ("bound_interfaces", ["3-2:1.0"]), ("driver_registered_in_log", True),
                     ("blacklist_in_config", False), ("cmdline", "quiet module_blacklist=snd_usb_audio " + D.BLACKLIST_ARG)):
            self.assertTrue(D.gate(**dict(self.OK, **{k: v})), k)


class Classify(unittest.TestCase):
    def f(self, fu2):
        return {"fu2_sticky": fu2, "unit5_sticky": True}

    def test_matrix(self):
        self.assertEqual(D.classify(True, 0, self.f(False), True, True), "POSITIVE")
        self.assertEqual(D.classify(True, 0, self.f(True), False, False), "NEGATIVE")
        self.assertEqual(D.classify(False, 0, self.f(False), True, True), "INVALID")
        self.assertEqual(D.classify(True, 1, self.f(False), True, True), "INCONCLUSIVE")
        self.assertEqual(D.classify(True, 0, self.f(False), True, False), "INCONCLUSIVE")  # unit5 alone / mixed


class LoadGuards(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self._st, self._root, self._gate = D.STATE_DIR, T._need_root, D.run_gate
        D.STATE_DIR = self.d
        T._need_root = lambda required=True: None

    def tearDown(self):
        D.STATE_DIR, T._need_root, D.run_gate = self._st, self._root, self._gate
        shutil.rmtree(self.d, ignore_errors=True)

    def test_requires_explicit_safety_flag(self):
        with self.assertRaises(SystemExit):
            D.cmd_load(self.d, False)

    def test_failed_gate_does_not_load(self):
        D.run_gate = lambda: (["module already loaded"], "")
        self.assertEqual(D.cmd_load(self.d, True), 1)
        self.assertFalse(os.path.exists(os.path.join(self.d, "load_done")))

    def test_second_run_refused(self):
        open(os.path.join(self.d, "load_done"), "w").write("x")
        with self.assertRaises(SystemExit):
            D.cmd_load(self.d, True)


if __name__ == "__main__":
    unittest.main()
