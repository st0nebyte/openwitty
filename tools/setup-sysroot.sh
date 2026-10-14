#!/usr/bin/env bash
# Root-free C++/Qt build dependencies: download Ubuntu .debs and unpack into ./.sysroot.
# Runtime libs already present on the system are reused (symlinked); see DEVELOPMENT.md.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .sysroot .tools/dl/debs
apt-get install --print-uris -y qt6-base-dev qt6-declarative-dev qml6-module-qtquick \
  qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript qml6-module-qtquick-templates libusb-1.0-0-dev libgtest-dev \
  libasound2-dev ninja-build 2>/dev/null | grep -E "^'http" | awk '{print $1}' | tr -d "'" |
  (cd .tools/dl/debs && xargs -n1 -P8 wget -q -nc)
for d in .tools/dl/debs/*.deb; do dpkg -x "$d" .sysroot; done
# dev symlinks that point at libs the system already provides
lib=.sysroot/usr/lib/x86_64-linux-gnu
for f in /usr/lib/x86_64-linux-gnu/libQt6*.so.6.*; do [ -e "$lib/$(basename "$f")" ] || ln -s "$f" "$lib/"; done
for l in "$lib"/libQt6*.so; do [ -e "$l" ] || ln -sf "/usr/lib/x86_64-linux-gnu/$(basename "${l%.so}").so.6" "$l"; done
ln -sf /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0 "$lib/libusb-1.0.so"; rm -f "$lib/libusb-1.0.a"
cat <<MSG
Build with:
  export PKG_CONFIG_PATH=\$PWD/.sysroot/usr/lib/x86_64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=\$PWD/.sysroot
  export LD_LIBRARY_PATH=\$PWD/.sysroot/usr/lib/x86_64-linux-gnu QML_IMPORT_PATH=\$PWD/.sysroot/usr/lib/x86_64-linux-gnu/qt6/qml
  cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=\$PWD/.sysroot/usr && cmake --build build
MSG
