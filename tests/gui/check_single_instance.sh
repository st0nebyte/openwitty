#!/bin/sh
# End-to-end desktop behaviour with the real binaries, offscreen, in private runtime/config directories (no
# device: the window runs --mock; the developer's own window, tray and service are never reachable):
#   * tray unavailable: openwitty-tray exits cleanly with an explanation;
#   * openwitty-tray --quit without a running icon;
#   * a second (and third, fourth) window start brings the running window to the front and exits;
#   * the tray links no USB or ALSA library.
#   check_single_instance.sh <openwitty> <openwitty-tray>
set -u
gui=$1; tray=$2
tmp=$(mktemp -d) || exit 1
trap 'kill "$first" 2>/dev/null; rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/run" "$tmp/config"
export XDG_RUNTIME_DIR="$tmp/run" XDG_CONFIG_HOME="$tmp/config" QT_QPA_PLATFORM=offscreen
first=
fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }

out=$("$tray" --wait-for-tray-seconds 0 2>&1); rc=$?
check "tray unavailable: exit 0" "[ $rc -eq 0 ]"
check "tray unavailable: explains that OpenWitty works without it" "printf '%s' \"\$out\" | grep -q 'no system tray'"
start=$(date +%s)
"$tray" --wait-for-tray-seconds 2 >/dev/null 2>&1; rc=$?
took=$(( $(date +%s) - start ))
check "tray waits for a late tray, then gives up quietly (exit 0, ~2 s)" "[ $rc -eq 0 ] && [ $took -ge 1 ] && [ $took -le 6 ]"
out=$("$tray" --quit 2>&1)
check "tray --quit without a running icon" "printf '%s' \"\$out\" | grep -q 'no tray icon is running'"

"$gui" --mock --quit-after-seconds 15 >"$tmp/first.log" 2>&1 &
first=$!
i=0; while [ ! -S "$XDG_RUNTIME_DIR/openwitty-gui.sock" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
check "first window listens on the per-session socket" "[ -S '$XDG_RUNTIME_DIR/openwitty-gui.sock' ]"
for n in 1 2 3; do
    s=$(date +%s)
    out=$("$gui" --mock --show settings-desktop 2>&1); rc=$?
    d=$(( $(date +%s) - s ))
    check "start $((n+1)): exits at once (rc 0, ${d}s) and says the window was brought to the front" \
        "[ $rc -eq 0 ] && [ $d -le 3 ] && printf '%s' \"\$out\" | grep -q 'already open'"
done
sleep 0.5
n=$(grep -c 'activation request from another start' "$tmp/first.log")
check "the first window received all three requests ($n)" "[ $n -eq 3 ]"
check "the first window is still running" "kill -0 $first"

if command -v readelf >/dev/null 2>&1; then
    needed=$(readelf -d "$tray" | grep NEEDED)
    check "tray links no libusb" "! printf '%s' \"\$needed\" | grep -q libusb"
    check "tray links no ALSA" "! printf '%s' \"\$needed\" | grep -q libasound"
fi
if command -v strings >/dev/null 2>&1; then
    check "tray contains no USB transport code" "! strings -a '$tray' | grep -qE 'libusb_|LIBUSB_ERROR|snd_ctl_open'"
fi

[ $fail -eq 0 ] && echo "SINGLE INSTANCE: PASS" || echo "SINGLE INSTANCE: FAIL"
exit $fail
