#!/usr/bin/env python3
"""Boot-time USB endpoint-0 request tracing for CONNECT 2 (Phase 8A.19).

Capture is done by the kernel itself: four kprobe events defined on the kernel command line
(`kprobe_event=`), recorded into the in-memory ftrace ring buffer from early boot. Nothing is
loaded or attached from userspace and nothing persists across a reboot.

Subcommands:
  probes                 print the probe definitions and the boot argument (no side effects)
  analyze TRACE          decode a saved trace offline (no privileges, no kernel access)
  collect --out-dir DIR  (root) save the raw trace, stop tracing, remove only our probes, verify
  verify-clean           (root) check tracefs holds none of our state
  entry status|install|remove   (root) manage the temporary Limine boot entry
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

TARGET_VID = 0x29C2
TARGET_PID = 0x0004
GROUP = "owtrace"
TRACEFS = "/sys/kernel/tracing"
LIMINE_CONF = "/boot/limine.conf"
ENTRY_BEGIN = "# OPENWITTY-8A19-BEGIN (temporary USB trace entry; remove after use)"
ENTRY_END = "# OPENWITTY-8A19-END"
ENTRY_NAME = "OpenWitty USB trace (temporary)"
# `ftrace=nop` makes the kernel expand the boot ring buffer to trace_buf_size at once
# (trace.c set_cmdline_ftrace); without it the global buffer stays at its 7 KB/CPU minimum (Phase 8A.19 attempt 1).
BUF_ARGS = ["ftrace=nop", "trace_buf_size=32M"]

# xHCI trace events (built-in xhci-hcd; verified present in the running 7.2.10-1-cachyos tracefs). They follow a
# control URB from enqueue through TRB queueing, doorbell, transfer event, cancellation and giveback.
XHCI_EVENTS = ["xhci_urb_enqueue", "xhci_urb_giveback", "xhci_urb_dequeue", "xhci_queue_trb",
               "xhci_ring_ep_doorbell", "xhci_handle_event", "xhci_handle_transfer", "xhci_handle_command",
               "xhci_dbg_cancel_urb", "xhci_dbg_reset_ep", "xhci_handle_cmd_stop_ep", "xhci_handle_cmd_set_deq",
               "xhci_handle_cmd_reset_ep", "xhci_handle_cmd_config_ep"]


def trace_event_arg():
    return "trace_event=" + ",".join("xhci-hcd:" + e for e in XHCI_EVENTS)
DYNDBG_ARG = 'dyndbg="file drivers/usb/core/message.c line 88 +p"'

# struct offsets of the installed 7.2.10-1-cachyos build (pahole on its BTF).
OFF_URB_DEV, OFF_URB_PIPE, OFF_URB_BUF, OFF_URB_BUFLEN, OFF_URB_SETUP = 64, 80, 96, 136, 144
OFF_DEV_DEVNUM, OFF_DEV_BUS, OFF_DEV_VID, OFF_DEV_PID, OFF_BUS_NUM = 0, 80, 976, 978, 16

# (name, definition). No commas inside a definition: the boot parser turns commas into spaces.
PROBES = [
    ("uw", "p:%s/uw usb_start_wait_urb setup=+0(+%d(%%di)):x64 pipe=+%d(%%di):x32 len=+%d(%%di):u32 "
           "tmo=%%si:s32 devnum=+%d(+%d(%%di)):s32 bus=+%d(+%d(+%d(%%di))):s32 "
           "vid=+%d(+%d(%%di)):x16 pid=+%d(+%d(%%di)):x16 outdata=+0(+%d(%%di)):x64"
     % (GROUP, OFF_URB_SETUP, OFF_URB_PIPE, OFF_URB_BUFLEN, OFF_DEV_DEVNUM, OFF_URB_DEV,
        OFF_BUS_NUM, OFF_DEV_BUS, OFF_URB_DEV, OFF_DEV_VID, OFF_URB_DEV, OFF_DEV_PID, OFF_URB_DEV,
        OFF_URB_BUF)),
    ("uwr", "r:%s/uwr usb_start_wait_urb ret=$retval:s32 indata=+0(+%d($arg1)):x64 actual=+0($arg3):s32"
     % (GROUP, OFF_URB_BUF)),
    ("si", "p:%s/si usb_set_interface devnum=+%d(%%di):s32 vid=+%d(%%di):x16 pid=+%d(%%di):x16 "
           "iface=%%si:s32 alt=%%dx:s32" % (GROUP, OFF_DEV_DEVNUM, OFF_DEV_VID, OFF_DEV_PID)),
    ("sir", "r:%s/sir usb_set_interface ret=$retval:s32" % GROUP),
]


def boot_arg():
    return "kprobe_event=" + ";".join(d.replace(" ", ",") for _, d in PROBES)


def extra_cmdline_args():
    return [boot_arg(), trace_event_arg()] + BUF_ARGS + [DYNDBG_ARG]


# ---------------------------------------------------------------- USB setup decoding

STD_REQUESTS = {0: "GET_STATUS", 1: "CLEAR_FEATURE", 3: "SET_FEATURE", 5: "SET_ADDRESS",
                6: "GET_DESCRIPTOR", 7: "SET_DESCRIPTOR", 8: "GET_CONFIGURATION",
                9: "SET_CONFIGURATION", 10: "GET_INTERFACE", 11: "SET_INTERFACE",
                12: "SYNCH_FRAME", 0x30: "SET_SEL", 0x31: "SET_ISOCH_DELAY"}
TYPES = {0: "standard", 1: "class", 2: "vendor", 3: "reserved"}
RECIPIENTS = {0: "device", 1: "interface", 2: "endpoint", 3: "other"}
ERRNO = {0: "OK", -2: "ENOENT(killed)", -19: "ENODEV", -32: "EPIPE", -71: "EPROTO",
         -75: "EOVERFLOW", -108: "ESHUTDOWN", -110: "ETIMEDOUT", -5: "EIO", -12: "ENOMEM",
         -104: "ECONNRESET"}
UAC_CUR, UAC_RANGE = 1, 2


def split_setup(value):
    """u64 read little-endian from the 8 SETUP bytes -> dict (raises ValueError if out of range)."""
    if not isinstance(value, int) or value < 0 or value >= 1 << 64:
        raise ValueError("setup must be a 64-bit unsigned integer")
    return {"bmRequestType": value & 0xFF, "bRequest": (value >> 8) & 0xFF,
            "wValue": (value >> 16) & 0xFFFF, "wIndex": (value >> 32) & 0xFFFF,
            "wLength": (value >> 48) & 0xFFFF}


def describe(s):
    bm, req = s["bmRequestType"], s["bRequest"]
    d = {"direction": "IN" if bm & 0x80 else "OUT", "type": TYPES[(bm >> 5) & 3],
         "recipient": RECIPIENTS.get(bm & 0x1F, "reserved"), "name": None}
    if d["type"] == "standard":
        d["name"] = STD_REQUESTS.get(req, "std_0x%02x" % req)
        if req == 11 and d["recipient"] == "interface":
            d["interface"], d["alternate"] = s["wIndex"], s["wValue"]
        if req == 6:
            d["descriptor_type"], d["descriptor_index"] = s["wValue"] >> 8, s["wValue"] & 0xFF
        if req == 9:
            d["configuration"] = s["wValue"] & 0xFF
    elif d["type"] == "class" and d["recipient"] == "interface":
        d["name"] = {UAC_CUR: "UAC_CUR", UAC_RANGE: "UAC_RANGE"}.get(req, "class_0x%02x" % req)
        d["entity"], d["interface"] = s["wIndex"] >> 8, s["wIndex"] & 0xFF
        d["control_selector"], d["channel"] = s["wValue"] >> 8, s["wValue"] & 0xFF
    else:
        d["name"] = "req_0x%02x" % req
    return d


def is_fu2(s, d):
    return (d["type"] == "class" and d["recipient"] == "interface" and s["wIndex"] == 0x0200
            and s["bRequest"] in (UAC_CUR, UAC_RANGE) and s["wValue"] in (0x0100, 0x0200))


def q88_db(raw):
    raw &= 0xFFFF
    if raw & 0x8000:
        raw -= 0x10000
    return raw / 256.0


# ---------------------------------------------------------------- trace parsing

LINE = re.compile(r"^\s*(?P<comm>.+)-(?P<pid>\d+)\s+\[(?P<cpu>\d+)\]\s+\S+\s+(?P<ts>\d+\.\d+): "
                  r"(?P<ev>uw|uwr|si|sir): \((?P<where>[^)]*)\)\s*(?P<kv>.*)$")
HEADER = re.compile(r"entries-in-buffer/entries-written:\s*(\d+)/(\d+)")
KV = re.compile(r"(\w+)=(-?(?:0x[0-9a-fA-F]+|\d+))")


def parse_trace(text):
    """-> (records, info). Malformed event lines are counted, never silently accepted."""
    recs, bad, loss = [], 0, False
    info = {"entries_in_buffer": None, "entries_written": None, "malformed_lines": 0}
    for line in text.splitlines():
        if line.startswith("#"):
            m = HEADER.search(line)
            if m:
                info["entries_in_buffer"], info["entries_written"] = int(m.group(1)), int(m.group(2))
                loss = int(m.group(1)) < int(m.group(2))
            continue
        if not line.strip():
            continue
        m = LINE.match(line)
        if not m:
            if re.search(r"\b(uw|uwr|si|sir):", line):
                bad += 1
            continue
        kv = {}
        for k, v in KV.findall(m.group("kv")):
            kv[k] = int(v, 0)
        recs.append({"comm": m.group("comm"), "pid": int(m.group("pid")), "cpu": int(m.group("cpu")),
                     "ts": float(m.group("ts")), "ev": m.group("ev"), "where": m.group("where"), "kv": kv})
    info["malformed_lines"] = bad
    info["loss"] = loss
    return recs, info


def pair_requests(recs):
    """Pair uw (entry) with the next uwr (return) of the same task. -> (requests, unpaired)"""
    open_by_pid, out, unpaired = {}, [], 0
    for r in recs:
        if r["ev"] == "uw":
            if r["pid"] in open_by_pid:
                unpaired += 1  # a previous entry never returned in this task's stream
            open_by_pid[r["pid"]] = r
        elif r["ev"] == "uwr":
            e = open_by_pid.pop(r["pid"], None)
            if e is None:
                unpaired += 1
                continue
            out.append({"entry": e, "ret": r})
    unpaired += len(open_by_pid)
    return out, unpaired


CALLER = re.compile(r"^(?P<caller>.+?)\s+<-\s+")


def pair_set_interface(recs, vid=TARGET_VID, pid=TARGET_PID):
    """Pair `si` (entry) with the next `sir` (return) of the same task.

    Returns (calls, orphan_returns). An orphan return (entry overwritten or never recorded) keeps its
    status and caller but has no interface/alternate; it must never be reported as a complete call."""
    open_by_pid, calls, orphans = {}, [], []
    for r in recs:
        if r["ev"] == "si":
            open_by_pid[r["pid"]] = r
        elif r["ev"] == "sir":
            e = open_by_pid.pop(r["pid"], None)
            m = CALLER.match(r["where"])
            caller = m.group("caller") if m else r["where"]
            ret = r["kv"].get("ret")
            if e is None:
                orphans.append({"ts_return": r["ts"], "task": r["comm"], "ret": ret,
                                "ret_name": ERRNO.get(ret, str(ret)), "caller": caller})
                continue
            if e["kv"].get("vid") != vid or e["kv"].get("pid") != pid:
                continue
            calls.append({"ts_entry": e["ts"], "ts_return": r["ts"], "elapsed_s": round(r["ts"] - e["ts"], 6),
                          "task": e["comm"], "devnum": e["kv"].get("devnum"), "interface": e["kv"].get("iface"),
                          "alternate": e["kv"].get("alt"), "ret": ret, "ret_name": ERRNO.get(ret, str(ret)),
                          "caller": caller})
    return calls, orphans


def build_requests(recs, vid=TARGET_VID, pid=TARGET_PID):
    pairs, unpaired = pair_requests(recs)
    reqs, other, malformed = [], 0, 0
    for p in pairs:
        e, r = p["entry"], p["ret"]
        kv, rk = e["kv"], r["kv"]
        if kv.get("vid") != vid or kv.get("pid") != pid:
            other += 1
            continue
        try:
            s = split_setup(kv["setup"])
        except (KeyError, ValueError):
            malformed += 1
            reqs.append({"error": "malformed_setup", "ts": e["ts"]})
            continue
        d = describe(s)
        ret = rk.get("ret")
        if ret is not None and ret >= (1 << 31):
            ret -= 1 << 32
        entry_out = kv.get("outdata", 0)
        in_data = rk.get("indata", 0)
        reqs.append({
            "ts_submit": e["ts"], "ts_complete": r["ts"], "elapsed": r["ts"] - e["ts"],
            "comm": e["comm"], "bus": kv.get("bus"), "devnum": kv.get("devnum"),
            "setup": s, "desc": d, "length_field": s["wLength"], "timeout_ms": kv.get("tmo"),
            "ret": ret, "ret_name": ERRNO.get(ret, str(ret)),
            "actual": rk.get("actual"), "fu2": is_fu2(s, d), "pid": e["pid"],
            "data_out": entry_out if not (s["bmRequestType"] & 0x80) else None,
            "data_in": in_data if (s["bmRequestType"] & 0x80) else None})
    return reqs, other, unpaired, malformed


def is_timeout(q):
    t = q.get("timeout_ms") or 0
    if "error" in q or q["ret"] is None or q["ret"] >= 0:
        return False
    return q["ret"] == -110 or (t > 0 and q["elapsed"] * 1000.0 >= 0.95 * t)


# ---------------------------------------------------------------- analysis

def fu2_timeline(reqs):
    out = []
    for q in reqs:
        if "error" in q or not q["fu2"]:
            continue
        s, d = q["setup"], q["desc"]
        vol = s["wValue"] == 0x0200
        item = {"t": q["ts_submit"], "dir": d["direction"], "kind": "volume" if vol else "mute",
                "request": d["name"], "ret": q["ret"], "elapsed_ms": round(q["elapsed"] * 1000, 3),
                "len": s["wLength"]}
        raw = q["data_out"] if d["direction"] == "OUT" else q["data_in"]
        if q["ret"] is not None and q["ret"] >= 0 and d["name"] == "UAC_CUR" and raw is not None:
            if vol and s["wLength"] == 2:
                item["raw"] = raw & 0xFFFF
                item["db"] = q88_db(raw)
            elif not vol and s["wLength"] == 1:
                item["raw"] = raw & 0xFF
        out.append(item)
    return out


def first_timeout_detail(reqs, calls, orphans):
    """Everything known about the EARLIEST timed-out EP0 operation, with explicit gaps.

    Candidates are (a) paired uw/uwr requests that timed out, (b) usb_set_interface calls that returned -110
    and (c) orphan usb_set_interface returns. The earliest by completion time wins; a usb_set_interface call
    is joined with a timed-out request that falls inside its interval (same task)."""
    cands = []
    for q in reqs:
        if "error" not in q and is_timeout(q):
            cands.append((q["ts_complete"], "req", q))
    for c in calls:
        if c["ret"] == -110:
            cands.append((c["ts_return"], "si", c))
    for o in orphans:
        if o["ret"] == -110:
            cands.append((o["ts_return"], "orphan", o))
    if not cands:
        return None
    cands.sort(key=lambda x: x[0])
    _, kind, item = cands[0]
    si, req, orphan = None, None, None
    if kind == "req":
        req = item
        si = next((c for c in calls if c["ret"] == -110 and c["task"] == req["comm"]
                   and c["ts_entry"] <= req["ts_submit"] <= c["ts_return"]), None)
    elif kind == "si":
        si = item
        req = next((q for q in reqs if "error" not in q and is_timeout(q) and q["comm"] == si["task"]
                    and si["ts_entry"] <= q["ts_submit"] <= si["ts_return"]), None)
    else:
        orphan = item
    d = {"request_captured": req is not None, "set_interface_entry_captured": si is not None}
    if req is not None:
        d.update({"task": req["comm"], "bus": req["bus"], "devnum": req["devnum"], "setup": req["setup"],
                  "meaning": req["desc"]["name"], "interface": req["desc"].get("interface"),
                  "alternate": req["desc"].get("alternate"), "ts_submit": req["ts_submit"],
                  "ts_complete": req["ts_complete"], "duration_s": round(req["elapsed"], 6), "ret": req["ret"]})
    if si is not None:
        d["usb_set_interface"] = si
        if req is None:
            # The control URB records are missing: the SETUP below is derived from the call arguments
            # (SET_INTERFACE is bm=0x01 b=0x0b wValue=alternate wIndex=interface wLength=0), not observed.
            d.update({"task": si["task"], "interface": si["interface"], "alternate": si["alternate"],
                      "ret": si["ret"], "devnum": si["devnum"], "setup_derived_not_observed": {
                          "bmRequestType": 0x01, "bRequest": 0x0B, "wValue": si["alternate"],
                          "wIndex": si["interface"], "wLength": 0}})
    if req is None:
        d["missing"] = "uw/uwr records (SETUP packet, submit and completion of the control URB)"
        if si is None:
            d["missing"] += " and the usb_set_interface entry"
        if orphan is not None:
            d["orphan_return"] = orphan
    return d


def analyze(text, dmesg=None, report=None, expect_xhci=False):
    recs, info = parse_trace(text)
    reqs, other, unpaired, malformed = build_requests(recs)
    timeouts = [q for q in reqs if is_timeout(q)]
    res = {"info": info, "records": len(recs), "target_requests": len(reqs),
           "non_target_requests": other, "unpaired_events": unpaired, "malformed_setups": malformed,
           "timeouts": [], "fu2": fu2_timeline(reqs), "set_interface_requests": [],
           "complete": (not info["loss"] and unpaired == 0 and malformed == 0
                        and info["malformed_lines"] == 0)}
    for q in timeouts:
        res["timeouts"].append({
            "ts_submit": q["ts_submit"], "ts_complete": q["ts_complete"],
            "elapsed_s": round(q["elapsed"], 6), "ret": q["ret"], "ret_name": q["ret_name"],
            "task": q["comm"], "bmRequestType": q["setup"]["bmRequestType"],
            "bRequest": q["setup"]["bRequest"], "wValue": q["setup"]["wValue"],
            "wIndex": q["setup"]["wIndex"], "wLength": q["setup"]["wLength"],
            "meaning": q["desc"]["name"], "direction": q["desc"]["direction"],
            "interface": q["desc"].get("interface"), "alternate": q["desc"].get("alternate")})
    for q in reqs:
        if "error" in q:
            continue
        if q["desc"]["name"] == "SET_INTERFACE":
            res["set_interface_requests"].append({
                "ts_submit": q["ts_submit"], "elapsed_s": round(q["elapsed"], 6), "task": q["comm"],
                "interface": q["desc"].get("interface"), "alternate": q["desc"].get("alternate"),
                "ret": q["ret"], "ret_name": q["ret_name"]})
    xrecs = parse_xhci(text)
    res["xhci_records"] = len(xrecs)
    res["xhci_lifecycles"] = [{"task": q["comm"], "ts_submit": q["ts_submit"], "ts_complete": q["ts_complete"],
                               "setup": q["setup"], "lifecycle": xhci_lifecycle(xrecs, q)} for q in timeouts] if xrecs else []
    calls, orphans = pair_set_interface(recs)
    res["usb_set_interface_calls"] = calls
    res["orphan_set_interface_returns"] = orphans
    res["first_timeout"] = first_timeout_detail(reqs, calls, orphans)
    first_to = timeouts[0] if timeouts else None
    if first_to is not None:
        after = [f for f in res["fu2"] if f["t"] >= first_to["ts_complete"]]
        if after:
            res["first_timeout_to_first_fu2_s"] = round(after[0]["t"] - first_to["ts_complete"], 6)
    if dmesg:
        res["debug_message_ties"] = tie_debug_messages(dmesg, timeouts)
    res["completeness"] = completeness(res, report, expect_xhci)
    return res


DBG = re.compile(r"\[\s*(\d+\.\d+)\]\s+usb [\d.-]+: (?P<task>.+?) timed out or killed on ep0out "
                 r"len=(?P<a>\d+)/(?P<b>\d+)")


def tie_debug_messages(dmesg_text, timeouts, tol=0.005):
    ties = []
    for line in dmesg_text.splitlines():
        m = DBG.search(line)
        if not m:
            continue
        ts = float(m.group(1))
        match = [t for t in timeouts if abs(t["ts_complete"] - ts) <= tol and t["comm"] == m.group("task")]
        ties.append({"dmesg_ts": ts, "task": m.group("task"), "matched_request": bool(match),
                     "delta_ms": round((ts - match[0]["ts_complete"]) * 1000, 3) if match else None})
    return ties



# ---------------------------------------------------------------- xHCI lifecycle (Phase 8A.20)

XLINE = re.compile(r"^\s*(?P<comm>.+)-(?P<pid>\d+)\s+\[(?P<cpu>\d+)\]\s+\S+\s+(?P<ts>\d+\.\d+): "
                   r"(?P<ev>xhci_\w+): (?P<text>.*)$")
X_URB = re.compile(r"^(?P<dev>\S+) ep(?P<ep>\d+)(?P<dir>in|out)-(?P<type>\w+): urb (?P<urb>[0-9a-f]+) "
                   r"pipe (?P<pipe>\d+) slot (?P<slot>\d+) length (?P<actual>-?\d+)/(?P<len>-?\d+)")
X_SETUP = re.compile(r"^(?P<ring>\w+): @(?P<dma>0x[0-9a-f]+) bRequestType (?P<bm>[0-9a-f]{2}) bRequest (?P<b>[0-9a-f]{2}) "
                     r"wValue (?P<v>[0-9a-f]{4}) wIndex (?P<i>[0-9a-f]{4}) wLength (?P<l>\d+) .*type 'Setup Stage'")
X_TRB = re.compile(r"^(?P<ring>\w+): @(?P<dma>0x[0-9a-f]+) .*type '(?P<type>[^']+)'")
X_DB = re.compile(r"Ring doorbell for Slot (?P<slot>\d+) ep(?P<ep>\d+)(?P<dir>in|out)")
X_EVT = re.compile(r"TRB (?P<trb>[0-9a-f]{16}) status '(?P<status>[^']*)' len (?P<len>-?\d+) slot (?P<slot>\d+) "
                   r"ep (?P<ep>\d+) type '(?P<type>[^']*)'")
X_PTR = re.compile(r"(?:urb |URB )(?P<urb>[0-9a-f]{8,16})")


def parse_xhci(text):
    out = []
    for line in text.splitlines():
        m = XLINE.match(line)
        if not m:
            continue
        r = {"comm": m.group("comm"), "pid": int(m.group("pid")), "cpu": int(m.group("cpu")),
             "ts": float(m.group("ts")), "ev": m.group("ev"), "text": m.group("text")}
        t = r["text"]
        if r["ev"] in ("xhci_urb_enqueue", "xhci_urb_giveback", "xhci_urb_dequeue"):
            u = X_URB.match(t)
            if u:
                r["urb"] = int(u.group("urb"), 16)
                r.update({"dev": u.group("dev"), "ep": int(u.group("ep")), "slot": int(u.group("slot")),
                          "len": int(u.group("len")), "actual": int(u.group("actual")), "utype": u.group("type")})
        elif r["ev"] == "xhci_queue_trb":
            su = X_SETUP.match(t)
            tb = X_TRB.match(t)
            if su:
                r["dma"] = int(su.group("dma"), 16)
                r["trb"] = "Setup Stage"
                r["setup"] = {"bmRequestType": int(su.group("bm"), 16), "bRequest": int(su.group("b"), 16),
                              "wValue": int(su.group("v"), 16), "wIndex": int(su.group("i"), 16),
                              "wLength": int(su.group("l"))}
            elif tb:
                r["dma"] = int(tb.group("dma"), 16)
                r["trb"] = tb.group("type")
        elif r["ev"] == "xhci_ring_ep_doorbell":
            d = X_DB.search(t)
            if d:
                r.update({"slot": int(d.group("slot")), "ep": int(d.group("ep")), "dir": d.group("dir")})
        elif r["ev"] in ("xhci_handle_event", "xhci_handle_transfer"):
            e = X_EVT.search(t)
            if e:
                r.update({"trb_ptr": int(e.group("trb"), 16), "status": e.group("status"), "slot": int(e.group("slot")),
                          "ep": int(e.group("ep")), "etype": e.group("type"), "resid": int(e.group("len"))})
        elif r["ev"] == "xhci_dbg_cancel_urb":
            c = X_PTR.search(t)
            if c:
                r["urb"] = int(c.group("urb"), 16)
        out.append(r)
    return out


def xhci_lifecycle(xrecs, req, window=0.5, join_gap=0.005):
    """Follow ONE USB-core request through the host controller.

    tracefs prints the xHCI `urb` pointer HASHED (`%p`), so it cannot be compared with a raw kernel pointer. The
    request is joined to its xhci_urb_enqueue by task (pid), bus prefix of the device name, a short time gap after
    the USB-core entry, and the SETUP bytes of the queued Setup Stage TRB. The (hashed) pointer printed by the xHCI
    events is then used only to follow that URB's dequeue/cancel/giveback events. Nothing here is exported."""
    t0, t1 = req["ts_submit"], req["ts_complete"]
    life = {"urb_enqueue": None, "trbs": [], "setup_trb_matches": None, "setup_trb": None, "doorbell": None,
            "transfer_events": [], "urb_dequeue": None, "cancel_urb": None, "recovery_commands": [],
            "giveback": None, "slot": None, "devname": None, "join": "task+bus+time+SETUP"}
    bus = req.get("bus")
    if req.get("devnum") == 1:
        life["classification"] = ("ROOT-HUB: the virtual root hub is served in software; no controller transfer exists "
                                  "(not applicable)")
        return life
    enq = [r for r in xrecs if r["ev"] == "xhci_urb_enqueue" and r["pid"] == req.get("pid")
           and r.get("utype") == "control" and t0 <= r["ts"] <= t0 + join_gap
           and (bus is None or str(r.get("dev", "")).startswith("%s-" % bus))]
    if not enq:
        life["classification"] = "A: no xhci_urb_enqueue by the requesting task after the USB-core submit (not queued to the controller)"
        return life
    e = min(enq, key=lambda r: r["ts"])
    urb = e["urb"]
    life.update({"urb_enqueue": e["ts"], "slot": e["slot"], "devname": e["dev"]})
    nxt = [r["ts"] for r in xrecs if r["ev"] == "xhci_urb_enqueue" and r["pid"] == e["pid"] and r["ts"] > e["ts"]]
    end = min(nxt) if nxt else t1 + window
    trbs = [r for r in xrecs if r["ev"] == "xhci_queue_trb" and r["pid"] == e["pid"] and e["ts"] <= r["ts"] < end and "dma" in r]
    life["trbs"] = [{"ts": r["ts"], "type": r.get("trb")} for r in trbs]  # DMA addresses stay internal
    setup = next((r for r in trbs if r.get("trb") == "Setup Stage"), None)
    life["setup_trb"] = setup["setup"] if setup else None
    life["setup_trb_matches"] = (setup is not None and setup["setup"] == req["setup"])
    dmas = {r["dma"] for r in trbs}
    trb_type = {r["dma"]: r.get("trb") for r in trbs}
    db = [r for r in xrecs if r["ev"] == "xhci_ring_ep_doorbell" and r.get("slot") == e["slot"] and r.get("ep") == 0
          and e["ts"] <= r["ts"] <= e["ts"] + 0.01]
    life["doorbell"] = db[0]["ts"] if db else None
    lo, hi = e["ts"], t1 + window
    ev = [r for r in xrecs if r["ev"] == "xhci_handle_event" and r.get("etype") == "Transfer Event" and r.get("slot") == e["slot"]
          and r.get("trb_ptr") in dmas and lo <= r["ts"] <= hi]
    deq = [r for r in xrecs if r["ev"] == "xhci_urb_dequeue" and r.get("urb") == urb and lo <= r["ts"] <= hi]
    cancel = [r for r in xrecs if r["ev"] == "xhci_dbg_cancel_urb" and r.get("urb") == urb and lo <= r["ts"] <= hi]
    life["urb_dequeue"] = deq[0]["ts"] if deq else None
    life["cancel_urb"] = cancel[0]["ts"] if cancel else None
    cut = life["urb_dequeue"]
    # A 'Stopped' Transfer Event after our own dequeue is the controller acknowledging the Stop Endpoint command
    # for the TD in progress; it is evidence of WHERE the transfer was, not an independent error.
    before = [r for r in ev if cut is None or r["ts"] < cut]
    after = [r for r in ev if cut is not None and r["ts"] >= cut]
    life["transfer_events"] = [{"ts": r["ts"], "status": r["status"], "resid": r["resid"]} for r in before]
    life["post_cancel_events"] = [{"ts": r["ts"], "status": r["status"], "resid": r["resid"],
                                   "trb": trb_type.get(r["trb_ptr"])} for r in after]
    stopped = next((r for r in after if r["status"].startswith("Stopped")), None)
    life["stopped_at_trb"] = trb_type.get(stopped["trb_ptr"]) if stopped else None
    gb = [r for r in xrecs if r["ev"] == "xhci_urb_giveback" and r.get("urb") == urb and lo <= r["ts"] <= hi]
    life["giveback"] = gb[0]["ts"] if gb else None
    wend = (life["giveback"] + 0.002) if life["giveback"] else (t1 + 0.01)
    wstart = cut if cut is not None else lo
    life["recovery_commands"] = [{"ts": r["ts"], "ev": r["ev"]} for r in xrecs
                                 if r["ev"] in ("xhci_handle_cmd_stop_ep", "xhci_handle_cmd_set_deq", "xhci_handle_cmd_reset_ep",
                                                "xhci_dbg_reset_ep") and wstart <= r["ts"] <= wend]
    life["classification"] = classify_xhci(life, req)
    return life


