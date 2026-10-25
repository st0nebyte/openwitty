#!/bin/sh
# Every place that states the release version must agree with CMakeLists.txt (project version + suffix).
#   check_version_consistency.sh <source-dir> <LEWITT_VERSION> <LEWITT_VERSION_DEBIAN> <LEWITT_VERSION_ARCH> <openwitty-cli>
set -u
src=$1; v=$2; deb=$3; arch=$4; cli=$5
fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
check "PKGBUILD pkgver = $arch" "grep -qx 'pkgver=$arch' '$src/packaging/arch/PKGBUILD'"
check "Debian changelog top entry = $deb-1" "head -1 '$src/packaging/debian/changelog' | grep -qF 'openwitty ($deb-1)'"
check "AppStream newest release = $deb" "grep -m1 '<release ' '$src/packaging/linux/io.github.st0nebyte.openwitty.metainfo.xml' | grep -qF 'version=\"$deb\"'"
for m in openwitty openwitty-cli openwitty-tray openwitty-volumed; do
    check "man page $m.1 says openwitty $v" "head -1 '$src/packaging/man/$m.1' | grep -qF '\"openwitty $v\"'"
done
check "CHANGELOG.md has a $v section" "grep -q '^## $v ' '$src/CHANGELOG.md'"
check "openwitty-cli --version prints $v" "'$cli' --version | head -1 | grep -qF 'openwitty-cli $v '"
check "no hard-coded version in the GUI" "! grep -rnE 'OpenWitty [0-9]+\.[0-9]+\.[0-9]+' '$src/src/gui/qml'"
[ $fail -eq 0 ] && echo "VERSION CONSISTENCY: PASS" || echo "VERSION CONSISTENCY: FAIL"
exit $fail
