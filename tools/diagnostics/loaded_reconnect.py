#!/usr/bin/env python3
"""Phase 8A.26: passive evidence for ONE user-operated physical reconnect of CONNECT 2 while snd-usb-audio is ALREADY loaded.

Strictly read-only and unprivileged: it reads sysfs, /proc, the kernel journal, `amixer -c N controls` (names only),
`pw-dump` and (optionally) the GET-only `openwitty-cli read settings`. It never loads/unloads a module, never touches
/boot, never resets or rebinds anything, never sends a SET and never plays audio.

  prepare                         create the private run directory (shared helpers of deferred_probe.py)
  check --run-dir D --stage pre   baseline gate + state before the reconnect (writes D/checkpoint.json)
  watch --run-dir D [--deadline]  sysfs-only observer of exactly one disconnect + reappearance (writes D/reconnect.json)
  check --run-dir D --stage post  post gate: exactly one new disconnect/enumeration, new device number, driver still loaded
  classify --run-dir D [--cli P]  passive verdict; may be repeated later (every pass is kept); never touches the device
"""

import argparse
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deferred_probe as D  # noqa: E402
import post_boot_health as H  # noqa: E402

EXPECTED_KERNEL = "7.3.0-5-cachyos"
BASE_REL = ".local/state/openwitty/trace-8a26"
AUDIO_DRIVER = "snd-usb-audio"
# AudioControl + the two AudioStreaming interfaces of configuration 1. 1.3 / 1.4 are vendor-specific and never required.
REQUIRED_AUDIO_INTERFACES = ("1.0", "1.1", "1.2")
POLL_S = 0.1          # one passive sysfs sampling sequence; every timing result carries this sampling uncertainty
BIND_WAIT_S = 30.0
SETTLE_S = H.SETTLE_S


def interface_drivers(port, base="/sys/bus/usb/devices"):
    """-> {"1.0": "snd-usb-audio" | None, ...} for the interfaces of `port` (None = unbound)."""
    out = {}
    try:
        names = sorted(os.listdir(base))
    except OSError:
        return out
    for n in names:
        if n.startswith(port + ":"):
            link = os.path.join(base, n, "driver")
            out[n.split(":", 1)[1]] = os.path.basename(os.path.realpath(link)) if os.path.islink(link) else None
    return out


def audio_bound(drivers):
    """ALL of 1.0, 1.1 and 1.2 exist and are bound to snd-usb-audio. A missing interface is never 'bound'."""
    return all(drivers.get(k) == AUDIO_DRIVER for k in REQUIRED_AUDIO_INTERFACES)


def journal_events(text):
    """Counts of the target port's events plus driver registration, from the current-boot kernel journal."""
    info = D.analyze_reenumeration(text)
    info["driver_registered"] = text.count("registered new interface driver " + AUDIO_DRIVER)
    return info


def pre_state_failing(findings, pw):
    """The comparison is only meaningful if CONNECT 2 starts in the failing state of 8A.25."""
    return bool(findings["fu2_sticky"]) and pw is not None and pw["state"] in (H.PW_NO_ROUTES, H.PW_HW_FALSE)


def gate_pre(kernel, devices, drivers, module_loaded, jstatus, jtext, findings, pw, boot, expect_boot_prefix=None):
    p = []
    if kernel != EXPECTED_KERNEL:
        p.append("running kernel is %s, expected %s" % (kernel, EXPECTED_KERNEL))
    if expect_boot_prefix and not boot.startswith(expect_boot_prefix):
        p.append("boot id %s... is not the expected boot (%s...)" % (boot[:8], expect_boot_prefix))
    if not module_loaded:
        p.append("snd_usb_audio is not loaded")
    if jstatus != "OK":
        p.append("kernel journal evidence unavailable (%s)" % jstatus)
    elif "registered new interface driver " + AUDIO_DRIVER not in jtext:
        p.append("journal of this boot shows no snd-usb-audio registration")
    if len(devices) != 1:
        p.append("expected exactly one CONNECT 2, found %d" % len(devices))
    else:
        d = devices[0]
        if d["port"] != D.EXPECTED["port"]:
            p.append("device on port %s, expected %s" % (d["port"], D.EXPECTED["port"]))
        if d["controller_pci"] != D.EXPECTED["controller"]:
            p.append("device on controller %s, expected %s" % (d["controller_pci"], D.EXPECTED["controller"]))
    if not audio_bound(drivers):
        p.append("audio interfaces are not bound to %s: %s" % (AUDIO_DRIVER, drivers))
    if jstatus == "OK" and len(devices) == 1:
        p += journal_devnum_problems(journal_events(jtext), devices[0]["devnum"], "baseline")
    if jstatus == "OK" and not pre_state_failing(findings, pw):
        p.append("CONNECT 2 is not in the failing state (FU2 failure + no usable route): the comparison would not be meaningful")
    return p


