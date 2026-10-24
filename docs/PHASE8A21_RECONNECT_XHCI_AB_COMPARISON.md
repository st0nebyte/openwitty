# Phase 8A.21 – Live reconnect A/B USB and xHCI trace

Status: experiment executed once, analysed, tracing removed. No hardware write, USB reset or reboot was
performed by OpenWitty; the only physical action was one user-performed unplug/replug.

## Procedure
1. `tools/diagnostics/usb_ep0_live.py live` (new): defines the four kprobes (global dynamic events), creates a
   **private tracefs instance** `owtrace_live`, enables the probes and the 14 xHCI events *only inside it*
   (32 MiB per-CPU buffer), polls read-only sysfs for VID:PID presence, stops 45 s after the device returns or at
   a 240 s deadline, saves the raw trace outside Git (0600), removes only its own instance/probes and verifies
   the tracefs baseline. It sends no USB request.
2. Independent watchdog process: on parent death or hard deadline it stops the parent and performs the cleanup.
3. Tool validation before use on CONNECT 2 (offline fake tracefs, then the real system with a private instance and
   no device action): deadline path, SIGTERM path and SIGKILL-of-parent path all left tracefs identical to the
   baseline (kprobe_events empty, no instances, buffer 1410 KB, tracing_on 1). Caveat found: `pkill -f` by name
   also kills the watchdog (same command line); kill the session by PID.
4. Preparation checkpoint committed and pushed (`c9d23a6`) before the hardware step.
5. Pre/post state via the GET-only `openwitty-cli read settings --method interface --interface 3`, `amixer -c C2
   controls` (names only), `pw-dump`, `pactl get-sink-volume` (no volume/mute SET at any point).

## Identity
Kernel `7.2.10-1-cachyos`, CONNECT 2 (29c2:0004, bcdDevice 1.21) on bus 3 port 2. Cold boot (8A.20): device
number 2. Reconnect: device number 3 (address changed; joined by VID:PID, bus and SETUP bytes, not by address).

## Safety confirmation
Obtained from the user before arming: no playback/recording; speakers/amplifiers disconnected/off/independently
safe; headphones/listeners protected; no reliance on KDE software volume (40%) or hardware mute; understood that a
reconnect may change the physical output state; exactly one reconnect.

## Completeness
Tracefs instance buffer 32771 KB, 345 334 entries written = entries in buffer, **per-CPU overrun 0**, 342 588 xHCI
records (all joinable lines parsed), 0 unpaired, 0 malformed. Disconnect seen at +21.8 s, reconnect at +27.8 s,
stop at +72.9 s. The whole reconnect probe (first request of the new device at 1635.099 s through the last
initial stream setup at 1641.9 s) is inside the capture. Note: `usb_ep0_trace.py analyze` prints `PARTIAL` only
because it expects a boot-style `collect_report.json` and finds no timed-out request to attribute; the live
report supplies buffer size and overrun counts. Capture is treated as **COMPLETE for this question**.

## Request-by-request comparison (wire-level SETUP request time)
| Item | Cold boot (8A.20, dev 2) | Reconnect (dev 3) |
|---|---|---|
| SET_INTERFACE(1,0) initial | 0.235 ms, ok | 0.236 ms, ok |
| first SET_INTERFACE(1,1) | **40.871 ms** (slow) | 0.117 ms (fast) |
| following SET_INTERFACE(1,0) | **no completion, -110 after 5448 ms** | 0.121 ms, ok |
| first SET_INTERFACE(2,1) | 0.138 ms | 0.118 ms |
| following SET_INTERFACE(2,0) | 0.153 ms (WirePlumber's later alt-0: 5144 ms timeout) | 0.119 ms |
| FU2 initial GET | -4.00 dB | -4.00 dB |
| FU2 min-test (-60) readback | -4.00 (sticky) | **-60.00 (followed)** |
| FU2 next test readback | 0 dB -> -4.00 (sticky) | -3.50 -> -3.50 (followed) |
| FU2 restore | final SET 0 dB, no restore | SET -4.00 dB (restore), later -20.0 and -23.5 |
| unit 5 channel 3/4 STALLs (-32) | 20 | 20 |
| EP0 timeouts | 2 | **0** |
| kernel sticky-mixer messages | units 2:0 **and** 5:0 | unit 5:0 only |
| ALSA `PCM Playback Volume` | absent | **present** |
| PipeWire `route.hw-volume` | false | **true** on one route (both false before; the other stays false) |
`usb_set_interface()` total time in the reconnect is ~30 ms per call (core + endpoint work); no call exceeded
35 ms and there was no 5 s stall. Requests after the probe came from kworker, WirePlumber and PipeWire, as in the
cold boot; no request failed except the same unit-5 STALLs.

## xHCI lifecycle
No request timed out, so there is no stalled transfer to follow through the controller; the reconnect shows
normal enqueue/giveback for all 113 target requests and no Stop-Endpoint recovery. The device-side-status-stage
hypothesis from 8A.20 therefore has no counter-instance in this capture, and no direct new support either.

## Audio observations and output state
Pre-reconnect Speaker -4.00 dB (unmuted), Headphone -32.40 dB muted; post-reconnect **Speaker -23.50 dB**
(unmuted), Headphone -32.40 dB muted, software sink 40% unchanged. The -20/-23.5 dB writes after the kernel's
restore were issued by the audio stack once hardware volume became available (they follow the 40% sink volume);
OpenWitty/this tool sent no SET. The output level got *quieter*, but a reconnect can change the physical output
state in either direction, as seen earlier (-33.34 dB muted -> -4.00 dB unmuted). The user reported no audio
issue during the capture (nothing played).

## Interpretation (outcomes A–E)
The reconnect **is a clean good case**: first alt-1 selection fast, no alt-0 stall, FU2 readbacks follow writes,
kernel restores FU2 (-4.00 dB), ALSA volume and PipeWire hw-volume return. The only differences from the cold boot
are the slow first alt-1 and the subsequent unanswered alt-0 request.
- Strengthened: the failure is tied to the *probe-time device state* (the slow first alt-1 → unanswered alt-0
  sequence precedes the sticky classification); this good reconnect (one observation) fits "slow alt-1 then
  stall" and "fast alt-1 then no stall".
- Weakened: a purely host-side cause that is independent of device timing (same kernel, same driver, same
  hardware path, different outcome); a persistent device state that always sticks FU2.
- Not shown: causation, device firmware defect, or why the first alt-1 is slow after power-on/boot enumeration.
  Cold boot differs from reconnect in more than one way (boot race with other drivers, device power state
  after host reset, timing), so one reconnect cannot isolate the factor.
- Rejected earlier-stage idea: that the hardware-volume loss is a property of the kernel/driver version alone.

## OpenWitty recommendation (not implemented)
A read-only, non-privileged "device health" diagnostic would be useful: ALSA `PCM Playback Volume` presence,
PipeWire `route.hw-volume`, recent kernel messages containing "sticky mixer values" for the device. GUI states:
*available*, *unavailable after boot*, *not detected*, *diagnosis incomplete*. It must never suggest that
reconnecting is safe while speakers/headphones are active (reconnect can raise or lower the output level).
No automatic reconnect, forced write or kernel workaround is proposed.

## Cleanup verification
After the run: `kprobe_events` empty, `instances/` empty, `buffer_size_kb` 1410, `tracing_on` 1, no temporary
bootloader entry; raw capture kept at `~/.local/state/openwitty/trace-8a21/` (0600, outside Git); this document
contains no serial number, UUID, kernel pointer or raw trace data.
