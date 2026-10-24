#!/usr/bin/env python3
"""Passive post-boot health check for CONNECT 2 (Phase 8A.23). Sends no USB request, writes nothing.

Reads: /proc/cmdline (flags only), sysfs identity, `amixer -c <card> controls`, `pw-dump`, `pactl`,
`journalctl -k` (current boot; falls back to a note if unreadable) and, optionally, the GET-only
`openwitty-cli read settings`. Serial numbers and UUIDs are never printed.

Usage: post_boot_health.py [--cli PATH] [--json OUT] [--journal-file FILE]
"""
import argparse
import json
import os
import re
import subprocess
import sys

VID, PID = "29c2", "0004"
TS = re.compile(r"^(?:\[\s*(?P<up>\d+\.\d+)\]|(?P<wall>\w{3} \d+ [\d:.]+))")


def parse_kernel_log(text):
    """-> dict of findings. FU2 = unit 2, the separate unit 5 is reported on its own."""
    out = {"fu2_sticky": False, "unit5_sticky": False, "other_sticky": [], "set_interface_failed": [],
           "ep0_timeouts": [], "new_device": [], "driver_registered": [], "disconnects": []}
    for line in text.splitlines():
        tm = TS.match(line)
        stamp = tm.group(0) if tm else ""
        m = re.search(r"usb (?P<dev>[\d.-]+): (?P<unit>\d+):(?P<ch>\d+): sticky mixer values", line)
        if m:
            u = int(m.group("unit"))
            if u == 2:
                out["fu2_sticky"] = True
            elif u == 5:
                out["unit5_sticky"] = True
            else:
                out["other_sticky"].append(u)
            continue
        if re.search(r"usb_set_interface failed \(-110\)", line):
            out["set_interface_failed"].append(stamp)
        elif "timed out or killed on ep0" in line:
            out["ep0_timeouts"].append(stamp)
        elif re.search(r"idVendor=%s, idProduct=%s" % (VID, PID), line):
            out["new_device"].append(stamp)
        elif "registered new interface driver snd-usb-audio" in line:
            out["driver_registered"].append(stamp)
        elif "USB disconnect" in line and "3-" in line:
            out["disconnects"].append(stamp)
    return out


def classify(findings, pcm_volume, hw_volume_true):
    """POSITIVE / NEGATIVE / INCONCLUSIVE per the Phase 8A.23 criteria (unit 5 alone is not evidence)."""
    if pcm_volume is None or hw_volume_true is None:
        return "INCONCLUSIVE"
    if pcm_volume and hw_volume_true and not findings["fu2_sticky"]:
        return "POSITIVE"
    if (not pcm_volume) and (not hw_volume_true) and findings["fu2_sticky"]:
        return "NEGATIVE"
    return "INCONCLUSIVE"


def run(cmd, timeout=15):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout
    except (OSError, subprocess.SubprocessError):
        return None


def find_card():
    for d in sorted(os.listdir("/proc/asound")) if os.path.isdir("/proc/asound") else []:
        p = "/proc/asound/%s/usbid" % d
        if os.path.exists(p) and open(p).read().strip() == "%s:%s" % (VID, PID):
            return d
    return None


def usb_identity():
    base = "/sys/bus/usb/devices"
    for n in os.listdir(base):
        try:
            if (open("%s/%s/idVendor" % (base, n)).read().strip() == VID
                    and open("%s/%s/idProduct" % (base, n)).read().strip() == PID):
                path = os.path.realpath("%s/%s" % (base, n))
                return {"port": n, "devnum": open(path + "/devnum").read().strip(),
                        "busnum": open(path + "/busnum").read().strip(),
                        "bcdDevice": open(path + "/bcdDevice").read().strip(),
                        "controller_pci": re.findall(r"0000:[0-9a-f]{2}:[0-9a-f]{2}\.\d", path)[-1:]}
        except OSError:
            continue
    return None


def parse_routes(objs):
    """-> [(name, direction, hw_volume)] from the active `Route` params of PipeWire devices."""
    res = []
    for o in objs:
        if not str(o.get("type", "")).endswith("Device"):
            continue
        for r in o.get("info", {}).get("params", {}).get("Route", []):
            info = r.get("info") or []
            flat = {info[i]: info[i + 1] for i in range(1, len(info) - 1, 2)}
            if "route.hw-volume" in flat:
                res.append((r.get("name"), r.get("direction"), str(flat["route.hw-volume"]).lower() == "true"))
    return res


def route_hw_volume():
    raw = run(["pw-dump"])
    if not raw:
        return None
    try:
        return parse_routes(json.loads(raw))
    except ValueError:
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cli")
    ap.add_argument("--json")
    ap.add_argument("--journal-file")
    a = ap.parse_args(argv)
    rep = {"kernel": os.uname().release}
    cmd = open("/proc/cmdline").read()
    rep["cmdline_has_trace_params"] = any(k in cmd for k in ("kprobe_event", "trace_event", "trace_buf_size", "dyndbg", "usbcore.quirks", "quirk_flags"))
    rep["identity"] = usb_identity()
    card = find_card()
    rep["alsa_card"] = card
    pcm_volume = None
    if card is not None:
        ctl = run(["amixer", "-c", card.replace("card", ""), "controls"])
        if ctl is not None:
            pcm_volume = "PCM Playback Volume" in ctl
    rep["pcm_playback_volume"] = pcm_volume
    routes = route_hw_volume()
    rep["route_hw_volume"] = routes
    outs = None if routes is None else [v for _, d, v in routes if d == "Output"]
    hw = None if not outs else any(outs)
    if a.journal_file:
        text = open(a.journal_file).read()
    else:
        text = run(["journalctl", "-k", "-b", "0", "--no-pager", "-o", "short-precise"]) or run(
            ["sudo", "-n", "journalctl", "-k", "-b", "0", "--no-pager", "-o", "short-precise"])
    rep["kernel_log_readable"] = bool(text)
    rep["kernel"] = os.uname().release
    f = parse_kernel_log(text or "")
    rep["kernel_findings"] = f
    sink = run(["pactl", "get-sink-volume", "@DEFAULT_SINK@"])
    rep["default_sink_volume"] = sink.splitlines()[0] if sink else None
    if a.cli:
        s = run([a.cli, "read", "settings", "--method", "interface", "--interface", "3"], 30)
        rep["hardware_snapshot"] = [l for l in (s or "").splitlines() if l.startswith(("phantom=", "microphone", "instrument"))]
    rep["classification"] = classify(f, pcm_volume, hw) if text else "INCONCLUSIVE"
    print(json.dumps(rep, indent=1, default=str))
    if a.json:
        fd = os.open(a.json, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as fh:
            json.dump(rep, fh, indent=1, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main())
