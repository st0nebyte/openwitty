# Windows Codex handoff (preparation only – do not start now)

Goal: learn what a Windows stack (driver + LEWITT CONTROL CENTER) does with a LEWITT CONNECT 2 (USB 29c2:0004) at
startup, app start/exit, audio close, suspend and **full** shutdown, to compare with Linux. Hypothesis, not fact:
Windows may perform a deinitialisation or initialisation sequence that Linux lacks. A healthy Linux boot after a
Windows session would NOT prove a Windows shutdown command (Windows startup init could have set retained state).

Rules for the Windows session
- Observation only. No firmware update, factory reset, mode change (macOS/mobile), or settings change unless the
  user separately approves it. Do not move volume or mute. Audio safety: speakers/amps off or independently safe,
  headphones not worn, no reliance on software volume or hardware mute, before any unplug/replug/shutdown.
- Do not commit raw captures, device serials, machine/user names or unrelated USB traffic. Sanitize first.

Tasks
1. Identify: Windows version/build, CONTROL CENTER version, driver (LEWITT ASIO / usbaudio2.sys), INF, driver date,
   Fast Startup status (`powercfg /a`, `HiberbootEnabled` registry value), USB selective suspend setting, USB port
   (controller vendor).
2. Capture feasibility: compare (a) USBPcap + Wireshark (control transfers on the device's bus; limited at
   boot/shutdown), (b) ETW `Microsoft-Windows-USB-USBPORT`/`USBHUB3`/`USBXHCI` providers via `logman`/`tracelog`
   (can run in the boot/shutdown sessions with autologger), (c) a hardware USB analyser if available. State which
   works for early boot and late shutdown.
3. Sequences to record, each as a separate labelled capture: Windows boot with the device already powered;
   CONTROL CENTER start; audio playback start/stop and endpoint close; CONTROL CENTER exit; sleep/resume; **full
   shutdown** (Settings > "Shut down" with Fast Startup **disabled**, or `shutdown /s /full`; then repeat once with
   Fast Startup enabled, noting that hybrid shutdown hibernates the kernel and does not run full device shutdown).
4. For every control transfer record: time, bmRequestType, bRequest, wValue, wIndex, wLength, data, status — in
   particular any vendor-specific (type 2) request, any request to the DFU interface (do not send any), and any
   UAC2 `SET_CUR` on feature units 2/5 or the clock source; any `SET_INTERFACE` alt-0 before power-off.
5. Distinguish hybrid vs full shutdown: check Event Viewer (Kernel-Power 41/109, Kernel-Boot "FastStartup"
   boot type), `powercfg /a`, and `wmic`/registry `HiberbootEnabled`. Report which was used per capture.
6. Output: a short markdown summary with the request table above, the ETW/USBPcap tool settings used, and a
   yes/no per question: "Is any vendor request sent at startup / app exit / shutdown?", "Is alt-0 selected before
   power-off?", "Is the device left in suspend?". Transfer back to Linux as sanitized markdown or CSV (no raw .etl/.pcap
   committed to the repo).

Linux reference to compare against: `docs/PHASE8A19…8A21` and `PHASE8A22_PERSISTENT_USB_POWER_LINUX_SHUTDOWN.md`
(no shutdown-time traffic has been measured on Linux either; snd-usb-audio has no shutdown callback).
