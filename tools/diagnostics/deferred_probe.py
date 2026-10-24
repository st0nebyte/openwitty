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
PROFILES = {
    "8a24": {"begin": "# OPENWITTY-8A24-BEGIN (temporary deferred snd-usb-audio entry; remove after use)",
             "end": "# OPENWITTY-8A24-END", "name": "OpenWitty deferred snd-usb-audio (temporary)",
             "state": "/var/lib/openwitty-8a24", "base": ".local/state/openwitty/trace-8a24", "per_boot_marker": False},
    "8a25": {"begin": "# OPENWITTY-8A25-BEGIN (temporary unbound-reconnect snd-usb-audio entry; remove after use)",
             "end": "# OPENWITTY-8A25-END", "name": "OpenWitty unbound-reconnect snd-usb-audio (temporary)",
             "state": "/var/lib/openwitty-8a25", "base": ".local/state/openwitty/trace-8a25", "per_boot_marker": True},
}
PROFILE = "8a24"
ARCHIVE_DIR = "/var/lib/openwitty-archive"
ENTRY_BEGIN = PROFILES["8a24"]["begin"]
ENTRY_END = PROFILES["8a24"]["end"]
ENTRY_NAME = PROFILES["8a24"]["name"]
BLACKLIST_ARG = "modprobe.blacklist=snd_usb_audio"
MODULE = "snd_usb_audio"
FORBIDDEN_IN_SOURCE = ("modprobe.blacklist", "module_blacklist", "usbcore.quirks", "kprobe_event", "dyndbg",
                       "trace_event", "quirk_flags")


def set_profile(name):
    """Select the experiment profile (entry markers/name, state dir, run-dir base, marker style)."""
    global PROFILE, ENTRY_BEGIN, ENTRY_END, ENTRY_NAME, STATE_DIR, BASE_REL
    p = PROFILES[name]
    PROFILE, ENTRY_BEGIN, ENTRY_END, ENTRY_NAME = name, p["begin"], p["end"], p["name"]
    STATE_DIR, BASE_REL = p["state"], p["base"]


def any_entry_present(text):
    return any(pr["begin"] in text for pr in PROFILES.values())


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
    if any_entry_present(text):
        raise ValueError("an OpenWitty experimental entry is already present")
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


def entry_cmd(action, conf=LIMINE_CONF, state_dir=None, require_root=True, prior_state_dir=None):
    T._need_root(require_root)
    state_dir = state_dir or STATE_DIR
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
        if os.path.lexists(backup):
            sys.exit("refusing: %s already exists (stale experiment state); archive it with `rollover` first" % backup)
        prior = prior_state_dir if prior_state_dir is not None else (
            PROFILES["8a24"]["state"] if PROFILE == "8a25" else None)
        if prior and os.path.lexists(prior):
            sys.exit("refusing: previous experiment state %s not archived; run `rollover --profile 8a24` first" % prior)
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


# ------------------------------------------------------------------ 8A.25: reconnect observation (passive)

DISC_RE = re.compile(r"usb (?P<port>[\d.-]+): USB disconnect, device number (?P<n>\d+)")
NEWDEV_RE = re.compile(r"usb (?P<port>[\d.-]+): new (?P<speed>[\w-]+) USB device number (?P<n>\d+) using")
FOUND_RE = re.compile(r"usb (?P<port>[\d.-]+): New USB device found, idVendor=%s, idProduct=%s" % (EXPECTED["vid"], EXPECTED["pid"]))


def analyze_reenumeration(journal_text, port=None):
    """Count the disconnect / enumeration events of the target port in the current-boot kernel journal."""
    port = port or EXPECTED["port"]
    disc = [int(m.group("n")) for m in DISC_RE.finditer(journal_text) if m.group("port") == port]
    new = [int(m.group("n")) for m in NEWDEV_RE.finditer(journal_text) if m.group("port") == port]
    found = [m for m in FOUND_RE.finditer(journal_text) if m.group("port") == port]
    return {"disconnects": len(disc), "enumerations": len(new), "found_lines": len(found),
            "device_numbers": new, "disconnect_numbers": disc}


