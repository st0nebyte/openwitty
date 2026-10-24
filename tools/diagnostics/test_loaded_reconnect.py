#!/usr/bin/env python3
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deferred_probe as D  # noqa: E402
import loaded_reconnect as L  # noqa: E402
import post_boot_health as H  # noqa: E402

OK_DRV = {"1.0": "snd-usb-audio", "1.1": "snd-usb-audio", "1.2": "snd-usb-audio", "1.3": None, "1.4": None}
PART_DRV = {"1.0": "snd-usb-audio", "1.1": None, "1.2": None, "1.3": None, "1.4": None}
DEV2 = [{"port": "3-2", "devnum": "2", "busnum": "3", "controller_pci": "0000:0d:00.4"}]
DEV3 = [{"port": "3-2", "devnum": "3", "busnum": "3", "controller_pci": "0000:0d:00.4"}]

FAIL = ("kernel: usb 3-2: 2:0: broken mixer GET_CUR (-15360/0/128 => -6016)\n"
        "kernel: usb 3-2: 5:0: broken mixer GET_CUR (0/18432/256 => 6912)\n"
        "kernel: usbcore: registered new interface driver snd-usb-audio\n"
        "kernel: usb 3-2: 2:0: usb_set_interface failed (-110)\n")
BOOT = ("Okt 24 18:57:05 h kernel: Linux version 7.3.0-5-cachyos\n"
        "Okt 24 18:57:05.7 h kernel: usb 3-2: new high-speed USB device number 2 using xhci_hcd\n"
        "Okt 24 18:57:05.7 h kernel: usb 3-2: New USB device found, idVendor=29c2, idProduct=0004, bcdDevice= 1.21\n")
RECON = ("Okt 24 19:04:31 h kernel: usb 3-2: USB disconnect, device number 2\n"
         "Okt 24 19:04:34.1 h kernel: usb 3-2: new high-speed USB device number 3 using xhci_hcd\n"
         "Okt 24 19:04:34.1 h kernel: usb 3-2: New USB device found, idVendor=29c2, idProduct=0004, bcdDevice= 1.21\n")
RECON2 = RECON.replace("19:04", "19:20").replace("number 2", "number 3", 1).replace("number 3 using", "number 4 using", 1)
PRE_TEXT = BOOT + FAIL
NOROUTE = H.pipewire_connect2([{"id": 53, "type": "PipeWire:Interface:Device", "info": {
    "props": {"device.vendor.id": "0x29c2", "device.product.id": "0x0004"}, "params": {"Route": [], "EnumRoute": []}}}])
HEALTHY = H.pipewire_connect2([{"id": 53, "type": "PipeWire:Interface:Device", "info": {
    "props": {"device.vendor.id": "0x29c2", "device.product.id": "0x0004"},
    "params": {"Route": [{"name": "o", "direction": "Output", "info": [1, "route.hw-volume", True]}], "EnumRoute": [{}]}}}])
FAIL_AFTER = ("kernel: usb 3-2: 2:0: broken mixer GET_CUR (-15360/0/128 => -6016)\n"
              "kernel: usb 3-2: 2:0: usb_set_interface failed (-110)\n")   # no second driver registration: it stays loaded
F_FAIL = H.parse_kernel_log(FAIL)
F_UNIT5 = H.parse_kernel_log("kernel: usb 3-2: 5:0: broken mixer GET_CUR (0/18432/256 => 6912)\n")
F_CLEAN = H.parse_kernel_log("")


class Drivers(unittest.TestCase):
    def test_interface_drivers_from_fake_sysfs(self):
        d = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(d, "drv", "snd-usb-audio"))
            for n, bound in (("3-2:1.0", True), ("3-2:1.1", True), ("3-2:1.2", True), ("3-2:1.3", False), ("3-3:1.0", True)):
                os.makedirs(os.path.join(d, n))
                if bound:
                    os.symlink(os.path.join(d, "drv", "snd-usb-audio"), os.path.join(d, n, "driver"))
            got = L.interface_drivers("3-2", d)
            self.assertEqual(got, {"1.0": "snd-usb-audio", "1.1": "snd-usb-audio", "1.2": "snd-usb-audio", "1.3": None})
            self.assertTrue(L.audio_bound(got))
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_audio_bound_requires_exactly_the_three_audio_interfaces(self):
        self.assertTrue(L.audio_bound(OK_DRV))
        self.assertTrue(L.audio_bound({"1.0": "snd-usb-audio", "1.1": "snd-usb-audio", "1.2": "snd-usb-audio"}))
        self.assertTrue(L.audio_bound(dict(OK_DRV, **{"1.3": "usbhid", "1.4": "snd-usb-audio"})))   # vendor interfaces irrelevant
        self.assertFalse(L.audio_bound(PART_DRV))
        self.assertFalse(L.audio_bound({}))
        for missing in ("1.0", "1.1", "1.2"):                       # a missing interface is never "bound"
            d = dict(OK_DRV)
            del d[missing]
            self.assertFalse(L.audio_bound(d), missing)
        for unbound in ("1.0", "1.1", "1.2"):
            self.assertFalse(L.audio_bound(dict(OK_DRV, **{unbound: None})), unbound)
            self.assertFalse(L.audio_bound(dict(OK_DRV, **{unbound: "usbhid"})), unbound)
        self.assertFalse(L.audio_bound({"1.0": "snd-usb-audio"}))
        self.assertFalse(L.audio_bound({"2.0": "snd-usb-audio", "2.1": "snd-usb-audio", "2.2": "snd-usb-audio"}))


