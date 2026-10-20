#!/usr/bin/env python3
"""Passive sysfs wait, then timed serial-free CONNECT 2 topology snapshots."""

import argparse
from pathlib import Path
import subprocess
import sys
import time


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def present():
    for path in Path("/sys/bus/usb/devices").iterdir():
        if read(path / "idVendor") == "29c2" and read(path / "idProduct") == "0004":
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--wait-seconds", type=int, default=300)
    args = parser.parse_args()
    if not args.output_dir.is_dir() or args.wait_seconds < 1 or args.wait_seconds > 600:
        parser.error("existing output directory and 1–600 second wait required")
    tool = Path(__file__).with_name("connect2_topology_snapshot.py")
    deadline = time.monotonic() + args.wait_seconds
    while not present():
        if time.monotonic() >= deadline:
            print("CONNECT 2 did not reappear before the wait limit", file=sys.stderr)
            return 2
        time.sleep(0.1)
    detected = time.monotonic()
    print("CONNECT 2 USB presence detected; capturing timed topology", flush=True)
    for seconds, label in ((0, "immediate"), (5, "5s"), (30, "30s"), (90, "90s")):
        remaining = detected + seconds - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        target = args.output_dir / f"reconnect_{label}.json"
        result = subprocess.run([sys.executable, str(tool), "--output", str(target)], check=False)
        if result.returncode:
            print(f"snapshot failed: {label}", file=sys.stderr)
            return result.returncode
        print(f"captured {label}: {target}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
