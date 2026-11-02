#!/usr/bin/env bash
# Create the source tarball the PKGBUILD expects, from the committed tree of this repo
# (git archive: honours .gitignore'd research/.tools/.sysroot by construction - they are
# not tracked, so no proprietary file can end up in the package).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(git -C "$here" rev-parse --show-toplevel)
name=openwitty
# Arch version = CMake project version + pre-release label without dots (0.1.0 + beta.1 -> 0.1.0beta1; no label: 0.1.0),
# the same rule as LEWITT_VERSION_ARCH in CMakeLists.txt; must equal the PKGBUILD's pkgver.
base=$(sed -n 's/^project(openwitty VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
suffix=$(sed -n 's/^set(LEWITT_VERSION_SUFFIX "\(.*\)")/\1/p' "$root/CMakeLists.txt" | tr -d .)
ver=$base$suffix
[ -n "$ver" ] || { echo "cannot determine version" >&2; exit 1; }
out="$here/$name-$ver.tar.gz"
git -C "$root" archive --format=tar.gz --prefix="$name-$ver/" -o "$out" HEAD
echo "$out"
