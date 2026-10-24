#!/usr/bin/env python3
"""Phase 8A.24: deferred snd-usb-audio probe experiment helper (preparation; nothing here runs on import).

  entry status|install|remove|verify   temporary Limine entry that only adds `modprobe.blacklist=snd_usb_audio`
  prepare                              (USER) create the private run directory shared by all later stages
  check   --run-dir D                  (USER) post-login gate; writes D/checkpoint.json
  load    --run-dir D --confirm-audio-safe   (ROOT via sudo) the ONE deliberate `modprobe snd-usb-audio`
  classify --run-dir D [--cli PATH]    (USER, inside the desktop session) evaluate the result, passive

Why modprobe.blacklist and not module_blacklist: the kernel's module_blacklist (kernel/module/main.c, blacklisted()
in load_module) rejects EVERY load of that module, including an explicit modprobe (-EPERM). kmod's
modprobe.blacklist only filters alias-driven loads (udev coldplug/hotplug); `modprobe snd-usb-audio` by name is
still allowed. No files under /etc are changed. Host-controller drivers are untouched.
"""
import argparse
import errno
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402
import post_boot_health as H  # noqa: E402

LIMINE_CONF = "/boot/limine.conf"
STATE_DIR = "/var/lib/openwitty-8a24"
BASE_REL = ".local/state/openwitty/trace-8a24"
# The experiment is pinned to this exact device position; anything else is INVALID (never "fixed" by re-enumerating).
EXPECTED = {"vid": "29c2", "pid": "0004", "port": "3-2", "controller": "0000:0d:00.4"}
CHECKPOINT_MAX_AGE_S = 1800
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


# ------------------------------------------------------------------ evidence collection (read-only, fail closed)

def default_runner(cmd, timeout=20):
    """-> (rc, stdout, stderr); rc None if the command could not run at all."""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except (OSError, subprocess.SubprocessError) as e:
        return None, "", str(e)


def judge_journal(rc, out, err, require_boot_start=True, require_device_line=True):
    """-> (status, detail). Only OK allows concluding 'driver not registered'.
    statuses: OK | FAILED (command did not run / non-zero) | UNREADABLE (permission) | EMPTY | INCOMPLETE"""
    low = (err or "").lower()
    if rc is None:
        return "FAILED", "journalctl could not run: %s" % err.strip()[:120]
    if rc != 0:
        if "permission" in low or "not seeing messages" in low or "access" in low:
            return "UNREADABLE", "journalctl denied: %s" % err.strip()[:120]
        return "FAILED", "journalctl exit %s: %s" % (rc, err.strip()[:120])
    body = (out or "").strip()
    if not body or body.startswith("-- No entries"):
        return "EMPTY", "journal returned no kernel entries"
    if require_boot_start and "Linux version" not in out:
        return "INCOMPLETE", "no 'Linux version' boot-start line: kernel messages missing or truncated"
    if require_device_line and "idVendor=%s, idProduct=%s" % (EXPECTED["vid"], EXPECTED["pid"]) not in out:
        return "INCOMPLETE", "no CONNECT 2 enumeration line in the kernel journal"
    return "OK", "%d lines" % len(out.splitlines())


def read_kernel_journal(since=None, runner=default_runner, require_boot_start=True, require_device_line=True):
    """Try unprivileged, then `sudo -n`; never returns OK without verified evidence. -> (status, detail, text)"""
    cmd = ["journalctl", "-k", "-b", "0", "--no-pager", "-o", "short-precise"]
    if since:
        cmd += ["--since", since]
    tried = []
    for prefix in ([], ["sudo", "-n"]):
        rc, out, err = runner(prefix + cmd)
        st, detail = judge_journal(rc, out, err, require_boot_start, require_device_line)
        tried.append((st, detail))
        if st == "OK":
            return st, detail, out
    return tried[-1][0], "; ".join("%s: %s" % t for t in tried), ""


def find_devices(base="/sys/bus/usb/devices"):
    """All USB devices matching VID:PID -> [{port, devnum, busnum, controller_pci}] (empty list if none)."""
    res = []
    try:
        names = os.listdir(base)
    except OSError:
        return res
    for n in sorted(names):
        try:
            if (open("%s/%s/idVendor" % (base, n)).read().strip().lower() == EXPECTED["vid"]
                    and open("%s/%s/idProduct" % (base, n)).read().strip().lower() == EXPECTED["pid"]):
                path = os.path.realpath("%s/%s" % (base, n))
                pci = re.findall(r"0000:[0-9a-f]{2}:[0-9a-f]{2}\.\d", path)
                res.append({"port": n, "devnum": open(path + "/devnum").read().strip(),
                            "busnum": open(path + "/busnum").read().strip(),
                            "controller_pci": pci[-1] if pci else None})
        except OSError:
            continue
    return res


