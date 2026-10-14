#!/usr/bin/env bash
# makepkg is not available on the Ubuntu dev host. This emulates what makepkg does with
# packaging/arch/PKGBUILD (source extraction, build(), check(), package() with fake
# $srcdir/$pkgdir) so the build/install logic is exercised. It does NOT replace a real
# makepkg run / namcap on Arch. Everything happens in a mktemp dir; nothing is installed.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/lewitt-pkgemu.XXXXXX"); trap 'rm -rf "$work"' EXIT
bash -n "$root/packaging/arch/PKGBUILD"
# shellcheck disable=SC1091
source "$root/packaging/arch/PKGBUILD"
export srcdir=$work/src pkgdir=$work/pkg; mkdir -p "$srcdir" "$pkgdir"
# stand-in for `git archive HEAD`: tracked + untracked-not-ignored files of the working tree
( cd "$root" && git ls-files -co --exclude-standard -z | tar --null -T - --transform "s,^,$pkgname-$pkgver/," -czf "$work/$pkgname-$pkgver.tar.gz" )
tar -xzf "$work/$pkgname-$pkgver.tar.gz" -C "$srcdir"
for fn in build check package; do
    declare -F "$fn" >/dev/null || { echo "PKGBUILD lacks $fn()"; exit 1; }
    echo "== $fn()"; "$fn"
done
echo "== pkgdir contents"; (cd "$pkgdir" && find . -type f | sort | head -40)
test -x "$pkgdir/usr/bin/lewitt-control-center" && test -f "$pkgdir/usr/lib/udev/rules.d/70-lewitt-connect2.rules"
echo "PKGBUILD EMULATION: PASS"
