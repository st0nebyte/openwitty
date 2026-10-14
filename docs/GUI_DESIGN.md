# GUI design notes (Qt Quick reconstruction)

Reference: official guide screenshots (research/guide/img, not redistributed)
and the macOS/Windows app strings. All components are independent QML; no
proprietary artwork or logo is used.

| Original element | QML component | Notes |
|---|---|---|
| Window ~720×370, dark | `Main.qml` design space 760×430, uniformly scaled | proportions kept at any size / HiDPI |
| Header with sample-rate and gear | `Main.qml` header | sample rate is audio-stack state; selectable only in simulation |
| Input strip (meter with dB scale, LC/48V/CG, knob, preamp trio, Denoise/Comp, MUTE) | `InputStrip.qml` | mic: 0–72 dB + 48 V; instrument: 0–60 dB |
| Rotary knob with tick ring | `GainKnob.qml` | drag/wheel; canvas repaints only on change |
| Stereo meters + fader + MUTE | `OutputStrip.qml`, `LevelMeter.qml`, `Fader.qml` | loopback: meter+mute only |
| Monitoring column | `MonitorSection.qml` | mix knob, Mic/Inst, Dim, Mono |
| Autosetup wizard | `AutosetupWizard.qml` | step order from the app's strings; not protocol-verified |
| Settings page | `SettingsView.qml` | unverified items disabled with reasons; firmware/factory reset disabled |
| "No LEWITT device connected!" | overlay in `Main.qml` | |

Rules: controls are interactive only when `device.writable`; simulation is
announced in a banner; restricted features show a tooltip (96 kHz); 48 V and
high-volume mode require confirmation. Meters update from a 25 Hz status
poll; the controller publishes only changed keys.
