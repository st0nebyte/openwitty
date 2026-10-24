#!/usr/bin/env python3
"""Live USB EP0 + xHCI trace of ONE manual disconnect/reconnect of CONNECT 2 (Phase 8A.21).

This tool never sends a USB request, never resets, disconnects, configures or power-cycles anything, never
touches the audio stack and never edits boot configuration. It only

  * reads sysfs to see whether VID:PID 29c2:0004 is present (passive),
  * defines the Phase 8A.19/8A.20 kprobes (global dynamic events, removed again by name),
  * enables those events and the xHCI events in a PRIVATE tracefs instance with a bounded buffer,
  * waits for the user to unplug and replug, then stops after a settle period or at a fixed deadline,
  * saves the raw trace outside Git, removes only its own instance/probes and verifies the tracefs state.

Subcommands:  live (root)   |   live-cleanup (root, idempotent recovery)
"""

import argparse
import json
import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402

INSTANCE = "owtrace_live"
SYSFS_USB = "/sys/bus/usb/devices"
BUF_KB = 32768


def target_present(sysfs=SYSFS_USB, vid="29c2", pid="0004"):
    """Passive: True if a USB device with this VID:PID is listed in sysfs."""
    try:
        names = os.listdir(sysfs)
    except OSError:
        return False
    for n in names:
        base = os.path.join(sysfs, n)
        try:
            with open(os.path.join(base, "idVendor")) as f:
                v = f.read().strip()
            with open(os.path.join(base, "idProduct")) as f:
                p = f.read().strip()
        except OSError:
            continue
        if v.lower() == vid and p.lower() == pid:
            return True
    return False


def instance_dir(tracefs):
    return os.path.join(tracefs, "instances", INSTANCE)