def classify_xhci(life, req):
    if life["urb_enqueue"] is None:
        return "A: no xhci_urb_enqueue by the requesting task (not queued to the controller)"
    if life["setup_trb_matches"] is False:
        return "UNCERTAIN: the queued Setup Stage TRB does not match the USB-core SETUP (join not trusted)"
    if not life["trbs"]:
        return "A/B: URB enqueued but no TRBs were queued"
    if life["doorbell"] is None:
        return "B: TRBs queued but no endpoint-0 doorbell was rung for the slot"
    bad = [t for t in life["transfer_events"] if not t["status"].startswith(("Success", "Short Packet"))]
    if bad:
        return "D: controller reported an error completion (%s) for this transfer before any cancellation" % bad[0]["status"]
    if life["transfer_events"]:
        return "NORMAL: transfer event %s received" % life["transfer_events"][0]["status"]
    if life["urb_dequeue"] is not None or life["cancel_urb"] is not None:
        where = ""
        if life.get("stopped_at_trb"):
            where = ("; Stop Endpoint found the endpoint executing the '%s' TRB" % life["stopped_at_trb"])
            if life["stopped_at_trb"] == "Status Stage":
                where += (" (the Setup stage had completed; the transfer never finished its Status stage, with no error event, "
                          "which is what a device that keeps NAKing the status handshake looks like, but NAKs are not visible to xHCI software)")
        return ("C/E: TRBs queued and doorbell rung, but no Transfer Event arrived until the URB was dequeued after the timeout" + where)
    return "F/unresolved: doorbell rung, no transfer event and no dequeue observed"


