#!/bin/sh
# Disposable staging installs of a configured build tree (nothing outside a temporary directory is
# touched; nothing is enabled or started):
#   1. user-local:  cmake --install <build> --prefix <tmp>/home/.local
#   2. package:     DESTDIR=<tmp>/stage cmake --install <build> --prefix /usr
# Checks that the systemd user unit lands on systemd's user search path and that ExecStart names the
# executable as it will exist after installation (not a DESTDIR staging path).
set -u
build=$1; cmake=$2
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }

# The unit directory as configured: relative to the prefix (default share/systemd/user) or absolute (what
# distribution packages pass, e.g. /usr/lib/systemd/user). An absolute directory cannot follow a prefix
# override, so the user-local part only applies to a relative one.
unitdir=$(sed -n 's/^LEWITT_SYSTEMD_USER_UNIT_DIR:[A-Z]*=//p' "$build/CMakeCache.txt")
[ -n "$unitdir" ] || unitdir=share/systemd/user
case $unitdir in
    /*) echo "skip user-local install (absolute unit directory $unitdir)" ;;
    *)
"$cmake" --install "$build" --prefix "$tmp/home/.local" >"$tmp/log1" 2>&1 || { cat "$tmp/log1"; echo "FAIL install 1"; exit 1; }
u1="$tmp/home/.local/$unitdir/openwitty-volumed.service"
check "user-local unit at ~/.local/$unitdir" "[ -f '$u1' ]"
check "user-local ExecStart = installed binary" "grep -qx 'ExecStart=$tmp/home/.local/bin/openwitty-volumed' '$u1'"
check "user-local binary exists and is executable" "[ -x '$tmp/home/.local/bin/openwitty-volumed' ]"
check "user-local docs installed" "[ -f '$tmp/home/.local/share/doc/openwitty/VOLUME_INTEGRATION.md' ]"
check "unit recorded in install manifest" "grep -q '$unitdir/openwitty-volumed.service' '$build/install_manifest.txt'"
# every installed file or link must be in the manifest (uninstall from the manifest leaves nothing behind)
unlisted=$(cd "$tmp/home/.local" && find . \( -type f -o -type l \) | sed "s#^\.#$tmp/home/.local#" | while read -r f; do
    grep -qxF "$f" "$build/install_manifest.txt" || echo "$f"; done)
check "every installed file and link is in the manifest" "[ -z \"\$unlisted\" ]"
[ -n "$unlisted" ] && echo "     not in manifest: $unlisted"
check "no unenabled-unit leftovers in lib/systemd" "[ ! -e '$tmp/home/.local/lib/systemd/user/openwitty-volumed.service' ]"
check "no configure-time placeholder left" "! grep -q '@CMAKE_INSTALL' '$u1'"
if command -v systemd-analyze >/dev/null 2>&1; then
    out=$(systemd-analyze --user verify "$u1" 2>&1)
    check "user-local unit verifies" "! printf '%s' \"\$out\" | grep -qE 'Unknown key|ignoring|Failed|not found|Invalid|is not executable'"
fi
    ;;
esac

DESTDIR="$tmp/stage" "$cmake" --install "$build" --prefix /usr >"$tmp/log2" 2>&1 || { cat "$tmp/log2"; echo "FAIL install 2"; exit 1; }
case $unitdir in /*) pkgunitdir=$unitdir ;; *) pkgunitdir=/usr/$unitdir ;; esac
u2="$tmp/stage$pkgunitdir/openwitty-volumed.service"
check "package unit at $pkgunitdir (under DESTDIR)" "[ -f '$u2' ]"
check "package ExecStart = /usr/bin/openwitty-volumed (no DESTDIR)" "grep -qx 'ExecStart=/usr/bin/openwitty-volumed' '$u2'"
check "package binary staged" "[ -x '$tmp/stage/usr/bin/openwitty-volumed' ]"
check "package Documentation without DESTDIR" "grep -q 'Documentation=file:///usr/share/doc/openwitty/VOLUME_INTEGRATION.md' '$u2'"

[ $fail -eq 0 ] && echo "INSTALL STAGING: PASS" || echo "INSTALL STAGING: FAIL"
exit $fail