class GatePre(unittest.TestCase):
    def gate(self, **kw):
        a = dict(kernel=L.EXPECTED_KERNEL, devices=DEV2, drivers=OK_DRV, module_loaded=True, jstatus="OK", jtext=PRE_TEXT,
                 findings=F_FAIL, pw=NOROUTE, boot="d396d14b-xxxx", expect_boot_prefix="d396d14b")
        a.update(kw)
        return L.gate_pre(a["kernel"], a["devices"], a["drivers"], a["module_loaded"], a["jstatus"], a["jtext"],
                          a["findings"], a["pw"], a["boot"], a["expect_boot_prefix"])

    def test_good(self):
        self.assertEqual(self.gate(), [])

    def test_each_failure(self):
        for kw in ({"kernel": "7.2.10-1-cachyos"}, {"boot": "ffffffff-x"}, {"module_loaded": False},
                   {"jstatus": "EMPTY"}, {"jtext": BOOT}, {"devices": []}, {"devices": DEV2 + DEV3},
                   {"devices": [dict(DEV2[0], port="3-3")]}, {"devices": [dict(DEV2[0], controller_pci="0000:00:00.0")]},
                   {"drivers": PART_DRV}, {"findings": F_CLEAN}, {"pw": HEALTHY}, {"findings": F_UNIT5},
                   {"devices": DEV3},                              # sysfs number 3 but journal's last enumeration is 2
                   {"drivers": {k: v for k, v in OK_DRV.items() if k != "1.2"}}):
            self.assertTrue(self.gate(**kw), kw)

    def test_boot_prefix_optional(self):
        self.assertEqual(self.gate(boot="anything", expect_boot_prefix=None), [])


class Delta(unittest.TestCase):
    before = L.journal_events(PRE_TEXT + RECON)  # 8A.25-like: boot journal already holds one earlier disconnect

    def test_exactly_one_new_cycle(self):
        after = L.journal_events(PRE_TEXT + RECON + RECON2)
        self.assertEqual(L.delta_problems(self.before, after, "3", "4", "4"), [])

    def test_no_new_cycle_two_cycles_unchanged_number_reload(self):
        none = L.journal_events(PRE_TEXT + RECON)
        self.assertTrue(L.delta_problems(self.before, none, "3", "4", "4"))
        two = L.journal_events(PRE_TEXT + RECON + RECON2 + RECON2.replace("19:20", "19:25"))
        self.assertTrue(L.delta_problems(self.before, two, "3", "5", "5"))
        one = L.journal_events(PRE_TEXT + RECON + RECON2)
        self.assertTrue(L.delta_problems(self.before, one, "3", "3", "3"))
        self.assertTrue(L.delta_problems(self.before, one, "3", None, "4"))
        reload_ = L.journal_events(PRE_TEXT + RECON + RECON2 + "kernel: usbcore: registered new interface driver snd-usb-audio\n")
        self.assertTrue(any("registration" in p for p in L.delta_problems(self.before, reload_, "3", "4", "4")))

    def test_inconsistent_device_numbers(self):
        one = L.journal_events(PRE_TEXT + RECON + RECON2)
        self.assertTrue(any("sysfs" in p for p in L.delta_problems(self.before, one, "3", "4", "5")))      # sysfs differs
        self.assertTrue(any("sysfs" in p for p in L.delta_problems(self.before, one, "3", "4", None)))
        self.assertTrue(any("observer" in p for p in L.delta_problems(self.before, one, "3", "5", "5")))  # journal says 4
        # journal sequence that is not baseline + one number
        odd = L.journal_events(PRE_TEXT + RECON2)
        self.assertTrue(any("sequence" in p for p in L.delta_problems(self.before, odd, "3", "4", "4")))


