#!/usr/bin/env python3
"""Offline tests for usb_ep0_trace.py: synthetic traces only; no kernel, USB or tracefs access."""

import json
import os
import re
import shutil
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402

TARGET = (0x29C2, 0x0004)


def setup_u64(bm, b, value, index, length):
    return bm | (b << 8) | (value << 16) | (index << 32) | (length << 48)


def uw(ts, comm, pid, setup, vid=TARGET[0], pid_=TARGET[1], tmo=5000, devnum=2, bus=3, outdata=0, cpu=1):
    return ("%s-%d [%03d] ..... %.6f: uw: (usb_start_wait_urb+0x0/0x1d0) setup=0x%x pipe=0x80000100 "
            "len=0 tmo=%d devnum=%d bus=%d vid=0x%x pid=0x%x outdata=0x%x"
            % (comm, pid, cpu, ts, setup, tmo, devnum, bus, vid, pid_, outdata))


def uwr(ts, comm, pid, ret=0, indata=0, actual=0, cpu=1):
    return ("%s-%d [%03d] ..... %.6f: uwr: (usb_control_msg+0xdd/0x150 <- usb_start_wait_urb) "
            "ret=%d indata=0x%x actual=%d" % (comm, pid, cpu, ts, ret, indata, actual))


def trace(*lines, written=None, inbuf=None):
    n = len([x for x in lines if x])
    head = ["# tracer: nop", "#", "# entries-in-buffer/entries-written: %d/%d   #P:16"
            % (n if inbuf is None else inbuf, n if written is None else written), "#"]
    return "\n".join(head + list(lines)) + "\n"


SET_IFACE_2_1 = setup_u64(0x01, 11, 1, 2, 0)          # SET_INTERFACE interface 2 alt 1
SET_IFACE_1_0 = setup_u64(0x01, 11, 0, 1, 0)
FU2_VOL_SET = setup_u64(0x21, 1, 0x0200, 0x0200, 2)
FU2_VOL_GET = setup_u64(0xA1, 1, 0x0200, 0x0200, 2)
FU2_MUTE_GET = setup_u64(0xA1, 1, 0x0100, 0x0200, 1)
GET_STR_DESC = setup_u64(0x80, 6, 0x0303, 0x0409, 255)  # string descriptor index 3 (a serial number)


class ProbeDefinitions(unittest.TestCase):
    def test_boot_argument_is_safe_to_type_and_parse(self):
        arg = T.boot_arg()
        self.assertTrue(arg.startswith("kprobe_event="))
        for ch in (" ", "#", '"', "\t", "\n"):
            self.assertNotIn(ch, arg)
        self.assertLess(len(" ".join(T.extra_cmdline_args())), 1500)  # kernel COMMAND_LINE_SIZE is 2048

    def test_commas_only_separate_fields_and_roundtrip(self):
        for _, d in T.PROBES:
            self.assertNotIn(",", d)
            self.assertNotIn(";", d)
        rebuilt = [x.replace(",", " ") for x in T.boot_arg()[len("kprobe_event="):].split(";")]
        self.assertEqual(rebuilt, [d for _, d in T.PROBES])

    def test_no_raw_kernel_pointer_is_recorded(self):
        for _, d in T.PROBES:
            self.assertNotIn("urb=", d)

    def test_only_our_group_is_defined(self):
        for _, d in T.PROBES:
            self.assertIn(T.GROUP + "/", d)

    def test_ring_buffer_is_expanded_at_boot(self):
        args = T.extra_cmdline_args()
        self.assertIn("ftrace=nop", args)
        self.assertIn("trace_buf_size=32M", args)
        self.assertEqual(sum(1 for a in args if a.startswith("trace_event=")), 1)
        self.assertTrue(all(("xhci-hcd:" + e) in [x for x in args if x.startswith("trace_event=")][0] for e in T.XHCI_EVENTS))

    def test_no_stale_quirk_argument(self):
        self.assertNotIn("usbcore.quirks", " ".join(T.extra_cmdline_args()))


class SetupDecoding(unittest.TestCase):
    def test_set_interface_is_standard_request_0x0b(self):
        s = T.split_setup(SET_IFACE_2_1)
        d = T.describe(s)
        self.assertEqual(s["bRequest"], 0x0B)
        self.assertEqual((d["type"], d["recipient"], d["direction"], d["name"]),
                         ("standard", "interface", "OUT", "SET_INTERFACE"))
        self.assertEqual((d["interface"], d["alternate"]), (2, 1))
        self.assertEqual(s["wLength"], 0)

    def test_zero_length_ep0_out_is_not_assumed_to_be_set_interface(self):
        s = T.split_setup(setup_u64(0x21, 0x0A, 0, 0, 0))  # class SET_IDLE-like, also no data
        self.assertNotEqual(T.describe(s)["name"], "SET_INTERFACE")
        s = T.split_setup(setup_u64(0x00, 9, 1, 0, 0))     # SET_CONFIGURATION
        self.assertEqual(T.describe(s)["name"], "SET_CONFIGURATION")

    def test_direction_is_preserved(self):
        self.assertEqual(T.describe(T.split_setup(setup_u64(0x81, 10, 0, 1, 1)))["direction"], "IN")
        self.assertEqual(T.describe(T.split_setup(FU2_VOL_SET))["direction"], "OUT")
        self.assertEqual(T.describe(T.split_setup(FU2_VOL_GET))["direction"], "IN")

    def test_real_capture_shape_decodes(self):
        s = T.split_setup(0x40001000000A3)  # observed root-hub status request
        self.assertEqual((s["bmRequestType"], s["bRequest"], s["wValue"], s["wIndex"], s["wLength"]),
                         (0xA3, 0, 0, 1, 4))

    def test_out_of_range_setup_rejected(self):
        for bad in (-1, 1 << 64, "x", None):
            with self.assertRaises(ValueError):
                T.split_setup(bad)

    def test_q88(self):
        self.assertEqual(T.q88_db(0xC400), -60.0)
        self.assertEqual(T.q88_db(0xFC00), -4.0)
        self.assertEqual(T.q88_db(0), 0.0)


