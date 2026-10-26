#!/bin/bash
# Cross-compile the N64Recomp runtime (N64ModernRuntime: ultramodern + librecomp) for the
# Nintendo 3DS, ARMv6K. This is the "does the runtime build for the 3DS at all?" check, and
# it produces the object files a full `GAME=1` link needs.
#
#   ./build-runtime.sh [--jobs N] [--out DIR]
#
# It expects, next to this repository's parent directory:
#   deps/N64ModernRuntime   (git clone --recurse-submodules https://github.com/sp00nznet/N64ModernRuntime)
#   deps/N64Recomp          (git clone https://github.com/N64Recomp/N64Recomp)
# and devkitARM (DEVKITPRO/DEVKITARM set, e.g. `source /etc/profile.d/devkit-env.sh`).
#
# It does NOT modify the checkouts: the few source changes the port needs are applied to a
# fresh copy in a temp directory (see runtime-patches/README.md for the same edits).
set -euo pipefail

JOBS=$(nproc 2>/dev/null || echo 4)
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --jobs|-j) JOBS=$2; shift ;;
        --out|-o)  OUT=$2; shift ;;
        *) echo "usage: $0 [--jobs N] [--out DIR]" >&2; exit 2 ;;
    esac
    shift
done

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)                 # platform/3ds/tools -> platform/3ds
# deps/ normally sits next to the repository (the layout setup.ps1/setup.sh use).
# R3DS_DEPS overrides it.
deps=${R3DS_DEPS:-}
if [ -z "$deps" ]; then
    for cand in "$root/../../deps" "$root/../deps" "$root/deps"; do
        [ -d "$cand" ] && { deps=$cand; break; }
    done
fi
[ -n "$deps" ] || { echo "could not find deps/ (set R3DS_DEPS)" >&2; exit 1; }
RT=${N64RT:-$deps/N64ModernRuntime}
NR=${N64RECOMP:-$deps/N64Recomp}
OUT=${OUT:-$here/build-runtime}

[ -d "$RT/ultramodern" ] || { echo "N64ModernRuntime not found at $RT" >&2; exit 1; }
[ -d "$NR/include" ]     || { echo "N64Recomp not found at $NR" >&2; exit 1; }
: "${DEVKITARM:?set DEVKITARM (source /etc/profile.d/devkit-env.sh)}"
export PATH="$DEVKITARM/bin:$PATH"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
echo "copying runtime to $work (checkouts are left untouched)"
cp -a "$RT/." "$work/rt"
cp -a "$NR/." "$work/nr"

# --- the port's source changes -----------------------------------------------------------
python3 "$here/patch_runtime.py" --runtime "$work/rt" --n64recomp "$work/nr"

# --- compile ----------------------------------------------------------------------------
mkdir -p "$OUT" "$work/inc"
cat > "$work/inc/miniz_export.h" <<'EOF'
// stand-in for CMake's generated miniz_export.h (static library)
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
EOF

CXX=${DEVKITARM}/bin/arm-none-eabi-g++
COMMON=(-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -std=gnu++20
        -D__3DS__ -Wno-unused-parameter -ffunction-sections -fdata-sections)
INCS=(-I"$work/inc"
      -I"$work/rt/ultramodern/include" -I"$work/rt/thirdparty"
      -I"$work/rt/thirdparty/concurrentqueue" -I"$work/rt/thirdparty/sse2neon"
      -I"$work/rt/thirdparty/o1heap" -I"$work/rt/thirdparty/miniz"
      -I"$work/rt/thirdparty/xxHash" -I"$work/rt/thirdparty/json"
      -I"$work/rt/librecomp/include" -I"$work/rt/librecomp/include/librecomp"
      -I"$work/nr/include"
      # N64Recomp headers pull these in (rabbitizer is only needed for its enums on the host
      # side of librecomp; the 3DS build never recompiles MIPS at runtime).
      -I"$work/nr/lib/rabbitizer/cplusplus/include" -I"$work/nr/lib/rabbitizer/include"
      -I"$work/nr/lib/fmt/include" -I"$work/nr/lib/ELFIO" -I"$work/nr/lib/tomlplusplus/include")

compile_one() {
    local src=$1 out=$2
    "$CXX" "${COMMON[@]}" "${INCS[@]}" -c "$src" -o "$out" 2>"$out.log" \
        || { echo "FAIL $(basename "$src")"; grep -m3 -E "error:" "$out.log"; return 1; }
    rm -f "$out.log"
}

export -f compile_one
export CXX work OUT
export COMMON_STR="${COMMON[*]}"

sources=()
for f in "$work"/rt/ultramodern/src/*.cpp "$work"/rt/librecomp/src/*.cpp; do
    # mods.cpp is the native shared-library mod loader (dlopen/dlfcn.h + host arch checks).
    # The 3DS has no dynamic loader; a 3DS port ships mods statically or not at all.
    case "$(basename "$f")" in mods.cpp) echo "skipping mods.cpp (native mod loader, dlopen)"; continue ;; esac
    sources+=("$f")
done

echo "cross-compiling ${#sources[@]} runtime sources for ARMv6K, $JOBS at a time"
fail=0
for f in "${sources[@]}"; do
    base=$(basename "$f" .cpp)
    # ultramodern and librecomp both have rsp.cpp / extensions.cpp / config.cpp, so the
    # object name is prefixed with the library the source came from.
    case "$f" in
        */ultramodern/src/*) obj="ultramodern_$base.o" ;;
        *)                   obj="librecomp_$base.o" ;;
    esac
    if ! "$CXX" "${COMMON[@]}" "${INCS[@]}" -c "$f" -o "$OUT/$obj" 2>"$OUT/$obj.log"; then
        fail=$((fail+1)); echo "FAIL $base"; grep -m3 -E "error:" "$OUT/$obj.log"
    fi
    rm -f "$OUT/$obj.log"
done

ok=$(ls "$OUT"/*.o 2>/dev/null | wc -l)
echo
echo "compiled: $ok   failed: $fail"
if [ "$fail" -ne 0 ]; then
    echo "Some sources still fail - see the errors above and PORT-NOTES.md."
fi
"${DEVKITARM}/bin/arm-none-eabi-size" "$OUT"/*.o 2>/dev/null | tail -1
