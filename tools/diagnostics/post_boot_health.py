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
    """-> dict of findings. FU2 = unit 2, the separate unit 5 is reported on its own.
    Kernel <= 7.2 logs `sticky mixer values`, 7.3 logs `broken mixer GET_CUR` for the same condition."""
    out = {"fu2_sticky": False, "unit5_sticky": False, "other_sticky": [], "set_interface_failed": [],
           "ep0_timeouts": [], "new_device": [], "driver_registered": [], "disconnects": []}
    for line in text.splitlines():
        tm = TS.match(line)
        stamp = tm.group(0) if tm else ""
        m = re.search(r"usb (?P<dev>[\d.-]+): (?P<unit>\d+):(?P<ch>\d+): (?:sticky mixer values|broken mixer GET_CUR)", line)
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


SETTLE_S = 60  # PipeWire/WirePlumber need this long after the driver registered before an absent route is a verdict

# PipeWire states for CONNECT 2 only (other sound cards are ignored)
PW_NO_DEVICE = "NO_DEVICE"              # CONNECT 2 not (yet) a PipeWire device: initialisation incomplete or card absent
PW_NO_ROUTES = "DEVICE_NO_ROUTES"       # device exists, Route and EnumRoute are empty (WirePlumber could not load the mixer)
PW_HW_FALSE = "ROUTES_HW_VOLUME_FALSE"  # output route(s) present, route.hw-volume=false
PW_HW_TRUE = "ROUTES_HW_VOLUME_TRUE"
PW_HW_UNKNOWN = "ROUTES_NO_HW_VOLUME_FIELD"


def _is_connect2_device(o):
    p = o.get("info", {}).get("props", {}) or {}
    if not str(o.get("type", "")).endswith("Interface:Device"):
        return False
    return (str(p.get("device.vendor.id", "")).lower() == "0x" + VID
            and str(p.get("device.product.id", "")).lower() == "0x" + PID)


def pipewire_connect2(objs):
    """Assess PipeWire's view of CONNECT 2 only. -> {state, device_id, route_count, enum_route_count, outputs,
    sink_nodes}. An absent route is its own state, never the same as route.hw-volume=false."""
    dev = None
    for o in objs:
        if _is_connect2_device(o):
            dev = o
            break
    if dev is None:
        return {"state": PW_NO_DEVICE, "device_id": None, "route_count": 0, "enum_route_count": 0,
                "outputs": [], "sink_nodes": 0}
    params = dev.get("info", {}).get("params", {})
    routes = params.get("Route", []) or []
    enum = params.get("EnumRoute", []) or []
    outs = []
    for r in routes:
        info = r.get("info") or []
        flat = {info[i]: info[i + 1] for i in range(1, len(info) - 1, 2)}
        if r.get("direction") == "Output":
            outs.append({"name": r.get("name"),
                         "hw_volume": (str(flat["route.hw-volume"]).lower() == "true") if "route.hw-volume" in flat else None})
    sinks = sum(1 for o in objs if str(o.get("type", "")).endswith("Interface:Node")
                and o.get("info", {}).get("props", {}).get("device.id") == dev.get("id")
                and o.get("info", {}).get("props", {}).get("media.class") == "Audio/Sink")
    if not routes and not enum:
        state = PW_NO_ROUTES
    elif any(o["hw_volume"] for o in outs):
        state = PW_HW_TRUE
    elif outs and all(o["hw_volume"] is False for o in outs):
        state = PW_HW_FALSE
    else:
        state = PW_HW_UNKNOWN
    return {"state": state, "device_id": dev.get("id"), "route_count": len(routes), "enum_route_count": len(enum),
            "outputs": outs, "sink_nodes": sinks}


