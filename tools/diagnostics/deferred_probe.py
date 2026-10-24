#!/usr/bin/env python3
"""Phase 8A.24: deferred snd-usb-audio probe experiment helper (preparation; nothing here runs on import).

  entry status|install|remove|verify   temporary Limine entry that only adds `modprobe.blacklist=snd_usb_audio`
  check                                post-login gate: entry active, module absent, device enumerated + unbound
  load --confirm-audio-safe            the ONE deliberate `modprobe snd-usb-audio` (root; refuses a second run)
  classify                             evaluate the result after the load (passive)

Why modprobe.blacklist and not module_blacklist: the kernel's module_blacklist (kernel/module/main.c, blacklisted()
in load_module) rejects EVERY load of that module, including an explicit modprobe (-EPERM). kmod's
modprobe.blacklist only filters alias-driven loads (udev coldplug/hotplug); `modprobe snd-usb-audio` by name is
still allowed. No files under /etc are changed. Host-controller drivers are untouched.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402
import post_boot_health as H  # noqa: E402

LIMINE_CONF = "/boot/limine.conf"
STATE_DIR = "/var/lib/openwitty-8a24"
OUT_DIR_DEFAULT = os.path.expanduser("~/.local/state/openwitty/trace-8a24")
ENTRY_BEGIN = "# OPENWITTY-8A24-BEGIN (temporary deferred snd-usb-audio entry; remove after use)"
ENTRY_END = "# OPENWITTY-8A24-END"
ENTRY_NAME = "OpenWitty deferred snd-usb-audio (temporary)"
BLACKLIST_ARG = "modprobe.blacklist=snd_usb_audio"
MODULE = "snd_usb_audio"
FORBIDDEN_IN_SOURCE = ("modprobe.blacklist", "module_blacklist", "usbcore.quirks", "kprobe_event", "dyndbg",
                       "trace_event", "quirk_flags")


# ------------------------------------------------------------------ Limine entry (pure text functions)

def build_entry(leaf):
    for bad in FORBIDDEN_IN_SOURCE:
        if bad in leaf["cmdline"]:
            raise ValueError("source cmdline already carries %r; refusing" % bad)
    return "\n".join([ENTRY_BEGIN, "/" + ENTRY_NAME,
                      "  protocol: " + leaf["protocol"],
                      "  module_path: " + leaf["module_path"],
                      "  path: " + leaf["path"],
                      "  cmdline: " + leaf["cmdline"].rstrip() + " " + BLACKLIST_ARG,
                      ENTRY_END, ""])


def has_entry(text):
    return ENTRY_BEGIN in text


def add_entry(text):
    if has_entry(text):
        raise ValueError("entry already present")
    term = "" if text.endswith("\n") else "\n"
    return text + term + "\n" + build_entry(T.parse_leaf(text))


def remove_entry(text):
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


def entry_cmd(action, conf=LIMINE_CONF, state_dir=STATE_DIR, require_root=True):
    T._need_root(require_root)
    data = open(conf, "rb").read()
    text = data.decode()
    backup = os.path.join(state_dir, "limine.conf.before")
    if action == "status":
        print("entry present:", has_entry(text))
        return 0
    if action == "verify":  # after removal: byte-identical to the pre-install backup
        ok = os.path.exists(backup) and open(backup, "rb").read() == data and not has_entry(text)
        print("limine.conf identical to pre-install backup:", ok)
        return 0 if ok else 1
    st = os.stat(conf)
    if action == "install":
        new = add_entry(text)
        os.makedirs(state_dir, mode=0o700, exist_ok=True)
        with open(backup, "wb") as f:
            f.write(data)
        with open(backup + ".sha256", "w") as f:
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
    os.chmod(tmp, st.st_mode & 0o777)
    os.replace(tmp, conf)
    chk = open(conf).read()
    ok = has_entry(chk) == (action == "install")
    if action == "remove":
        ok = ok and open(backup, "rb").read() == chk.encode()
    print("%s: %s (limine.conf %d -> %d bytes)" % (action, "ok" if ok else "VERIFY FAILED", len(data), len(chk.encode())))
    return 0 if ok else 1


# ------------------------------------------------------------------ gate + classification (pure where possible)

def gate(cmdline, module_loaded, device_present, bound_interfaces, driver_registered_in_log, blacklist_in_config):
    """-> list of problems; empty means the deliberate load may proceed."""
    problems = []
    if BLACKLIST_ARG not in cmdline.split():
        problems.append("kernel command line lacks %s (not the experimental entry)" % BLACKLIST_ARG)
    if "module_blacklist" in cmdline:
        problems.append("module_blacklist present: it would block the explicit modprobe too")
    if module_loaded:
        problems.append("snd_usb_audio is already loaded: autoload was NOT suppressed -> test INVALID")
    if driver_registered_in_log:
        problems.append("journal shows snd-usb-audio registration before the deliberate load -> INVALID")
    if not device_present:
        problems.append("CONNECT 2 (29c2:0004) is not enumerated")
    if bound_interfaces:
        problems.append("interfaces already bound to a driver: %s" % ",".join(bound_interfaces))
    if not blacklist_in_config:
        problems.append("`modprobe -c` does not report the command-line blacklist")
    return problems


def classify(gate_ok, load_rc, findings, pcm_volume, hw_volume):
    if not gate_ok:
        return "INVALID"
    if load_rc != 0:
        return "INCONCLUSIVE"
    return H.classify(findings, pcm_volume, hw_volume)


# ------------------------------------------------------------------ system probes (read-only)

def module_loaded():
    return os.path.isdir("/sys/module/" + MODULE)


def bound_interfaces():
    out = []
    base = "/sys/bus/usb/devices"
    ident = H.usb_identity()
    if not ident:
        return out
    for n in os.listdir(base):
        if n.startswith(ident["port"] + ":") and os.path.islink("%s/%s/driver" % (base, n)):
            out.append(n)
    return out


def journal_k(since=None):
    cmd = ["journalctl", "-k", "-b", "0", "--no-pager", "-o", "short-precise"]
    if since:
        cmd += ["--since", since]
    return H.run(cmd) or H.run(["sudo", "-n"] + cmd) or ""


def run_gate():
    cmdline = open("/proc/cmdline").read()
    log = journal_k()
    cfg = H.run(["modprobe", "-c"]) or ""
    problems = gate(cmdline, module_loaded(), H.usb_identity() is not None, bound_interfaces(),
                    "registered new interface driver snd-usb-audio" in log,
                    re.search(r"^blacklist\s+snd[_-]usb[_-]audio", cfg, re.M) is not None)
    return problems, log


def now_pair():
    return {"wall": time.strftime("%Y-%m-%d %H:%M:%S"), "wall_epoch": time.time(),
            "uptime_s": float(open("/proc/uptime").read().split()[0])}


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(obj, f, indent=1, default=str)


def cmd_check(out):
    problems, log = run_gate()
    ident = H.usb_identity()
    rep = {"time": now_pair(), "kernel": os.uname().release, "identity": ident, "module_loaded": module_loaded(),
           "bound_interfaces": bound_interfaces(), "problems": problems,
           "enumeration_lines": [l[:60] for l in log.splitlines() if "idVendor=29c2" in l],
           "gate": "OK" if not problems else "INVALID/NOT READY"}
    write_json(os.path.join(out, "checkpoint.json"), rep)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if not problems else 1


def cmd_load(out, confirmed):
    T._need_root()
    if not confirmed:
        sys.exit("refusing: pass --confirm-audio-safe only after the physical audio-safety gate was confirmed")
    marker = os.path.join(STATE_DIR, "load_done")
    if os.path.exists(marker):
        sys.exit("refusing: the deliberate load already ran once (%s); no repetition" % marker)
    problems, _ = run_gate()
    if problems:
        print("gate failed, NOT loading:\n  " + "\n  ".join(problems))
        return 1
    os.makedirs(STATE_DIR, mode=0o700, exist_ok=True)
    before = now_pair()
    open(marker, "w").write(before["wall"] + "\n")
    r = subprocess.run(["modprobe", "snd-usb-audio"], capture_output=True, text=True, timeout=60)
    after = now_pair()
    rep = {"before": before, "after": after, "rc": r.returncode, "stderr": r.stderr.strip()[:300]}
    write_json(os.path.join(out, "load.json"), rep)
    print(json.dumps(rep, indent=1))
    return 0 if r.returncode == 0 else 1


def cmd_classify(out, cli):
    load = json.load(open(os.path.join(out, "load.json")))
    since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(load["before"]["wall_epoch"] - 1))
    log_after = journal_k(since)
    findings = H.parse_kernel_log(log_after)
    ctl = H.run(["amixer", "-c", (H.find_card() or "C2").replace("card", ""), "controls"])
    pcm = None if ctl is None else ("PCM Playback Volume" in ctl)
    routes = H.route_hw_volume()
    outs = None if routes is None else [v for _, d, v in routes if d == "Output"]
    hw = None if not outs else any(outs)
    gate_problems = [p for p in json.load(open(os.path.join(out, "checkpoint.json")))["problems"]]
    res = classify(not gate_problems, load["rc"], findings, pcm, hw)
    rep = {"classification": res, "findings_after_load": findings, "pcm_playback_volume": pcm,
           "route_hw_volume": routes, "load": load}
    if cli:
        s = H.run([cli, "read", "settings", "--method", "interface", "--interface", "3"], 30)
        rep["hardware_snapshot"] = [l for l in (s or "").splitlines() if l.startswith(("phantom=", "microphone", "instrument"))]
    write_json(os.path.join(out, "classification.json"), rep)
    print(json.dumps(rep, indent=1, default=str))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("entry")
    e.add_argument("action", choices=["status", "install", "remove", "verify"])
    e.add_argument("--conf", default=LIMINE_CONF)
    for n in ("check", "load", "classify"):
        p = sub.add_parser(n)
        p.add_argument("--out-dir", default=OUT_DIR_DEFAULT)
        if n == "load":
            p.add_argument("--confirm-audio-safe", action="store_true")
        if n == "classify":
            p.add_argument("--cli")
    a = ap.parse_args(argv)
    if a.cmd == "entry":
        return entry_cmd(a.action, a.conf)
    if a.cmd == "check":
        return cmd_check(a.out_dir)
    if a.cmd == "load":
        return cmd_load(a.out_dir, a.confirm_audio_safe)
    return cmd_classify(a.out_dir, a.cli)


if __name__ == "__main__":
    sys.exit(main())
