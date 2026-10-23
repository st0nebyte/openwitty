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
BUF_ARGS = ["ftrace=nop", "trace_buf_size=8M"]
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
    return [boot_arg()] + BUF_ARGS + [DYNDBG_ARG]


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
                     "ts": float(m.group("ts")), "ev": m.group("ev"), "kv": kv})
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
            "actual": rk.get("actual"), "fu2": is_fu2(s, d),
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


def analyze(text, dmesg=None):
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
    first_to = timeouts[0] if timeouts else None
    if first_to is not None:
        after = [f for f in res["fu2"] if f["t"] >= first_to["ts_complete"]]
        if after:
            res["first_timeout_to_first_fu2_s"] = round(after[0]["t"] - first_to["ts_complete"], 6)
    if dmesg:
        res["debug_message_ties"] = tie_debug_messages(dmesg, timeouts)
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


def verify_clean(tracefs=TRACEFS):
    problems = []
    ke = _read(os.path.join(tracefs, "kprobe_events"))
    if GROUP + "/" in ke:
        problems.append("kprobe_events still contains %s probes" % GROUP)
    if os.path.isdir(os.path.join(tracefs, "events", GROUP)):
        problems.append("events/%s still present" % GROUP)
    return problems


def collect(out_dir, tracefs=TRACEFS, require_root=True):
    _need_root(require_root)
    os.makedirs(out_dir, mode=0o700, exist_ok=True)
    os.chmod(out_dir, 0o700)
    ev = os.path.join(tracefs, "events", GROUP)
    if not os.path.isdir(ev):
        sys.exit("no %s events present (was the trace entry booted?)" % GROUP)
    _write(os.path.join(ev, "enable"), "0")  # stop recording our events only
    bufsize = _read(os.path.join(tracefs, "buffer_size_kb")).splitlines()[0]
    if "expanded" in bufsize:
        print("WARNING: ring buffer was never expanded (%s): early events were almost certainly overwritten" % bufsize)
    trace = _read(os.path.join(tracefs, "trace"))
    raw = os.path.join(out_dir, "raw_trace.txt")
    fd = os.open(raw, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(trace)
    for name, _ in PROBES:  # remove only our probes
        try:
            tracefs_command(os.path.join(tracefs, "kprobe_events"), "-:%s/%s\n" % (GROUP, name))
        except OSError as e:
            print("warning: could not remove %s: %s" % (name, e), file=sys.stderr)
    _write(os.path.join(tracefs, "buffer_size_kb"), "1408")  # undo trace_buf_size growth
    problems = verify_clean(tracefs)
    print("raw trace saved: %s (%d bytes)" % (raw, len(trace)))
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
    lines = ["records=%d target_requests=%d non_target=%d unpaired=%d malformed=%d loss=%s complete=%s"
             % (res["records"], res["target_requests"], res["non_target_requests"],
                res["unpaired_events"], res["info"]["malformed_lines"], res["info"]["loss"], res["complete"])]
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
    c = sub.add_parser("collect")
    c.add_argument("--out-dir", required=True)
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
        res = analyze(open(args.trace).read(), open(args.dmesg).read() if args.dmesg else None)
        print(report(res))
        if args.json:
            with open(args.json, "w") as f:
                json.dump(res, f, indent=1)
        return 0
    if args.cmd == "collect":
        return collect(args.out_dir)
    if args.cmd == "verify-clean":
        _need_root()
        p = verify_clean()
        print("clean" if not p else "; ".join(p))
        return 0 if not p else 1
    return entry_cmd(args.action, conf=args.conf)


if __name__ == "__main__":
    sys.exit(main())
