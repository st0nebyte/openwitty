# Phase 10D — passive Linux audio and DAW-readiness inventory (CONNECT 2)

2026-10-26, one system: CachyOS, Linux 7.3.0, KDE Plasma 6 (Wayland), PipeWire 1.6, WirePlumber 0.5; device
`29c2:0004` (normal mode), device release `bcdDevice 0x0121`, high speed, with the example `ignore_ctl_error` quirk active.

**Passive only.** Sources: `/proc/asound/cardN/{stream0,pcm0p,pcm0c,usbmixer}`, the kernel's cached USB descriptors
(`/sys/bus/usb/devices/<dev>/descriptors`), PipeWire's registry (`pw-dump`, `pw-link -l`, `pw-metadata`, `pactl`/`wpctl`
listings), local ALSA/ACP configuration files and the kernel source. No audio stream was opened, no ALSA mixer was
opened, no profile/mute/volume/rate/routing was changed, no vendor GET/SET and no other USB request was made, no
service was touched. `usbmixer` was read only after checking in the kernel source (`snd_usb_mixer_proc_read`,
`snd_usb_mixer_dump_cval`) that it prints stored values without a USB transfer. Evidence class of everything here:
**A-sys** (no A-live, no A-daw). The ALSA card was identified through sysfs by USB identity (`29c2:0004`), not by
index.

## 1. Streams and rates

