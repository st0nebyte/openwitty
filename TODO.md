# TODO

## Needs the device (do in this order, read-only first)
1. `lewitt-cli list`; record VID/PID + descriptors (`lsusb -v`), `amixer -c N contents`.
2. `lewitt-cli probe` (dry run), then `probe --execute` (GETs only) — decides the access method.
3. Version → status → settings reads; compare with the official app / front panel.
4. Propose one low-risk write test (e.g. mute toggle) for explicit approval.

## Needs the user
* `sudo apt install innoextract` → extract Windows installer, cross-check protocol & assets.

## Engineering
* Recover `iface_version_t` layout; identify which settings groups are headphone/speaker.
* Disassemble the write path & tick cadence (`Connect2Device::tick`).
* Preset import/export (`.preset` JSON as shipped by the app).
* Visual fidelity: extract/inspect UI assets from the app (not yet done; current QML is an independent layout).
* Packaging (deb/PKGBUILD/AppImage), udev rule in `packaging/udev`.
* Kernel hwdep patch only if probe fails (docs/USB_ACCESS.md).
