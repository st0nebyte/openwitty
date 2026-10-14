#!/usr/bin/env bash
# Rootless install -> upgrade -> remove -> purge simulation of the .deb with dpkg operating on a
# private admin dir and instdir under a mktemp directory (dpkg --force-not-root). Maintainer
# scripts run with a STUB `udevadm` first in PATH that only logs, so the host is untouched.
#   check_deb_lifecycle.sh <package.deb>
set -u
deb=${1:?deb file}
T=$(mktemp -d "${TMPDIR:-/tmp}/lewitt-deb.XXXXXX"); trap 'rm -rf "$T"' EXIT
A=$T/root/var/lib/dpkg
mkdir -p "$A/info" "$A/updates" "$T/stub"
: > "$A/status"; : > "$A/available"
cat > "$T/stub/udevadm" <<STUB
#!/bin/sh
echo "udevadm \$*" >> "$T/udevadm.log"
STUB
chmod +x "$T/stub/udevadm"
D() { PATH="$T/stub:$PATH" dpkg --admindir="$A" --instdir="$T/root" --force-not-root \
        --force-bad-path --force-depends --force-script-chrootless --no-triggers --log="$T/dpkg.log" "$@" 2>&1; }
fail=0; ok(){ echo "  ok   $*"; }; bad(){ echo "  FAIL $*"; fail=1; }
D -i "$deb" >"$T/i1.out" || { cat "$T/i1.out"; bad "install"; }
[ -x "$T/root/usr/bin/openwitty" ] && ok "install: binary present" || bad "install: binary missing"
grep -q "reload-rules" "$T/udevadm.log" && ok "postinst reloads udev rules (stubbed)" || bad "postinst did not call udevadm"
grep -q "idVendor=29c2" "$T/udevadm.log" && ok "trigger scoped to vendor 29c2" || bad "trigger not vendor scoped"
D -i "$deb" >"$T/i2.out" && ok "upgrade/reinstall over itself" || { cat "$T/i2.out"; bad "reinstall"; }
D -r openwitty >"$T/r.out" && ok "remove" || { cat "$T/r.out"; bad "remove"; }
[ -e "$T/root/usr/bin/openwitty-cli" ] && bad "remove left binaries" || ok "remove deleted binaries"
[ -e "$T/root/usr/lib/udev/rules.d/70-openwitty.rules" ] && bad "remove left udev rule" || ok "remove deleted udev rule"
D -P openwitty >"$T/p.out" && ok "purge" || { cat "$T/p.out"; bad "purge"; }
left=$(cd "$T/root" && find . -type f ! -path "./var/lib/dpkg/*" 2>/dev/null | head -5)
[ -z "$left" ] && ok "no files left after purge" || bad "files left: $left"
# nothing outside the private root may have been touched: dpkg would have failed without root
[ $fail -eq 0 ] && echo "DEB LIFECYCLE: PASS" || echo "DEB LIFECYCLE: FAIL"
exit $fail
