# Licensing assessment (no licence has been chosen)

> Not legal advice. No licence file has been added and none is implied: until the owner
> decides, the code is **all rights reserved**, and package metadata says so
> (`LicenseRef-proprietary` / `LicenseRef-custom`). This page only lays out the trade-offs.

## What is in the repository (relevant to the choice)

* Own C++/QML code, own icon/UI artwork, own docs. No LEWITT binaries, firmware, installers,
  decompiled output or LEWITT artwork are tracked (see AUDIT_PHASE5E.md, "Source and history audit").
* `PROTOCOL.md` and `tests/` contain *facts about an interface* (message ids, field offsets,
  enum values) recovered by static analysis of the vendor's applications. Facts are generally
  not copyrightable; the *expression* in the vendor binaries is, and none of it is copied.
* Dependencies are used as shared libraries: Qt 6 (LGPL-3.0 / GPL / commercial), libusb-1.0
  (LGPL-2.1+), GoogleTest (BSD-3-Clause, tests only), libstdc++ (GPL-3 + runtime exception).
  Nothing is statically linked or bundled, so the LGPL libraries impose no source obligations
  on this project beyond not forbidding replacement of the libraries.

## Options

| | MIT | Apache-2.0 | GPL-3.0-or-later |
|---|---|---|---|
| Type | permissive | permissive + explicit patent grant | strong copyleft |
| Others may ship closed forks | yes | yes | no (derivatives must stay GPL) |
| Explicit patent licence from contributors | no | **yes** (and patent-retaliation clause) | yes (GPLv3 §11) |
| Compatible with Qt (LGPL/GPL) use | yes | yes | yes |
| Compatible with the *other* licence direction (embedding this code in GPL-2-only code) | yes | no | no |
| Can be pulled into the Linux kernel / ALSA tree later (hwdep route, USB_ACCESS option E) | yes (MIT is GPL-2-compatible) | **no** (Apache-2.0 is not GPL-2-compatible) | GPLv3 is **not** GPL-2-only compatible; "GPL-2.0-only" or "GPL-2.0-or-later" would be needed for kernel work |
| Contribution friction | lowest | low (CLA unnecessary; patent grant implicit) | low-moderate |
| Boilerplate | one file | LICENSE + NOTICE, per-file header recommended | LICENSE, per-file header recommended |

## Implications for an independent reverse-engineering project

* **Vendor relationship / clean-room posture.** The licence does not change the legal status
  of the reverse engineering itself (interoperability; jurisdiction-dependent, notably EU
  Software Directive Art. 6). What it does change is what downstream users may do with the
  protocol knowledge in `PROTOCOL.md` and the implementation. A permissive licence maximises
  reuse by other projects (PipeWire/ALSA UCM tooling, other GUIs); copyleft keeps improvements open.
* **Trademarks.** "LEWITT" and "CONNECT 2" are nominatively used. No licence option grants
  trademark rights; keep the "unofficial, not affiliated" labelling whichever is chosen.
* **Contributors.** Under MIT/Apache the default is "inbound = outbound": contributors license
  under the project licence and keep copyright; a CLA is not needed. Under GPL the same holds,
  but relicensing later (e.g. to something kernel-compatible) needs every contributor's
  consent, so decide *before* accepting outside contributions. A DCO (`Signed-off-by`) is
  sufficient for all three.
* **Patents.** Apache-2.0's explicit patent grant is the only option here that gives users a
  contractual patent licence from contributors; relevant if vendors hold patents on the
  control mechanisms (unknown).
* **Distribution of binaries.** Distros accept all three. The Debian `copyright` file and the
  Arch `license=()` field must be updated when a licence is chosen (placeholders marked).
* **Kernel route.** If USB_ACCESS option E (snd-usb-audio quirk + hwdep) is pursued, that code
  must be GPL-2.0-only-compatible; keep kernel-bound code separate or dual-licensed.

## Recommendation (for the owner to accept or reject)

* If the aim is maximum adoption and reuse, with a possible later kernel contribution:
  **MIT** (or **dual MIT OR GPL-2.0-only** for the protocol/transport part).
* If an explicit patent grant matters more than kernel compatibility: **Apache-2.0**.
* If the aim is that all derivatives stay open: **GPL-3.0-or-later** (accepting that the kernel
  route needs separate GPL-2 licensing of any kernel-bound code).

A reasonable default for a protocol-reconstruction project is MIT for `src/protocol` and
`src/transport` and the same for the rest, to keep the knowledge maximally reusable.

## What changes in the repo once a licence is chosen

1. Add `LICENSE` (and `NOTICE` for Apache-2.0); optional SPDX headers.
2. `packaging/debian/copyright`: replace `License: LicenseRef-proprietary` and the Copyright line.
3. `packaging/arch/PKGBUILD`: `license=('MIT')` (etc.) and install the licence file.
4. `packaging/linux/lewitt-control-center.metainfo.xml`: `<project_license>`.
5. `CMakeLists.txt`: install the licence file under `share/licenses/` / the docdir.
