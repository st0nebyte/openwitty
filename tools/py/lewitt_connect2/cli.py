"""lewitt-connect2: read-only diagnostic CLI."""
from __future__ import annotations

import argparse
import json
import sys

from . import protocol as P


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="lewitt-connect2", description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="list LEWITT USB devices and their interfaces (no control traffic)")
    r = sub.add_parser("read", help="read a message from the device (detaches snd-usb-audio!)")
    r.add_argument("what", choices=["version", "status", "settings"])
    r.add_argument("--allow-detach", action="store_true",
                   help="allow detaching the kernel audio driver for the duration of the read")
    sub.add_parser("decode", help="decode a settings blob (hex on stdin)")
    a = ap.parse_args(argv)

    if a.cmd == "decode":
        print(json.dumps(P.settings_decode(bytes.fromhex(sys.stdin.read().strip())), indent=2))
        return 0

    from . import transport as T
    devs = T.find_devices()
    if a.cmd == "list":
        if not devs:
            print("no LEWITT device (VID 0x29c2) found")
            return 1
        print(json.dumps([T.describe(d) for d in devs], indent=2))
        return 0
    if not devs:
        print("no LEWITT device (VID 0x29c2) found", file=sys.stderr)
        return 1
    mid = {"version": P.MSG_VERSION, "status": P.MSG_STATUS, "settings": P.MSG_SETTINGS}[a.what]
    try:
        raw = T.read_message(devs[0], mid, allow_detach_kernel_driver=a.allow_detach)
    except PermissionError as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2
    if a.what == "settings":
        print(json.dumps(P.settings_decode(raw), indent=2))
    else:
        print(raw.hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