def bound_interfaces(devices=None, base="/sys/bus/usb/devices"):
    out = []
    for d in (find_devices(base) if devices is None else devices):
        for n in os.listdir(base):
            if n.startswith(d["port"] + ":") and os.path.islink("%s/%s/driver" % (base, n)):
                out.append(n)
    return out


def module_loaded():
    return os.path.isdir("/sys/module/" + MODULE)


# ------------------------------------------------------------------ gate + classification

def gate(cmdline, module_loaded, devices, bound_interfaces, journal_status, journal_text, blacklist_in_config):
    """-> list of problems; empty means the deliberate load may proceed. Fails closed on missing evidence."""
    problems = []
    if BLACKLIST_ARG not in cmdline.split():
        problems.append("kernel command line lacks %s (not the experimental entry)" % BLACKLIST_ARG)
    if "module_blacklist" in cmdline:
        problems.append("module_blacklist present: it would block the explicit modprobe too")
    if module_loaded:
        problems.append("snd_usb_audio is already loaded: autoload was NOT suppressed -> test INVALID")
    if journal_status != "OK":
        problems.append("kernel journal evidence unavailable (%s): cannot show the driver has not registered" % journal_status)
    elif "registered new interface driver snd-usb-audio" in journal_text:
        problems.append("journal shows snd-usb-audio registration before the deliberate load -> INVALID")
    if len(devices) == 0:
        problems.append("CONNECT 2 (%s:%s) is not enumerated" % (EXPECTED["vid"], EXPECTED["pid"]))
    elif len(devices) > 1:
        problems.append("ambiguous identity: %d devices match %s:%s" % (len(devices), EXPECTED["vid"], EXPECTED["pid"]))
    else:
        d = devices[0]
        if d["port"] != EXPECTED["port"]:
            problems.append("device on port %s, expected %s -> INVALID (no re-enumeration attempted)" % (d["port"], EXPECTED["port"]))
        if d["controller_pci"] is None:
            problems.append("host-controller identity unavailable, expected %s" % EXPECTED["controller"])
        elif d["controller_pci"] != EXPECTED["controller"]:
            problems.append("device on controller %s, expected %s -> INVALID" % (d["controller_pci"], EXPECTED["controller"]))
    if bound_interfaces:
        problems.append("interfaces already bound to a driver: %s" % ",".join(bound_interfaces))
    if not blacklist_in_config:
        problems.append("`modprobe -c` does not report the command-line blacklist")
    return problems


def classify(gate_ok, load_rc, findings, pcm_volume, hw_volume, journal_status="OK"):
    if not gate_ok:
        return "INVALID"
    if load_rc != 0 or journal_status != "OK":
        return "INCONCLUSIVE"
    return H.classify(findings, pcm_volume, hw_volume)


def run_gate(runner=default_runner):
    cmdline = open("/proc/cmdline").read()
    st, detail, text = read_kernel_journal(runner=runner)
    rc, cfg, _ = runner(["modprobe", "-c"])
    devices = find_devices()
    problems = gate(cmdline, module_loaded(), devices, bound_interfaces(devices), st, text,
                    re.search(r"^blacklist\s+snd[_-]usb[_-]audio", cfg or "", re.M) is not None)
    return problems, {"journal_status": st, "journal_detail": detail, "devices": devices, "text": text}


# ------------------------------------------------------------------ run directory handoff (user <-> root)

class RunDirError(Exception):
    pass


def _ancestors_ok(path, uid):
    cur = os.path.dirname(path)
    while True:
        st = os.lstat(cur)
        if stat.S_ISLNK(st.st_mode):
            raise RunDirError("symlink in path: %s" % cur)
        if st.st_uid not in (0, uid):
            raise RunDirError("ancestor %s owned by uid %d" % (cur, st.st_uid))
        if st.st_mode & 0o022 and not st.st_mode & stat.S_ISVTX:
            raise RunDirError("ancestor %s writable by others" % cur)
        if cur == "/":
            return
        cur = os.path.dirname(cur)


