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
* MUTE long-press = panic mute (hardware); hidden showcase mode via icon
  sequence on the hardware (not reproduced).
* Firmware updates via Control Center; after an error power-cycle the device.
