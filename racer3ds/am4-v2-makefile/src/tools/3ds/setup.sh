#!/usr/bin/env bash
# =============================================================================
# Star Wars Episode I: Racer (3DS) - host tool build.
#
# Everything 3DS-specific is already vendored and patched in lib/ (this repo
# ships no .patch files and needs no network access): this script only builds
# the two HOST-side recompilers from the vendored N64Recomp tree:
#
#   lib/N64ModernRuntime/N64Recomp/build/N64Recomp   (ROM -> recompiled C)
#   lib/N64ModernRuntime/N64Recomp/build/RSPRecomp   (audio ucode -> C++)
#
# The ARM-side game build is a separate CMake step (see BUILDING.md) and needs
# devkitPro's 3DS package group (libctru, citro3d, picasso, makerom, ...).
#
#   ./tools/3ds/setup.sh [--jobs N]
# =============================================================================
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
nr="$repo/lib/N64ModernRuntime/N64Recomp"
JOBS=$(nproc 2>/dev/null || echo 4)

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs|-j) JOBS=$2; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
for tool in cmake python3; do
    command -v "$tool" >/dev/null || die "$tool is required (on Debian/Ubuntu: apt install cmake python3)"
done
[ -f "$nr/CMakeLists.txt" ] || die "vendored N64Recomp not found at $nr"

say "Building host tools N64Recomp + RSPRecomp"
gen=""
command -v ninja >/dev/null && gen="-G Ninja"
cmake -S "$nr" -B "$nr/build" $gen -DCMAKE_BUILD_TYPE=Release
cmake --build "$nr/build" -j"$JOBS" --target N64Recomp RSPRecomp

say "devkitPro check (ARM game build only)"
if [ -z "${DEVKITPRO:-}" ]; then
    cat <<'EOF'
  WARNING: DEVKITPRO is not set. The host tools are ready, but the ARM build
  needs devkitPro (libctru, citro3d, picasso, makerom):
    https://devkitpro.org/wiki/Getting_Started  (install: 3ds-dev 3ds-zlib)
  then:  source /etc/profile.d/devkit-env.sh
EOF
else
    echo "  DEVKITPRO=$DEVKITPRO"
    for t in arm-none-eabi-g++ picasso makerom bannertool; do
        if ! command -v "$t" >/dev/null 2>&1 && [ ! -x "$DEVKITPRO/tools/bin/$t" ]; then
            echo "  WARNING: $t not found (install the 3ds-dev package group)"
        fi
    done
fi

cat <<EOF

Done. Next, with your legally-owned US baserom.z64 (NEPE, CRC1 0x72F70398):

  ./tools/3ds/recompile.sh --rom /path/to/baserom.z64
  cmake -S platform/3ds -B build-3ds -G Ninja \\
        -DCMAKE_TOOLCHAIN_FILE=lib/rt64-3ds/cmake/devkitARM-3ds.cmake
  cmake --build build-3ds
EOF