def reenumeration_problems(info, baseline_devnum=None):
    """Exactly one genuine disconnect followed by exactly one re-enumeration since the boot-time enumeration."""
    p = []
    if info["disconnects"] != 1:
        p.append("expected exactly 1 USB disconnect of %s in this boot, journal shows %d" % (EXPECTED["port"], info["disconnects"]))
    if info["enumerations"] != 2 or info["found_lines"] != 2:
        p.append("expected exactly 2 enumerations (boot + one reconnect), journal shows %d (%d identity lines)"
                 % (info["enumerations"], info["found_lines"]))
    nums = info["device_numbers"]
    if len(nums) >= 2 and baseline_devnum is not None and str(nums[-1]) == str(baseline_devnum) and nums[0] == nums[-1]:
        p.append("device number unchanged (%s): cannot confirm a new USB generation by number" % nums[-1])
    return p


def watch_reconnect(deadline=240.0, settle=3.0, poll=0.25, devices_fn=None, clock=time.monotonic, sleep=time.sleep,
                    wall=time.time, installed_signals=True):
    """Passively observe ONE disconnect + reappearance of the target via sysfs. Sends nothing, resets nothing."""
    devices_fn = devices_fn or find_devices
    flag = {"sig": None}
    old = {}
    if installed_signals:
        import signal
        for sg in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            old[sg] = signal.signal(sg, lambda n, f: flag.update(sig=n))
    t0 = clock()
    d0 = devices_fn()
    rep = {"armed_epoch": wall(), "present_at_arm": len(d0) == 1, "devnum_before": d0[0]["devnum"] if d0 else None,
           "t_disconnect_epoch": None, "t_reconnect_epoch": None, "devnum_after": None, "multiple_seen": False}
    state = "present" if len(d0) == 1 else "bad_start"
    t_back = None
    try:
        while True:
            now = clock() - t0
            if flag["sig"] is not None:
                rep["status"] = "signal %d" % flag["sig"]
                break
            if state == "bad_start":
                rep["status"] = "device not present exactly once at arm"
                break
            if now >= deadline:
                rep["status"] = "deadline: no disconnect" if state == "present" else (
                    "deadline: device never returned" if state == "absent" else "deadline")
                break
            devs = devices_fn()
            if len(devs) > 1:
                rep["multiple_seen"] = True
            if state == "present" and len(devs) == 0:
                state, rep["t_disconnect_epoch"] = "absent", wall()
            elif state == "absent" and len(devs) == 1:
                state, rep["t_reconnect_epoch"], t_back = "back", wall(), now
                rep["devnum_after"] = devs[0]["devnum"]
                rep["port_after"], rep["controller_after"] = devs[0]["port"], devs[0]["controller_pci"]
            elif state == "back" and now - t_back >= settle:
                rep["status"] = "reconnected"
                break
            elif state == "present" and len(devs) == 1 and devs[0]["devnum"] != rep["devnum_before"]:
                # replug faster than one poll interval: new device number without an observed absence
                state, rep["t_disconnect_epoch"], rep["t_reconnect_epoch"], t_back = "back", wall(), wall(), now
                rep["devnum_after"] = devs[0]["devnum"]
                rep["port_after"], rep["controller_after"] = devs[0]["port"], devs[0]["controller_pci"]
            sleep(poll)
    finally:
        if installed_signals:
            import signal
            for sg, h in old.items():
                signal.signal(sg, h)
    return rep


