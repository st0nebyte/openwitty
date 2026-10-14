#!/usr/bin/env bash
# Reproducible, root-free toolchain bootstrap. Everything lands in ./.tools and ./.sysroot
# (both git-ignored). Official sources only; checksums verified where published.
set -euo pipefail
cd "$(dirname "$0")/.."
T=$PWD/.tools; mkdir -p "$T/dl" .sysroot; cd "$T/dl"
fetch() { [ -f "$2" ] || curl -fsSL -o "$2" "$1"; echo "$3  $2" | sha256sum -c -; }

# 1) Node 24 LTS (REA supports 22.19+/24.11+/26+, not 25)
curl -fsSL -O https://nodejs.org/dist/v24.21.0/SHASUMS256.txt
[ -f node-v24.21.0-linux-x64.tar.xz ] || curl -fsSL -O https://nodejs.org/dist/v24.21.0/node-v24.21.0-linux-x64.tar.xz
grep node-v24.21.0-linux-x64.tar.xz SHASUMS256.txt | sha256sum -c -
tar -C "$T" -xf node-v24.21.0-linux-x64.tar.xz

# 2) JDK 21 (Ghidra 12.1 requirement)
fetch 'https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz' \
      OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz ce79869e1307ed8ee1e2baa86a412b1eb5b75d10a01006d788a6f968bcfaee94
tar -C "$T" -xf OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz

# 3) Ghidra 12.1.4
fetch https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip \
      ghidra_12.1.4_PUBLIC_20260921.zip ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db
unzip -q -o ghidra_12.1.4_PUBLIC_20260921.zip -d "$T"

# 4) innoextract from upstream master (Inno Setup 6.3 support; 1.9 is too old). Needs boost+xz headers.
fetch https://github.com/dscharrer/innoextract/archive/6e9e34ed0876014fdb46e684103ef8c3605e382e.tar.gz \
      ../innoextract-src.tar.gz 4d885b27e452dcb137a9d97736b19478447f172bcfed1aa98d80138e9a4c08ae
mkdir -p "$T/ie-src" && tar -xf ../innoextract-src.tar.gz -C "$T/ie-src" --strip-components=1
cmake -S "$T/ie-src" -B "$T/ie-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$T/ie-build"

# 5) REA (analysis tool only; never a runtime dependency). --ignore-scripts, version pinned.
export PATH=$T/node-v24.21.0-linux-x64/bin:$PATH
mkdir -p "$T/rea" && cd "$T/rea" && { [ -f package.json ] || npm init -y >/dev/null; }
npm install --ignore-scripts --no-audit --no-fund rea-agents@6.4.0

cat > "$T/rea-env.sh" <<ENV
export PATH=$T/node-v24.21.0-linux-x64/bin:$T/rea/node_modules/.bin:\$PATH
export GHIDRA_INSTALL_DIR=$T/ghidra_12.1.4_PUBLIC
export JAVA_HOME=$T/jdk-21.0.12.1+1
export REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000
ENV
echo "done. source .tools/rea-env.sh && rea doctor --json"
