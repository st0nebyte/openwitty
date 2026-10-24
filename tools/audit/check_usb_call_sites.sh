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
if grep -nE 'UsbTransport[>(]' src/gui/*.cpp | grep -v 'readOnlyHardwareOptions()' >/dev/null; then echo "FAIL GUI constructs a UsbTransport without readOnlyHardwareOptions()"; fail=1; else echo "ok   GUI uses readOnlyHardwareOptions() only"; fi
if grep -nE 'interfaceNumber[[:space:]]*=' src/gui/*.cpp >/dev/null; then echo "FAIL GUI hard-codes an interface number"; fail=1; else echo "ok   GUI does not hard-code an interface number"; fi
grep -q 'isProhibitedInterface(d)' src/transport/InterfaceSelection.cpp && grep -q 'kUsbClassApplicationSpecific = 0xFE' src/transport/InterfaceSelection.hpp && echo "ok   interface selection rejects class 0xFE (DFU)" || { echo "FAIL interface selection lost its DFU rejection"; fail=1; }
# destructive trigger words must be referenced only by the guard, the enum/messages and tests
chk "FactoryReset only in guard/messages/mock-sim" 'Trigger::FactoryReset' '^src/protocol/(WriteGuard|Messages)\.(cpp|hpp)'
[ $fail -eq 0 ] && echo "USB CALL-SITE AUDIT: PASS" || echo "USB CALL-SITE AUDIT: FAIL"
exit $fail