# ---------------------------------------------------------------- completeness

def parse_cpu_stats(text):
    """Parse one per_cpu/cpuN/stats file -> dict of ints (floats for timestamps)."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"\s*([a-z ]+?):\s*([0-9.]+)\s*$", line)
        if m:
            k, v = m.group(1).strip().replace(" ", "_"), m.group(2)
            out[k] = float(v) if "." in v else int(v)
    return out


def buffer_expanded(buffer_text):
    first = buffer_text.splitlines()[0] if buffer_text else ""
    if "expanded" in first:
        return False
    m = re.match(r"\s*(\d+)", first)
    return bool(m) and int(m.group(1)) >= 4096


def completeness(res, report=None, expect_xhci=False):
    """Strict verdict. COMPLETE only if nothing indicates loss and the key sequences are whole."""
    reasons = []
    if report is None:
        reasons.append("no collect report: buffer expansion and per-CPU overwrite unknown")
    else:
        if not report.get("buffer_expanded"):
            reasons.append("ring buffer was not expanded (%s)" % report.get("buffer_size_kb"))
        if report.get("cpu_overrun_total", 0) > 0:
            reasons.append("per-CPU overrun events: %d" % report["cpu_overrun_total"])
        oldest = report.get("oldest_event_ts_min")
        if oldest is not None and oldest > 3.0:
            reasons.append("oldest retained event is at %.3fs (boot coverage starts late)" % oldest)
    if res["info"]["loss"]:
        reasons.append("trace header shows overwritten entries")
    if res["unpaired_events"]:
        reasons.append("%d unpaired entry/return events" % res["unpaired_events"])
    if res["malformed_setups"] or res["info"]["malformed_lines"]:
        reasons.append("malformed records present")
    ft = res.get("first_timeout")
    if ft is not None and not ft["request_captured"]:
        reasons.append("first timed-out request entry is missing (%s)" % ft.get("missing"))
    if res["timeouts"] == [] and ft is None:
        reasons.append("no timed-out request in the trace (nothing to attribute)")
    if expect_xhci:
        if not res.get("xhci_records"):
            reasons.append("no xHCI events recorded")
        elif not res.get("xhci_lifecycles"):
            reasons.append("no timed-out request to follow through the controller")
        else:
            c = res["xhci_lifecycles"][0]["lifecycle"]["classification"]
            if c.startswith(("UNCERTAIN", "UNKNOWN")):
                reasons.append("xHCI join not trusted: " + c)
    fu2 = res["fu2"]
    if not fu2:
        reasons.append("no FU2 requests recorded")
    else:
        first = fu2[0]
        if first["dir"] != "IN":
            reasons.append("FU2 sequence starts with a SET (prefix lost)")
        if not any(f["dir"] == "OUT" and f["kind"] == "volume" for f in fu2):
            reasons.append("FU2 sequence has no volume SET")
    if not reasons:
        return {"verdict": "COMPLETE", "reasons": []}
    # Integrity failures make every other statement unreliable (INVALID); missing sequences with
    # intact integrity are PARTIAL.
    integrity = any(r.startswith(("ring buffer was not expanded", "per-CPU overrun", "trace header",
                                  "oldest retained event")) for r in reasons)
    return {"verdict": "INVALID" if integrity else "PARTIAL", "reasons": reasons}


# ---------------------------------------------------------------- privileged helpers

def _need_root(required=True):
    if required and os.geteuid() != 0:
        sys.exit("this subcommand needs root")


def _read(p):
    with open(p) as f:
        return f.read()


def _write(p, s):
    with open(p, "w") as f:
        f.write(s)


def tracefs_command(path, text):
    """Append one command line to a tracefs dynamic-event file.

    Python's open(path, "a") seeks to the end first, which the kprobe_events file rejects with EINVAL;
    a plain O_APPEND write (as the shell's >> does) is accepted."""
    fd = os.open(path, os.O_WRONLY | os.O_APPEND)
    try:
        os.write(fd, text.encode())
    finally:
        os.close(fd)


def _lines(p):
    return [l for l in _read(p).splitlines() if l.strip()]


def tracefs_baseline(tracefs=TRACEFS):
    """Snapshot of the tracefs state we must leave unchanged (taken before the trace boot)."""
    def first(name):
        return (_read(os.path.join(tracefs, name)).splitlines() or [""])[0]
    return {"buffer_size_kb": first("buffer_size_kb"), "tracing_on": first("tracing_on"),
            "current_tracer": first("current_tracer"), "trace_clock": first("trace_clock"),
            "set_event": _lines(os.path.join(tracefs, "set_event")),
            "kprobe_events": _lines(os.path.join(tracefs, "kprobe_events")),
            "instances": sorted(os.listdir(os.path.join(tracefs, "instances")))
            if os.path.isdir(os.path.join(tracefs, "instances")) else []}


def baseline_buffer_kb(baseline):
    """KB to restore: the expanded size from '7 (expanded: 1408)', or the plain size."""
    if not baseline:
        return None
    text = baseline.get("buffer_size_kb", "")
    m = re.search(r"expanded:\s*(\d+)", text)
    if m:
        return int(m.group(1))
    m = re.match(r"\s*(\d+)", text)
    return int(m.group(1)) if m else None


def verify_clean(tracefs=TRACEFS, baseline=None):
    problems = []
    ke = _read(os.path.join(tracefs, "kprobe_events"))
    if GROUP + "/" in ke:
        problems.append("kprobe_events still contains %s probes" % GROUP)
    if os.path.isdir(os.path.join(tracefs, "events", GROUP)):
        problems.append("events/%s still present" % GROUP)
    if baseline:
        now = tracefs_baseline(tracefs)
        for k in ("tracing_on", "current_tracer", "set_event", "kprobe_events", "instances"):
            if now[k] != baseline[k]:
                problems.append("%s differs from baseline: %r != %r" % (k, now[k], baseline[k]))
        want = baseline_buffer_kb(baseline)
        if want is not None and not re.match(r"\s*%d\b" % want, now["buffer_size_kb"]):
            problems.append("buffer_size_kb %r != baseline %d" % (now["buffer_size_kb"], want))
    return problems


def read_cpu_stats(tracefs):
    out = {}
    base = os.path.join(tracefs, "per_cpu")
    if os.path.isdir(base):
        for name in sorted(os.listdir(base)):
            sp = os.path.join(base, name, "stats")
            if os.path.isfile(sp):
                out[name] = parse_cpu_stats(_read(sp))
    return out


def collect(out_dir, tracefs=TRACEFS, require_root=True, baseline=None):
    _need_root(require_root)
    os.makedirs(out_dir, mode=0o700, exist_ok=True)
    os.chmod(out_dir, 0o700)
    ev = os.path.join(tracefs, "events", GROUP)
    if not os.path.isdir(ev):
        sys.exit("no %s events present (was the trace entry booted?)" % GROUP)
    _write(os.path.join(ev, "enable"), "0")  # stop recording our events only
    xdir = os.path.join(tracefs, "events", "xhci-hcd")
    for name in XHCI_EVENTS:  # our xHCI events: stop them first, remove them from set_event after saving
        try:
            _write(os.path.join(xdir, name, "enable"), "0")
        except OSError:
            pass
    buf_text = (_read(os.path.join(tracefs, "buffer_size_kb")).splitlines() or [""])[0]
    expanded = buffer_expanded(buf_text)
    cpu_stats = read_cpu_stats(tracefs)
    trace = _read(os.path.join(tracefs, "trace"))
    raw = os.path.join(out_dir, "raw_trace.txt")
    fd = os.open(raw, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(trace)
    header = HEADER.search(trace)
    removed = []
    for name, _ in PROBES:  # remove only our probes
        try:
            tracefs_command(os.path.join(tracefs, "kprobe_events"), "-:%s/%s\n" % (GROUP, name))
            removed.append(name)
        except OSError as e:
            print("warning: could not remove %s: %s" % (name, e), file=sys.stderr)
    for name in XHCI_EVENTS:  # remove only our entries from set_event
        try:
            tracefs_command(os.path.join(tracefs, "set_event"), "!xhci-hcd:%s\n" % name)
        except OSError:
            pass
    want = baseline_buffer_kb(baseline)
    restored_kb = want if want is not None else 1408
    _write(os.path.join(tracefs, "buffer_size_kb"), str(restored_kb))  # undo trace_buf_size growth
    if baseline and baseline.get("tracing_on") is not None:
        _write(os.path.join(tracefs, "tracing_on"), baseline["tracing_on"])
    problems = verify_clean(tracefs, baseline)
    oldest = [v["oldest_event_ts"] for v in cpu_stats.values() if "oldest_event_ts" in v and v.get("entries", 0) > 0]
    report = {"buffer_size_kb": buf_text, "buffer_expanded": expanded,
              "entries_in_buffer": int(header.group(1)) if header else None,
              "entries_written": int(header.group(2)) if header else None,
              "cpu_stats": cpu_stats,
              "cpu_overrun_total": sum(v.get("overrun", 0) + v.get("commit_overrun", 0) for v in cpu_stats.values()),
              "oldest_event_ts_min": min(oldest) if oldest else None,
              "probes_removed": removed, "buffer_restored_kb": restored_kb,
              "baseline_used": bool(baseline), "cleanup_problems": problems}
    rp = os.path.join(out_dir, "collect_report.json")
    fd = os.open(rp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(report, f, indent=1)
    print("raw trace saved: %s (%d bytes)" % (raw, len(trace)))
    print("buffer before cleanup: %r expanded=%s | header %s/%s | cpu overrun total=%d"
          % (buf_text, expanded, report["entries_in_buffer"], report["entries_written"],
             report["cpu_overrun_total"]))
    if not expanded:
        print("WARNING: ring buffer was never expanded: early events were almost certainly overwritten")
    print("cleanup:", "clean" if not problems else "; ".join(problems))
    return 0 if not problems else 1


# ---------------------------------------------------------------- Limine entry

def parse_leaf(conf_text, kernel_id="linux-cachyos"):
    """Return the kernel entry's {protocol, path, module_path, cmdline} from the generated block."""
    lines = conf_text.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == "//" + kernel_id:
            block = {}
            for nxt in lines[i + 1:]:
                s = nxt.strip()
                if s.startswith("/"):
                    break
                m = re.match(r"(protocol|path|module_path|cmdline):\s*(.*)$", s)
                if m:
                    block[m.group(1)] = m.group(2)
            if all(k in block for k in ("protocol", "path", "module_path", "cmdline")):
                return block
    raise ValueError("kernel entry //%s not found or incomplete" % kernel_id)


def build_entry(leaf):
    if "usbcore.quirks" in leaf["cmdline"] or "dyndbg" in leaf["cmdline"] or "kprobe_event" in leaf["cmdline"]:
        raise ValueError("source cmdline already carries a test argument; refusing")
    cmd = leaf["cmdline"].rstrip() + " " + " ".join(extra_cmdline_args())
    return "\n".join([ENTRY_BEGIN, "/" + ENTRY_NAME,
                      "  protocol: " + leaf["protocol"],
                      "  module_path: " + leaf["module_path"],
                      "  path: " + leaf["path"],
                      "  cmdline: " + cmd,
                      ENTRY_END, ""])


def has_entry(text):
    return ENTRY_BEGIN in text


def add_entry(text):
    if has_entry(text):
        raise ValueError("entry already present")
    entry = build_entry(parse_leaf(text))
    term = "" if text.endswith("\n") else "\n"
    return text + term + "\n" + entry


def remove_entry(text):
    """Remove exactly the marked block (and the separator line add_entry put before it)."""
    if not has_entry(text):
        raise ValueError("entry not present")
    i = text.index(ENTRY_BEGIN)
    k = text.find(ENTRY_END, i)
    if k < 0:
        raise ValueError("unterminated entry block")
    j = k + len(ENTRY_END)
    if text[j:j + 1] == "\n":
        j += 1
    start = i - 1 if text[i - 2:i] == "\n\n" else i
    return text[:start] + text[j:]


def _sha(b):
    return hashlib.sha256(b).hexdigest()


def entry_cmd(action, conf=LIMINE_CONF, state_dir=None):
    _need_root()
    state_dir = state_dir or "/var/lib/openwitty-8a19"
    os.makedirs(state_dir, mode=0o700, exist_ok=True)
    with open(conf, "rb") as f:
        data = f.read()
    text = data.decode()
    if action == "status":
        print("entry present:", has_entry(text))
        return 0
    stat = os.stat(conf)
    if action == "install":
        new = add_entry(text)
        with open(os.path.join(state_dir, "tracefs_baseline.json"), "w") as f:
            json.dump(tracefs_baseline(), f, indent=1)
        backup = os.path.join(state_dir, "limine.conf.before")
        with open(backup, "wb") as f:
            f.write(data)
        with open(os.path.join(state_dir, "limine.conf.before.sha256"), "w") as f:
            f.write(_sha(data) + "\n")
    elif action == "remove":
        new = remove_entry(text)
    else:
        sys.exit("unknown entry action")
    tmp = conf + ".owtmp"
    with open(tmp, "w") as f:
        f.write(new)
        f.flush()
        os.fsync(f.fileno())
    os.chmod(tmp, stat.st_mode & 0o777)
    os.replace(tmp, conf)
    chk = open(conf).read()
    ok = (has_entry(chk) == (action == "install"))
    print("%s: %s (limine.conf %d -> %d bytes)" % (action, "ok" if ok else "VERIFY FAILED", len(data), len(chk.encode())))
    return 0 if ok else 1


# ---------------------------------------------------------------- CLI

def report(res):
    cp = res.get("completeness")
    lines = []
    if cp:
        lines.append("COMPLETENESS: %s%s" % (cp["verdict"], "" if not cp["reasons"] else " - " + "; ".join(cp["reasons"])))
    ft = res.get("first_timeout")
    if ft:
        lines.append("FIRST TIMEOUT: %s" % json.dumps(ft, sort_keys=True))
    lines += ["records=%d target_requests=%d non_target=%d unpaired=%d malformed=%d loss=%s complete=%s"
             % (res["records"], res["target_requests"], res["non_target_requests"],
                res["unpaired_events"], res["info"]["malformed_lines"], res["info"]["loss"], res["complete"])]
    for xl_ in res.get("xhci_lifecycles", []):
        lf = xl_["lifecycle"]
        lines.append("XHCI task=%s submit=%.6f: %s | slot=%s dev=%s enqueue=%s doorbell=%s transfer_events=%s dequeue=%s "
                     "recovery=%s giveback=%s" % (xl_["task"], xl_["ts_submit"], lf["classification"], lf["slot"],
                                                   lf["devname"], lf["urb_enqueue"], lf["doorbell"], lf["transfer_events"],
                                                   lf["urb_dequeue"], [c["ev"] for c in lf["recovery_commands"]], lf["giveback"]))
    for t in res["timeouts"]:
        lines.append("TIMEOUT %s task=%s submit=%.6f complete=%.6f elapsed=%.3fs ret=%s setup bm=0x%02x b=0x%02x "
                     "wValue=0x%04x wIndex=0x%04x wLength=%d (%s %s iface=%s alt=%s)"
                     % (t["ret_name"], t["task"], t["ts_submit"], t["ts_complete"], t["elapsed_s"], t["ret"],
                        t["bmRequestType"], t["bRequest"], t["wValue"], t["wIndex"], t["wLength"],
                        t["direction"], t["meaning"], t["interface"], t["alternate"]))
    for f in res["fu2"]:
        lines.append("FU2 t=%.6f %s %s %s ret=%s %.3fms%s" % (
            f["t"], f["dir"], f["kind"], f["request"], f["ret"], f["elapsed_ms"],
            " db=%.2f" % f["db"] if "db" in f else ""))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("probes")
    a = sub.add_parser("analyze")
    a.add_argument("trace")
    a.add_argument("--dmesg")
    a.add_argument("--json")
    a.add_argument("--report", help="collect_report.json written by collect")
    a.add_argument("--expect-xhci", action="store_true", help="require xHCI events and a joined lifecycle")
    c = sub.add_parser("collect")
    c.add_argument("--out-dir", required=True)
    c.add_argument("--baseline", default="/var/lib/openwitty-8a19/tracefs_baseline.json")
    sub.add_parser("verify-clean")
    e = sub.add_parser("entry")
    e.add_argument("action", choices=["status", "install", "remove"])
    e.add_argument("--conf", default=LIMINE_CONF)
    args = ap.parse_args(argv)
    if args.cmd == "probes":
        for _, d in PROBES:
            print(d)
        print("\nboot arguments:\n " + "\n ".join(extra_cmdline_args()))
        return 0
    if args.cmd == "analyze":
        rep_ = json.load(open(args.report)) if args.report else None
        res = analyze(open(args.trace).read(), open(args.dmesg).read() if args.dmesg else None, rep_, args.expect_xhci)
        print(report(res))
        if args.json:
            with open(args.json, "w") as f:
                json.dump(res, f, indent=1)
        return 0
    if args.cmd == "collect":
        base = json.load(open(args.baseline)) if os.path.exists(args.baseline) else None
        if base is None:
            print("note: no tracefs baseline found; buffer will be restored to the kernel default 1408 KB")
        return collect(args.out_dir, baseline=base)
    if args.cmd == "verify-clean":
        _need_root()
        bp = "/var/lib/openwitty-8a19/tracefs_baseline.json"
        p = verify_clean(baseline=json.load(open(bp)) if os.path.exists(bp) else None)
        print("clean" if not p else "; ".join(p))
        return 0 if not p else 1
    return entry_cmd(args.action, conf=args.conf)


if __name__ == "__main__":
    sys.exit(main())
