# Official CONNECT 2 online guide — summary

Source: https://www.lewitt-audio.com/support/online-guides/connect2 (fetched
2026-10-14; page text and screenshots stored under the git-ignored
`research/guide/`). This file records *documented product behaviour*; it is
not protocol evidence. Rows marked **(P)** are also supported by binary analysis.

## Hardware
* Inputs: mic (1) and instrument (2); outputs: loudspeaker, headphone.
  Touch wheel for gain/volume of the selected channel, LED metering, display,
  custom button, input/playback mix, autogain, MUTE. 27 W USB bus power needed.
* Device is UAC2; Windows needs the LEWITT ASIO driver (Thesycon), macOS does
  not. Linux class driver works for audio (not for the control protocol, see
  `USB_ACCESS.md`).

## Control Center layout (matches the screenshot used for our QML)
Header: logo / "CONTROL CENTER | CONNECT 2", sample-rate selector, settings gear.
Left → right: **Inputs** (Autosetup top-left; per input: meter, LC, 48 V (mic),
Clipguard, gain knob + autogain, Clean/Warm/Vivid, Denoise, Comp, MUTE),
**Outputs** (Loopback, Loudspeaker, Headphone: meter(s) + fader + MUTE; loopback
has no fader (P)), **Monitoring** (input/playback knob 100:0 … 0:100, direct
input signal Mic/Inst, computer audio, Dim, Mono). Mic knob 0–72 dB,
instrument 0–60 dB.
Settings page: Audio, LEDs, Custom button (collapsibles); control-center and
firmware versions, check/manual update, auto-update, usage data, tooltips,
onboarding, factory reset.

## Behaviour
* Speaker and headphone volume/mute are independent (P: separate groups).
* Direct monitoring: Input/playback blends direct vs computer audio; mic, inst
  or both selectable (P: byte 34 state machine); **speaker monitoring is off by
  default**, enabled by "Input/playback mix applies to speakers" (P: MixToSpeakers).
* Loopback: computer output returned as inputs 3/4 by default; do not monitor
  the loopback (feedback).
* Mobile streaming mode: mic+instrument mixed on inputs 1/2. macOS streaming
  mode: loopback on 1/2, mic/inst on 3/4, **device reboots** with settings
  preserved. Treated as disruptive; not enabled.
* Denoise & Comp **unavailable at 96 kHz** (implemented as documented
  restriction); Denoise starts a noise-floor measurement and must be repeated
  when environment/mic changes.
* Autosetup: configures 48 V, low-cut, preamp character, Denoise, Comp, Clipguard.
  Autogain: timed reference performance, green blink success / red failure
  on hardware; the app shows progress and "repeat" messages for noise,
  clipping or no response.
* Clipguard: protects against clipping; disable for max dynamic range.
* Custom button functions: show Control Center; toggle mic preamp styles;
  toggle instrument preamp styles; toggle output mutes (headphone↔speakers);
  same + mic; dim; mono/stereo; toggle brightness settings.
* Gain lock ("External gain lock") stops third-party apps (e.g. Teams) from
  changing gain (P: ExternalGainControl).
* **MUTE button ("Panic Mute"), verified 2026-10-16 against the official online guide**
  (https://www.lewitt-audio.com/de/support/online-guides/connect2, German original quoted):
  - Short press: "Drücke die MUTE Taste auf deinem CONNECT 2, um jeden ausgewählten Kanal
    stummzuschalten oder die Stummschaltung aufzuheben" — toggles mute for the *currently selected*
    channel only (matches our Phase 6B hardware findings for mic/instrument/speaker/headphone mute).
  - Long press/hold: "Halte die MUTE-Taste auf deinem Hardware-UI gedrückt, um alle Input und Output
    Kanäle zu muten" — mutes **all input and output channels simultaneously** ("Panic Mute").
  - Recovery: "Um den MUTE-Status zu deaktivieren, müsst ihr die Stummschaltung jedes Kanals einzeln
    aufheben" — **each channel must be unmuted individually**; the guide does not describe holding
    MUTE again as a way to undo it, so that should not be assumed.
  - LED: a muted channel, while a *different* channel is selected, "bleibt in der MUTE-Farbe (ab
    Werk auf Rot eingestellt) schwach beleuchtet" — stays dimly lit in the configured mute colour
    (factory red).
  - This is manufacturer-documented behaviour, not yet confirmed against our physical unit — see
    `docs/HARDWARE_TEST_RESULTS.md` for the hardware-verification status once tested.
* Hidden showcase mode via icon sequence on the hardware (not reproduced) — separate from the above,
  still unverified.
* Firmware updates via Control Center; after an error power-cycle the device.

## Additional facts from reviews and software captures (2026-10)
Sources: bonedo.de and delamar.de reviews, product page (see `VISUAL_REFERENCES.md`).
* Window is resizable; the UI scales. The lower **status bar shows the hint of the
  control under the pointer** ("Mute the signal"), controlled by *Show tooltips*.
* Autosetup chooser: Microphone (guided walkthrough, 45 s, +48 V step), Instrument
  (guided, 45 s), Mic + instrument (quick gain setup, 10 s).
* Onboarding: four steps (Inputs, Loopback, Outputs, Monitoring) with Back/Next/Skip
  and progress dots; re-triggerable in Settings.
* Settings (5.2.1): versions, Check for updates / Manual update, switches for
  automatic updates, usage data, tooltips, onboarding, links (register, support,
  file a bug), Factory reset.
* Active MUTE is a solid red button; Dim/Mono/Clean etc. use a lime outline.
* The CONNECT 2 has two headphone sockets that share **one** headphone control
  path (one Headphone strip in software) — confirmed by the binary (single
  `Headphone` group; `Headphone1/2` identifiers belong to CONNECT 6).
* Loopback strip: meters + MUTE only, no fader — consistent with the binary
  (`Loopback` group has `LeftLevel/RightLevel/Mute`, no `Volume`).
* Reviews describe 24-segment hardware LED metering and a capacitive touch surface
  (hardware, not Control Center).
