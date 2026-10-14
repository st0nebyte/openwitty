# TODO

## Needs the device (do in this order, read-only first)
1. `lewitt-cli list`; record VID/PID + descriptors (`lsusb -v`), `amixer -c N contents`.
2. `lewitt-cli probe` (dry run), then `probe --execute` (GETs only) — decides the access method.
3. Version → status → settings reads; compare with the official app / front panel.
4. Propose one low-risk write test (e.g. mute toggle) for explicit approval.

## Needs the user
* Approve REA MCP registration (`rea setup --dry-run` first; edits ~/.claude.json and ~/.claude/skills).
* If `ls -l /dev/bus/usb/...` shows no write access for the user, an admin must add the udev rule (not fixable without root).

## Engineering
* Windows binary: recover `AudioControlRequestGet/Set` call sites with REA/Ghidra and confirm bmRequestType/bRequest/wValue/wIndex match macOS.
* Use REA MCP session for iterative function/xref work (CLI re-imports each call, ~2 min).
* Recover `iface_version_t` layout; identify which settings groups are headphone/speaker.
* Disassemble the write path & tick cadence (`Connect2Device::tick`).
* Preset import/export (`.preset` JSON as shipped by the app).
* Visual fidelity: extract/inspect UI assets from the app (not yet done; current QML is an independent layout).
* Packaging (deb/PKGBUILD/AppImage), udev rule in `packaging/udev`.
* Kernel hwdep patch only if probe fails (docs/USB_ACCESS.md).