def journal_devnum_problems(ev, sysfs_devnum, label):
    """The last enumeration number in the journal must equal the device number sysfs shows now."""
    nums = ev.get("device_numbers") or []
    if not nums:
        return ["%s: no enumeration number in the journal to compare with sysfs" % label]
    if str(nums[-1]) != str(sysfs_devnum):
        return ["%s: journal says device number %s, sysfs says %s" % (label, nums[-1], sysfs_devnum)]
    return []


def delta_problems(before, after, devnum_before, devnum_after, sysfs_after=None):
    """Exactly one NEW disconnect and one NEW enumeration since the baseline checkpoint, and a consistent device-number
    story: journal sequence = baseline sequence + one new number = number the observer saw = number sysfs shows now."""
    p = []
    for key, name in (("disconnects", "USB disconnect"), ("enumerations", "enumeration"), ("found_lines", "identity line")):
        if after[key] - before[key] != 1:
            p.append("expected exactly 1 new %s, journal shows %d" % (name, after[key] - before[key]))
    if after.get("driver_registered", 0) != before.get("driver_registered", 0):
        p.append("snd-usb-audio registration count changed (%d -> %d): the module was reloaded?"
                 % (before.get("driver_registered", 0), after.get("driver_registered", 0)))
    if devnum_after is None or str(devnum_after) == str(devnum_before):
        p.append("device number did not change (%s -> %s)" % (devnum_before, devnum_after))
    bn, an = [str(x) for x in before.get("device_numbers", [])], [str(x) for x in after.get("device_numbers", [])]
    if len(an) != len(bn) + 1 or an[:len(bn)] != bn:
        p.append("journal enumeration sequence %s is not the baseline %s plus exactly one new number" % (an, bn))
    elif devnum_after is not None and an[-1] != str(devnum_after):
        p.append("observer saw device number %s, journal's new enumeration is %s" % (devnum_after, an[-1]))
    if sysfs_after is None or str(sysfs_after) != str(devnum_after):
        p.append("sysfs shows device number %s now, observer recorded %s" % (sysfs_after, devnum_after))
    return p


def probe_timing(text):
    """Journal timestamps around the re-enumeration. NOT request latency: ordinary journal lines only."""
    stamps = [l[:22] for l in text.splitlines() if "idVendor=%s, idProduct=%s" % (D.EXPECTED["vid"], D.EXPECTED["pid"]) in l]
    return {"last_enumeration_journal_ts": stamps[-1] if stamps else None}


def verdict(post_ok, findings_after, pcm, pw, drivers, age_s, journal_ok):
    """-> (verdict, reason). INVALID if the post gate failed; INCONCLUSIVE for incomplete binding or evidence."""
    if not post_ok:
        return "INVALID", "post-reconnect gate failed"
    if not journal_ok:
        return "INCONCLUSIVE", "kernel journal after the reconnect unreadable or incomplete"
    if not audio_bound(drivers):
        return "INCONCLUSIVE", "audio interfaces not (yet) bound to %s: %s" % (AUDIO_DRIVER, drivers)
    return H.classify_evidence(findings_after, pcm, pw, age_s)


# ------------------------------------------------------------------ commands