class RequestPairing(unittest.TestCase):
    def test_normal_completion(self):
        txt = trace(uw(10.0, "(udev-worker)", 7, SET_IFACE_1_0), uwr(10.0004, "(udev-worker)", 7))
        res = T.analyze(txt)
        self.assertEqual(res["target_requests"], 1)
        self.assertTrue(res["complete"])
        self.assertEqual(res["timeouts"], [])
        self.assertEqual(res["set_interface_requests"][0]["ret"], 0)

    def test_five_second_timeout_is_recorded_with_full_setup(self):
        txt = trace(uw(6.347446, "(udev-worker)", 7, SET_IFACE_2_1),
                    uwr(11.347446, "(udev-worker)", 7, ret=-110))
        res = T.analyze(txt)
        t = res["timeouts"][0]
        self.assertEqual((t["bmRequestType"], t["bRequest"], t["wValue"], t["wIndex"], t["wLength"]),
                         (0x01, 0x0B, 1, 2, 0))
        self.assertEqual((t["ret"], t["ret_name"], t["meaning"], t["interface"], t["alternate"]),
                         (-110, "ETIMEDOUT", "SET_INTERFACE", 2, 1))
        self.assertAlmostEqual(t["elapsed_s"], 5.0, places=6)
        self.assertEqual(t["task"], "(udev-worker)")

    def test_timed_out_or_killed_urb_is_not_success(self):
        for ret in (-110, -2):
            txt = trace(uw(1.0, "w", 9, SET_IFACE_1_0), uwr(6.0, "w", 9, ret=ret))
            self.assertEqual(len(T.analyze(txt)["timeouts"]), 1, ret)
        ok = trace(uw(1.0, "w", 9, SET_IFACE_1_0), uwr(6.0, "w", 9, ret=0))
        self.assertEqual(T.analyze(ok)["timeouts"], [])
        fast_err = trace(uw(1.0, "w", 9, SET_IFACE_1_0), uwr(1.001, "w", 9, ret=-32))
        self.assertEqual(T.analyze(fast_err)["timeouts"], [])

    def test_negative_statuses_retained(self):
        txt = trace(uw(1.0, "a", 1, SET_IFACE_1_0), uwr(1.01, "a", 1, ret=-71))
        q, _, _, _ = T.build_requests(T.parse_trace(txt)[0])
        self.assertEqual((q[0]["ret"], q[0]["ret_name"]), (-71, "EPROTO"))

    def test_missing_completion_is_flagged(self):
        res = T.analyze(trace(uw(1.0, "a", 1, SET_IFACE_1_0)))
        self.assertEqual(res["unpaired_events"], 1)
        self.assertFalse(res["complete"])

    def test_orphan_return_is_flagged(self):
        res = T.analyze(trace(uwr(1.0, "a", 1)))
        self.assertEqual(res["unpaired_events"], 1)
        self.assertFalse(res["complete"])

    def test_interleaved_tasks_pair_by_task(self):
        txt = trace(uw(1.0, "a", 1, SET_IFACE_1_0), uw(1.1, "b", 2, SET_IFACE_2_1),
                    uwr(1.2, "b", 2), uwr(6.0, "a", 1, ret=-110))
        res = T.analyze(txt)
        self.assertEqual(len(res["timeouts"]), 1)
        self.assertEqual(res["timeouts"][0]["interface"], 1)

    def test_malformed_setup_flagged(self):
        line = ("a-1 [001] ..... 1.000000: uw: (usb_start_wait_urb+0x0/0x1d0) pipe=0x80000100 len=0 "
                "tmo=5000 devnum=2 bus=3 vid=0x29c2 pid=0x4 outdata=0x0")
        res = T.analyze(trace(line, uwr(1.1, "a", 1)))
        self.assertEqual(res["malformed_setups"], 1)
        self.assertFalse(res["complete"])

    def test_unparseable_event_line_counted(self):
        res = T.analyze(trace("garbage uw: broken line", ))
        self.assertEqual(res["info"]["malformed_lines"], 1)
        self.assertFalse(res["complete"])


class LossAndFiltering(unittest.TestCase):
    def test_buffer_overwrite_detected_as_loss_not_cap(self):
        txt = trace(uw(1.0, "a", 1, SET_IFACE_1_0), uwr(1.1, "a", 1), written=120, inbuf=2)
        res = T.analyze(txt)
        self.assertTrue(res["info"]["loss"])
        self.assertFalse(res["complete"])

    def test_no_loss_when_counts_match(self):
        txt = trace(uw(1.0, "a", 1, SET_IFACE_1_0), uwr(1.1, "a", 1))
        self.assertFalse(T.analyze(txt)["info"]["loss"])

    def test_second_device_and_same_vendor_other_product_excluded(self):
        txt = trace(uw(1.0, "a", 1, SET_IFACE_1_0, vid=0x1D6B, pid_=3), uwr(1.1, "a", 1),
                    uw(2.0, "a", 1, SET_IFACE_1_0, vid=0x29C2, pid_=0x0005), uwr(2.1, "a", 1),
                    uw(3.0, "a", 1, SET_IFACE_1_0), uwr(3.1, "a", 1))
        res = T.analyze(txt)
        self.assertEqual((res["target_requests"], res["non_target_requests"]), (1, 2))

    def test_descriptor_payload_never_leaves_the_analysis(self):
        marker = 0x4141414141414141
        txt = trace(uw(1.0, "a", 1, GET_STR_DESC), uwr(1.1, "a", 1, ret=18, indata=marker, actual=18))
        blob = json.dumps(T.analyze(txt))
        self.assertNotIn(str(marker), blob)
        self.assertNotIn(hex(marker), blob)
        self.assertNotIn("4141", blob)

    def test_other_devices_not_retained(self):
        txt = trace(uw(1.0, "a", 1, GET_STR_DESC, vid=0xABCD, pid_=0x1234), uwr(1.1, "a", 1))
        self.assertNotIn("abcd", json.dumps(T.analyze(txt)).lower())


