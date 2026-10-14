# Architecture (planned)

protocol.py (pure, tested) → transport.py (USB, read-only) → [device state / sync] → [GUI: Qt6/QML].
Hardware-level controls go through the device protocol; sample rate/buffer/routing stay in ALSA/PipeWire.