def open_run_dir(path, euid=None, sudo_uid=None):
    """Validate and open the shared run directory -> (dirfd, uid, info).

    The directory must be a real (non-symlink) 0700 directory owned by the experimenting user, with an
    experiment.json recording the same uid. As root, that uid must also equal SUDO_UID. All later file access is
    relative to the returned descriptor with O_NOFOLLOW, so a swap/symlink after validation cannot redirect root."""
    euid = os.geteuid() if euid is None else euid
    if not os.path.isabs(path) or path != os.path.normpath(path) or os.path.realpath(path) != path:
        raise RunDirError("run dir must be an absolute path without symlinks/'..'")
    st = os.lstat(path)
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISDIR(st.st_mode):
        raise RunDirError("run dir is not a plain directory")
    if st.st_mode & 0o077:
        raise RunDirError("run dir mode %o is not private (0700)" % (st.st_mode & 0o777))
    uid = st.st_uid
    if euid != 0 and uid != euid:
        raise RunDirError("run dir owned by uid %d, not you" % uid)
    if euid == 0:
        if uid == 0:
            raise RunDirError("run dir is owned by root: expected the experimenting user's directory")
        if sudo_uid is None or int(sudo_uid) != uid:
            raise RunDirError("run dir owner uid %d does not match SUDO_UID (%s)" % (uid, sudo_uid))
    _ancestors_ok(path, uid)
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fst = os.fstat(fd)
        if (fst.st_ino, fst.st_dev) != (st.st_ino, st.st_dev):
            raise RunDirError("run dir changed during validation")
        info = read_json(fd, "experiment.json", uid)
        if info.get("uid") != uid:
            raise RunDirError("experiment.json uid mismatch")
    except BaseException:
        os.close(fd)
        raise
    return fd, uid, info


def read_json(dirfd, name, uid):
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=dirfd)
    except OSError as e:
        raise RunDirError("cannot open %s: %s" % (name, e.strerror))
    with os.fdopen(fd) as f:
        st = os.fstat(f.fileno())
        if not stat.S_ISREG(st.st_mode) or st.st_uid not in (uid, 0):
            raise RunDirError("%s has unexpected type/owner" % name)
        return json.load(f)


def write_json(dirfd, name, obj, uid, replace=True):
    """Create dirfd/name exclusively (O_NOFOLLOW), mode 0600, owned by the experimenting user (chown when root)."""
    if replace:
        try:
            os.unlink(name, dir_fd=dirfd)
        except FileNotFoundError:
            pass
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=dirfd)
    with os.fdopen(fd, "w") as f:
        if os.geteuid() == 0:
            os.fchown(f.fileno(), uid, os.fstat(dirfd).st_gid)
        json.dump(obj, f, indent=1, default=str)
        f.flush()
        os.fsync(f.fileno())


def now_pair():
    return {"wall": time.strftime("%Y-%m-%d %H:%M:%S"), "wall_epoch": time.time(),
            "uptime_s": float(open("/proc/uptime").read().split()[0])}


def boot_id():
    return open("/proc/sys/kernel/random/boot_id").read().strip()


