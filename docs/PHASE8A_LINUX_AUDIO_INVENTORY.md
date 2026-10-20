# Phase 8A — CONNECT 2 Linux audio inventory (read-only)

Observed on the CachyOS development host, 2026-10-20. This is one device and
one active PipeWire profile, not a claim about every firmware, profile, or host.
No mixer, profile, default-device, route, stream, or USB setting was changed.
Commands below are inventory queries; committed results omit the device serial
and other unique identifiers. Numeric PipeWire/ALSA IDs are ephemeral.

## Evidence

| Query | CONNECT 2 observation |
|---|---|
| `lsusb -d 29c2:0004` | USB VID/PID `29c2:0004` present. |
| `aplay -l` | One ALSA playback PCM: card `C2`, device 0, `USB Audio`; two playback channels (`Playback Channel Map`: FL/FR). |
| `wpctl status`; `pactl list short sinks`; `pactl info` | One active CONNECT 2 PipeWire playback sink, `analog-stereo`, and one 4-channel capture source. CONNECT 2 is KDE/PipeWire's configured and current default sink. The sink was suspended at the instant of inspection, not absent. |
| `pactl list cards` | Active profile: analog stereo output + 4-channel analog input. Also advertised: analog-output-only, IEC958-stereo output (with/without input), input-only, Pro Audio, and off. Profile names do not prove a separate physical digital output. |
| `pactl list cards`; `pactl list sinks`; `wpctl inspect 61 --associated` | Active playback route/port is one generic `analog-output`. An `iec958-stereo-output` port belongs to alternative profiles. No distinct Speaker and Headphone playback sinks or routes are exposed. The active stereo node has FL/FR playback ports plus monitor ports; these are audio channels/monitor taps, **not** speaker/headphone destinations. |
| `amixer -c 0 scontrols`; `amixer -c 0 contents`; `amixer -c 0 sget PCM` | ALSA exposes a joined `PCM Playback Switch` and mic capture switch, but no playback-volume control. The PCM switch was on. Its effect on the CONNECT 2's separate output mutes has **not** been tested. |
| `pw-cli enum-params 54 Route`; `pw-cli enum-params 61 Props` | Active `analog-output` route advertises `route.hw-volume=false`, `route.hw-mute=true`. Sink/route `softVolumes` equal `channelVolumes` at about 0.0911 linear for both channels; sink `volume` parameter is 1.0. The `route.hw-mute` flag may refer to ALSA's joined PCM switch, **not** either hardware output mute. |
| `pactl list sinks`; `wpctl status` | PipeWire/PulseAudio-compat sink was unmuted at 45% (`−20.81 dB`) when observed. The sink advertises `HARDWARE HW_MUTE_CTRL DECIBEL_VOLUME` flags, but these do not override the more specific absent ALSA volume control, `route.hw-volume=false`, and active soft-volume values. |
| `build-release/openwitty-cli read settings --interface auto` | One safe vendor-interface USB GET succeeded: hardware Speakers 0.00 dB, Headphones −27.26 dB and muted. These do **not** match the one unmuted PipeWire sink at 45%. Output-volume/mute fields are separate CONNECT 2 settings; no SET was sent. The unqualified CLI read tried its older AudioControl default and returned `LIBUSB_ERROR_IO`; the explicit automatic vendor selection succeeded. |
| `wpctl settings device.restore-routes`; `wpctl settings node.restore-default-targets`; `wpctl settings node.stream.restore-props` | All three WirePlumber restore settings currently report `true`. These may reapply software sink/stream levels independently of CONNECT 2 state; their exact effect on this device has not been experimentally changed. |
| `pgrep -af '[o]penwitty'`; `lslocks -o PID,PATH` | No running OpenWitty control client or holder of `openwitty-connect2-control.lock` was found at preflight. |

The read-only inventory supports **one stable Linux stereo playback destination**
without a proxy sink. It does **not** establish that changing the front-panel
selected-channel byte 56 routes playback; that byte is touch-control focus.
Independent simultaneous Speaker and Headphone meter activity was observed in
Phase 7G (see `HARDWARE_TEST_RESULTS.md`), also inconsistent with treating the
two outputs as mutually exclusive Linux ports. Whether a given audio sample
reaches either physical output depends on device-internal routing/mute/mix;
this inventory did not measure the analog signal path.

```text
KDE / applications
  -> PipeWire-Pulse or native PipeWire streams
  -> one CONNECT 2 analog-stereo sink (FL/FR, current soft volume 45%)
  -> one ALSA USB Audio playback PCM (snd-usb-audio)
  -> CONNECT 2 internal playback / monitor path
       -> Speakers: independent hardware volume + mute
       -> Headphones: independent hardware volume + mute

OpenWitty read-only control GETs -> separate vendor interface 3
                              -> observed Speaker/Headphone settings
```

The diagram's **last split is inferred** from the one stereo USB PCM plus
independent, hardware-verified output controls, not from an inspected circuit.
It does not claim that the two paths always carry the same mix.

## Component ownership and reconnection limits

ALSA `snd-usb-audio` owns the streaming PCM and exposes a joined playback
switch, not the device's two volume sliders. PipeWire owns the active audio
graph and currently applies a soft sink volume. WirePlumber supplies ALSA
profile/route selection, stream linking and default-node/volume restoration
policy ([PipeWire configuration](https://pipewire.pages.freedesktop.org/pipewire/page_config.html),
[WirePlumber settings](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html)).
KDE controls the PipeWire/PulseAudio-compatible sink UI, not the vendor
settings directly.

No new unplug/replug test was performed in Phase 8A. The owner-approved Phase 7C
test (`HARDWARE_TEST_RESULTS.md` §18.3) observed USB/ALSA removal and their
return, a returning PipeWire sink/source, and OpenWitty read-only control
reconnection. That test did **not** measure KDE volume restoration or audible
playback after reconnection. The present PipeWire IDs and ALSA card number must
not be persisted as device identity. WirePlumber currently has default-node,
route, and stream restoration enabled; Phase 8B must characterize what those
policies actually do on reconnect before any volume bridge is activated.

## Unresolved inventory questions

* Whether the joined ALSA `PCM Playback Switch` mutes one output, both outputs,
  or only the USB playback path. Do not toggle it as part of this inventory.
* Whether IEC958/Pro Audio profiles change physical routing, node count, or
  software attenuation. Do not switch profiles without a separate test plan.
* Whether the device has an undocumented ALSA-accessible hardware-volume
  control; none appeared in the current ALSA control list.
* Precise relationships among physical Custom Button mode, hardware output
  mutes, and routing; selected-channel focus is not a routing indicator.
* Actual KDE volume-key event flow, WirePlumber restore timing, and PipeWire
  node-volume behavior during disconnect/reconnect. Read-only event observation
  and mock testing should precede any intervention.