def rollover(profile, state_dir=None, archive_dir=ARCHIVE_DIR, conf=LIMINE_CONF, allow_unloaded=False, now=None):
    """Archive (never delete) a finished experiment's root state: backup, sha, single-load marker(s).

    Refuses unless the experiment demonstrably completed (marker's run directory holds classification.json), no
    experimental Limine entry is present, and the archive target does not exist. Returns the archive path."""
    state_dir = state_dir or PROFILES[profile]["state"]
    if not os.path.isdir(state_dir):
        raise ValueError("no state directory %s: nothing to roll over" % state_dir)
    conf_text = open(conf).read()
    if any_entry_present(conf_text):
        raise ValueError("an experimental Limine entry is still installed: remove it first")
    markers = sorted(n for n in os.listdir(state_dir) if n.startswith("load_done"))
    classification = None
    marker_text = []
    for n in markers:
        txt = open(os.path.join(state_dir, n)).read().strip()
        marker_text.append("%s: %s" % (n, txt))
        parts = txt.split(" ", 2)
        run = parts[2] if len(parts) > 2 else ""
        cj = os.path.join(run, "classification.json")
        if run and os.path.isabs(run) and os.path.isfile(cj):
            classification = json.load(open(cj)).get("classification")
    if not markers and not allow_unloaded:
        raise ValueError("no load marker: the experiment never loaded; pass --allow-unloaded to archive an aborted run")
    if markers and classification is None:
        raise ValueError("previous experiment not demonstrably complete (no classification.json for its run)")
    backup = os.path.join(state_dir, "limine.conf.before")
    sha_backup = _sha(open(backup, "rb").read()) if os.path.exists(backup) else None
    sha_conf = _sha(open(conf, "rb").read())
    stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(now))
    dest = os.path.join(archive_dir, "%s-%s" % (profile, stamp))
    if os.path.lexists(dest):
        raise ValueError("archive target exists: " + dest)
    os.makedirs(archive_dir, mode=0o700, exist_ok=True)
    os.makedirs(dest, mode=0o700)
    os.rename(state_dir, os.path.join(dest, "state"))
    note = ["profile: " + profile, "archived: " + time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now)),
            "classification of that experiment: %s" % classification,
            "markers: " + ("; ".join(marker_text) or "none"),
            "backup sha256: %s" % sha_backup, "current limine.conf sha256: %s" % sha_conf,
            "current conf identical to backup: %s" % (sha_backup == sha_conf)]
    with open(os.path.join(dest, "ROLLOVER.txt"), "w") as f:
        f.write("\n".join(note) + "\n")
    return dest


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


def _checkpoint_name(stage):
    return "checkpoint_post.json" if stage == "post" else "checkpoint.json"