def _collect(runner, cli=None, since=None):
    st, detail, text = D.read_kernel_journal(since=since, runner=runner, require_boot_start=since is None,
                                             require_device_line=since is None)
    card = H.find_card()
    ctl = None
    if card is not None:
        rc, out, _ = runner(["amixer", "-c", card.replace("card", ""), "controls"])
        ctl = out if rc == 0 else None
    pcm = None if ctl is None else ("PCM Playback Volume" in ctl)
    snap = None
    if cli:
        rc, s, _ = runner([cli, "read", "settings", "--method", "interface", "--interface", "3"], 30)
        snap = H.parse_hw_snapshot(s if rc == 0 else "")
    return st, detail, text, pcm, H.pipewire_connect2_now(), snap


def cmd_prepare(home=None, euid=None):
    D.BASE_REL = BASE_REL
    return D.cmd_prepare(home=home, euid=euid)


def cmd_check(run_dir, stage, cli=None, expect_boot_prefix=None, runner=D.default_runner, kernel=None, boot=None):
    fd, uid, _ = D.open_run_dir(run_dir)
    kernel = kernel or os.uname().release
    boot = boot or D.boot_id()
    try:
        st, detail, text, pcm, pw, snap = _collect(runner, cli)
        devices = D.find_devices()
        drivers = interface_drivers(D.EXPECTED["port"])
        ev = journal_events(text)
        findings = H.parse_kernel_log(text)
        extra = {}
        problems = []
        if stage == "pre":
            problems = gate_pre(kernel, devices, drivers, D.module_loaded(), st, text, findings, pw, boot, expect_boot_prefix)
            extra = {"events_before": ev, "pcm_playback_volume": pcm, "pipewire_connect2": pw, "hardware_snapshot": snap,
                     "findings_before": findings, "interface_drivers": drivers,
                     "cmdline_has_temporary_blacklist_arg": D.BLACKLIST_ARG in open("/proc/cmdline").read().split()}
        else:
            bdev = None
            try:
                base = D.read_json(fd, "checkpoint.json", uid)
                rec = D.read_json(fd, "reconnect.json", uid)
            except D.RunDirError as e:
                base = rec = None
                problems.append("post stage needs checkpoint.json and reconnect.json (%s)" % e)
            if base is not None:
                try:
                    bdev = (base.get("devices") or [{}])[0].get("devnum")
                    if base.get("gate") != "OK" or base.get("boot_id") != boot:
                        problems.append("baseline checkpoint is not an OK gate of this boot")
                    if kernel != EXPECTED_KERNEL:
                        problems.append("running kernel is %s, expected %s" % (kernel, EXPECTED_KERNEL))
                    if rec.get("status") != "reconnected":
                        problems.append("reconnect observation status: %s" % rec.get("status"))
                    if rec.get("multiple_seen"):
                        problems.append("more than one matching device was seen during the reconnect")
                    if not D.module_loaded():
                        problems.append("snd_usb_audio is no longer loaded")
                    if not audio_bound(drivers):
                        problems.append("audio interfaces %s are not all bound to %s: %s"
                                        % (", ".join(REQUIRED_AUDIO_INTERFACES), AUDIO_DRIVER, drivers))
                    if st != "OK":
                        problems.append("kernel journal evidence unavailable (%s)" % st)
                    else:
                        problems += delta_problems(base["events_before"], ev, bdev, rec.get("devnum_after"),
                                                   devices[0]["devnum"] if len(devices) == 1 else None)
                    if len(devices) != 1 or devices[0]["port"] != D.EXPECTED["port"] \
                            or devices[0]["controller_pci"] != D.EXPECTED["controller"]:
                        problems.append("identity after reconnect is not exactly one CONNECT 2 on %s / %s"
                                        % (D.EXPECTED["port"], D.EXPECTED["controller"]))
                except (KeyError, TypeError, AttributeError, IndexError, ValueError) as e:
                    problems.append("checkpoint/observer evidence malformed or incomplete (%s: %s)" % (type(e).__name__, e))
                extra = {"events_after": ev, "devnum_before": bdev, "devnum_after": (rec or {}).get("devnum_after"),
                         "t_reconnect_epoch": (rec or {}).get("t_reconnect_epoch"), "interface_drivers": drivers}
        rep = {"stage": stage, "time": D.now_pair(), "boot_id": boot, "kernel": kernel, "devices": devices,
               "module_loaded": D.module_loaded(), "journal_status": st, "journal_detail": detail, "problems": problems,
               "gate": "OK" if not problems else "INVALID/NOT READY"}
        rep.update(extra)
        D.write_json(fd, "checkpoint.json" if stage == "pre" else "checkpoint_post.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if not problems else 1


def observe_reconnect(deadline, devices_fn=None, drivers_fn=None, clock=time.monotonic, sleep=time.sleep, wall=time.time,
                      poll=POLL_S, bind_wait=BIND_WAIT_S, install_signals=True):
    """ONE passive sysfs sampling sequence: present -> absent (disconnect) -> present again -> audio interfaces bound.

    Every event time is only known to lie between two consecutive samples, so each event is recorded as a pair
    (last sample before, first sample showing it) in wall-clock epochs. The derived reconnect->bound interval is given as
    bounds [min, max], never as a point value, and is NOT USB request latency: sysfs shows the device node when the core
    registers it, and the driver bind happens afterwards in the kernel. Nothing is written to the device, its driver or the bus."""
    devices_fn = devices_fn or D.find_devices
    drivers_fn = drivers_fn or (lambda: interface_drivers(D.EXPECTED["port"]))
    flag = {"sig": None}
    old = {}
    if install_signals:
        import signal
        for sg in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            old[sg] = signal.signal(sg, lambda n, f: flag.update(sig=n))
    t0 = clock()
    d0 = devices_fn()
    rep = {"armed_epoch": wall(), "sampling_interval_s": poll, "present_at_arm": len(d0) == 1,
           "devnum_before": d0[0]["devnum"] if d0 else None, "multiple_seen": False, "devnum_after": None,
           "t_disconnect_epoch": None, "t_reconnect_epoch": None, "reappear_bounds_epoch": None,
           "t_bound_epoch": None, "bound_bounds_epoch": None, "reconnect_to_bound_bounds_s": None,
           "interface_drivers_at_bound": None, "timing_note": "sysfs sampling bounds; not USB request latency"}
    state = "present" if len(d0) == 1 else "bad_start"
    prev = wall()          # time of the last sample taken
    t_reapp_lo = t_reapp_hi = None
    t_bound_lo = None
    t_react_mono = None
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
                rep["status"] = {"present": "deadline: no disconnect", "absent": "deadline: device never returned"}.get(
                    state, "deadline")
                break
            if t_react_mono is not None and clock() - t_react_mono >= bind_wait:
                rep["status"] = "reconnected"          # reappeared, audio interfaces did not bind within the wait
                break
            devs = devices_fn()
            cur = wall()
            if len(devs) > 1:
                rep["multiple_seen"] = True
            if state == "present" and len(devs) == 0:
                state = "absent"
                rep["t_disconnect_epoch"] = cur
            elif state == "absent" and len(devs) == 1:
                state = "reappeared"
                t_reapp_lo, t_reapp_hi = prev, cur
                rep["t_reconnect_epoch"] = cur
                rep["reappear_bounds_epoch"] = [prev, cur]
                rep["devnum_after"] = devs[0]["devnum"]
                t_react_mono = clock()
                t_bound_lo = prev                       # not yet known to be unbound earlier than the last absent sample
            if state == "reappeared":
                drv = drivers_fn()
                if audio_bound(drv):
                    rep["t_bound_epoch"] = cur
                    rep["bound_bounds_epoch"] = [t_bound_lo, cur]
                    rep["interface_drivers_at_bound"] = drv
                    rep["reconnect_to_bound_bounds_s"] = [round(max(0.0, t_bound_lo - t_reapp_hi), 3),
                                                          round(cur - t_reapp_lo, 3)]
                    rep["status"] = "reconnected"
                    break
                t_bound_lo = cur                         # sampled and still not (fully) bound
            prev = cur
            sleep(poll)
    finally:
        for sg, h in old.items():
            import signal
            signal.signal(sg, h)
    rep.setdefault("status", "unknown")
    return rep


def cmd_watch(run_dir, deadline):
    fd, uid, _ = D.open_run_dir(run_dir)
    try:
        try:
            base = D.read_json(fd, "checkpoint.json", uid)
        except D.RunDirError:
            sys.exit("run `check --stage pre` first")
        if base.get("gate") != "OK" or base.get("boot_id") != D.boot_id():
            sys.exit("baseline checkpoint is not an OK gate of this boot")
        print("ARMED: observing sysfs only. Unplug and replug CONNECT 2 ONCE now (same port). Deadline %d s." % deadline, flush=True)
        rep = observe_reconnect(deadline)
        D.write_json(fd, "reconnect.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0 if rep.get("status") == "reconnected" else 1


def cmd_classify(run_dir, cli, runner=D.default_runner):
    fd, uid, _ = D.open_run_dir(run_dir)
    try:
        try:
            post = D.read_json(fd, "checkpoint_post.json", uid)
            rec = D.read_json(fd, "reconnect.json", uid)
        except D.RunDirError as e:
            sys.exit("refusing: run `check --stage post` first (%s)" % e)
        t_re = rec.get("t_reconnect_epoch")
        if not isinstance(t_re, (int, float)) or not isinstance(post.get("problems"), list):
            rep = {"classification": "INVALID", "reason": "reconnect observation or post checkpoint incomplete"}
        else:
            since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t_re - 2))
            st, detail, text_after, pcm, pw, snap = _collect(runner, cli, since=since)
            findings = H.parse_kernel_log(text_after)
            drivers = interface_drivers(D.EXPECTED["port"])
            age = max(0.0, time.time() - t_re)
            v, reason = verdict(not post["problems"], findings, pcm, pw, drivers, age, st == "OK")
            rep = {"classification": v, "reason": reason, "age_since_reconnect_s": round(age, 1),
                   "findings_after_reconnect": findings, "pcm_playback_volume": pcm, "pipewire_connect2": pw,
                   "interface_drivers": drivers, "hardware_snapshot": snap,
                   "journal_after": {"status": st, "detail": detail},
                   "probe_timing": dict(probe_timing(text_after),
                                        reappear_bounds_epoch=rec.get("reappear_bounds_epoch"),
                                        bound_bounds_epoch=rec.get("bound_bounds_epoch"),
                                        reconnect_to_bound_bounds_s=rec.get("reconnect_to_bound_bounds_s"),
                                        sampling_interval_s=rec.get("sampling_interval_s"),
                                        note="sysfs sampling bounds and journal timestamps only; not USB request latency"),
                   "devnum": {"before": post.get("devnum_before"), "after": post.get("devnum_after")}}
        D.write_json(fd, "classification_%s.json" % time.strftime("%H%M%S"), rep, uid)
        D.write_json(fd, "classification.json", rep, uid)
    finally:
        os.close(fd)
    print(json.dumps(rep, indent=1, default=str))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("prepare")
    c = sub.add_parser("check")
    c.add_argument("--run-dir", required=True)
    c.add_argument("--stage", choices=["pre", "post"], required=True)
    c.add_argument("--cli")
    c.add_argument("--expect-boot-prefix")
    w = sub.add_parser("watch")
    w.add_argument("--run-dir", required=True)
    w.add_argument("--deadline", type=float, default=240.0)
    k = sub.add_parser("classify")
    k.add_argument("--run-dir", required=True)
    k.add_argument("--cli")
    a = ap.parse_args(argv)
    if os.geteuid() == 0:
        sys.exit("this tool is unprivileged by design: run it as the normal desktop user (no sudo)")
    try:
        if a.cmd == "prepare":
            return cmd_prepare()
        if a.cmd == "check":
            return cmd_check(a.run_dir, a.stage, a.cli, a.expect_boot_prefix)
        if a.cmd == "watch":
            return cmd_watch(a.run_dir, a.deadline)
        return cmd_classify(a.run_dir, a.cli)
    except D.RunDirError as e:
        sys.exit("run directory rejected: %s" % e)


if __name__ == "__main__":
    sys.exit(main())
