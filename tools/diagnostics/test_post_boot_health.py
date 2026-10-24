#!/usr/bin/env python3
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import post_boot_health as H  # noqa: E402

FAIL_LOG = """Okt 24 10:13:38.359880 h kernel: usb 3-2: (udev-worker) timed out or killed on ep0out len=0/0
Okt 24 10:13:38.451461 h kernel: usb 3-2: 2:0: sticky mixer values (-15360/0/128 => -1024), disabling
Okt 24 10:13:38.455440 h kernel: usb 3-2: 5:0: sticky mixer values (0/18432/256 => 6912), disabling
Okt 24 10:13:44.527449 h kernel: usb 3-2: 2:0: usb_set_interface failed (-110)
"""
HEALTHY_LOG = "Okt 24 10:40:42.513439 h kernel: usb 3-2: 5:0: sticky mixer values (0/18432/256 => 6912), disabling\n"

K73_LOG = """Okt 24 14:39:11 h kernel: usb 3-2: 2:0: broken mixer GET_CUR (-15360/0/128 => -6016)
Okt 24 14:39:11 h kernel: usb 3-2: 5:0: broken mixer GET_CUR (0/18432/256 => 6912)
Okt 24 14:39:17 h kernel: usb 3-2: 2:0: usb_set_interface failed (-110)
"""


class Parse(unittest.TestCase):
    def test_kernel_73_wording(self):
        f = H.parse_kernel_log(K73_LOG)
        self.assertTrue(f["fu2_sticky"] and f["unit5_sticky"])
        self.assertEqual(len(f["set_interface_failed"]), 1)

    def test_kernel_73_unit5_alone_is_not_fu2(self):
        f = H.parse_kernel_log(K73_LOG.splitlines()[1] + "\n")
        self.assertFalse(f["fu2_sticky"])
        self.assertTrue(f["unit5_sticky"])

    def test_unit_distinction(self):
        f = H.parse_kernel_log(FAIL_LOG)
        self.assertTrue(f["fu2_sticky"] and f["unit5_sticky"])
        self.assertEqual(len(f["set_interface_failed"]), 1)
        self.assertEqual(len(f["ep0_timeouts"]), 1)

    def test_unit5_alone_is_not_fu2(self):
        f = H.parse_kernel_log(HEALTHY_LOG)
        self.assertFalse(f["fu2_sticky"])
        self.assertTrue(f["unit5_sticky"])

    def test_missing_dynamic_debug_is_not_absence_of_timeout(self):
        f = H.parse_kernel_log(FAIL_LOG.splitlines()[1] + "\n")
        self.assertEqual(f["ep0_timeouts"], [])  # recorded as "none logged", not as "none occurred"
        self.assertTrue(f["fu2_sticky"])


class Classify(unittest.TestCase):
    def test_positive(self):
        self.assertEqual(H.classify(H.parse_kernel_log(HEALTHY_LOG), True, True), "POSITIVE")

    def test_negative(self):
        self.assertEqual(H.classify(H.parse_kernel_log(FAIL_LOG), False, False), "NEGATIVE")

    def test_mixed_is_inconclusive(self):
        self.assertEqual(H.classify(H.parse_kernel_log(FAIL_LOG), True, False), "INCONCLUSIVE")
        self.assertEqual(H.classify(H.parse_kernel_log(HEALTHY_LOG), True, False), "INCONCLUSIVE")

    def test_unknown_inputs_inconclusive(self):
        self.assertEqual(H.classify(H.parse_kernel_log(""), None, True), "INCONCLUSIVE")


class Routes(unittest.TestCase):
    def test_parse_routes(self):
        objs = [{"type": "PipeWire:Interface:Device", "info": {"params": {"Route": [
            {"name": "analog-output", "direction": "Output",
             "info": [4, "port.type", "analog", "route.hw-mute", "true", "route.hw-volume", "true"]},
            {"name": "analog-input-mic", "direction": "Input", "info": [2, "route.hw-volume", "false"]}]}}}]
        self.assertEqual(H.parse_routes(objs), [("analog-output", "Output", True), ("analog-input-mic", "Input", False)])


if __name__ == "__main__":
    unittest.main()
