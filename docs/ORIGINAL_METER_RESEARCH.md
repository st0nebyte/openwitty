# Original LEWITT CONTROL CENTER meter pipeline — static research

Status: static-analysis findings plus a separately reported native-Windows bus
capture, 2026-10-18. The static analysis below did not itself measure live USB
traffic or screen cadence. No proprietary capture or binary is tracked here.

## Material and limits

The locally available Windows installer `controlcenterinstallerwin_5_2_2_1.exe`
was extracted into a temporary directory outside this repository. The examined
`LEWITT CONTROL CENTER.exe` identifies itself as 5.2.2; its SHA-256 is
`aed77fbd238fa71c64c962f12990e01c494bd76069c41181caf6cdf00575cf4a`.
The installer filename ends in `_1`; that suffix is **not** independently
verified as the executable's build number. Addresses below are PE virtual
addresses with image base `0x140000000`. The executable is stripped, so C++
class attribution uses RTTI/vtable relationships and is an inference unless a
symbol is explicitly present. Key conclusions were checked against machine
instructions, not accepted solely from a decompiler.

The repository's earlier `PROTOCOL.md` records analysis of the official macOS
5.2.2 x86_64 binary, but that binary/Ghidra project was **not present locally in
this session**. Those notes provide cross-platform context, not an independently
repeated macOS analysis. No proprietary binary, extraction, disassembly, or raw
capture is tracked here.

## Evidence

| Evidence | Exact location | What it supports | Confidence |
|---|---|---|---|
| `Connect2Device` RTTI and vtable identify the device methods | Windows 5.2.2, RTTI name VA `0x140a59e60`, vtable `0x1408f5558`; constructor fragment `0x140200c60` installs that vtable and the status-message vtable at object `+0x268` | Addresses below belong to the CONNECT 2 device object and its status message | High for object relationship; inferred class names from RTTI |
| Device update calls virtual slot `+0x20` every invocation; calls `+0x18` only when its byte argument is true | Windows, function `0x140200cd0`, calls at `0x140200d7d` and `0x140200d8b`; byte argument tested at `0x140200d80` | Status read is part of each device-update invocation; settings read can be conditional. It does **not** establish how often the update is invoked | High |
| Slot `+0x20` copies the previous 64-byte status snapshot, then enters generic read with the status message object | Windows, `0x140201010`–`0x140201053`, tail jump to `0x14020a300`, object `+0x268` | A device update requests a fresh status message through the common read path; previous status is retained for change detection | High |
| Slot `+0x18` uses the separate settings message object | Windows, `0x140201060`–`0x1402010a9`, object `+0xc8` | Status and settings are separate reads in the same update path | High |
| Eight leaf getters each load a signed 16-bit value from the status buffer and multiply by `0.00390625` (`1/256`) | Windows, input 1 `0x1401ffca0` (object `+0x270`), input 2 `0x1401ffc80` (`+0x279`), loopback L/R `0x1401ffc60`/`0x1401ffc40` (`+0x282`/`+0x284`), speaker L/R `0x1401ffc20`/`0x1401ffc00` (`+0x286`/`+0x288`), headphone L/R `0x1401ffbe0`/`0x1401ffbc0` (`+0x28a`/`+0x28c`); multiplier VA `0x14093ef30` | The eight CONNECT 2 levels are decoded from one status snapshot as signed Q8.8 values. These getters contain no temporal smoothing or audio-sample acquisition | High |
| A UI/property callback also reads status `+0x270` directly and applies the same multiplier | Windows, `0x14029e790`–`0x14029e7ab` | At least one presentation-facing value path consumes the decoded device status rather than a distinct PCM sample in this callback. Its caller and draw cadence remain unresolved | Medium |
| Windows executable resolves Thesycon control API names, including `TUSBAUDIO_AudioControlRequestGet` | Windows string VA `0x140547e08`; see also `docs/REVERSE_ENGINEERING_TOOLS.md` | Windows has an AudioControl-request GET path. This alone does not prove every status read uses that exact API call without a full call-chain trace | Medium |
| Existing macOS analysis identifies message id 2 as 64-byte `iface_status_t` and the USB MEM GET shape | `PROTOCOL.md` Transport, Messages, and `iface_status_t` sections; macOS 5.2.2 notes | Likely wire request: class/interface IN, UAC2 request `0x03` (UAC1 `0x85`), `wValue=0x0102`, `wIndex=0x3300 | interface`, length 64, timeout 500 ms. This is earlier documented macOS evidence, not a new Windows bus trace | High for prior macOS disassembly; medium for this Windows path |

The status buffer starts at Windows object `+0x270`, so the eight getter
displacements correspond to status byte offsets `0x00`, `0x09`, `0x12`, `0x14`,
`0x16`, `0x18`, `0x1a`, and `0x1c`. This matches `PROTOCOL.md` and OpenWitty's
`StatusMessage`. The Q8.8 step is an encoding step, **not** evidence of the
device's physical level accuracy, RMS/peak semantics, or internal update rate.

## Timing and rendering

**No CONNECT 2 meter acquisition interval was established.** The update
function does not contain a sleep or fixed poll-period constant. A Windows
`timeSetEvent` import and wrapper (`0x140065ba0`, called from `0x1400676a0`)
exist, but the wrapper accepts its period from a caller and was not connected
to the CONNECT 2 status-read call chain. Reporting that timer as a meter rate
would be unjustified. The status read may be scheduled faster than OpenWitty's
4 Hz, but the static evidence here does not prove it.

