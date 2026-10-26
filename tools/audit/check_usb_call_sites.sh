#!/usr/bin/env bash
# Static guard: the set of source locations that can reach USB hardware is fixed.
#   * control transfers: src/transport/UsbTransport.cpp (read/write) and the CLI probe (GET only)
#   * detach/attach/claim/release: src/transport/UsbTransport.cpp only (gated by allowDisruptive)
#   * never anywhere: reset, set_configuration, set_interface_alt_setting, bulk/interrupt/iso,
#     clear_halt, auto-detach, usbfs ioctls, sysfs bind/unbind/authorized writes.
#   * the CLI is read-only EXCEPT for exactly one deliberately-reviewed, narrowly-scoped write
#     command added 2026-10 (Phase 6C): `write led-brightness <0-100>`, which can only ever touch
#     settings byte 37 and requires --execute + LEWITT_ALLOW_HARDWARE_WRITE=1 for real hardware.
#     No other write-like command ("set"/"trigger"/"reset"/"factory") may ever appear.
# A new call site must be added here deliberately, together with a review.
set -u
root=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$root" || exit 2
fail=0
chk() { # name regex allowed-file-regex
    local hits; hits=$(grep -rnE "$2" src --include='*.cpp' --include='*.hpp' --include='*.h' | grep -vE "$3" || true)
    if [ -n "$hits" ]; then echo "FAIL $1:"; echo "$hits"; fail=1; else echo "ok   $1"; fi
}
chk "control transfers only in UsbTransport.cpp / cli probe" 'libusb_control_transfer\(' '^src/transport/UsbTransport\.cpp|^src/cli/main\.cpp'
chk "detach/claim only in UsbTransport.cpp" 'libusb_(detach_kernel_driver|attach_kernel_driver|claim_interface|release_interface)\(' '^src/transport/UsbTransport\.cpp'
chk "no reset/config/alt-setting/bulk/interrupt/clear_halt/auto_detach" 'libusb_(reset_device|set_configuration|set_interface_alt_setting|bulk_transfer|interrupt_transfer|clear_halt|set_auto_detach_kernel_driver|submit_transfer|fill_(bulk|interrupt|iso))' '^$'
chk "no raw usbfs ioctls" 'USBDEVFS_|ioctl\(' '^$'
chk "no sysfs writes (bind/unbind/authorized/remove)" '(unbind|/bind|authorized|/remove|driver_override)"' '^src/transport/UsbDiagnostics\.cpp.*(suggest|std::puts|//)'
# the CLI may expose no write-like command other than the one reviewed "write" dispatch below
if grep -nE '"(set|trigger|reset|factory)"' src/cli/main.cpp >/dev/null; then
    echo "FAIL cli has a write-like command beyond the one reviewed 'write' dispatch"; fail=1
else
    echo "ok   cli exposes no write-like command beyond 'write'"
fi
# the one allowed write command must be exactly 'write' (singular dispatch), scoped to
# led-brightness only, and gated by the hardware-write opt-in env var
writecmds=$(grep -coE '"write"' src/cli/main.cpp)
if [ "$writecmds" -ne 1 ]; then
    echo "FAIL expected exactly one \"write\" CLI dispatch, found $writecmds"; fail=1
else
    echo "ok   exactly one 'write' CLI dispatch"
fi
if ! grep -q 'led-brightness' src/cli/main.cpp; then
    echo "FAIL write command must be scoped to led-brightness"; fail=1
else
    echo "ok   write command scoped to led-brightness"
fi
if ! grep -q 'LEWITT_ALLOW_HARDWARE_WRITE' src/cli/main.cpp; then
    echo "FAIL write command must require LEWITT_ALLOW_HARDWARE_WRITE opt-in on real hardware"; fail=1
else
    echo "ok   write command gated by LEWITT_ALLOW_HARDWARE_WRITE"
