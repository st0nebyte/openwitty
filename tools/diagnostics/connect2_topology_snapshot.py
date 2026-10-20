#!/usr/bin/env python3
"""Read-only, serial-free CONNECT 2 USB/ALSA/PipeWire topology snapshot.

No USB control transfer, mixer setter, PipeWire setter, or daemon restart.
Output is a small allowlisted JSON document, never a raw pw-dump/USB capture.
"""

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


VID, PID = "29c2", "0004"
USB_ROOT = Path("/sys/bus/usb/devices")


def read(path):
    try:
        return path.read_text().strip()
    except (OSError, UnicodeError):
        return None


def run(*args):
    try:
        p = subprocess.run(args, check=False, capture_output=True, text=True, timeout=5)
        return p.stdout if p.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def number(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def usb_snapshot():
    found = []
    for device in sorted(USB_ROOT.iterdir()):
        if read(device / "idVendor") != VID or read(device / "idProduct") != PID:
            continue
        interfaces = []
        for iface in sorted(USB_ROOT.glob(device.name + ":*")):
            driver = iface / "driver"
            interfaces.append({
                "number": number(read(iface / "bInterfaceNumber")),
                "class": read(iface / "bInterfaceClass"),
                "subclass": read(iface / "bInterfaceSubClass"),
                "protocol": read(iface / "bInterfaceProtocol"),
                "current_altsetting": number(read(iface / "bAlternateSetting")),
                "endpoints": number(read(iface / "bNumEndpoints")),
                "driver": driver.resolve().name if driver.exists() else None,
            })
        found.append({
            "sysfs_port": device.name,  # physical port path, not the device serial
            "vid": VID,
            "pid": PID,
            "configuration": number(read(device / "bConfigurationValue")),
            "configurations": number(read(device / "bNumConfigurations")),
            "speed_mbps": read(device / "speed"),
            "interfaces": interfaces,
        })
    return found


def stream_capabilities(card):
    text = read(Path(f"/proc/asound/card{card}/stream0"))
    if not text:
        return {}
    result = {}
    section = None
    keep = {"Status", "Interface", "Altset", "Format", "Channels", "Endpoint",
            "Rates", "Data packet interval", "Bits", "Channel map", "Sync Endpoint",
            "Sync EP Interface", "Sync EP Altset", "Implicit Feedback Mode"}
    for line in text.splitlines():
        if line in ("Playback:", "Capture:"):
            section = line[:-1].lower()
            result[section] = []
        elif section and ":" in line:
            key, value = (x.strip() for x in line.split(":", 1))
            if key in keep:
                result[section].append({key: value})
        elif section and line.strip().startswith(("Interface ", "Altset ")):
            key, value = line.strip().split(" ", 1)
            result[section].append({key.lower(): number(value)})
    return result


def mixer_controls(card):
    contents = run("amixer", "-c", str(card), "contents") or ""
    result = []
    current = None
    for line in contents.splitlines():
        match = re.match(r"numid=\d+,iface=\w+,name='([^']+)'", line)
        if match:
            current = {"name": match.group(1)}
            result.append(current)
        elif current and line.strip().startswith("; type="):
            spec = line.strip()
            for key in ("type", "access", "values", "min", "max"):
                found = re.search(rf"\b{key}=([^,]+)", spec)
                if found:
                    current[key] = found.group(1)
        elif current and line.strip().startswith(": values="):
            current["current"] = line.split("=", 1)[1].strip()
    return result


def usb_mixer_units(card):
    text = read(Path(f"/proc/asound/card{card}/usbmixer")) or ""
    result = []
    unit = None
    for line in text.splitlines():
        unit_match = re.match(r"  Unit: (\d+)", line)
        control_match = re.match(r'    Control: name="([^"]+)"', line)
        if unit_match:
            unit = int(unit_match.group(1))
        elif control_match and unit is not None:
            result.append({"unit": unit, "name": control_match.group(1)})
    return result


def alsa_snapshot():
    cards = read(Path("/proc/asound/cards")) or ""
    result = []
    playback = run("aplay", "-l") or ""
    for match in re.finditer(r"^\s*(\d+) \[C2\s*\]:.*CONNECT 2", cards, re.M):
        index = int(match.group(1))
        devices = []
        for line in playback.splitlines():
            pcm = re.match(r"card (\d+): .*device (\d+): (.*)", line)
            if pcm and int(pcm.group(1)) == index:
                devices.append({"number": int(pcm.group(2)),
                                "description": pcm.group(3).split(" [", 1)[0]})
        controls = run("amixer", "-c", str(index), "scontrols") or ""
        control_names = re.findall(r"Simple mixer control '([^']+)'", controls)
        result.append({"card_number": index, "card_id": "C2", "playback_devices": devices,
                       "simple_controls": control_names, "mixer_controls": mixer_controls(index),
                       "usb_mixer_units": usb_mixer_units(index),
                       "stream": stream_capabilities(index)})
    return result


def clean_sink_name(name):
    if not name:
        return None
    if "usb-Lewitt_GmbH_CONNECT_2_" in name:
        return re.sub(r"usb-Lewitt_GmbH_CONNECT_2_[^.]+", "usb-CONNECT2-[redacted]", name)
    return name


def selected_props(props, keys):
    return {key: props.get(key) for key in keys if key in props}


def pipewire_snapshot():
    raw = run("pw-dump")
    if raw is None:
        return {"available": False}
    try:
        objects = json.loads(raw)
    except json.JSONDecodeError:
        return {"available": False, "parse_error": True}
    device = next((o for o in objects if o.get("type") == "PipeWire:Interface:Device"
                   and o.get("info", {}).get("props", {}).get("alsa.components") == "USB29c2:0004"), None)
    device_id = device.get("id") if device else None
    result = {"available": True, "device_present": device is not None,
              "device_id_ephemeral": device_id, "profiles": [], "active_profile": None,
              "routes": [], "nodes": []}
    if device:
        params = device.get("info", {}).get("params", {})
        result["profiles"] = [selected_props(p, ("name", "available"))
                              for p in params.get("EnumProfile", [])]
        active = params.get("Profile", [])
        result["active_profile"] = active[0].get("name") if active else None
        for route in params.get("Route", []):
            info = route.get("info", [])
            pairs = dict(zip(info[1::2], info[2::2])) if isinstance(info, list) else {}
            props = route.get("props", {})
            result["routes"].append({
                "name": route.get("name"), "direction": route.get("direction"),
                "available": route.get("available"), "hw_volume": pairs.get("route.hw-volume"),
                "hw_mute": pairs.get("route.hw-mute"),
                "volume": props.get("channelVolumes"), "soft_volume": props.get("softVolumes"),
                "mute": props.get("mute"),
            })
    for obj in objects:
        if obj.get("type") != "PipeWire:Interface:Node":
            continue
        info = obj.get("info", {})
        props = info.get("props", {})
        if props.get("device.id") != device_id or device_id is None:
            continue
        if props.get("media.class") not in ("Audio/Sink", "Audio/Source"):
            continue
        node_params = info.get("params", {}).get("Props", [])
        node_props = node_params[0] if node_params else {}
        result["nodes"].append({
            "id_ephemeral": obj.get("id"), "kind": props.get("media.class"),
            "name": clean_sink_name(props.get("node.name")),
            "description": props.get("node.description"),
            "profile": props.get("device.profile.name"),
            "alsa_pcm": props.get("api.alsa.path"),
            "channels": props.get("audio.channels"), "positions": props.get("audio.position"),
            "volume": node_props.get("channelVolumes"),
            "soft_volume": node_props.get("softVolumes"),
            "mute": node_props.get("mute"),
        })
    result["nodes"].sort(key=lambda n: (n["kind"] or "", n["name"] or ""))
    return result


def default_sink():
    info = run("pactl", "info") or ""
    match = re.search(r"^Default Sink:\s*(.+)$", info, re.M)
    return clean_sink_name(match.group(1)) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="create a new private snapshot file; never overwrite")
    args = parser.parse_args()
    snapshot = {
        "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "monotonic_ns": time.monotonic_ns(),
        "usb": usb_snapshot(), "alsa": alsa_snapshot(),
        "pipewire": pipewire_snapshot(), "default_sink": default_sink(),
    }
    encoded = json.dumps(snapshot, indent=2, sort_keys=True) + "\n"
    if args.output:
        flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(args.output, flags, 0o600)
        with os.fdopen(fd, "w") as output:
            output.write(encoded)
    else:
        sys.stdout.write(encoded)


if __name__ == "__main__":
    main()