The binary contains JUCE 8.0.10 version strings and UI `Meter` RTTI, but this
investigation did not resolve the relevant meter paint/update call chain.
The level getters themselves perform only Q8.8 conversion. They provide no
evidence for or against a separate GUI animation layer, attack/release,
interpolation, or peak hold elsewhere. Compressor/expander “attack” and
“release” strings refer to audio-processing controls and must not be used as
evidence of GUI meter dynamics. No meter-specific USB request, asynchronous
transfer, or OS audio-capture callback was identified **in the examined
CONNECT 2 path**; absence of an identified path is not proof that none exists.
The Windows imports include `WINMM.dll` but do not by themselves identify a
capture stream. No capture-based conclusion can be drawn from an import list.

## Comparison with OpenWitty

OpenWitty currently reads the same eight status fields at 4 Hz (one serialized
status GET every 250 ms) and animates presentation independently at the display
frame cadence. `src/gui/MeterDynamics.hpp` applies a 20 ms attack, 450 ms
release, 750 ms hold, and 24 dB/s peak-marker decay; these are OpenWitty
presentation choices, **not** copied or verified LEWITT behavior. See
`docs/PHASE7G_AUDIO_METER_PLAN.md` for the implemented pipeline.

This static comparison establishes a shared status-message **source**, but
cannot rank the remaining bottlenecks. A low status GET rate could limit
temporal fidelity; firmware might refresh status more slowly; the official GUI
may smooth or render more effectively; or another source could exist. None is
confirmed. Sixty frames per second cannot restore an unobserved transient from
four status samples per second, and a higher GET rate should not be inferred
from the original application's apparent smoothness alone.

## Open questions and earlier experiment proposal (before the native capture)

1. What schedules `Connect2Device` update at `0x140200cd0` on each platform,
   with what interval and under what visibility/device-state conditions?
2. Does the generic read path issue exactly one physical GET per invocation,
   or retry? Does another CONNECT 2 path acquire meter data?
3. What is the firmware's status-field refresh rate and whether values are
   instantaneous, peak, or averaged over a window?
4. Which official UI method paints the eight levels, and does it maintain
   independent envelopes or peak markers?
5. Does the Windows application ever capture OS PCM specifically for CONNECT 2
   metering? The examined getter path does not, but the full application has
   not been ruled out.

The then-proposed controlled hardware experiment, **only after separate owner approval**,
is a single-owner, read-only comparison of 4 Hz and 8 Hz status GETs using
OpenWitty's existing request (`wValue=0x0102`), with 16 Hz considered only if
8 Hz is safe and informative. Timestamp GET start/end and hash or compare only
the eight sanitized level fields; report request count, errors, GET-duration
distribution, new-value fraction, CPU, audio XRUNs/coexistence, and visible
responsiveness. Keep settings polling and rendering unchanged. Stop on USB
errors, audio disruption, or no meaningful new samples. Do not run the official
application concurrently or probe unknown requests. A later passive trace of
the official application on its supported OS could answer the original poll
interval directly, but must be separately planned because launching it may
cause device writes outside this read-only task.

## Native-Windows capture reported by the owner (Phase 7G.4)

The owner reports a real capture of the original LEWITT CONTROL CENTER with a
physical CONNECT 2. The report (`CONNECT2_WINDOWS_USB_METER_CAPTURE.md`) and
machine-readable summary (`capture-timing-summary.json`) are on a **separate
Windows computer**; their user-specific directory paths are intentionally omitted.
They were not available on this Linux computer and have **not** been independently
read or audited here. Only the following user-supplied measurements are recorded:

| Windows capture condition | Status GETs/s | Mean start interval | Other reported observation |
|---|---:|---:|---|
| Visible, idle | 46.845 | 21.347 ms | 64-byte status response |
| Visible, YouTube playback | 46.775 | 21.379 ms | Two probable stereo meter fields changed on every successive response |
| Minimized, playback | 34.813 | 28.725 ms | Lower acquisition rate than visible |

A separate 128-byte read reportedly remained unchanged; no periodic control-
transfer errors were observed. These measurements provide strong reported evidence
that the original application acquires the **64-byte status** around 47 times/s
when visible, far above OpenWitty's 4 Hz production cadence. They do not establish
the exact firmware meter semantics, source update ceiling, instantaneous/peak/RMS
behavior, GUI frame rate, or why minimized operation is slower. The exact USB
request metadata and capture methodology require the sanitized report for an
independent review. No raw capture or identifying device data is committed.

The next Linux step is a **separately approved**, single-owner, read-only 40 Hz
10-second status probe (at most 400 GETs); only after reviewing that result may
a separate 48 Hz/10-second trial (at most 480 GETs) be proposed. Neither rate is
yet a GUI default or an authorized hardware experiment. See the Phase 7G plan.

## Phase 7G.2 follow-up (read-only hardware sampling)

After separate owner approval, OpenWitty's bounded read-only diagnostic sampled status at
4 Hz (40/40 GETs) and 8 Hz (80/80 GETs), with no transfer errors. The active speaker meter
fields changed on every consecutive response at both rates; 38/39 interior 8 Hz midpoint
speaker-L values differed from both neighboring 250 ms-spaced samples. Therefore this
device/signal supplied additional observations at 8 Hz. This does **not** reveal the original
application's poll interval, establish firmware's maximum update rate or meter semantics, or
justify 16 Hz automatically. See `docs/HARDWARE_TEST_RESULTS.md` §23 for timing and caveats.
