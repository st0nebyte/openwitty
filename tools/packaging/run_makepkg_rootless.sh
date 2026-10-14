#!/usr/bin/env bash
# Run the genuine Arch `makepkg` script on a non-Arch host, rootless, against
# packaging/arch/PKGBUILD. Everything lives in <repo>/.tools/arch (git-ignored).
#   * fetches the `pacman` package (contains the makepkg bash script + libmakepkg) from the
#     Arch mirror over TLS and checks its SHA256 against the mirror's core.db
#     (the db itself is NOT PGP-verified: provenance is TLS + hash only);
#   * uses Ubuntu's bsdtar (libarchive-tools, apt-get download, extracted - not installed);
#   * builds with --nodeps (host has no pacman database); no root, nothing installed.
# Result is an authentic-format .pkg.tar.zst built with the HOST toolchain, not an Arch chroot
# build; it does not replace `makepkg -s` + `namcap` on a real Arch/CachyOS system.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
T=$repo/.tools/arch; mkdir -p "$T/dl" "$T/root"
mirror=https://geo.mirror.pkgbuild.com
cd "$T/dl"
if ! ls pacman-*.pkg.tar.zst >/dev/null 2>&1; then
    name=$(curl -fsS "$mirror/core/os/x86_64/" | grep -oE 'pacman-[0-9][^"]*-x86_64\.pkg\.tar\.zst' | head -1)
    curl -fsSO "$mirror/core/os/x86_64/$name"
    curl -fsS "$mirror/core/os/x86_64/core.db" -o core.db
    want=$(tar -xOf core.db --wildcards 'pacman-[0-9]*/desc' | awk '/%SHA256SUM%/{getline; print; exit}')
    have=$(sha256sum "$name" | cut -d' ' -f1)
    [ "$want" = "$have" ] || { echo "SHA256 mismatch for $name"; rm -f "$name"; exit 1; }
fi
[ -e "$T/root/usr/bin/makepkg" ] || tar --zstd -xf pacman-*.pkg.tar.zst -C "$T/root"
if [ ! -x "$T/root/ubuntu/usr/bin/bsdtar" ]; then
    command -v bsdtar >/dev/null || { apt-get download libarchive-tools >/dev/null && dpkg-deb -x libarchive-tools_*.deb "$T/root/ubuntu"; }
fi
W=$T/work; rm -rf "$W"; mkdir -p "$W/pkg" "$W/build"
cp "$repo/packaging/arch/PKGBUILD" "$W/build/"
( cd "$repo" && git ls-files -co --exclude-standard -z | tar --null -T - \
      --transform "s,^,openwitty-0.1.0/," -czf "$W/build/openwitty-0.1.0.tar.gz" )
cp "$T/root/etc/makepkg.conf" "$W/makepkg.conf"
printf 'PKGDEST=%s\nBUILDDIR=%s\n' "$W/pkg" "$W/bd" >> "$W/makepkg.conf"
export MAKEPKG_LIBRARY=$T/root/usr/share/makepkg
export PATH=$T/root/ubuntu/usr/bin:$T/root/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/root/ubuntu/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}
# Project-local rootless Qt sysroot when present (see tools/setup-sysroot.sh)
if [ -d "$repo/.sysroot/usr" ]; then
    export LD_LIBRARY_PATH=$repo/.sysroot/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
    export PKG_CONFIG_PATH=$repo/.sysroot/usr/lib/x86_64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=$repo/.sysroot
    export CMAKE_PREFIX_PATH=$repo/.sysroot/usr QML_IMPORT_PATH=$repo/.sysroot/usr/lib/x86_64-linux-gnu/qt6/qml
fi
cd "$W/build"
makepkg --config "$W/makepkg.conf" --nodeps --force --nosign --noprogressbar
ls -l "$W/pkg"