class Fu2Timeline(unittest.TestCase):
    def build(self):
        return trace(
            uw(11.416000, "(udev-worker)", 7, FU2_MUTE_GET), uwr(11.416400, "(udev-worker)", 7, ret=1, indata=0, actual=1),
            uw(11.417000, "(udev-worker)", 7, FU2_VOL_GET), uwr(11.417400, "(udev-worker)", 7, ret=2, indata=0xFC00, actual=2),
            uw(11.418000, "(udev-worker)", 7, FU2_VOL_SET, outdata=0xC400), uwr(11.418400, "(udev-worker)", 7, ret=2),
            uw(11.419000, "(udev-worker)", 7, FU2_VOL_GET), uwr(11.419400, "(udev-worker)", 7, ret=2, indata=0xFC00, actual=2),
            uw(11.420000, "(udev-worker)", 7, FU2_VOL_SET, outdata=0x0000), uwr(11.420400, "(udev-worker)", 7, ret=2))

    def test_sequence_and_values(self):
        res = T.analyze(self.build())
        kinds = [(f["dir"], f["kind"], f.get("db")) for f in res["fu2"]]
        self.assertEqual(kinds, [("IN", "mute", None), ("IN", "volume", -4.0), ("OUT", "volume", -60.0),
                                 ("IN", "volume", -4.0), ("OUT", "volume", 0.0)])
        # readback after the -60 dB write is unchanged: the sticky pattern is visible
        self.assertEqual(res["fu2"][1]["raw"], res["fu2"][3]["raw"])

    def test_timeout_to_first_fu2(self):
        txt = trace(uw(6.347446, "(udev-worker)", 7, SET_IFACE_2_1), uwr(11.347446, "(udev-worker)", 7, ret=-110),
                    uw(11.416000, "(udev-worker)", 7, FU2_MUTE_GET), uwr(11.416400, "(udev-worker)", 7, ret=1))
        self.assertAlmostEqual(T.analyze(txt)["first_timeout_to_first_fu2_s"], 0.068554, places=6)

    def test_failed_final_restore_is_visible(self):
        txt = trace(uw(1.0, "k", 3, FU2_VOL_SET, outdata=0xFC00), uwr(1.001, "k", 3, ret=-71))
        f = T.analyze(txt)["fu2"][0]
        self.assertEqual(f["ret"], -71)
        self.assertNotIn("db", f)

    def test_wrong_unit_is_not_fu2(self):
        other = setup_u64(0xA1, 1, 0x0200, 0x0500, 2)
        self.assertEqual(T.analyze(trace(uw(1.0, "k", 3, other), uwr(1.1, "k", 3, ret=2)))["fu2"], [])


class DebugMessageTie(unittest.TestCase):
    def test_dynamic_debug_line_ties_to_timeout(self):
        txt = trace(uw(6.347446, "(udev-worker)", 7, SET_IFACE_2_1), uwr(11.347446, "(udev-worker)", 7, ret=-110))
        dm = "[   11.347446] usb 3-2: (udev-worker) timed out or killed on ep0out len=0/0\n"
        ties = T.analyze(txt, dm)["debug_message_ties"]
        self.assertTrue(ties[0]["matched_request"])
        self.assertEqual(ties[0]["delta_ms"], 0.0)

    def test_unrelated_debug_line_does_not_tie(self):
        txt = trace(uw(1.0, "(udev-worker)", 7, SET_IFACE_2_1), uwr(6.0, "(udev-worker)", 7, ret=-110))
        dm = "[   17.491004] usb 3-2: wireplumber timed out or killed on ep0out len=0/0\n"
        self.assertFalse(T.analyze(txt, dm)["debug_message_ties"][0]["matched_request"])


def si_line(ts, comm, pid, iface, alt, vid=TARGET[0], pid_=TARGET[1], devnum=2):
    return ("%s-%d [001] ..... %.6f: si: (usb_set_interface+0x4/0x1e0) devnum=%d vid=0x%x pid=0x%x iface=%d alt=%d"
            % (comm, pid, ts, devnum, vid, pid_, iface, alt))


def sir_line(ts, comm, pid, ret, caller="snd_usb_create_stream+0x189/0x220 [snd_usb_audio]"):
    return ("%s-%d [001] ..... %.6f: sir: (%s <- usb_set_interface) ret=%d" % (comm, pid, ts, caller, ret))


class SetInterfaceAndFirstTimeout(unittest.TestCase):
    def test_full_first_timeout_with_interface_and_alternate(self):
        w = "(udev-worker)"
        txt = trace(si_line(6.3471, w, 7, 1, 0), uw(6.3472, w, 7, SET_IFACE_1_0), uwr(11.3472, w, 7, ret=-110),
                    sir_line(11.3517, w, 7, -110))
        res = T.analyze(txt)
        ft = res["first_timeout"]
        self.assertTrue(ft["request_captured"] and ft["set_interface_entry_captured"])
        self.assertEqual((ft["interface"], ft["alternate"], ft["ret"], ft["task"]), (1, 0, -110, w))
        self.assertEqual(ft["setup"]["bRequest"], 0x0B)
        self.assertAlmostEqual(ft["duration_s"], 5.0, places=6)
        self.assertEqual(ft["usb_set_interface"]["interface"], 1)
        self.assertEqual(ft["usb_set_interface"]["caller"], "snd_usb_create_stream+0x189/0x220 [snd_usb_audio]")
        self.assertNotIn("missing", ft)

    def test_return_only_timeout_is_reported_as_incomplete_not_complete(self):
        txt = trace(sir_line(11.3517, "(udev-worker)", 7, -110))
        res = T.analyze(txt)
        ft = res["first_timeout"]
        self.assertFalse(ft["request_captured"])
        self.assertIn("missing", ft)
        self.assertEqual(ft["orphan_return"]["ret"], -110)
        self.assertNotIn("interface", ft)
        self.assertEqual(res["timeouts"], [])

    def test_earliest_timeout_wins_and_derived_setup_is_labelled(self):
        w = "(udev-worker)"
        txt = trace(si_line(5.842918, w, 7, 1, 0), sir_line(11.351722, w, 7, -110),
                    si_line(12.377972, "wireplumber", 9, 2, 0), uw(12.407676, "wireplumber", 9, SET_IFACE_1_0),
                    uwr(17.493069, "wireplumber", 9, ret=-110), sir_line(17.495691, "wireplumber", 9, -110,
                                                                            caller="endpoint_set_interface+0x51/0x140 [snd_usb_audio]"))
        ft = T.analyze(txt)["first_timeout"]
        self.assertEqual((ft["task"], ft["interface"], ft["alternate"]), (w, 1, 0))
        self.assertFalse(ft["request_captured"])
        self.assertTrue(ft["set_interface_entry_captured"])
        self.assertEqual(ft["setup_derived_not_observed"], {"bmRequestType": 1, "bRequest": 11, "wValue": 0,
                                                            "wIndex": 1, "wLength": 0})
        self.assertNotIn("setup", ft)
        self.assertIn("missing", ft)
        self.assertAlmostEqual(ft["usb_set_interface"]["elapsed_s"], 5.508804, places=6)

    def test_non_target_set_interface_ignored(self):
        txt = trace(si_line(1.0, "a", 1, 3, 1, vid=0x1D6B, pid_=3), sir_line(1.1, "a", 1, 0))
        self.assertEqual(T.analyze(txt)["usb_set_interface_calls"], [])