| Item | Observation |
|---|---|
| Interfaces | 0 AudioControl (UAC2, ADC 2.0, category 0x08, interrupt EP 0x83), 1 playback streaming, 2 capture streaming, 3 vendor (`ff/f0`, OpenWitty's control interface), 4 DFU (`fe/01`) |
| Playback | interface 1 alt 1: 2 channels, 24-bit in 4-byte slots (S32_LE), isochronous **asynchronous**, EP 0x01; kernel: implicit feedback from capture EP 0x82 |
| Capture | interface 2 alt 1: **4 channels**, 24-bit in 4-byte slots (S32_LE), isochronous asynchronous, EP 0x82 (also the implicit-feedback source) |
| Advertised rates | 44 100, 48 000, 96 000 Hz (both directions; one internal programmable clock source, id 41, behind a one-input clock selector, id 17) |
| Kernel stream state | playback and capture `Status: Stop`; `pcm0p`/`pcm0c` `hw_params: closed`, `subdevices_avail: 1` → **no stream open** |
| Device's current rate | **Unknown** (no open stream; none was started) |
| PipeWire graph | `clock.rate = 48000`, **`clock.allowed-rates = [ 48000 ]`**, quantum 1024 (min 32, max 2048), no forced rate/quantum |

Consequence: with this PipeWire configuration every PipeWire client runs the device at 48 kHz; a 44.1/96 kHz client
is resampled by PipeWire. 44.1/96 kHz on the device need `clock.allowed-rates` to include them (a user/PipeWire
setting OpenWitty must not change silently) or a direct ALSA `hw:` client while PipeWire has the device suspended.
Because playback uses capture-side implicit feedback, both directions always run at one rate.

## 2. Channel semantics: the device declares no surround positions

Descriptor facts: the capture input terminal (id 4) has terminal type **Microphone (0x0201)**, **4 channels**,
`bmChannelConfig = 0` (no spatial positions) and a channel-name string index (8; the string itself is not cached
in sysfs and was not requested). The AS_GENERAL descriptor of interface 2 also says 4 channels, configuration 0.
Playback: USB-streaming input terminal, 2 channels, configuration 0, to an output terminal of type **Headphones**
(0x0302) through Feature Unit 2.

| Layer | Labels for capture ch 1–4 | Nature |
|---|---|---|
| Official | Microphone, Instrument, Loopback L, Loopback R | intended semantics |
| Device descriptor | none (configuration 0) | — |
| Kernel (`stream0` channel map) | FL FR FC LFE | the kernel's default map for an unlabelled 4-channel stream |
| PipeWire (active profile) | FL FR RL RR ("Analog Surround 4.0") | the ACP mapping `analog-surround-40` (`channel-map = front-left,front-right,rear-left,rear-right`) |

So "surround" is a Linux default applied to an unlabelled stream, **not** the hardware's semantics. The ALSA
`surround40` PCM for USB-Audio cards is a plain 4-channel device (no remix plugin in `USB-Audio.conf`); the labels
alone do not remix. Whether a particular client downmixes the 4.0 source to mono/stereo and how is **unverified**
(needs LB-5 with signal).

## 3. PipeWire profiles, nodes, routes, links

| Item | Observation |
|---|---|
| Active profile | `output:analog-stereo+input:analog-surround-40` (priority 6512) |
| Sink | "CONNECT 2 Analog Stereo", `front:N`, 2 ch FL/FR, S32LE, 44.1–96 kHz, suspended; default sink |
| Source | "CONNECT 2 Analog Surround 4.0", `surround40:N`, 4 ch FL/FR/RL/RR, S32LE, 44.1–96 kHz, suspended; **default source** |
| Input route | `analog-input-mic` (port type mic), **`route.hw-mute=true`, `route.hw-volume=true`**, **mute = true**, channel volumes 1.0 ×4, `volumeBase` 0.000251 (−72 dB) |
| Output route | `analog-output`, `route.hw-mute=true`, `route.hw-volume=true` (with the quirk), channel volumes 0.0668, unmuted |
| Links | none involving the CONNECT 2 nodes (no client connected) |
| Alternative profiles (declared, not activated) | `off`; `output:analog-stereo`; `output:iec958-stereo(+input:analog-surround-40)`; `input:analog-surround-40`; `pro-audio` |

Unverified (inactive, never switched): what `pro-audio` exposes (PipeWire's pro-audio profile normally creates
nodes with `AUX0…AUXn` positions on the raw device — not observed here); the IEC958 profiles — the descriptors
show no S/PDIF terminal (only a Headphones output terminal), so they are most likely ACP's generic probing of the
same PCM, not a digital output.

**Source mute state:** muted, at the route (hardware) level; the node-level software mute is off.

## 4. Standard USB gain control (Feature Unit 5)

| Evidence | Finding |
|---|---|
| Descriptor | FU 5, source = the Microphone input terminal; master: no controls; ch 1, ch 2: mute + volume host-programmable (`0x0f`); ch 3, ch 4: volume programmable, mute read-only (`0x0e`) |
| Kernel (cached `usbmixer`) | `Mic Capture Volume`: 4 channels, raw 0…18432 (1/256 dB) = **0…+72 dB**; `Mic Capture Switch`: channels 1–2 only; FU 2 `PCM Playback Volume` −60…0 dB and `PCM Playback Switch`; clock validity control |
| Earlier traces (Phase 8A.19 ff.) | at probe the kernel's SETs to FU 5 channels 1–2 succeed; **channels 3–4 STALL** (10 tries each); FU 5 GET_CUR returns a constant "sticky" 6912 = **27.00 dB** |
| ACP path in use | `analog-input-mic.conf` binds the route's mute and volume to the ALSA element **"Mic"** = `Mic Capture Switch/Volume` = FU 5 (`switch = mute`, `volume = merge`) |
| Vendor reads (earlier sessions) | mic gain 27.00 dB (first read), 28.00 dB after the Phase 6B +1 dB test; mic and instrument muted in every Phase 8 snapshot |

What this establishes: the PipeWire/KDE input mute and volume of the default CONNECT 2 source are wired, by
configuration, to UAC2 Feature Unit 5 (hardware mute/volume, not software). FU 5's range (0…72 dB) equals the
official microphone gain range, and its two-channel mute matches the two physical inputs.

What it does **not** establish (unverified): that an FU 5 SET changes the actual mic/instrument preamp gain or the
device's own mute (byte 2/12); how FU 5 channel 2 maps to the instrument's 0…60 dB range; whether WirePlumber writes
FU 5 on login/restore (the route reports volume 1.0 = +72 dB while recorded vendor gains were 27/28 dB — either no
write reached the preamp, or FU 5 is not the preamp, or the states diverged); what External Gain Lock (byte 57)
blocks; whether apps with automatic gain control (browsers/WebRTC, conferencing tools) move it. **No claim is made
that a desktop input slider changes the physical preamp.**

Risk to keep in mind until GAIN-1: unmuting or moving the default input in the desktop could, if FU 5 is the
preamp, change the microphone gain (up to +72 dB) and mute — a feedback/level risk with monitoring on.

## 5. DAW readiness

| Layer | Known now (A-sys) | Needs a test |
|---|---|---|
| Device (visible to Linux) | 2 out / 4 in, 24-bit, 44.1/48/96 kHz, async, implicit feedback, FU 2/FU 5 | channel identity, levels, latency |
| Active profile | 2-ch sink, 4-ch "4.0" source; graph 48 kHz only | client downmix behaviour; mute/volume side effects (GAIN-1) |
| PipeWire-JACK | would see the sink/source ports by their PipeWire names/positions | port names, 4 separate capture ports, latency (DAW-1, DAW-7) |
| Native ALSA (`hw:`) | 4-ch S32_LE capture, 2-ch playback, rates per descriptor | exclusive-access interplay with PipeWire, rates other than 48 kHz |
| Audacity (PortAudio) | ALSA/Pulse/JACK hosts possible | which host offers 4 channels; mono/stereo recording semantics |
| REAPER (ALSA/JACK/PipeWire) | — | 4-input enumeration, track assignment |
| Ardour (JACK/PipeWire-JACK, ALSA) | — | ports, latency calibration |

Everything that depends on audio passing (identity, merging, latency, xruns, long runs, recovery) needs LB-/DAW-
steps. The four capture channels stay a four-channel interface in every proposal; no automatic stereo collapse.

## 6. Open questions

1. Does the device's channel-name string (descriptor index 8) name the channels? (would need a string-descriptor
   request — not passive).
2. Pro Audio profile: positions/port names (would need a profile switch — not done).
3. FU 5 ↔ preamp gain/mute ↔ External Gain Lock ↔ desktop/WebRTC AGC (GAIN-1).
4. Downmix of the 4.0 source by mono/stereo clients (LB-5).
5. Hardware loopback vs PipeWire sink monitor (LB-6).
6. Behaviour at 44.1/96 kHz (needs `allowed-rates` or direct ALSA; SR-2).
