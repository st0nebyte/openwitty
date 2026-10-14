# TODO

1. **Needs hardware**: plug in CONNECT 2, run `lewitt-connect2 list`, record real VID/PID and interface list; `amixer -c N contents`; confirm UAC1/UAC2 request variant.
2. **Needs user**: `sudo apt install innoextract`, then extract the Windows installer (`research/win`) to cross-check the protocol and pull UI assets.
3. Disassemble: write path / tick cadence, `iface_status_t`, `iface_version_t`, `iface_triggers_t`.
4. Resolve Linux access problem (see PROTOCOL.md).
5. Decide GUI stack (Qt 6/QML preferred; `qmake6` not installed here).
6. Preset import/export (`.preset` JSON) once settings semantics are confirmed.
7. Packaging (deb, PKGBUILD) once there is something to package.