def full_trace(first_set_prefix=True, with_timeout=True, mutate=None):
    w, ls = "(udev-worker)", []
    if with_timeout:
        ls += [si_line(6.3471, w, 7, 1, 0), uw(6.3472, w, 7, SET_IFACE_1_0), uwr(11.3472, w, 7, ret=-110),
               sir_line(11.3517, w, 7, -110)]
    t = 11.416
    seq = [(FU2_MUTE_GET, 0, 1), (setup_u64(0xA1, 2, 0x0200, 0x0200, 8), 0x0000, 8), (FU2_VOL_GET, 0xFC00, 2),
           (FU2_VOL_SET, 0xC400, 2), (FU2_VOL_GET, 0xFC00, 2), (FU2_VOL_SET, 0x0000, 2)]
    if not first_set_prefix:
        seq = seq[3:]
    for st, val, n in seq:
        out = (st >> 0) & 0x80 == 0
        ls += [uw(t, w, 7, st, outdata=val if out else 0),
               uwr(t + 0.0004, w, 7, ret=n, indata=0 if out else val, actual=n)]
        t += 0.001
    return trace(*ls)


GOOD_REPORT = {"buffer_expanded": True, "buffer_size_kb": "8192", "cpu_overrun_total": 0,
               "oldest_event_ts_min": 0.78}


class Completeness(unittest.TestCase):
    def test_complete_when_everything_whole(self):
        v = T.analyze(full_trace(), report=GOOD_REPORT)["completeness"]
        self.assertEqual(v["verdict"], "COMPLETE", v)

    def test_unexpanded_buffer_is_invalid(self):
        r = dict(GOOD_REPORT, buffer_expanded=False, buffer_size_kb="7 (expanded: 8192)")
        v = T.analyze(full_trace(), report=r)["completeness"]
        self.assertEqual(v["verdict"], "INVALID")
        self.assertTrue(any("not expanded" in x for x in v["reasons"]))

    def test_cpu_overrun_is_invalid(self):
        v = T.analyze(full_trace(), report=dict(GOOD_REPORT, cpu_overrun_total=3))["completeness"]
        self.assertEqual(v["verdict"], "INVALID")

    def test_late_coverage_is_invalid(self):
        v = T.analyze(full_trace(), report=dict(GOOD_REPORT, oldest_event_ts_min=11.0))["completeness"]
        self.assertEqual(v["verdict"], "INVALID")

    def test_header_loss_is_invalid(self):
        txt = re.sub(r"entries-in-buffer/entries-written: \d+/\d+", "entries-in-buffer/entries-written: 10/200", full_trace())
        self.assertIn("10/200", txt)
        self.assertEqual(T.analyze(txt, report=GOOD_REPORT)["completeness"]["verdict"], "INVALID")

    def test_missing_first_timeout_entry_is_partial(self):
        txt = trace(sir_line(11.3517, "(udev-worker)", 7, -110))
        v = T.analyze(txt, report=GOOD_REPORT)["completeness"]
        self.assertEqual(v["verdict"], "PARTIAL")
        self.assertTrue(any("first timed-out request entry is missing" in x for x in v["reasons"]))

    def test_fu2_prefix_lost_is_partial(self):
        v = T.analyze(full_trace(first_set_prefix=False), report=GOOD_REPORT)["completeness"]
        self.assertEqual(v["verdict"], "PARTIAL")
        self.assertTrue(any("starts with a SET" in x for x in v["reasons"]))

    def test_no_report_is_never_complete(self):
        self.assertNotEqual(T.analyze(full_trace())["completeness"]["verdict"], "COMPLETE")

    def test_no_timeout_means_nothing_to_attribute(self):
        v = T.analyze(full_trace(with_timeout=False), report=GOOD_REPORT)["completeness"]
        self.assertNotEqual(v["verdict"], "COMPLETE")

    def test_full_sequence_reconstructed_in_order(self):
        fu2 = T.analyze(full_trace())["fu2"]
        self.assertEqual([(f["dir"], f["kind"], f["request"]) for f in fu2],
                         [("IN", "mute", "UAC_CUR"), ("IN", "volume", "UAC_RANGE"), ("IN", "volume", "UAC_CUR"),
                          ("OUT", "volume", "UAC_CUR"), ("IN", "volume", "UAC_CUR"), ("OUT", "volume", "UAC_CUR")])
        self.assertEqual(fu2[4]["db"], -4.0)   # unchanged after the -60 dB write
        self.assertEqual(fu2[5]["db"], 0.0)    # last attempted write: 0 dB, no restore


LIVE_REPORT = dict(GOOD_REPORT, disconnect_seen=True, reconnect_seen=True, oldest_event_ts_min=1607.6)


def healthy_reconnect_trace(with_si=True):
    w = "kworker/1:2"
    ls = [] if not with_si else [si_line(1635.148, w, 7, 1, 0), uw(1635.1481, w, 7, SET_IFACE_1_0), uwr(1635.1483, w, 7, ret=0),
          sir_line(1635.1485, w, 7, 0)]
    t = 1635.27
    for st, val, n in ((FU2_MUTE_GET, 0, 1), (FU2_VOL_GET, 0xFC00, 2), (FU2_VOL_SET, 0xC400, 2),
                       (FU2_VOL_GET, 0xC400, 2), (FU2_VOL_SET, 0xFC00, 2)):
        out = (st >> 0) & 0x80 == 0
        ls += [uw(t, w, 7, st, outdata=val if out else 0), uwr(t + 0.0004, w, 7, ret=n, indata=0 if out else val, actual=n)]
        t += 0.001
    return trace(*ls)


