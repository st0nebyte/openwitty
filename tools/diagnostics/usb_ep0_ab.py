#!/usr/bin/env python3
"""Side-by-side request comparison of two EP0 traces (Phase 8A.21: cold boot vs live reconnect).

Pure offline analysis of already-captured text; touches no hardware and no tracefs.
Usage: usb_ep0_ab.py --cold TRACE --reconnect TRACE [--json OUT]
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb_ep0_trace as T  # noqa: E402

SLOW_ALT1_S = 0.010
NOT_OBSERVED = "NOT OBSERVED"


def incarnations(reqs):
    """Group target requests by (bus, devnum): a reconnect gets a new device number (or the same one reused,
    in which case a long gap is not used to split; the caller sees one group and the SI sequence shows it)."""
    groups = {}
    for q in sorted((q for q in reqs if "error" not in q), key=lambda q: q["ts_submit"]):
        groups.setdefault((q["bus"], q["devnum"]), []).append(q)
    return groups


def _si(q):
    return {"t": q["ts_submit"], "iface": q["desc"].get("interface"), "alt": q["desc"].get("alternate"),
            "ms": round(q["elapsed"] * 1000, 3), "ret": q["ret"], "timeout": T.is_timeout(q)}


def _cell(s):
    if s is None:
        return NOT_OBSERVED
    return "%.3f ms, ret %s%s" % (s["ms"], s["ret"], " TIMEOUT" if s["timeout"] else "")


def interface_rows(sis, iface):
    seq = [s for s in sis if s["iface"] == iface]
    first0 = next((s for s in seq if s["alt"] == 0), None)
    first1 = next((s for s in seq if s["alt"] == 1), None)
    after0 = None
    if first1 is not None:
        after0 = next((s for s in seq if s["alt"] == 0 and s["t"] > first1["t"]), None)
    return {"initial_alt0": first0, "first_alt1": first1,
            "first_alt1_slow": None if first1 is None else first1["ms"] / 1000.0 >= SLOW_ALT1_S,
            "following_alt0": after0,
            "gap_ms": None if after0 is None else round((after0["t"] - first1["t"]) * 1000 - first1["ms"], 1)}


def fu2_rows(reqs):
    tl = T.fu2_timeline(reqs)
    vol = [x for x in tl if x["kind"] == "volume" and x["request"] == "UAC_CUR"]
    gets = [x for x in vol if x["dir"] == "IN"]
    sets = [x for x in vol if x["dir"] == "OUT"]
    out = {"initial_get_db": gets[0].get("db") if gets else None, "sets": []}
    for s in sets:
        nxt = next((g for g in gets if g["t"] > s["t"]), None)
        out["sets"].append({"set_db": s.get("db"), "set_ret": s["ret"],
                            "readback_db": nxt.get("db") if nxt else None})
    out["final_set_db"] = sets[-1].get("db") if sets else None
    out["follows_writes"] = (None if not sets else
                             all(x["readback_db"] is not None and x["set_db"] is not None
                                 and abs(x["readback_db"] - x["set_db"]) < 0.5 for x in out["sets"]))
    return out


def summarize(reqs):
    res = []
    for (bus, dev), g in incarnations(reqs).items():
        sis = [_si(q) for q in g if q["desc"]["name"] == "SET_INTERFACE"]
        stalls = [q for q in g if q["desc"]["type"] == "class" and (q["setup"]["wIndex"] >> 8) == 5 and q["ret"] == -32]
        res.append({"bus": bus, "devnum": dev, "t_first": g[0]["ts_submit"], "t_last": g[-1]["ts_submit"],
                    "requests": len(g), "timeouts": sum(1 for q in g if T.is_timeout(q)),
                    "iface1": interface_rows(sis, 1), "iface2": interface_rows(sis, 2),
                    "fu2": fu2_rows(g), "unit5_stalls": len(stalls)})
    return res


def load(path):
    text = open(path).read()
    recs, info = T.parse_trace(text)
    reqs, other, unpaired, malformed = T.build_requests(recs)
    return summarize(reqs), {"records": len(recs), "target": len(reqs), "unpaired": unpaired,
                             "malformed": malformed, "loss": info["loss"]}


def _fmt(v):
    return NOT_OBSERVED if v is None else str(v)


def markdown(label_a, sa, label_b, sb):
    rows = []

    def add(name, fa, fb):
        rows.append((name, fa, fb))
    for tag, key in (("SET_INTERFACE(1,0) initial", ("iface1", "initial_alt0")),
                     ("first SET_INTERFACE(1,1)", ("iface1", "first_alt1")),
                     ("following SET_INTERFACE(1,0)", ("iface1", "following_alt0")),
                     ("first SET_INTERFACE(2,1)", ("iface2", "first_alt1")),
                     ("following SET_INTERFACE(2,0)", ("iface2", "following_alt0"))):
        add(tag, _cell(sa[key[0]][key[1]]) if sa else NOT_OBSERVED, _cell(sb[key[0]][key[1]]) if sb else NOT_OBSERVED)
    add("first alt-1 slow (>=10 ms)?", _fmt(sa and sa["iface1"]["first_alt1_slow"]),
        _fmt(sb and sb["iface1"]["first_alt1_slow"]))
    add("FU2 initial GET (dB)", _fmt(sa and sa["fu2"]["initial_get_db"]), _fmt(sb and sb["fu2"]["initial_get_db"]))
    add("FU2 SET -> readback (dB)", "; ".join("%s->%s" % (x["set_db"], x["readback_db"]) for x in sa["fu2"]["sets"]) or NOT_OBSERVED
        if sa else NOT_OBSERVED,
        "; ".join("%s->%s" % (x["set_db"], x["readback_db"]) for x in sb["fu2"]["sets"]) or NOT_OBSERVED
        if sb else NOT_OBSERVED)
    add("FU2 readback follows writes?", _fmt(sa and sa["fu2"]["follows_writes"]), _fmt(sb and sb["fu2"]["follows_writes"]))
    add("FU2 final SET (dB)", _fmt(sa and sa["fu2"]["final_set_db"]), _fmt(sb and sb["fu2"]["final_set_db"]))
    add("unit 5 STALLs (-32)", _fmt(sa and sa["unit5_stalls"]), _fmt(sb and sb["unit5_stalls"]))
    add("EP0 timeouts", _fmt(sa and sa["timeouts"]), _fmt(sb and sb["timeouts"]))
    out = ["| Item | %s | %s |" % (label_a, label_b), "|---|---|---|"]
    out += ["| %s | %s | %s |" % r for r in rows]
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cold", required=True)
    ap.add_argument("--reconnect", required=True)
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    ca, ia = load(a.cold)
    cb, ib = load(a.reconnect)
    print("cold incarnations: %d, reconnect incarnations: %d" % (len(ca), len(cb)))
    for lbl, inc in (("cold", ca), ("reconnect", cb)):
        for i, s in enumerate(inc):
            print("%s #%d bus %s dev %s t=%.3f..%.3f requests=%d timeouts=%d" %
                  (lbl, i, s["bus"], s["devnum"], s["t_first"], s["t_last"], s["requests"], s["timeouts"]))
    pick_a = max(ca, key=lambda s: s["requests"], default=None)
    pick_b = max(cb, key=lambda s: (s["t_first"]), default=None)
    print(markdown("cold boot", pick_a, "reconnect (latest incarnation)", pick_b))
    if a.json:
        fd = os.open(a.json, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            json.dump({"cold": ca, "reconnect": cb, "cold_info": ia, "reconnect_info": ib}, f, indent=1, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main())
