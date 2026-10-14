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
# destructive trigger words must be referenced only by the guard, the enum/messages and tests
chk "FactoryReset only in guard/messages/mock-sim" 'Trigger::FactoryReset' '^src/protocol/(WriteGuard|Messages)\.(cpp|hpp)'
[ $fail -eq 0 ] && echo "USB CALL-SITE AUDIT: PASS" || echo "USB CALL-SITE AUDIT: FAIL"
exit $fail