class ReconnectCompleteness(unittest.TestCase):
    def test_healthy_reconnect_is_complete(self):
        v = T.analyze(healthy_reconnect_trace(), report=LIVE_REPORT, mode="reconnect")["completeness"]
        self.assertEqual(v["verdict"], "COMPLETE", v)

    def test_same_trace_in_boot_mode_stays_partial(self):
        v = T.analyze(healthy_reconnect_trace(), report=dict(LIVE_REPORT, oldest_event_ts_min=0.78))["completeness"]
        self.assertEqual(v["verdict"], "PARTIAL")

    def test_boot_mode_still_requires_boot_coverage(self):
        v = T.analyze(full_trace(), report=LIVE_REPORT)["completeness"]
        self.assertEqual(v["verdict"], "INVALID")

    def test_reconnect_needs_both_observations(self):
        for k in ("disconnect_seen", "reconnect_seen"):
            v = T.analyze(healthy_reconnect_trace(), report=dict(LIVE_REPORT, **{k: False}), mode="reconnect")["completeness"]
            self.assertEqual(v["verdict"], "PARTIAL", k)

    def test_reconnect_without_live_report_not_complete(self):
        v = T.analyze(healthy_reconnect_trace(), mode="reconnect")["completeness"]
        self.assertNotEqual(v["verdict"], "COMPLETE")

    def test_reconnect_loss_still_invalid(self):
        self.assertEqual(T.analyze(healthy_reconnect_trace(), report=dict(LIVE_REPORT, cpu_overrun_total=2),
                                   mode="reconnect")["completeness"]["verdict"], "INVALID")
        txt = healthy_reconnect_trace().replace("entries-in-buffer/entries-written: 14/14", "entries-in-buffer/entries-written: 10/14")
        self.assertEqual(T.analyze(txt, report=LIVE_REPORT, mode="reconnect")["completeness"]["verdict"], "INVALID")

    def test_reconnect_missing_probe_partial(self):
        v = T.analyze(healthy_reconnect_trace(with_si=False), report=LIVE_REPORT, mode="reconnect")["completeness"]
        self.assertEqual(v["verdict"], "PARTIAL", v)


class XhciCompleteness(unittest.TestCase):
    def test_expect_xhci_without_events_is_partial(self):
        v = T.analyze(full_trace(), report=GOOD_REPORT, expect_xhci=True)["completeness"]
        self.assertEqual(v["verdict"], "PARTIAL")
        self.assertTrue(any("no xHCI events" in r for r in v["reasons"]))

    def test_expect_xhci_with_events_and_trusted_join_is_not_penalised(self):
        w = "(udev-worker)"
        base = full_trace()
        extra = [x_enq(6.8473, w, 7)] + x_trbs(6.8474, w, 7) + [x_db(6.8475, w, 7), x_deq(11.3471, w, 7), x_gb(11.348)]
        txt = base.rstrip("\n") + "\n" + "\n".join(extra) + "\n"
        # make the timeout request start where the xHCI enqueue is
        txt = txt.replace("6.347200", "6.847200").replace("11.347200", "11.347200")
        v = T.analyze(txt, report=GOOD_REPORT, expect_xhci=True)["completeness"]
        self.assertFalse(any("xHCI" in r for r in v["reasons"]), v)


class BufferHelpers(unittest.TestCase):
    def test_buffer_expansion_detection(self):
        self.assertFalse(T.buffer_expanded("7 (expanded: 1408)"))
        self.assertFalse(T.buffer_expanded("1410"))
        self.assertTrue(T.buffer_expanded("8192"))
        self.assertFalse(T.buffer_expanded(""))

    def test_baseline_buffer_parse(self):
        self.assertEqual(T.baseline_buffer_kb({"buffer_size_kb": "7 (expanded: 1408)"}), 1408)
        self.assertEqual(T.baseline_buffer_kb({"buffer_size_kb": "2048"}), 2048)
        self.assertIsNone(T.baseline_buffer_kb(None))

    def test_cpu_stats_parse(self):
        st = T.parse_cpu_stats("entries: 12\noverrun: 3\ncommit overrun: 0\nbytes: 900\n"
                               "oldest event ts:   11.418409\nnow ts:   99.1\ndropped events: 0\nread events: 0\n")
        self.assertEqual((st["entries"], st["overrun"], st["commit_overrun"]), (12, 3, 0))
        self.assertAlmostEqual(st["oldest_event_ts"], 11.418409)