fi
# Phase 6E / 8C: the CLI may carry exactly four single-field write specs (LED brightness, instrument
# mute, speaker volume, speaker mute); each real-hardware execution is scoped by
# DeviceSession::hardwareWriteOffsets to its own field bytes, and every field except LED brightness
# needs its own authorization variable.
nspec=$(grep -cE '^\s*\{"[a-z-]+", settings_off::' src/cli/main.cpp)
if [ "$nspec" -ne 4 ]; then echo "FAIL expected exactly 4 CLI write field specs, found $nspec"; fail=1; else echo "ok   exactly 4 CLI write field specs"; fi
for spec in '{"instrument-mute", settings_off::kMute\[1\], "LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE"}' \
            '{"speaker-volume", settings_off::kSpeakerVolume, "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME", 2}' \
            '{"speaker-mute", settings_off::kSpeakerMute, "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"}'; do
    grep -qF "$(printf '%s' "$spec" | sed 's/\\\[/[/; s/\\\]/]/')" src/cli/main.cpp && echo "ok   field spec present with own authorization: ${spec%%,*}}" \
        || { echo "FAIL field spec missing or without its own authorization: $spec"; fail=1; }
done
grep -q 'pol.hardwareWriteOffsets.push_back(f.offset + i)' src/cli/main.cpp && grep -q 'for (std::size_t i = 0; i < f.length; ++i)' src/cli/main.cpp \
    && echo "ok   CLI scopes hardware writes to the field bytes" || { echo "FAIL CLI does not scope hardwareWriteOffsets"; fail=1; }