class Verdict(unittest.TestCase):
    def v(self, f, pcm, pw, drivers=OK_DRV, age=300, post_ok=True, jok=True):
        return L.verdict(post_ok, f, pcm, pw, drivers, age, jok)[0]

    def test_positive_requires_route(self):
        self.assertEqual(self.v(F_CLEAN, True, HEALTHY), "POSITIVE")
        self.assertEqual(self.v(F_CLEAN, True, NOROUTE), "INCONCLUSIVE")
        self.assertEqual(self.v(F_UNIT5, True, HEALTHY), "POSITIVE")      # unit 5 alone is not an FU2 failure

    def test_negative_when_failure_repeats_and_settled(self):
        self.assertEqual(self.v(F_FAIL, True, NOROUTE), "NEGATIVE")
        self.assertEqual(self.v(F_FAIL, True, NOROUTE, age=10), "INCONCLUSIVE")

    def test_incomplete_binding_and_missing_evidence_are_inconclusive(self):
        self.assertEqual(self.v(F_CLEAN, True, HEALTHY, drivers=PART_DRV), "INCONCLUSIVE")
        self.assertEqual(self.v(F_FAIL, True, NOROUTE, drivers=PART_DRV), "INCONCLUSIVE")
        self.assertEqual(self.v(F_CLEAN, None, HEALTHY), "INCONCLUSIVE")
        self.assertEqual(self.v(F_CLEAN, True, None), "INCONCLUSIVE")
        self.assertEqual(self.v(F_CLEAN, True, HEALTHY, jok=False), "INCONCLUSIVE")

    def test_failed_post_gate_is_invalid(self):
        self.assertEqual(self.v(F_CLEAN, True, HEALTHY, post_ok=False), "INVALID")


class Timeline:
    """Fake clock + sysfs for observe_reconnect: present until t_off, absent until t_on, device 3 from t_on, bound at t_bind."""

    def __init__(self, t_off=1.0, t_on=2.5, t_bind=2.9, multiple=False, start_present=True):
        self.t = 0.0
        self.t_off, self.t_on, self.t_bind, self.multiple, self.start_present = t_off, t_on, t_bind, multiple, start_present

    def clock(self):
        return self.t

    def wall(self):
        return 1000.0 + self.t

    def sleep(self, s):
        self.t += s

    def devices(self):
        if not self.start_present:
            return []
        if self.t < self.t_off:
            return [dict(DEV2[0])]
        if self.t < self.t_on:
            return []
        return [dict(DEV3[0])] * (2 if self.multiple else 1)

    def drivers(self):
        return OK_DRV if self.t >= self.t_bind else PART_DRV

    def run(self, deadline=60, **kw):
        return L.observe_reconnect(deadline, devices_fn=self.devices, drivers_fn=self.drivers, clock=self.clock,
                                   sleep=self.sleep, wall=self.wall, install_signals=False, **kw)


class ObserveReconnect(unittest.TestCase):
    def test_single_sequence_no_settle_delay_and_honest_bounds(self):
        tl = Timeline()
        rep = tl.run()
        self.assertEqual(rep["status"], "reconnected")
        self.assertEqual((rep["devnum_before"], rep["devnum_after"]), ("2", "3"))
        self.assertLess(tl.t, 3.2)                                   # returned right after binding, no 3 s settle wait
        lo, hi = rep["reappear_bounds_epoch"]
        self.assertLessEqual(hi - lo, L.POLL_S + 1e-6)                # reappearance known to within one sampling interval
        self.assertTrue(lo < 1002.5 <= hi + 1e-9)
        blo, bhi = rep["bound_bounds_epoch"]
        self.assertTrue(blo < 1002.9 <= bhi + 1e-9)
        mn, mx = rep["reconnect_to_bound_bounds_s"]
        self.assertLessEqual(mn, 0.4 + 1e-6)                          # true interval 0.4 s lies inside the bounds
        self.assertGreaterEqual(mx, 0.4 - 1e-6)
        self.assertLessEqual(mx - mn, 3 * L.POLL_S)
        self.assertEqual(rep["sampling_interval_s"], L.POLL_S)
        self.assertIn("not USB request latency", rep["timing_note"])
        self.assertEqual(rep["interface_drivers_at_bound"], OK_DRV)

    def test_bound_in_the_same_sample_as_reappearance(self):
        rep = Timeline(t_on=2.5, t_bind=2.0).run()
        mn, mx = rep["reconnect_to_bound_bounds_s"]
        self.assertEqual(mn, 0.0)
        self.assertLessEqual(mx, L.POLL_S + 1e-6)

    def test_reappeared_but_never_bound(self):
        rep = Timeline(t_bind=999).run(bind_wait=2.0)
        self.assertEqual(rep["status"], "reconnected")
        self.assertIsNone(rep["t_bound_epoch"])
        self.assertIsNone(rep["reconnect_to_bound_bounds_s"])        # no misleading measurement

    def test_failure_modes(self):
        self.assertEqual(Timeline(t_off=999).run(deadline=3)["status"], "deadline: no disconnect")
        self.assertEqual(Timeline(t_on=999).run(deadline=5)["status"], "deadline: device never returned")
        self.assertEqual(Timeline(start_present=False).run()["status"], "device not present exactly once at arm")
        rep = Timeline(multiple=True).run(deadline=5)
        self.assertTrue(rep["multiple_seen"])
        self.assertNotEqual(rep["status"], "reconnected")            # two devices: never reported as a clean reconnect