class BaselineRestore(unittest.TestCase):
    def make(self, d, buf="8192\n", on="0\n"):
        os.makedirs(os.path.join(d, "events", T.GROUP))
        os.makedirs(os.path.join(d, "instances"))
        os.makedirs(os.path.join(d, "per_cpu", "cpu0"))
        files = {"trace": "# entries-in-buffer/entries-written: 5/9   #P:1\n", "kprobe_events": "",
                 "buffer_size_kb": buf, "tracing_on": on, "current_tracer": "nop\n", "trace_clock": "[local] global\n",
                 "set_event": "", os.path.join("events", T.GROUP, "enable"): "1",
                 os.path.join("per_cpu", "cpu0", "stats"): "entries: 5\noverrun: 4\ncommit overrun: 0\nbytes: 10\noldest event ts: 11.2\n"}
        for name, content in files.items():
            with open(os.path.join(d, name), "w") as f:
                f.write(content)

    def test_collect_restores_recorded_baseline_exactly_and_reports_loss(self):
        with tempfile.TemporaryDirectory() as tf:
            fake, out = os.path.join(tf, "t"), os.path.join(tf, "out")
            os.makedirs(fake)
            self.make(fake)
            base = {"buffer_size_kb": "7 (expanded: 2048)", "tracing_on": "1", "current_tracer": "nop",
                    "trace_clock": "[local] global", "set_event": [], "kprobe_events": [], "instances": []}
            T.collect(out, tracefs=fake, require_root=False, baseline=base)
            self.assertEqual(open(os.path.join(fake, "buffer_size_kb")).read(), "2048")
            self.assertEqual(open(os.path.join(fake, "tracing_on")).read(), "1")
            rep = json.load(open(os.path.join(out, "collect_report.json")))
            self.assertEqual((rep["buffer_restored_kb"], rep["cpu_overrun_total"], rep["entries_written"]), (2048, 4, 9))
            self.assertTrue(rep["buffer_expanded"])
            self.assertEqual(stat.S_IMODE(os.stat(os.path.join(out, "collect_report.json")).st_mode), 0o600)

    def test_collect_disables_and_unregisters_only_our_xhci_events(self):
        with tempfile.TemporaryDirectory() as tf:
            fake, out = os.path.join(tf, "t"), os.path.join(tf, "out")
            os.makedirs(fake)
            self.make(fake)
            for name in T.XHCI_EVENTS + ["xhci_foreign_event"]:
                os.makedirs(os.path.join(fake, "events", "xhci-hcd", name))
                with open(os.path.join(fake, "events", "xhci-hcd", name, "enable"), "w") as f:
                    f.write("1")
            T.collect(out, tracefs=fake, require_root=False)
            for name in T.XHCI_EVENTS:
                self.assertEqual(open(os.path.join(fake, "events", "xhci-hcd", name, "enable")).read(), "0")
            self.assertEqual(open(os.path.join(fake, "events", "xhci-hcd", "xhci_foreign_event", "enable")).read(), "1")
            lines = [l.strip() for l in open(os.path.join(fake, "set_event")).read().splitlines() if l.strip()]
            self.assertEqual(lines, ["!xhci-hcd:" + n for n in T.XHCI_EVENTS])

    def test_collect_can_be_rerun_after_an_interruption(self):
        with tempfile.TemporaryDirectory() as tf:
            fake, out = os.path.join(tf, "t"), os.path.join(tf, "out")
            os.makedirs(fake)
            self.make(fake)
            T.collect(out, tracefs=fake, require_root=False)   # first run (stand-in for an interrupted one)
            first = open(os.path.join(out, "raw_trace.txt")).read()
            T.collect(out, tracefs=fake, require_root=False)   # re-run must succeed and not lose the saved trace
            self.assertEqual(open(os.path.join(out, "raw_trace.txt")).read(), first)
            self.assertEqual(stat.S_IMODE(os.stat(os.path.join(out, "raw_trace.txt")).st_mode), 0o600)

    def test_collect_without_baseline_uses_default_and_says_so(self):
        with tempfile.TemporaryDirectory() as tf:
            fake, out = os.path.join(tf, "t"), os.path.join(tf, "out")
            os.makedirs(fake)
            self.make(fake, buf="7 (expanded: 8192)\n")
            T.collect(out, tracefs=fake, require_root=False)
            rep = json.load(open(os.path.join(out, "collect_report.json")))
            self.assertFalse(rep["buffer_expanded"])
            self.assertFalse(rep["baseline_used"])

    def test_verify_clean_flags_foreign_state_changes(self):
        with tempfile.TemporaryDirectory() as tf:
            self.make(tf)
            shutil.rmtree(os.path.join(tf, "events", T.GROUP))
            base = T.tracefs_baseline(tf)
            self.assertEqual(T.verify_clean(tf, base), [])
            with open(os.path.join(tf, "set_event"), "w") as f:
                f.write("sched:sched_switch\n")
            self.assertTrue(any("set_event" in p for p in T.verify_clean(tf, base)))


URB = 0x00000000BC2E0F8E


def uw_urb(ts, comm, pid, setup, **kw):
    return uw(ts, comm, pid, setup, **kw)


def uwr_urb(ts, comm, pid, ret=0, **kw):
    return uwr(ts, comm, pid, ret=ret, **kw)


def xl(ts, comm, pid, ev, text, cpu=0):
    return "%s-%d [%03d] ..... %.6f: %s: %s" % (comm, pid, cpu, ts, ev, text)


def x_enq(ts, comm, pid, urb=URB, dev="3-2", slot=1, ln=0):
    return xl(ts, comm, pid, "xhci_urb_enqueue", "%s ep0out-control: urb %016x pipe 2147484288 slot %d length 0/%d sgs 0/0 stream 0 flags 00110200" % (dev, urb, slot, ln))


def x_gb(ts, urb=URB, dev="3-2", slot=1):
    return xl(ts, "<idle>", 0, "xhci_urb_giveback", "%s ep0out-control: urb %016x pipe 2147484288 slot %d length 0/0 sgs 0/0 stream 0 flags 00110200" % (dev, urb, slot))


def x_deq(ts, comm, pid, urb=URB, dev="3-2", slot=1):
    return xl(ts, comm, pid, "xhci_urb_dequeue", "%s ep0out-control: urb %016x pipe 2147484288 slot %d length 0/0 sgs 0/0 stream 0 flags 00110200" % (dev, urb, slot))


SET_TRBS = [0xFFFD8510, 0xFFFD8520]


def x_trbs(ts, comm, pid, bm=0x01, b=0x0B, v=0, i=1, l=0, base=0xFFFD8510):
    return [xl(ts, comm, pid, "xhci_queue_trb", "CTRL: @0x%016x bRequestType %02x bRequest %02x wValue %04x wIndex %04x wLength %d length 8 TD size 0 intr 0 type 'Setup Stage' flags I:i:c" % (base, bm, b, v, i, l)),
            xl(ts, comm, pid, "xhci_queue_trb", "CTRL: @0x%016x Buffer 0000000000000000 length 0 TD size 0 intr 0 type 'Status Stage' flags I:c:e:C" % (base + 16))]


def x_db(ts, comm, pid, slot=1):
    return xl(ts, comm, pid, "xhci_ring_ep_doorbell", "Ring doorbell for Slot %d ep0in" % slot)


def x_evt(ts, trb=0xFFFD8520, status="Success", slot=1):
    return xl(ts, "<idle>", 0, "xhci_handle_event", "EVENT: @0x00000000ffffc610 TRB %016x status '%s' len 0 slot %d ep 1 type 'Transfer Event' flags e:c" % (trb, status, slot), cpu=3)


def x_cmd(ts, ev):
    return xl(ts, "<idle>", 0, ev, "slot 1 ep 1 state 3")


def life_for(*lines, ret=-110, submit=6.884, complete=12.370, extra_uw=True):
    w = "(udev-worker)"
    base = [uw_urb(submit, w, 7, SET_IFACE_1_0), uwr_urb(complete, w, 7, ret=ret)]
    res = T.analyze(trace(*(base + list(lines))))
    return res["xhci_lifecycles"][0]["lifecycle"] if res["xhci_lifecycles"] else None


