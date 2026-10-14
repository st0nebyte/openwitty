# Feature inventory / parity matrix (CONNECT 2)

Source: official macOS Control Center 5.2.2 (tooltips, symbols, settings map).
"Linux" column: **none** = nothing implemented; **decode** = field located and
decodable, no write path; nothing is hardware-verified yet.

| Feature (original UI) | Mechanism | Settings offset | Linux |
|---|---|---|---|
| Input 1 / Input 2 gain (0–85 dB) | settings write | 0 / 10 | decode |
| Auto Setup / Auto Gain (per input + both) | triggers msg (id 4) + status polling | ? | none |
| Phantom power (input 1; input 2 per tooltips 2211) | settings | 5 (input 2 ?) | decode |
| High-pass filter ("remove rumble") | settings | 3 / 13 | decode |
| Clipguard | settings | 4 / 14 | decode |
| Preamp character Clean / Warm / Vivid | settings | 6 / 15 | decode |
| Denoise | settings (+ noise measurement trigger) | 7 / 16 | decode |
| Compress ("consistent level…") | settings | 9 / 18 | decode |
| Mute per input | settings | 2 / 12 | decode |
| Output mutes / volumes, Dim, Mono, Balance | settings | 19–35 | decode (grouping ?) |
| Direct monitoring (mic / instrument) | settings | 34 | decode |
| Loopback ("send computer output back") | settings (?) | ? | none |
| Output source select | settings | 23 | decode |
| Hardware custom button action | settings | 53 | decode |
| Native volume-control mapping (HW knob → OS volume) | settings | 59 | decode |
| LED brightness / reset LED colors | settings / trigger | 37 / ? | decode / none |
| Level meters | status msg (id 2) | ? | none |
| Firmware / API version | version msg (id 3) | ? | none |
| Sample-rate / buffer size | driver (Thesycon on Win/mac) | n/a | **ALSA/PipeWire**, not device protocol |
| Firmware update (DFU) | separate DFU interface | n/a | out of scope until explicitly approved |
| Presets (`.preset` JSON, in `Presets/`) | host-side files | n/a | none (format is documented by the shipped samples) |
| Tooltips / translations (en, zh) | host-side | n/a | none |

Note: the shipped macOS payload contains CONNECT 6 code paths too; this
inventory only lists what `buildConnect2Tree` / `Connect2::*` reference.
