#!/usr/bin/env python3
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402
import usb_ep0_ab as A  # noqa: E402


def req(t, dev, bm, b, wv, wi, wl, ms=0.1, ret=0, data_in=None, data_out=None, tmo=5000):
    s = {"bmRequestType": bm, "bRequest": b, "wValue": wv, "wIndex": wi, "wLength": wl}
    d = T.describe(s)
    return {"ts_submit": t, "ts_complete": t + ms / 1000, "elapsed": ms / 1000, "comm": "x", "bus": 3, "devnum": dev,
            "setup": s, "desc": d, "length_field": wl, "timeout_ms": tmo, "ret": ret, "ret_name": "", "actual": wl,
            "fu2": T.is_fu2(s, d), "pid": 1, "data_out": data_out, "data_in": data_in}


def si(t, dev, iface, alt, ms=0.1, ret=0):
    return req(t, dev, 0x01, 0x0B, alt, iface, 0, ms, ret)


class TestAB(unittest.TestCase):
    def test_incarnations_split_by_devnum(self):
        r = [si(1, 5, 1, 0), si(2, 5, 1, 1), si(20, 9, 1, 0)]
        self.assertEqual(sorted(k[1] for k in A.incarnations(r)), [5, 9])

    def test_sticky_boot_pattern(self):
        r = [si(1, 5, 1, 0), si(2, 5, 1, 1, ms=30.0), si(2.05, 5, 1, 0, ms=5400.0, ret=-110),
             si(3, 5, 2, 1, ms=0.2), si(3.01, 5, 2, 0, ms=0.1)]
        s = A.summarize(r)[0]
        self.assertTrue(s["iface1"]["first_alt1_slow"])
        self.assertTrue(s["iface1"]["following_alt0"]["timeout"])
        self.assertFalse(s["iface2"]["first_alt1_slow"])
        self.assertEqual(s["timeouts"], 1)

    def test_missing_ops_not_observed(self):
        s = A.summarize([si(1, 5, 1, 0)])[0]
        self.assertIsNone(s["iface2"]["first_alt1"])
        md = A.markdown("a", s, "b", None)
        self.assertIn("NOT OBSERVED", md)

    def test_fu2_readback_follows(self):
        q = lambda raw: raw & 0xFFFF
        r = [req(1, 5, 0xA1, 0x01, 0x0200, 0x0200, 2, data_in=q(-1024)),
             req(2, 5, 0x21, 0x01, 0x0200, 0x0200, 2, data_out=q(-15360)),
             req(3, 5, 0xA1, 0x01, 0x0200, 0x0200, 2, data_in=q(-15360))]
        f = A.summarize(r)[0]["fu2"]
        self.assertEqual(f["initial_get_db"], -4.0)
        self.assertTrue(f["follows_writes"])
        r[2] = req(3, 5, 0xA1, 0x01, 0x0200, 0x0200, 2, data_in=q(-1024))
        self.assertFalse(A.summarize(r)[0]["fu2"]["follows_writes"])

    def test_unit5_stalls(self):
        r = [req(1, 5, 0x21, 0x01, 0x0203, 0x0500, 2, ret=-32)]
        self.assertEqual(A.summarize(r)[0]["unit5_stalls"], 1)


if __name__ == "__main__":
    unittest.main()