def live_cleanup(tracefs=T.TRACEFS, baseline=None, out_dir=None, rmdir=os.rmdir):
    """Idempotent. Stops and removes ONLY our private instance and our probes, then verifies.

    If the instance still exists and out_dir is given, its trace is saved first (best effort)."""
    notes = []
    inst = instance_dir(tracefs)
    if os.path.isdir(inst):
        for ev in [os.path.join(inst, "events", T.GROUP, "enable")] + \
                  [os.path.join(inst, "events", "xhci-hcd", e, "enable") for e in T.XHCI_EVENTS] + \
                  [os.path.join(inst, "tracing_on")]:
            try:
                T._write(ev, "0")
            except OSError:
                pass
        if out_dir:
            try:
                os.makedirs(out_dir, mode=0o700, exist_ok=True)
                raw = os.path.join(out_dir, "raw_trace_recovered.txt")
                fd = os.open(raw, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                with os.fdopen(fd, "w") as f:
                    f.write(T._read(os.path.join(inst, "trace")))
                notes.append("saved remaining trace to %s" % raw)
            except OSError as e:
                notes.append("could not save trace: %s" % e)
        try:
            rmdir(inst)
            notes.append("instance removed")
        except OSError as e:
            notes.append("could not remove instance: %s" % e)
    try:
        present = T._read(os.path.join(tracefs, "kprobe_events"))
    except OSError:
        present = ""
    for name, _ in T.PROBES:
        if "%s/%s " % (T.GROUP, name) in present:
            try:
                T.tracefs_command(os.path.join(tracefs, "kprobe_events"), "-:%s/%s\n" % (T.GROUP, name))
            except OSError as e:
                notes.append("could not remove probe %s: %s" % (name, e))
    problems = T.verify_clean(tracefs, baseline)
    return problems, notes


def _pid_alive(pid):
    try:
        with open("/proc/%d/stat" % pid) as f:
            st = f.read().rsplit(")", 1)[1].split()[0]
        return st != "Z"
    except (OSError, IndexError):
        return False


def start_watchdog(parent_pid, hard_seconds, tracefs, baseline, out_dir, rmdir=os.rmdir, poll=0.2):
    """Independent supervisor process. If the parent dies abruptly (SIGKILL) or outlives the hard limit, it
    stops the parent and performs the cleanup itself. Returns the watchdog pid."""
    pid = os.fork()
    if pid:
        return pid
    try:
        os.setsid()
        deadline = time.monotonic() + hard_seconds
        while True:
            if not _pid_alive(parent_pid):
                break
            if time.monotonic() >= deadline:
                for sig, wait in ((signal.SIGTERM, 3.0), (signal.SIGKILL, 1.0)):
                    try:
                        os.kill(parent_pid, sig)
                    except OSError:
                        break
                    t_end = time.monotonic() + wait
                    while time.monotonic() < t_end and _pid_alive(parent_pid):
                        time.sleep(0.05)
                    if not _pid_alive(parent_pid):
                        break
                break
            time.sleep(poll)
        live_cleanup(tracefs, baseline, out_dir, rmdir)
    finally:
        os._exit(0)


def live_session(out_dir, tracefs=T.TRACEFS, sysfs=SYSFS_USB, deadline=240.0, settle=45.0, poll=0.25,
                 hard_extra=30.0, require_root=True, populate=None, rmdir=os.rmdir,
                 clock=time.monotonic, sleep=time.sleep, use_watchdog=True, announce=print):
    """Run one live capture. Returns (exit_code, report_dict)."""
    T._need_root(require_root)
    os.makedirs(out_dir, mode=0o700, exist_ok=True)
    os.chmod(out_dir, 0o700)
    inst = instance_dir(tracefs)
    if os.path.exists(inst):
        raise SystemExit("stale instance %s exists: run live-cleanup first" % inst)
    baseline = T.tracefs_baseline(tracefs)
    bpath = os.path.join(out_dir, "live_baseline.json")
    fd = os.open(bpath, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(baseline, f, indent=1)

    flag = {"sig": None}

    def on_signal(signum, _frame):
        flag["sig"] = signum

    old = {s: signal.signal(s, on_signal) for s in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP)}
    wd = start_watchdog(os.getpid(), deadline + hard_extra, tracefs, baseline, out_dir, rmdir) if use_watchdog else None
    rep = {"armed": False, "disconnect_seen": False, "reconnect_seen": False, "stop_reason": None,
           "t_arm": None, "t_disconnect": None, "t_reconnect": None, "t_stop": None, "wall_clock_arm": None}
    code = 1
    try:
        for name, d in T.PROBES:
            T.tracefs_command(os.path.join(tracefs, "kprobe_events"), d + "\n")
        os.mkdir(inst)
        if populate:
            populate(inst)
        T._write(os.path.join(inst, "buffer_size_kb"), str(BUF_KB))
        T._write(os.path.join(inst, "events", T.GROUP, "enable"), "1")
        for e in T.XHCI_EVENTS:
            T._write(os.path.join(inst, "events", "xhci-hcd", e, "enable"), "1")
        T._write(os.path.join(inst, "tracing_on"), "1")
        t0 = clock()
        rep.update({"armed": True, "t_arm": 0.0, "wall_clock_arm": time.strftime("%Y-%m-%dT%H:%M:%S%z")})
        was_present = target_present(sysfs)
        rep["present_at_arm"] = was_present
        announce("ARMED: tracing is running. Device present: %s. Unplug and replug CONNECT 2 once now. "
                 "Deadline %.0f s." % (was_present, deadline))
        state = "present" if was_present else "absent_at_arm"
        t_reconn = None
        while True:
            now = clock() - t0
            if flag["sig"] is not None:
                rep["stop_reason"] = "signal %d" % flag["sig"]
                break
            if now >= deadline:
                rep["stop_reason"] = ("deadline: no disconnect observed" if state == "present" else
                                      "deadline: device never returned" if state in ("absent", "absent_at_arm")
                                      else "deadline")
                break
            here = target_present(sysfs)
            if state in ("present",) and not here:
                state = "absent"
                rep["disconnect_seen"] = True
                rep["t_disconnect"] = round(now, 3)
                announce("DISCONNECT seen at %.1f s" % now)
            elif state in ("absent", "absent_at_arm") and here:
                state = "reconnected"
                rep["reconnect_seen"] = True
                rep["t_reconnect"] = round(now, 3)
                t_reconn = now
                announce("RECONNECT seen at %.1f s; settling for %.0f s (do not play audio)" % (now, settle))
            elif state == "reconnected" and now - t_reconn >= settle:
                rep["stop_reason"] = "settled after reconnect"
                break
            sleep(poll)
        rep["t_stop"] = round(clock() - t0, 3)
        T._write(os.path.join(inst, "events", T.GROUP, "enable"), "0")
        for e in T.XHCI_EVENTS:
            T._write(os.path.join(inst, "events", "xhci-hcd", e, "enable"), "0")
        T._write(os.path.join(inst, "tracing_on"), "0")
        buf_text = (T._read(os.path.join(inst, "buffer_size_kb")).splitlines() or [""])[0]
        trace = T._read(os.path.join(inst, "trace"))
        cpu = T.read_cpu_stats(inst)
        raw = os.path.join(out_dir, "raw_trace.txt")
        fd = os.open(raw, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(trace)
        h = T.HEADER.search(trace)
        oldest = [v["oldest_event_ts"] for v in cpu.values() if "oldest_event_ts" in v and v.get("entries", 0) > 0]
        rep.update({"buffer_size_kb": buf_text, "buffer_expanded": T.buffer_expanded(buf_text),
                    "entries_in_buffer": int(h.group(1)) if h else None,
                    "entries_written": int(h.group(2)) if h else None, "cpu_stats": cpu,
                    "cpu_overrun_total": sum(v.get("overrun", 0) + v.get("commit_overrun", 0) for v in cpu.values()),
                    "oldest_event_ts_min": min(oldest) if oldest else None})
        code = 0
    except Exception as e:  # always clean up, then report
        rep["error"] = "%s: %s" % (type(e).__name__, e)
        code = 2
    finally:
        problems, notes = live_cleanup(tracefs, baseline, out_dir, rmdir)
        rep["cleanup_problems"], rep["cleanup_notes"] = problems, notes
        if wd:
            try:
                os.kill(wd, signal.SIGKILL)
                os.waitpid(wd, 0)
            except OSError:
                pass
        for s, h in old.items():
            signal.signal(s, h)
        rp = os.path.join(out_dir, "live_report.json")
        fd = os.open(rp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            json.dump(rep, f, indent=1)
    if problems:
        code = max(code, 1)
    announce("STOPPED (%s). Cleanup: %s" % (rep.get("stop_reason"), "clean" if not problems else "; ".join(problems)))
    return code, rep


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    lv = sub.add_parser("live")
    lv.add_argument("--out-dir", required=True)
    lv.add_argument("--deadline", type=float, default=240.0, help="seconds until the session stops by itself")
    lv.add_argument("--settle", type=float, default=45.0, help="seconds to keep recording after the device returns")
    lc = sub.add_parser("live-cleanup")
    lc.add_argument("--out-dir")
    args = ap.parse_args(argv)
    if args.cmd == "live":
        if not (10 <= args.deadline <= 900) or not (5 <= args.settle <= 300):
            sys.exit("deadline must be 10..900 s and settle 5..300 s")
        code, _ = live_session(args.out_dir, deadline=args.deadline, settle=args.settle)
        return code
    T._need_root()
    problems, notes = live_cleanup(out_dir=args.out_dir)
    print("; ".join(notes) or "nothing to do")
    print("clean" if not problems else "; ".join(problems))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
