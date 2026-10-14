#!/usr/bin/env bash
# Packaging regression check: install into an isolated DESTDIR and verify the result.
#   check_staging.sh <build-dir> [<source-dir>]
# Nothing is installed on the host; everything happens under a mktemp directory.
# Optional tools (desktop-file-validate, appstreamcli, xmllint, readelf) are used when
# present and reported as SKIPPED otherwise. Exit 0 = all executed checks passed.
set -u
build=${1:?build dir}; src=${2:-$(cd "$(dirname "$0")/../.." && pwd)}
stage=$(mktemp -d "${TMPDIR:-/tmp}/lewitt-stage.XXXXXX"); trap 'rm -rf "$stage"' EXIT
fail=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fail=1; }
skip() { echo "  skip $*"; }
have() { command -v "$1" >/dev/null 2>&1; }

echo "== install to $stage"
DESTDIR=$stage cmake --install "$build" --prefix /usr >/dev/null || { echo "install failed"; exit 1; }
P=$stage/usr

echo "== file layout"
need=(bin/lewitt-cli bin/lewitt-control-center
      share/applications/lewitt-control-center.desktop
      share/metainfo/lewitt-control-center.metainfo.xml
      share/icons/hicolor/scalable/apps/lewitt-control-center.svg
      share/icons/hicolor/48x48/apps/lewitt-control-center.png
      share/icons/hicolor/256x256/apps/lewitt-control-center.png
      share/doc/lewitt-connect2-unofficial/README.md
      share/doc/lewitt-connect2-unofficial/INSTALLATION.md
      share/doc/lewitt-connect2-unofficial/USB_PERMISSIONS.md
      share/doc/lewitt-connect2-unofficial/copyright)
for f in "${need[@]}"; do [ -e "$P/$f" ] && ok "$f" || bad "missing $f"; done
if grep -q 'LEWITT_HAVE_USB' "$build/CMakeCache.txt" 2>/dev/null || grep -q 'LEWITT_ENABLE_USB:BOOL=ON' "$build/CMakeCache.txt"; then
    [ -e "$P/lib/udev/rules.d/70-lewitt-connect2.rules" ] && ok "udev rule (70-)" || bad "udev rule missing"
fi
# nothing outside the prefix, nothing proprietary
outside=$(cd "$stage" && find . -mindepth 1 -maxdepth 1 ! -name usr)
[ -z "$outside" ] && ok "only /usr populated" || bad "files outside /usr: $outside"
if find "$stage" \( -iname '*.exe' -o -iname '*.dmg' -o -iname '*.pkg' -o -iname '*.dll' -o -iname '*.pcap*' \) | grep -q .; then
    bad "proprietary-looking artefact installed"; else ok "no installers/captures"; fi

echo "== runtime independence"
for b in "$P/bin/lewitt-cli" "$P/bin/lewitt-control-center"; do
    n=$(basename "$b")
    if have readelf; then
        if readelf -d "$b" | grep -Eq 'RPATH|RUNPATH'; then bad "$n has RPATH/RUNPATH: $(readelf -d "$b" | grep -E 'RPATH|RUNPATH')"
        else ok "$n: no RPATH/RUNPATH"; fi
        if readelf -d "$b" | grep NEEDED | grep -Eq '\.sysroot|\.tools'; then bad "$n links dev libs"; else ok "$n: NEEDED are system sonames"; fi
    else skip "$n RPATH (no readelf)"; fi
    if strings -a "$b" 2>/dev/null | grep -Eq '/\.sysroot|/\.tools|/\.venv|/home/[a-z]|/tmp/lewitt'; then
        bad "$n embeds a development path: $(strings -a "$b" | grep -E '/\.sysroot|/\.tools|/\.venv|/home/[a-z]|/tmp/lewitt' | head -2)"
    else ok "$n: no dev paths embedded"; fi
    if strings -a "$b" | grep -Eiq 'python3|node_modules|ghidra|wine'; then
        # Only flag references that would be executed (heuristic: exec-ish words)
        strings -a "$b" | grep -Ei 'python3 |/usr/bin/python|node_modules|ghidra_|/wine' | head -1 | grep -q . && bad "$n references a dev tool"
    fi
done

echo "== launch (isolated, no hardware)"
export QT_QPA_PLATFORM=offscreen HOME=$stage/home; mkdir -p "$HOME"
v=$("$P/bin/lewitt-cli" --version 2>&1) && ok "lewitt-cli --version: $v" || bad "lewitt-cli --version"
"$P/bin/lewitt-control-center" --version >/dev/null 2>&1 && ok "gui --version" || bad "gui --version"
if "$P/bin/lewitt-cli" read settings --mock >/dev/null 2>&1; then ok "cli mock read"; else bad "cli mock read"; fi
shot=$stage/shot.png
if timeout 60 "$P/bin/lewitt-control-center" --mock --screenshot "$shot" >"$stage/gui.log" 2>&1 && [ -s "$shot" ]; then
    ok "gui --mock started, rendered and exited cleanly"
else bad "gui mock start failed: $(tail -3 "$stage/gui.log")"; fi

echo "== desktop metadata"
if have desktop-file-validate; then
    desktop-file-validate "$P/share/applications/lewitt-control-center.desktop" && ok "desktop-file-validate" || bad "desktop-file-validate"
else skip "desktop-file-validate"; fi
if have appstreamcli; then
    out=$(appstreamcli validate --no-net "$P/share/metainfo/lewitt-control-center.metainfo.xml" 2>&1)
    if echo "$out" | grep -Eq '^E:'; then bad "appstream errors: $out"; else ok "appstreamcli (no errors)"; fi
else skip "appstreamcli"; fi
if have xmllint; then
    xmllint --noout "$P/share/metainfo/lewitt-control-center.metainfo.xml" "$P/share/icons/hicolor/scalable/apps/lewitt-control-center.svg" \
        && ok "xmllint" || bad "xmllint"
else skip "xmllint"; fi
icon=$(sed -n 's/^Icon=//p' "$P/share/applications/lewitt-control-center.desktop")
[ -e "$P/share/icons/hicolor/scalable/apps/$icon.svg" ] && ok "Icon=$icon resolves" || bad "Icon=$icon missing"
ex=$(sed -n 's/^Exec=\([^ ]*\).*/\1/p' "$P/share/applications/lewitt-control-center.desktop" | head -1)
[ -x "$P/bin/$ex" ] && ok "Exec=$ex resolves" || bad "Exec=$ex missing"
grep -q 'unofficial' "$P/share/applications/lewitt-control-center.desktop" && ok "desktop name says unofficial" || bad "desktop name lacks 'unofficial'"

echo "== udev rule"
r=$src/packaging/udev/70-lewitt-connect2.rules
grep -q 'TAG+="uaccess"' "$r" && ok "uses uaccess" || bad "no uaccess tag"
grep -Eq 'MODE="0?666"|GROUP="plugdev"' "$r" && bad "rule grants world/group access" || ok "no MODE=0666 / group grants"
grep -q '29c2' "$r" && ok "vendor-scoped" || bad "not vendor scoped"
grep -Eq 'idProduct' "$r" && ok "product scoped" || echo "  note rule is vendor-wide (no idProduct filter)"

[ $fail -eq 0 ] && echo "PACKAGING CHECK: PASS" || echo "PACKAGING CHECK: FAIL"
exit $fail