class XhciLifecycle(unittest.TestCase):
    w = "(udev-worker)"

    def stalled(self, doorbell=True, enqueue=True, trbs=True, event=None, dequeue=True, recovery=False):
        ls = []
        if enqueue:
            ls.append(x_enq(6.8841, self.w, 7))
        if trbs:
            ls += x_trbs(6.8842, self.w, 7)
        if doorbell:
            ls.append(x_db(6.8843, self.w, 7))
        if event:
            ls.append(x_evt(event[0], status=event[1]))
        if dequeue:
            ls.append(x_deq(12.3699, self.w, 7))
        if recovery:
            ls += [x_cmd(12.371, "xhci_handle_cmd_stop_ep"), x_cmd(12.372, "xhci_handle_cmd_set_deq")]
        ls.append(x_gb(12.373))
        return ls

    def test_stopped_event_after_own_dequeue_is_not_an_error_and_locates_the_stall(self):
        ls = self.stalled(recovery=True)
        ls.insert(-1, x_evt(12.3700, trb=0xFFFD8520, status="Stopped"))      # status-stage TRB
        lf = life_for(*ls)
        self.assertTrue(lf["classification"].startswith("C/E"), lf["classification"])
        self.assertEqual(lf["stopped_at_trb"], "Status Stage")
        self.assertEqual(lf["transfer_events"], [])
        self.assertEqual(lf["post_cancel_events"][0]["status"], "Stopped")
        self.assertIn("Setup stage had completed", lf["classification"])

    def test_stopped_at_setup_stage_is_reported_as_such(self):
        ls = self.stalled()
        ls.insert(-1, x_evt(12.3700, trb=0xFFFD8510, status="Stopped"))      # setup-stage TRB
        lf = life_for(*ls)
        self.assertEqual(lf["stopped_at_trb"], "Setup Stage")
        self.assertNotIn("Setup stage had completed", lf["classification"])

    def test_error_before_cancel_is_still_D_but_stopped_after_is_not(self):
        lf = life_for(*self.stalled(event=(9.0, "USB Transaction Error")), ret=-71)
        self.assertTrue(lf["classification"].startswith("D"))

    def test_later_unrelated_reset_ep_commands_are_not_attributed(self):
        ls = self.stalled(recovery=True) + [x_cmd(12.45, "xhci_handle_cmd_reset_ep"), x_cmd(12.46, "xhci_handle_cmd_set_deq")]
        self.assertEqual(len(life_for(*ls)["recovery_commands"]), 2)

    def test_pending_transfer_cancelled_after_timeout_is_C_E(self):
        lf = life_for(*self.stalled(recovery=True))
        self.assertTrue(lf["classification"].startswith("C/E"), lf["classification"])
        self.assertEqual((lf["slot"], lf["devname"]), (1, "3-2"))
        self.assertTrue(lf["setup_trb_matches"])
        self.assertIsNotNone(lf["doorbell"])
        self.assertEqual(lf["transfer_events"], [])
        self.assertIsNotNone(lf["urb_dequeue"])
        self.assertEqual(len(lf["recovery_commands"]), 2)
        self.assertIsNotNone(lf["giveback"])

    def test_other_tasks_enqueue_is_not_joined(self):
        ls = self.stalled()
        ls[0] = x_enq(6.8841, "someone-else", 99)
        self.assertTrue(life_for(*ls)["classification"].startswith("A"))

    def test_other_bus_is_not_joined(self):
        ls = self.stalled()
        ls[0] = x_enq(6.8841, self.w, 7, dev="5-1")
        self.assertTrue(life_for(*ls)["classification"].startswith("A"))

    def test_hashed_pointer_differs_from_any_raw_value_so_it_is_never_compared(self):
        lf = life_for(*self.stalled())
        blob = json.dumps(lf)
        self.assertNotIn('"urb"', blob)
        self.assertNotIn('"dma"', blob)
        self.assertNotIn("fffd85", blob.lower())

    def test_root_hub_requests_are_not_classified_as_unqueued(self):
        lf = T.xhci_lifecycle([], {"ts_submit": 1.0, "ts_complete": 1.1, "setup": T.split_setup(SET_IFACE_1_0),
                                   "pid": 1, "bus": 3, "devnum": 1})
        self.assertTrue(lf["classification"].startswith("ROOT-HUB"))

    def test_late_unrelated_enqueue_beyond_join_gap_is_not_joined(self):
        ls = self.stalled()
        ls[0] = x_enq(6.884 + 0.011, self.w, 7)
        self.assertTrue(life_for(*ls)["classification"].startswith("A"))

    def test_not_queued_is_A(self):
        lf = life_for(*self.stalled(enqueue=False, trbs=False, doorbell=False))
        self.assertTrue(lf["classification"].startswith("A"), lf["classification"])

    def test_queued_without_doorbell_is_B(self):
        lf = life_for(*self.stalled(doorbell=False))
        self.assertTrue(lf["classification"].startswith("B"), lf["classification"])

    def test_error_completion_is_D(self):
        lf = life_for(*self.stalled(event=(12.0, "USB Transaction Error")), ret=-71)
        self.assertTrue(lf["classification"].startswith("D"), lf["classification"])
        self.assertIn("USB Transaction Error", lf["classification"])

    def test_success_event_for_a_timed_out_urb_is_not_called_a_stall(self):
        lf = life_for(*self.stalled(event=(7.0, "Success")))
        self.assertTrue(lf["classification"].startswith("NORMAL"), lf["classification"])

    def test_event_for_another_slot_or_trb_does_not_count(self):
        ls = self.stalled()
        ls.insert(3, x_evt(7.0, slot=5))
        ls.insert(3, x_evt(7.0, trb=0xDEAD0000))
        self.assertTrue(life_for(*ls)["classification"].startswith("C/E"))

    def test_reused_urb_pointer_is_joined_by_time_not_pointer_alone(self):
        earlier = [x_enq(1.0, self.w, 7), *x_trbs(1.0001, self.w, 7), x_db(1.0002, self.w, 7), x_evt(1.001), x_gb(1.0011)]
        lf = life_for(*(earlier + self.stalled()))
        self.assertAlmostEqual(lf["urb_enqueue"], 6.8841, places=4)
        self.assertTrue(lf["classification"].startswith("C/E"))

    def test_setup_mismatch_between_core_and_controller_is_flagged(self):
        ls = self.stalled()
        ls[1] = x_trbs(6.8842, self.w, 7, i=2)[0]
        lf = life_for(*ls)
        self.assertFalse(lf["setup_trb_matches"])
        self.assertTrue(lf["classification"].startswith("UNCERTAIN"))

    def test_no_xhci_records_means_no_lifecycle_claim(self):
        txt = trace(uw_urb(6.884, self.w, 7, SET_IFACE_1_0), uwr_urb(12.370, self.w, 7, ret=-110))
        self.assertEqual(T.analyze(txt)["xhci_lifecycles"], [])

    def test_real_format_lines_parse(self):
        line = "           lsusb-7095    [000] d..1.   690.471853: xhci_queue_trb: CTRL: @0x00000000fffd8510 bRequestType 80 bRequest 06 wValue 0f00 wIndex 0000 wLength 5 length 8 TD size 0 intr 0 type 'Setup Stage' flags I:i:c"
        r = T.parse_xhci(line)[0]
        self.assertEqual(r["setup"], {"bmRequestType": 0x80, "bRequest": 6, "wValue": 0x0F00, "wIndex": 0, "wLength": 5})
        ev = "          <idle>-0       [003] d.h2.   690.476670: xhci_handle_event: EVENT: @0x00000000ffffc610 TRB 00000000fffd8530 status 'Success' len 0 slot 1 ep 1 type 'Transfer Event' flags e:c"
        e = T.parse_xhci(ev)[0]
        self.assertEqual((e["slot"], e["ep"], e["status"], e["trb_ptr"]), (1, 1, "Success", 0xFFFD8530))
        enq = "           lsusb-7095    [000] .....   690.471853: xhci_urb_enqueue: 6-4 ep0out-control: urb 00000000bc2e0f8e pipe 2147484288 slot 1 length 0/5 sgs 0/0 stream 0 flags 00110200"
        u = T.parse_xhci(enq)[0]
        self.assertEqual((u["dev"], u["slot"], u["urb"]), ("6-4", 1, 0xBC2E0F8E))


