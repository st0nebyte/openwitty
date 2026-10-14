# GUI design notes (Qt Quick reconstruction)

Targets the **5.x** look of the official Control Center. Reference list and
caveats: `VISUAL_REFERENCES.md`. All artwork is independently drawn.

## Coordinate system
Design space **720 × 373** (the official window's proportions, measured on the
guide screenshot and the 5.x capture), scaled uniformly to the window
(`Main.qml`), HiDPI-safe. A 28 px simulation banner sits *outside* the stage.

| Band | y (design px) |
|---|---|
| header (title, sample rate, gear) | 0–50 |
| section bar (Autosetup, Inputs / Outputs / Monitoring) | 50–78 |
| strips | 78–345 |
| footer status bar (control hints) | 345–373 |

| Column | x |
|---|---|
| Inputs (2 strips of 152) | 0–310 (divider at 155) |
| Loopback / Loudspeaker / Headphone (3 × 85) | 310–565 (dividers 395, 480) |
| Monitoring | 565–720 |

## Colour measurements (5.x capture, pixel samples)
header/outputs/monitoring/footer `#1e1d1e`–`#1f1d1e`; inputs panel `#3e3e3e`;
meter well `#323031`; meter green `#83b12a`; meter amber `#f8aa37` (from
−12 dB); accent/outline lime `#8fc21d`; primary button fill `#98bb0c`;
fader cap white, rail `#8f8d8d`; active MUTE red.

## Component mapping
| Original element | QML | Notes |
|---|---|---|
| Strip: icon, LC / 48V / Clipguard, knob + autogain icon, Clean/Warm/Vivid, Denoise/Comp, MUTE | `InputStrip.qml` | mic 0–72 dB (+48 V), instrument 0–60 dB; positions from zoomed crops |
| Rotary knob: black outline, tick ring, white pointer, bold value, orange arc near max | `GainKnob.qml` | arc start threshold (90 %) is an approximation |
| Stereo meter with −∞…0 scale, green / amber zones | `LevelMeter.qml` | amber from −12 dB (observed) |
| Slim fader with white cap | `Fader.qml` | |
| Loopback (meters + MUTE only), speaker/headphone (+fader) | `OutputStrip.qml` | no loopback volume (matches binary) |
| Input/playback knob (100:0 … 0:100, 300° sweep), Mic/Inst direct monitoring, Dim, Mono | `MonitorSection.qml` | |
| State buttons: grey / lime outline + dark fill / red solid (MUTE) | `ToggleButton.qml` | |
| Footer hint text instead of floating tooltips | `Hint.qml` singleton + footer | "Show tooltips" preference |
| Autosetup chooser (3 columns, durations, lime buttons) + guided steps | `AutosetupWizard.qml` | overlay, not a separate window |
| Onboarding tour (dim, lime frame, Back/Next/Skip, 4 dots) | `OnboardingOverlay.qml` | on demand (Settings ▸ Retrigger onboarding); first-run flag not persisted yet |
| Settings: collapsible Audio/LEDs/Custom button, versions, lime switches, links | `SettingsView.qml` | overlay; separate-window behaviour unknown |
| Pictograms: mic, guitar, loop, speakers, headphones, shuffle, low-cut, clipguard, autogain, laptop | `GlyphIcon.qml` | own drawings |

## Behaviour rules
* Controls are interactive only when `device.writable`; simulation announced in
  a banner; restricted features show a hint (96 kHz).
* 48 V, high-volume speaker mode and device-mode changes need confirmation.
* Unsent edits are shown on top of device state, flagged (dot + footer
  "Change not sent — Retry / Discard"), never silently dropped; device-side
  changes are merged with the original's device-wins algorithm.
* Identical values send no command. Settings are re-read periodically.

## Visual regression
* `tools/visual/compare.py` (needs local references): region colours, divider
  positions, side-by-side/diff image.
* `tools/visual/check_geometry.py` (CTest `visual_geometry`, no third-party
  assets): dividers within ±4 px, region colours within ±8/channel.
* Result vs. the 5.x capture (design space): dividers ref 153/307/393/480/566
  vs ours 154/309/394/479/564; mean absolute pixel difference ≈ 14/255 (text,
  icon and anti-aliasing dominate). **Not pixel-perfect.**

## Known differences
* Wordmark/logo, fonts (system sans vs. original), exact icon artwork, knob
  shading, native title bar.
* Onboarding step texts are paraphrased.
* Sample-rate list limited to 44.1 / 48 / 96 kHz (documented rates).