def cmd_check(run_dir, runner=default_runner, stage="pre"):
    """stage "pre": baseline gate (also the whole gate of the 8A.24 flow).
    stage "post" (8A.25): after the user's ONE physical reconnect; additionally needs reconnect.json from `watch`,
    the OK baseline checkpoint of this boot, and a journal showing exactly one disconnect + one re-enumeration."""
    fd, uid, _ = open_run_dir(run_dir)
    try:
        problems, ev = run_gate(runner)
        extra = {}
        if stage == "post":
            try:
                base = read_json(fd, "checkpoint.json", uid)
                rc_ = read_json(fd, "reconnect.json", uid)
            except RunDirError as e:
                base, rc_ = None, None
                problems.append("post stage needs checkpoint.json and reconnect.json (%s)" % e)
            if base is not None:
                if base.get("gate") != "OK" or base.get("boot_id") != boot_id():
                    problems.append("baseline checkpoint is not an OK gate of this boot")
                if rc_.get("status") != "reconnected":
                    problems.append("reconnect observation status: %s" % rc_.get("status"))
                if rc_.get("multiple_seen"):
                    problems.append("more than one matching device was seen during the reconnect")
                if rc_.get("devnum_after") is None:
                    problems.append("no re-enumeration observed")
                info = analyze_reenumeration(ev["text"])
                bdev = (base.get("devices") or [{}])[0].get("devnum")
                problems += reenumeration_problems(info, bdev)
                extra = {"reenumeration": info, "devnum_before": bdev, "devnum_after": rc_.get("devnum_after"),
                         "t_reconnect_epoch": rc_.get("t_reconnect_epoch")}
        rep = {"stage": stage, "time": now_pair(), "boot_id": boot_id(), "kernel": os.uname().release,
               "devices": ev["devices"], "module_loaded": module_loaded(), "journal_status": ev["journal_status"],
               "journal_detail": ev["journal_detail"], "problems": problems,
               "enumeration_lines": [l[:60] for l in ev["text"].splitlines() if "idVendor=29c2" in l],
               "gate": "OK" if not problems else "INVALID/NOT READY"}
        rep.update(extra)
        write_json(fd, _checkpoint_name(stage), rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if not problems else 1


def cmd_watch(run_dir, deadline):
    if os.geteuid() == 0:
        sys.exit("run `watch` as the normal user")
    fd, uid, _ = open_run_dir(run_dir)
    try:
        try:
            base = read_json(fd, "checkpoint.json", uid)
        except RunDirError:
            sys.exit("run `check` (pre stage) first")
        if base.get("gate") != "OK" or base.get("boot_id") != boot_id():
            sys.exit("baseline checkpoint is not an OK gate of this boot")
        print("ARMED: observing sysfs only. Unplug and replug CONNECT 2 ONCE now (same port). Deadline %d s." % deadline, flush=True)
        rep = watch_reconnect(deadline=deadline)
        write_json(fd, "reconnect.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if rep.get("status") == "reconnected" else 1


def cmd_load(run_dir, confirmed, runner=default_runner, marker_dir=None, euid=None, sudo_uid=None, modprobe=None):
    euid = os.geteuid() if euid is None else euid
    if euid != 0:
        sys.exit("`load` needs root (run it with sudo)")
    if not confirmed:
        sys.exit("refusing: pass --confirm-audio-safe only after the physical audio-safety gate was confirmed")
    marker_dir = marker_dir or STATE_DIR
    per_boot = PROFILES[PROFILE]["per_boot_marker"]
    marker = os.path.join(marker_dir, "load_done." + boot_id() if per_boot else "load_done")
    if os.path.lexists(marker) or (per_boot and any(n.startswith("load_done.") and n.endswith(boot_id())
                                                   for n in (os.listdir(marker_dir) if os.path.isdir(marker_dir) else []))):
        sys.exit("refusing: the deliberate load already ran once (%s); no repetition" % marker)
    sudo_uid = os.environ.get("SUDO_UID") if sudo_uid is None else sudo_uid
    try:
        fd, uid, info = open_run_dir(run_dir, euid, sudo_uid)
    except RunDirError as e:
        sys.exit("refusing: run directory rejected: %s" % e)
    reconnect = None
    try:
        try:
            cp = read_json(fd, _checkpoint_name("post" if PROFILE == "8a25" else "pre"), uid)
            if PROFILE == "8a25":
                reconnect = read_json(fd, "reconnect.json", uid)
        except RunDirError as e:
            sys.exit("refusing: no usable checkpoint (%s); run the gate stages first" % e)
        if cp.get("gate") != "OK" or cp.get("boot_id") != boot_id():
            sys.exit("refusing: checkpoint is not an OK gate of THIS boot")
        if time.time() - cp["time"]["wall_epoch"] > CHECKPOINT_MAX_AGE_S:
            sys.exit("refusing: checkpoint older than %d s; run the check again" % CHECKPOINT_MAX_AGE_S)
        problems, ev = run_gate(runner)
        if PROFILE == "8a25":
            problems += reenumeration_problems(analyze_reenumeration(ev["text"]), cp.get("devnum_before"))
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
        if reconnect:
            rep["reconnect_to_load_s"] = round(before["wall_epoch"] - reconnect["t_reconnect_epoch"], 3)
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
        cp = read_json(fd, _checkpoint_name("post" if PROFILE == "8a25" else "pre"), uid)
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
        if PROFILE == "8a25":
            rep["reenumeration"] = {k: cp.get(k) for k in ("reenumeration", "devnum_before", "devnum_after", "t_reconnect_epoch")}
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
    ap.add_argument("--profile", choices=sorted(PROFILES), default="8a24")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("entry")
    e.add_argument("action", choices=["status", "install", "remove", "verify"])
    e.add_argument("--conf", default=LIMINE_CONF)
    sub.add_parser("prepare")
    ro = sub.add_parser("rollover")
    ro.add_argument("--allow-unloaded", action="store_true")
    for n in ("check", "watch", "load", "classify"):
        p = sub.add_parser(n)
        p.add_argument("--run-dir", required=True)
        if n == "check":
            p.add_argument("--stage", choices=["pre", "post"], default="pre")
        if n == "watch":
            p.add_argument("--deadline", type=float, default=240.0)
        if n == "load":
            p.add_argument("--confirm-audio-safe", action="store_true")
        if n == "classify":
            p.add_argument("--cli")
    a = ap.parse_args(argv)
    set_profile(a.profile)
    try:
        if a.cmd == "entry":
            return entry_cmd(a.action, a.conf)
        if a.cmd == "prepare":
            return cmd_prepare()
        if a.cmd == "rollover":
            T._need_root()
            try:
                print("archived to", rollover(a.profile, allow_unloaded=a.allow_unloaded))
            except ValueError as e:
                sys.exit("refusing: %s" % e)
            return 0
        if a.cmd == "check":
            return cmd_check(a.run_dir, stage=a.stage)
        if a.cmd == "watch":
            return cmd_watch(a.run_dir, a.deadline)
        if a.cmd == "load":
            return cmd_load(a.run_dir, a.confirm_audio_safe)
        return cmd_classify(a.run_dir, a.cli)
    except RunDirError as e:
        sys.exit("run directory rejected: %s" % e)


if __name__ == "__main__":
    sys.exit(main())