CONF = """timeout: 5
default_entry: 2

comment: machine-id=00000000000000000000000000000000
/+CachyOS
  //linux-cachyos
  ### auto
  comment: Kernel version: 7.2.10-1-cachyos
  protocol: linux
  module_path: boot():/m/linux-cachyos/initramfs#aaaa
  path: boot():/m/linux-cachyos/vmlinuz#bbbb
  cmdline: quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=00000000

  //linux-cachyos-lts
  protocol: linux
  path: boot():/m/lts/vmlinuz#cccc
  cmdline: quiet rw
"""


class LimineEntry(unittest.TestCase):
    def test_entry_keeps_original_args_and_adds_ours_once(self):
        new = T.add_entry(CONF)
        line = [l for l in new.splitlines() if "cmdline:" in l and "kprobe_event" in l]
        self.assertEqual(len(line), 1)
        base = "quiet nowatchdog splash rw rootflags=subvol=/@ root=UUID=00000000"
        self.assertTrue(line[0].strip().startswith("cmdline: " + base + " kprobe_event="))
        self.assertEqual(line[0].count("dyndbg="), 1)
        self.assertNotIn("usbcore.quirks", new)
        self.assertIn("path: boot():/m/linux-cachyos/vmlinuz#bbbb", new)
        self.assertEqual(new.count("module_path: boot():/m/linux-cachyos/initramfs#aaaa"), 2)

    def test_existing_entries_and_default_index_untouched(self):
        new = T.add_entry(CONF)
        self.assertTrue(new.startswith(CONF.rstrip("\n")))
        self.assertIn("default_entry: 2", new)

    def test_install_remove_roundtrip(self):
        self.assertEqual(T.remove_entry(T.add_entry(CONF)), CONF)

    def test_remove_keeps_unrelated_changes_made_in_between(self):
        new = T.add_entry(CONF).replace("timeout: 5", "timeout: 7")
        self.assertEqual(T.remove_entry(new), CONF.replace("timeout: 5", "timeout: 7"))

    def test_refuses_double_install_and_missing_removal(self):
        with self.assertRaises(ValueError):
            T.add_entry(T.add_entry(CONF))
        with self.assertRaises(ValueError):
            T.remove_entry(CONF)

    def test_refuses_source_with_test_args(self):
        bad = CONF.replace("splash rw", "splash dyndbg=+p rw", 1)
        with self.assertRaises(ValueError):
            T.add_entry(bad)
        with self.assertRaises(ValueError):
            T.add_entry(CONF.replace("quiet nowatchdog", "usbcore.quirks=29c2:0004:g quiet nowatchdog"))

    def test_unterminated_block_refused(self):
        with self.assertRaises(ValueError):
            T.remove_entry(CONF + T.ENTRY_BEGIN + "\n/x\n")


class CollectAndClean(unittest.TestCase):
    @staticmethod
    def slurp(path):
        with open(path) as f:
            return f.read()

    def make_fake_tracefs(self, d):
        os.makedirs(os.path.join(d, "events", T.GROUP))
        for name, content in (("trace", "# entries-in-buffer/entries-written: 1/1\n"), ("kprobe_events", ""),
                              ("buffer_size_kb", "8192\n"), (os.path.join("events", T.GROUP, "enable"), "1")):
            with open(os.path.join(d, name), "w") as f:
                f.write(content)

    def test_collect_saves_private_raw_and_removes_only_our_probes(self):
        with tempfile.TemporaryDirectory() as tf:
            fake, out = os.path.join(tf, "tracefs"), os.path.join(tf, "out")
            os.makedirs(fake)
            self.make_fake_tracefs(fake)
            T.collect(out, tracefs=fake, require_root=False)
            raw = os.path.join(out, "raw_trace.txt")
            self.assertEqual(stat.S_IMODE(os.stat(raw).st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(os.stat(out).st_mode), 0o700)
            removed = [l.strip() for l in self.slurp(os.path.join(fake, "kprobe_events")).splitlines()]
            self.assertEqual(removed, ["-:owtrace/%s" % n for n, _ in T.PROBES])
            self.assertEqual(self.slurp(os.path.join(fake, "events", T.GROUP, "enable")), "0")
            self.assertEqual(self.slurp(os.path.join(fake, "buffer_size_kb")), "1408")

    def test_verify_clean_detects_leftovers(self):
        with tempfile.TemporaryDirectory() as tf:
            self.make_fake_tracefs(tf)
            self.assertTrue(T.verify_clean(tf))
            shutil.rmtree(os.path.join(tf, "events", T.GROUP))
            self.assertEqual(T.verify_clean(tf), [])
            with open(os.path.join(tf, "kprobe_events"), "w") as f:
                f.write("p:owtrace/uw usb_start_wait_urb\n")
            self.assertTrue(T.verify_clean(tf))

    def test_collect_requires_existing_events(self):
        with tempfile.TemporaryDirectory() as tf:
            with self.assertRaises(SystemExit):
                T.collect(os.path.join(tf, "o"), tracefs=tf, require_root=False)


if __name__ == "__main__":
    unittest.main(verbosity=1)