def cmd_prepare(home=None, euid=None):
    euid = os.geteuid() if euid is None else euid
    if euid == 0:
        sys.exit("run `prepare` as the normal user (the run directory must belong to the user)")
    base = os.path.join(home or os.path.expanduser("~"), BASE_REL)
    os.makedirs(base, mode=0o700, exist_ok=True)
    run = os.path.join(base, "run-" + time.strftime("%Y%m%d-%H%M%S"))
    os.mkdir(run, 0o700)
    os.chmod(run, 0o700)
    fd = os.open(run, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        write_json(fd, "experiment.json", {"uid": os.stat(run).st_uid, "created": now_pair(), "dir": run,
                                           "expected": EXPECTED, "kernel": os.uname().release}, os.stat(run).st_uid)
    finally:
        os.close(fd)
    print(run)
    return 0


def cmd_check(run_dir, runner=default_runner):
    fd, uid, _ = open_run_dir(run_dir)
    try:
        problems, ev = run_gate(runner)
        rep = {"time": now_pair(), "boot_id": boot_id(), "kernel": os.uname().release, "devices": ev["devices"],
               "module_loaded": module_loaded(), "journal_status": ev["journal_status"],
               "journal_detail": ev["journal_detail"], "problems": problems,
               "enumeration_lines": [l[:60] for l in ev["text"].splitlines() if "idVendor=29c2" in l],
               "gate": "OK" if not problems else "INVALID/NOT READY"}
        write_json(fd, "checkpoint.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if not problems else 1


def cmd_load(run_dir, confirmed, runner=default_runner, marker_dir=None, euid=None, sudo_uid=None, modprobe=None):
    euid = os.geteuid() if euid is None else euid
    if euid != 0:
        sys.exit("`load` needs root (run it with sudo)")
    if not confirmed:
        sys.exit("refusing: pass --confirm-audio-safe only after the physical audio-safety gate was confirmed")
    marker_dir = marker_dir or STATE_DIR
    marker = os.path.join(marker_dir, "load_done")
    if os.path.lexists(marker):
        sys.exit("refusing: the deliberate load already ran once (%s); no repetition" % marker)
    sudo_uid = os.environ.get("SUDO_UID") if sudo_uid is None else sudo_uid
    try:
        fd, uid, info = open_run_dir(run_dir, euid, sudo_uid)
    except RunDirError as e:
        sys.exit("refusing: run directory rejected: %s" % e)
    try:
        try:
            cp = read_json(fd, "checkpoint.json", uid)
        except RunDirError as e:
            sys.exit("refusing: no usable checkpoint (%s); run `check` first" % e)
        if cp.get("gate") != "OK" or cp.get("boot_id") != boot_id():
            sys.exit("refusing: checkpoint is not an OK gate of THIS boot")
        if time.time() - cp["time"]["wall_epoch"] > CHECKPOINT_MAX_AGE_S:
            sys.exit("refusing: checkpoint older than %d s; run `check` again" % CHECKPOINT_MAX_AGE_S)
        problems, ev = run_gate(runner)
        if problems:
            print("gate failed, NOT loading:\n  " + "\n  ".join(problems))
            return 1
        os.makedirs(marker_dir, mode=0o700, exist_ok=True)
        before = now_pair()
        mfd = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)  # atomic single-load
        with os.fdopen(mfd, "w") as f:
            f.write("%s %s\n" % (before["wall"], info.get("dir", "")))
        if modprobe is None:
            r = subprocess.run(["modprobe", "snd-usb-audio"], capture_output=True, text=True, timeout=60)
            rc, err = r.returncode, r.stderr
        else:
            rc, err = modprobe()
        after = now_pair()
        rep = {"before": before, "after": after, "rc": rc, "stderr": (err or "").strip()[:300],
               "boot_id": boot_id()}
        try:
            write_json(fd, "load.json", rep, uid, replace=False)
        except OSError as e:
            print("WARNING: load happened but load.json could not be written: %s" % e)
        print(json.dumps(rep, indent=1))
        return 0 if rc == 0 else 1
    finally:
        os.close(fd)


def cmd_classify(run_dir, cli, runner=default_runner, euid=None):
    euid = os.geteuid() if euid is None else euid
    if euid == 0:
        sys.exit("run `classify` as the normal user inside the desktop session (PipeWire is per-user)")
    fd, uid, _ = open_run_dir(run_dir)
    try:
        load = read_json(fd, "load.json", uid)
        cp = read_json(fd, "checkpoint.json", uid)
        since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(load["before"]["wall_epoch"] - 1))
        st, detail, log_after = read_kernel_journal(since=since, runner=runner, require_boot_start=False,
                                                    require_device_line=False)
        findings = H.parse_kernel_log(log_after)
        card = H.find_card()
        ctl = None
        if card is not None:
            rc, ctl, _ = runner(["amixer", "-c", card.replace("card", ""), "controls"])
            ctl = ctl if rc == 0 else None
        pcm = None if ctl is None else ("PCM Playback Volume" in ctl)
        routes = H.route_hw_volume()
        outs = None if routes is None else [v for _, d, v in routes if d == "Output"]
        hw = None if not outs else any(outs)
        res = classify(not cp["problems"], load["rc"], findings, pcm, hw, st)
        rep = {"classification": res, "journal_after_load": {"status": st, "detail": detail},
               "findings_after_load": findings, "pcm_playback_volume": pcm, "route_hw_volume": routes, "load": load}
        if cli:
            rc, s, _ = runner([cli, "read", "settings", "--method", "interface", "--interface", "3"], 30)
            rep["hardware_snapshot"] = H.parse_hw_snapshot(s if rc == 0 else "")
        write_json(fd, "classification.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("entry")
    e.add_argument("action", choices=["status", "install", "remove", "verify"])
    e.add_argument("--conf", default=LIMINE_CONF)
    sub.add_parser("prepare")
    for n in ("check", "load", "classify"):
        p = sub.add_parser(n)
        p.add_argument("--run-dir", required=True)
        if n == "load":
            p.add_argument("--confirm-audio-safe", action="store_true")
        if n == "classify":
            p.add_argument("--cli")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "entry":
            return entry_cmd(a.action, a.conf)
        if a.cmd == "prepare":
            return cmd_prepare()
        if a.cmd == "check":
            return cmd_check(a.run_dir)
        if a.cmd == "load":
            return cmd_load(a.run_dir, a.confirm_audio_safe)
        return cmd_classify(a.run_dir, a.cli)
    except RunDirError as e:
        sys.exit("run directory rejected: %s" % e)


if __name__ == "__main__":
    sys.exit(main())