class Flow(unittest.TestCase):
    """prepare -> check pre -> watch result -> check post -> classify, with injected collectors (no device access)."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        home = os.path.join(self.d, "home")
        os.makedirs(home, mode=0o700)
        with contextlib.redirect_stdout(io.StringIO()) as b:
            L.cmd_prepare(home=home)
        self.run = b.getvalue().strip()
        self.assertIn("trace-8a26", self.run)
        self.saved = (L._collect, D.find_devices, L.interface_drivers, D.module_loaded)
        self.state = {"text": PRE_TEXT, "after": "", "pcm": True, "pw": NOROUTE, "dev": DEV2, "drv": OK_DRV, "st": "OK"}
        L._collect = lambda runner, cli=None, since=None: (
            self.state["st"], "n lines", self.state["text"] if since is None else self.state["after"], self.state["pcm"],
            self.state["pw"], None)
        D.find_devices = lambda *a, **k: self.state["dev"]
        L.interface_drivers = lambda port, base=None: self.state["drv"]
        D.module_loaded = lambda: True

    def tearDown(self):
        L._collect, D.find_devices, L.interface_drivers, D.module_loaded = self.saved
        shutil.rmtree(self.d, ignore_errors=True)

    def call(self, fn, *a, **kw):
        with contextlib.redirect_stdout(io.StringIO()):
            return fn(*a, **kw)

    def reconnect_json(self, **kw):
        import time
        d = {"status": "reconnected", "devnum_before": "2", "devnum_after": "3", "t_reconnect_epoch": time.time() - 200,
             "multiple_seen": False, "reconnect_to_bound_observed_s": 0.5}
        d.update(kw)
        json.dump(d, open(os.path.join(self.run, "reconnect.json"), "w"))

    def pre(self):
        return self.call(L.cmd_check, self.run, "pre", kernel=L.EXPECTED_KERNEL)

    def post(self, text, dev=DEV3, after=""):
        self.state.update(text=text + after, dev=dev, after=(RECON + after) if "USB disconnect" in text else after)
        return self.call(L.cmd_check, self.run, "post", kernel=L.EXPECTED_KERNEL)

    def cls(self):
        self.call(L.cmd_classify, self.run, None)
        return json.load(open(os.path.join(self.run, "classification.json")))

    def test_negative_flow_and_repeatable_classify(self):
        self.assertEqual(self.pre(), 0)
        self.reconnect_json()
        self.assertEqual(self.post(PRE_TEXT + RECON, after=FAIL_AFTER), 0)
        c = self.cls()
        self.assertEqual(c["classification"], "NEGATIVE")
        self.assertEqual(c["devnum"], {"before": "2", "after": "3"})
        self.assertIn("not USB request latency", c["probe_timing"]["note"])
        self.assertEqual(self.cls()["classification"], "NEGATIVE")           # passive classify may repeat

    def test_positive_flow(self):
        self.pre()
        self.reconnect_json()
        self.state.update(pw=HEALTHY)
        self.assertEqual(self.post(PRE_TEXT + RECON), 0)
        self.assertEqual(self.cls()["classification"], "POSITIVE")

    def test_pre_gate_failure_blocks_watch(self):
        self.state["text"] = BOOT                                             # no registration, no failure
        self.assertEqual(self.pre(), 1)
        with self.assertRaises(SystemExit):
            self.call(L.cmd_watch, self.run, 5)

    def test_post_failures_are_invalid_and_classify_says_so(self):
        self.pre()
        self.reconnect_json()
        self.assertEqual(self.post(PRE_TEXT), 1)                              # no new disconnect in the journal
        self.assertEqual(self.cls()["classification"], "INVALID")
        self.reconnect_json(status="deadline: no disconnect", devnum_after=None)
        self.assertEqual(self.post(PRE_TEXT + RECON, DEV2), 1)
        self.reconnect_json(multiple_seen=True)
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)

    def test_post_without_watch_and_unloaded_driver_are_invalid(self):
        self.pre()
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)
        self.reconnect_json()
        D.module_loaded = lambda: False
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)

    def test_classify_before_post_check_refuses(self):
        self.pre()
        with self.assertRaises(SystemExit):
            self.call(L.cmd_classify, self.run, None)


class FlowHardening(Flow):
    """Post-gate hardening: missing/unreadable evidence is a clean INVALID, never an exception or a false OK."""

    def ready(self):
        self.pre()
        self.reconnect_json()

    def test_partial_binding_after_reconnect_fails_the_gate(self):
        self.ready()
        for missing in ("1.0", "1.1", "1.2"):
            self.state["drv"] = {k: v for k, v in OK_DRV.items() if k != missing}
            self.assertEqual(self.post(PRE_TEXT + RECON), 1, missing)
        self.state["drv"] = OK_DRV
        self.assertEqual(self.post(PRE_TEXT + RECON), 0)

    def test_journal_read_failure_in_post_stage_is_a_clean_refusal(self):
        self.ready()
        for st in ("EMPTY", "FAILED", "UNREADABLE", "INCOMPLETE"):
            self.state["st"] = st
            self.assertEqual(self.post(PRE_TEXT + RECON), 1, st)     # no UnboundLocalError, no OK
            c = json.load(open(os.path.join(self.run, "checkpoint_post.json")))
            self.assertTrue(any("journal" in p for p in c["problems"]))
            self.assertEqual(self.cls()["classification"], "INVALID")

    def test_missing_or_malformed_files_are_invalid_not_exceptions(self):
        self.pre()
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)             # no reconnect.json at all
        json.dump({"status": "reconnected"}, open(os.path.join(self.run, "reconnect.json"), "w"))
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)             # observer data missing
        self.reconnect_json()
        cp = json.load(open(os.path.join(self.run, "checkpoint.json")))
        del cp["events_before"]
        os.unlink(os.path.join(self.run, "checkpoint.json"))
        json.dump(cp, open(os.path.join(self.run, "checkpoint.json"), "w"))
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)             # KeyError inside the gate -> clean problem
        c = json.load(open(os.path.join(self.run, "checkpoint_post.json")))
        self.assertTrue(any("malformed" in p for p in c["problems"]))

    def test_inconsistent_device_numbers_fail_the_gate(self):
        self.ready()
        self.assertEqual(self.post(PRE_TEXT + RECON, dev=[dict(DEV3[0], devnum="7")]), 1)     # sysfs 7, observer 3, journal 3
        self.reconnect_json(devnum_after="9")
        self.assertEqual(self.post(PRE_TEXT + RECON), 1)                                      # observer 9 vs sysfs/journal 3
        self.reconnect_json()
        self.assertEqual(self.post(PRE_TEXT + RECON), 0)

    def test_classify_with_incomplete_observer_data_is_invalid(self):
        self.ready()
        self.assertEqual(self.post(PRE_TEXT + RECON), 0)
        self.reconnect_json(t_reconnect_epoch=None)
        self.assertEqual(self.cls()["classification"], "INVALID")


class Safety(unittest.TestCase):
    def test_source_has_no_hardware_or_module_or_bootloader_operations(self):
        src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "loaded_reconnect.py")).read()
        import re
        code = re.sub(r'""".*?"""', "", src, count=1, flags=re.S)
        code = "\n".join(l.split("#")[0] for l in code.splitlines()).replace("(no sudo)", "")
        for bad in ("modprobe", "rmmod", "insmod", "limine", "/boot", "authorized", "unbind", "bind\"", "reset", "usb_modeswitch",
                    "subprocess", "sudo", "os.system", "SET_CUR", "aplay", "paplay", "pw-cat", "pw-play", "speaker-test"):
            self.assertNotIn(bad, code, bad)

    def test_refuses_root(self):
        if os.geteuid() == 0:
            with self.assertRaises(SystemExit):
                L.main(["prepare"])


if __name__ == "__main__":
    unittest.main()