def classify_evidence(findings, pcm_volume, pw, age_s=None, settle_s=SETTLE_S):
    """Version-independent, evidence-based verdict that needs no hardware write. -> (verdict, reason).

    * A merely present ALSA volume control is never success.
    * FU2 failure (either kernel wording, unit 2 only) + CONNECT 2 PipeWire device without a usable hardware-volume
      route, once the audio stack has settled (age_s >= settle_s) -> NEGATIVE, whatever ALSA shows.
    * POSITIVE: no FU2 failure and consistent CONNECT 2 health (ALSA control + route.hw-volume=true).
    * Missing / contradictory / not-yet-settled evidence -> INCONCLUSIVE."""
    if pw is None or pcm_volume is None:
        return "INCONCLUSIVE", "ALSA or PipeWire evidence missing"
    state = pw["state"]
    if not findings["fu2_sticky"]:
        if pcm_volume and state == PW_HW_TRUE:
            return "POSITIVE", "no FU2 failure; ALSA control present; CONNECT 2 route.hw-volume=true"
        return "INCONCLUSIVE", "no FU2 failure but CONNECT 2 ALSA/PipeWire health not consistent (%s)" % state
    if state == PW_NO_DEVICE:
        return "INCONCLUSIVE", "FU2 failure, but CONNECT 2 is not a PipeWire device (initialisation incomplete?)"
    if state == PW_HW_TRUE:
        return "INCONCLUSIVE", "FU2 failure contradicted by route.hw-volume=true"
    if age_s is None or age_s < settle_s:
        return "INCONCLUSIVE", "audio stack not settled (%s s since driver load, need %d): classify again later" % (
            "unknown" if age_s is None else "%.0f" % age_s, settle_s)
    return "NEGATIVE", "FU2 failure and stable unusable CONNECT 2 PipeWire mixer/route (%s); ALSA control %s" % (
        state, "present" if pcm_volume else "absent")


HW_LINE = re.compile(r"^phantom=(?P<ph>\d)\s+speaker=(?P<sv>-?\d+(?:\.\d+)?) dB(?P<sm> \(muted\))?\s+"
                     r"headphone=(?P<hv>-?\d+(?:\.\d+)?) dB(?P<hm> \(muted\))?\s")


def parse_hw_snapshot(text):
    """Parse the real `openwitty-cli read settings` output (src/cli/main.cpp printSettings): the speaker and
    headphone volume/mute are on the `phantom=` line. Never uses the `raw=` line.
    -> {complete, speaker_db, speaker_muted, headphone_db, headphone_muted, lines}"""
    out = {"complete": False, "speaker_db": None, "speaker_muted": None, "headphone_db": None,
           "headphone_muted": None, "lines": []}
    for line in (text or "").splitlines():
        if line.startswith(("microphone:", "instrument:", "phantom=")):
            out["lines"].append(line)
        m = HW_LINE.match(line + " ")
        if m:
            out.update(speaker_db=float(m.group("sv")), speaker_muted=bool(m.group("sm")),
                       headphone_db=float(m.group("hv")), headphone_muted=bool(m.group("hm")), complete=True)
    return out


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


def pipewire_connect2_now():
    raw = run(["pw-dump"])
    if not raw:
        return None
    try:
        return pipewire_connect2(json.loads(raw))
    except ValueError:
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cli")
    ap.add_argument("--json")
    ap.add_argument("--journal-file")
    ap.add_argument("--age-seconds", type=float, help="seconds since the driver registered; needed for a NEGATIVE verdict")
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
        rep["hardware_snapshot"] = parse_hw_snapshot(s)
    pw = pipewire_connect2_now()
    rep["pipewire_connect2"] = pw
    rep["classification"] = classify(f, pcm_volume, hw) if text else "INCONCLUSIVE"  # legacy (7.2) rule, all cards
    if text:
        rep["classification_evidence"], rep["classification_reason"] = classify_evidence(f, pcm_volume, pw, a.age_seconds)
    else:
        rep["classification_evidence"], rep["classification_reason"] = "INCONCLUSIVE", "kernel log unreadable"
    print(json.dumps(rep, indent=1, default=str))
    if a.json:
        fd = os.open(a.json, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as fh:
            json.dump(rep, fh, indent=1, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main())
