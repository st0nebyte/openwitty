# Third-party components, assets and rights review

OpenWitty's own code, documentation and branding are released under the MIT licence
(`LICENSE`, © 2026 st0nebyte). This page records what that does **not** cover and the
rights review done before the repository was uploaded.

## Run-time dependencies (not bundled, not modified)

| Component | Licence | Use |
|---|---|---|
| Qt 6 (Core, Gui, Qml, Quick; QML modules QtQuick, QtQuick.Window, …) | LGPL-3.0 / GPL / commercial | dynamically linked from the distribution |
| libusb-1.0 | LGPL-2.1+ | dynamically linked (optional: `-DLEWITT_ENABLE_USB=OFF`) |
| C++ standard library | GPL-3.0 with GCC runtime exception | toolchain |

Distribution packages declare these as dependencies; nothing is statically linked or copied
into the repository.

## Build/test-only

GoogleTest (BSD-3-Clause) is found on the build host (test builds only). CMake, Ninja, the
rootless sysroot scripts and the lintian/makepkg helper downloads are never committed
(`.tools/`, `.sysroot/` are git-ignored).

## Assets

* `assets/**` — OpenWitty branding, *manually reconstructed vector concept* by the repository
  owner (see `assets/README.md`). The green `#97C21E` is described there as an *approximate*
  LEWITT-style colour, not an official brand code. Preserved unmodified.
* `docs/screenshots/*.png` — renders of OpenWitty's **own** QML interface running against the
  *simulated* device. No LEWITT screenshots or artwork are included.
* The QML interface layout was modelled on publicly visible screenshots of the vendor's
  control application (see `docs/dev/VISUAL_REFERENCES.md`, `src/gui/qml/Theme.qml`); no vendor
  image, font or code was copied. If the vendor objects to the similarity, the visual design
  is isolated in `src/gui/qml` and can be changed without touching the protocol/core.
* No LEWITT logo or trade dress is used. "LEWITT", "CONNECT" and "CONTROL CENTER" are trademarks
  of their owners and appear only to identify compatibility.

## Reverse-engineering material

* Tracked: protocol *facts* (message ids, offsets, enums, request parameters) in `PROTOCOL.md`,
  `tests/`, and own analysis scripts in `tools/re/` (Ghidra Java scripts, small Python helpers).
  They contain **no** vendor code, decompiler output dumps, binaries, firmware or installers.
  Function names/addresses quoted in docs are identifiers, not code.
* Not tracked and never uploaded (`.gitignore`): `research/` (vendor installers, extracted
  binaries, Ghidra project/caches, fetched guide pages), `.tools/`, `.sysroot/`, `.venv/`.
* `docs/dev/OFFICIAL_GUIDE.md` is a paraphrased feature summary of the public online guide with a
  source link; the guide's text and screenshots are not reproduced.
* Interoperability analysis of a vendor's own software for the purpose of writing a compatible
  program is permitted in many jurisdictions (e.g. EU Software Directive 2009/24/EC Art. 6);
  this is not legal advice and local law varies.

## Unresolved / to watch

* Possible visual similarity with the vendor UI (see above).
* The icon colour and the "W" waveform concept resemble LEWITT's palette; the owner's assets
  are used as provided.
* Contributors must not paste vendor code, decompiler output or vendor artwork into the
  repository; add such material only under `research/` (ignored).
