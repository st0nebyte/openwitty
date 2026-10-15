#!/usr/bin/env bash
# CLI write-scope regression (mock/simulated only; sends nothing to hardware).
#   test_cli_write_scope.sh <path-to-openwitty-cli>
set -u
cli=${1:?cli path}
fail=0
check() { # name expected_rc actual_rc [output-pattern output]
    if [ "$2" -eq "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1 (rc $3, expected $2)"; fail=1; fi
}
out=$("$cli" write led-brightness 40 --mock 2>&1); rc=$?
check "led dry-run mock" 0 $rc
echo "$out" | grep -q "dry run only" || { echo "  FAIL led dry-run text"; fail=1; }
echo "$out" | grep -q "changed byte offsets: 37" || { echo "  FAIL led diff is exactly byte 37"; fail=1; }

out=$("$cli" write instrument-mute on --mock 2>&1); rc=$?
check "instrument-mute dry-run mock" 0 $rc
echo "$out" | grep -q "changed byte offsets: 12" || { echo "  FAIL mute diff is exactly byte 12: $out"; fail=1; }
echo "$out" | grep -q "SIMULATED" || { echo "  FAIL simulated device not labelled"; fail=1; }

out=$("$cli" write instrument-mute on --mock --execute 2>&1); rc=$?
check "instrument-mute execute on mock verifies" 0 $rc
echo "$out" | grep -q "write state: verified" || { echo "  FAIL not verified: $out"; fail=1; }
echo "$out" | grep -q "post-write byte 12: 0x01" || { echo "  FAIL byte 12 not muted"; fail=1; }

out=$("$cli" write instrument-mute off --mock --execute 2>&1); rc=$?
check "instrument-mute off on mock (already off => nothing to write)" 0 $rc

# real-hardware path must refuse without authorization; run with a scrubbed environment
out=$(env -u LEWITT_ALLOW_HARDWARE_WRITE -u LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE "$cli" write instrument-mute on --execute 2>&1); rc=$?
check "hardware execute without authorization refused" 2 $rc
out=$(env -u LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE LEWITT_ALLOW_HARDWARE_WRITE=1 "$cli" write instrument-mute on --execute 2>&1); rc=$?
check "hardware execute without per-field authorization refused" 2 $rc
echo "$out" | grep -q "LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE" || { echo "  FAIL refusal does not name the variable"; fail=1; }
# LED's authorization must NOT unlock instrument mute and vice versa
out=$(env LEWITT_ALLOW_HARDWARE_WRITE=1 "$cli" write instrument-mute on --execute 2>&1); rc=$?
check "global write permission alone does not unlock instrument-mute" 2 $rc

for bad in "write" "write mic-gain 10 --mock" "write byte 12 1 --mock" "write instrument-mute maybe --mock" "write led-brightness 101 --mock"; do
    # shellcheck disable=SC2086
    "$cli" $bad >/dev/null 2>&1; rc=$?
    check "rejected: $bad" 64 $rc
done
[ $fail -eq 0 ] && echo "CLI WRITE SCOPE: PASS" || echo "CLI WRITE SCOPE: FAIL"
exit $fail