# the GUI must never enable hardware writes or authorize offsets
if grep -nE '(allowHardwareWrites|hardwareWriteOffsets)[[:space:]]*(=|\{|:)' src/gui/*.cpp src/gui/*.hpp >/dev/null; then echo "FAIL GUI references hardware write policy"; fail=1; else echo "ok   GUI never enables hardware writes"; fi
# Phase 7A: the GUI may build a UsbTransport only from the fixed read-only option set, and the
# control interface may never be a hard-coded number or a DFU (class 0xFE) interface.
# Phase 8D/8G/10B: besides readOnlyHardwareOptions() the GUI may use outputControlHardwareOptions(), at exactly one
# site, only when one of the individually confirmed write permissions is on (core/UserConfig, all default off), and
# only paired with guiWritePolicy(speaker, headphone, ledBrightness) - the union of exactly those fields' bytes.
# The settings backend itself never writes the device.
if grep -nE 'UsbTransport[>(]' src/gui/*.cpp | grep -vE 'readOnlyHardwareOptions\(\)|outputControlHardwareOptions\(\)' >/dev/null; then echo "FAIL GUI constructs a UsbTransport with unreviewed options"; fail=1; else echo "ok   GUI uses readOnlyHardwareOptions() or the gated output options only"; fi
n_out=$(grep -c 'outputControlHardwareOptions()' src/gui/DeviceController.cpp)
if [ "$n_out" -ne 1 ] || ! grep -B2 'outputControlHardwareOptions()' src/gui/DeviceController.cpp | grep -q 'if (anyWritePermission)' \
   || ! grep -B6 'outputControlHardwareOptions()' src/gui/DeviceController.cpp | grep -q 'userConfig.speakerControl || userConfig.headphoneControl || userConfig.ledBrightnessControl' \
   || ! grep -A1 'outputControlHardwareOptions()' src/gui/DeviceController.cpp | grep -q 'guiWritePolicy(userConfig.speakerControl, userConfig.headphoneControl, userConfig.ledBrightnessControl)'; then
    echo "FAIL GUI writes must be one gated outputControlHardwareOptions()+guiWritePolicy(confirmed fields) site"; fail=1
else echo "ok   GUI writes: one site, only with a confirmed permission, policy = union of the confirmed fields"; fi
# guiWritePolicy adds exactly the LED brightness byte to the output policy, nothing else
gwp=$(grep -A8 'WritePolicy guiWritePolicy' src/core/OutputControl.cpp)
if printf '%s' "$gwp" | grep -q 'WritePolicy p = outputWritePolicy(speaker, headphone);' \
   && printf '%s' "$gwp" | grep -q 'hardwareWriteOffsets.push_back(settings_off::kLedBrightness);' \
   && [ "$(printf '%s' "$gwp" | grep -c 'settings_off::')" -eq 1 ] \
   && ! printf '%s' "$gwp" | grep -qE 'hardwareTriggers|allowRoutingChanges[[:space:]]*=[[:space:]]*true|allowPersistentChanges[[:space:]]*=[[:space:]]*true'; then
    echo "ok   guiWritePolicy = output policy + LED brightness byte only (no triggers, routing or persistent changes)"
else echo "FAIL guiWritePolicy scope changed"; fail=1; fi
if grep -nE 'UsbTransport|DeviceSession|commit|stageOutput|writeDevice' src/gui/IntegrationSettings.cpp >/dev/null; then
    echo "FAIL IntegrationSettings must not touch the device"; fail=1
else echo "ok   IntegrationSettings never touches the device"; fi
if [ "$(grep -c 'if (on && !confirmed) return;' src/gui/IntegrationSettings.cpp)" -ge 3 ] \
   && grep -q 'if (on && (!confirmed || !desktopSync())) return;' src/gui/IntegrationSettings.cpp \
   && grep -q 'bool vendorFallback{false};' src/core/UserConfig.hpp \
   && grep -q 'bool speakerControl{false};' src/core/UserConfig.hpp && grep -q 'bool headphoneControl{false};' src/core/UserConfig.hpp \
   && grep -q 'bool ledBrightnessControl{false};' src/core/UserConfig.hpp \
   && ! grep -nE '(speakerControl|headphoneControl|ledBrightnessControl)[[:space:]]*=[[:space:]]*(true|.*legacyOutputControl)' src/core/UserConfig.cpp >/dev/null; then
    echo "ok   device-writing settings default off, need confirmation, and the legacy setting grants nothing"
else echo "FAIL device-writing settings must default off and need confirmation (legacy must grant nothing)"; fail=1; fi
# the GUI-hosted desktop link only mirrors (native mode); it never uses the vendor fallback
if grep -q 'o.deviceWrites = DeviceWriteMode::Native' src/gui/DeviceController.cpp && ! grep -q 'DeviceWriteMode::Fallback' src/gui/*.cpp; then
    echo "ok   GUI desktop link is native (mirror-only)"
else echo "FAIL GUI desktop link must stay in native mode"; fail=1; fi
# the output policy itself may only name the speaker/headphone volume and mute bytes
if grep -A4 'std::vector<std::size_t> outputWriteOffsets' src/core/OutputControl.cpp | grep -q 'kSpeakerVolume, kSpeakerVolume + 1, kSpeakerMute' \
   && grep -A4 'std::vector<std::size_t> outputWriteOffsets' src/core/OutputControl.cpp | grep -q 'kHeadphoneVolume, kHeadphoneVolume + 1, kHeadphoneMute' \
   && grep -A4 'WritePolicy outputWritePolicy' src/core/OutputControl.cpp | grep -q 'allowRoutingChanges = false' \
   && grep -A4 'WritePolicy outputWritePolicy' src/core/OutputControl.cpp | grep -q 'allowPersistentChanges = false'; then
    echo "ok   output write policy limited to speaker/headphone volume+mute bytes"
else echo "FAIL output write policy scope changed"; fail=1; fi
# openwitty-volumed: speaker bytes only, double opt-in, dry run by default
if grep -q 'outputWritePolicy(/\*speaker=\*/true, /\*headphone=\*/false)' src/daemon/main.cpp \
   && grep -q 'envAuthorized("LEWITT_ALLOW_HARDWARE_WRITE") && envAuthorized("LEWITT_ALLOW_WRITE_SPEAKER_VOLUME")' src/daemon/main.cpp \
   && grep -q 'bool writes{false};' src/daemon/main.cpp \
   && grep -q 'readOnlyHardwareOptions()), WritePolicy{}' src/daemon/main.cpp \
   && grep -q 'a.writes = a.ok && a.mode == Mode::Fallback;' src/daemon/main.cpp \
   && [ "$(grep -cE 'a\.writes *=' src/daemon/main.cpp)" -eq 1 ] \
   && ! grep -nE 'hardwareWriteOffsets|allowHardwareWrites|sendTrigger|commit\(' src/daemon/main.cpp src/desktop/*.cpp >/dev/null \
   && [ "$(grep -c 'session_.commitAndVerify(' src/desktop/DesktopVolumeLink.cpp)" -eq 1 ] \
   && grep -q 'stageOutput(m, Output::Speaker' src/desktop/DesktopVolumeLink.cpp; then
    echo "ok   openwitty-volumed: read-only mirror default, vendor writes only via --vendor-fallback (speaker bytes, exact double opt-in), one verified write path"
else echo "FAIL openwitty-volumed write scope/gating changed"; fail=1; fi
if grep -nE 'interfaceNumber[[:space:]]*=' src/gui/*.cpp >/dev/null; then echo "FAIL GUI hard-codes an interface number"; fail=1; else echo "ok   GUI does not hard-code an interface number"; fi
grep -q 'isProhibitedInterface(d)' src/transport/InterfaceSelection.cpp && grep -q 'kUsbClassApplicationSpecific = 0xFE' src/transport/InterfaceSelection.hpp && echo "ok   interface selection rejects class 0xFE (DFU)" || { echo "FAIL interface selection lost its DFU rejection"; fail=1; }
# Phase 9B: the tray companion and the shared desktop-UI helpers never reach the device, the control lease
# or ALSA, and never change the background service (they only read `systemctl --user show`).
if grep -rnE 'UsbTransport|DeviceSession|ControlLease|libusb|snd_|AlsaDesktopMixer|DesktopMixer|collectAudioSetup|#include "(transport|desktop|protocol|core/(DeviceSession|VolumeSync|OutputControl))' \
       src/tray src/desktopui >/dev/null; then
    echo "FAIL tray/desktop-UI code references device, lease or ALSA code:"
    grep -rnE 'UsbTransport|DeviceSession|ControlLease|libusb|snd_|AlsaDesktopMixer|DesktopMixer|collectAudioSetup|#include "(transport|desktop|protocol|core/(DeviceSession|VolumeSync|OutputControl))' src/tray src/desktopui
    fail=1
else echo "ok   tray and desktop-UI helpers never reference device, lease or ALSA code"; fi
if grep -rnE '"(enable|disable|start|stop|restart|kill|mask|--now)"' src/tray src/desktopui >/dev/null; then
    echo "FAIL tray/desktop-UI code can change the background service"; fail=1
else echo "ok   tray and desktop-UI helpers only read the service state"; fi
# Single instance: both programs act on every claim outcome, and the window claims before any device code.
if grep -q 'InstanceChannel::Role::Failed' src/gui/main.cpp && grep -q 'InstanceChannel::Role::Failed' src/tray/main.cpp \
   && ! grep -rnE '\.listen\(' src/gui src/tray >/dev/null \
   && [ "$(grep -n 'claimOrForward' src/gui/main.cpp | head -1 | cut -d: -f1)" -lt "$(grep -n 'DeviceController controller' src/gui/main.cpp | cut -d: -f1)" ]; then
    echo "ok   window/tray single-instance claim handled, before any device code"
else echo "FAIL single-instance claim must be handled (Failed) and precede DeviceController"; fail=1; fi
# Phase 10A: no production code authorizes a trigger on real hardware (WritePolicy::hardwareTriggers stays empty).
if grep -rnE 'hardwareTriggers[[:space:]]*(=|\.push_back|\.insert|\{)' src --include='*.cpp' --include='*.hpp' | grep -v 'std::vector<Trigger> hardwareTriggers;' >/dev/null; then
    echo "FAIL production code authorizes hardware triggers:"; grep -rnE 'hardwareTriggers[[:space:]]*(=|\.push_back|\.insert|\{)' src --include='*.cpp' --include='*.hpp'; fail=1
else echo "ok   no hardware trigger is authorized in production code"; fi
# Phase 10A: GUI controls are enabled per capability (can.<key>), never by the global writable flag alone, and
# the test-only transport injection is not used by the application.
if grep -nE 'enabled:[[:space:]]*root\.interactive([[:space:]]*[;}]|[[:space:]]*$)' src/gui/qml/InputStrip.qml src/gui/qml/OutputStrip.qml \
       src/gui/qml/MonitorSection.qml src/gui/qml/SettingsView.qml >/dev/null; then
    echo "FAIL a QML device control is enabled by the global writable flag:"; grep -nE 'enabled:[[:space:]]*root\.interactive([[:space:]]*[;}]|[[:space:]]*$)' src/gui/qml/*.qml; fail=1
else echo "ok   QML device controls are enabled per capability"; fi
if grep -rn 'connectTransportForTesting' src --include='*.cpp' --include='*.qml' | grep -vE '^src/gui/DeviceController\.cpp' >/dev/null \
   || grep -n 'connectTransportForTesting' src/gui/qml/*.qml >/dev/null; then
    echo "FAIL connectTransportForTesting used outside tests"; fail=1
else echo "ok   test-only transport injection unused by the application"; fi
# Phase 10B.1: GUI writes on real hardware need two identical snapshots before the SET; every GUI commit goes
# through guiPollPolicy(); the hardware check can only read; the LED slider commits once on release on real hardware.
if grep -nE -- '->commitAndVerify\(\)' src/gui/*.cpp >/dev/null \
   || ! grep -A3 'PollPolicy DeviceWorker::guiPollPolicy' src/gui/DeviceController.cpp | grep -q 'requireStableSnapshot = session_ && !session_->isSimulated()'; then
    echo "FAIL GUI commits must use guiPollPolicy() (stable snapshot on real hardware)"; fail=1
else echo "ok   GUI commits use guiPollPolicy(): two matching snapshots before a real-device SET"; fi
if grep -nE 'DeviceSession|Transport|ControlLease|libusb|commit|write\(' src/core/SettingsComparison.hpp src/core/SettingsComparison.cpp >/dev/null; then
    echo "FAIL SettingsComparison must work on snapshots only"; fail=1
else echo "ok   SettingsComparison has no session, transport or lease access"; fi
hwfns=$(awk '/^void DeviceWorker::(hwCheck|enableHardwareCheck)/,/^}/' src/gui/DeviceController.cpp | sed 's/"[^"]*"//g; s://.*$::')
if [ -z "$hwfns" ] || printf '%s' "$hwfns" | grep -nE 'commit|edit\(|sendTrigger|setValue|Transport|ControlLease|connect|adoptSession|reconnect' >/dev/null; then
    echo "FAIL hardware-check functions may only read through refreshDeviceState()"; fail=1
else echo "ok   hardware check reads only, through the window's own session (no second connection)"; fi
if ! grep -q 'commitOnRelease: !device.simulated' src/gui/qml/SettingsView.qml \
   || ! grep -q 'enabled: root.can("set.ledBrightness") && root.ledWrite !== "pending"' src/gui/qml/SettingsView.qml; then
    echo "FAIL LED brightness slider must commit once on release on real hardware and wait for the result"; fail=1
else echo "ok   LED brightness slider: one commit per release on real hardware, disabled while sending"; fi
# destructive trigger words must be referenced only by the guard, the enum/messages and tests
chk "FactoryReset only in guard/messages/mock-sim" 'Trigger::FactoryReset' '^src/protocol/(WriteGuard|Messages)\.(cpp|hpp)'
[ $fail -eq 0 ] && echo "USB CALL-SITE AUDIT: PASS" || echo "USB CALL-SITE AUDIT: FAIL"
exit $fail
