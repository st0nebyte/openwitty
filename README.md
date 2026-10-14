# LEWITT CONNECT 2 for Linux (work in progress)

Goal: native Linux replacement for LEWITT Control Center, built from
interoperability analysis of the official software. Early stage: protocol
discovery. **Nothing is hardware-verified yet.**

* `PROTOCOL.md` – what is known, with evidence level for each item
* `FEATURES.md` – feature inventory / parity matrix
* `TODO.md` – next steps, incl. what needs hardware or user action
* `src/lewitt_connect2/` – protocol constants, settings decoder, read-only transport + CLI
* `tools/re/` – the analysis scripts used (capstone + LIEF)
* `research/` – proprietary installers/extractions; git-ignored, never redistributed

```
python3 -m venv .venv && .venv/bin/pip install pyusb
PYTHONPATH=src .venv/bin/python -m lewitt_connect2.cli list      # no control traffic
python3 -m unittest discover -s tests
```
`read` is gated behind `--allow-detach` because reading currently requires
detaching `snd-usb-audio` (see PROTOCOL.md). There is no write command.
