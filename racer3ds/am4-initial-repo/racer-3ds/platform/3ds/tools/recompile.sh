#!/bin/bash
# PC half of the pipeline: ROM -> recompiled C -> ARM objects for the 3DS.
#
#   ./recompile.sh --rom "/path/to/Star Wars Episode I - Racer (U) [!].z64"
#   ./recompile.sh --rom baserom.z64 --release      # also runs build-runtime.sh
#
# What it does, in order (each step prints what it produced):
#   1. checks the ROM's byte order and CRCs against game-config/recomp.toml
#   2. builds N64Recomp + RSPRecomp from source (cmake) if they are not already built
#   3. patches a copy of the runtime for ARMv6K/3DS (tools/patch_runtime.py)
#   4. runs N64Recomp over symbols.toml  -> game/RecompiledFuncs/*.c  (906 functions)
#   5. runs RSPRecomp over aspMain.toml   -> game/aspMain.cpp         (audio microcode)
#   6. cross-compiles both into game/build/*.o with devkitARM
#
# The last step that ties those objects to a playable binary is the 3DS game entry point
# (renderer/audio/input callbacks into recomp::start()); see BUILDING.md, "The game build".
# Everything this script produces is ROM-derived: do not commit it, do not share it.
set -euo pipefail

ROM=""
DEPS="${R3DS_DEPS:-}"
JOBS=$(nproc 2>/dev/null || echo 4)
RELEASE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rom)     ROM=$2; shift ;;
        --deps)    DEPS=$2; shift ;;
        --jobs|-j) JOBS=$2; shift ;;
        --release) RELEASE=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

here=$(cd "$(dirname "$0")" && pwd)
plat=$(cd "$here/.." && pwd)                  # platform/3ds
repo=$(cd "$plat/../.." && pwd)               # the deliverable root
game="$plat/game"
cfg="$repo/game-config"

say() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- 1. ROM
say "1/6  ROM"
[ -n "$ROM" ] || die "pass --rom <baserom.z64>"
[ -f "$ROM" ] || die "no such file: $ROM"
size=$(stat -c%s "$ROM")
[ "$size" = 33554432 ] || echo "warning: $ROM is $size bytes; Racer (U) [!] is 33554432"
magic=$(od -An -tx1 -N4 "$ROM" | tr -d ' \n')
case "$magic" in
    80371240) echo "     byte order: z64 (big endian) - correct" ;;
    37804012|40123780) die "byte order: $magic. The recompiler needs .z64.
       Either convert it once (the console does this itself: see TROUBLESHOOTING.md),
       or use the baserom.z64 the app writes to sdmc:/3ds/racer/." ;;
    *) die "not an N64 ROM (first four bytes: $magic)" ;;
esac
want_crc1=$(grep -m1 -o 'CRC1: 0x[0-9A-Fa-f]*' "$cfg/recomp.toml" | awk '{print $2}')
[ -n "$want_crc1" ] && echo "     expected $want_crc1 (game-config/recomp.toml)"

# ---------------------------------------------------------------- 2. tools
say "2/6  N64Recomp / RSPRecomp"
if [ -z "$DEPS" ]; then
    for cand in "$repo/deps" "$plat/../../deps" "$plat/../deps"; do
        [ -d "$cand/N64Recomp" ] && { DEPS=$(cd "$cand" && pwd); break; }
    done
fi
if [ -z "$DEPS" ]; then
    DEPS="$repo/deps"
    mkdir -p "$DEPS"
    [ -d "$DEPS/N64Recomp" ]         || git clone --depth 1 --recursive https://github.com/N64Recomp/N64Recomp "$DEPS/N64Recomp"
    [ -d "$DEPS/N64ModernRuntime" ]  || git clone --depth 1 --recursive https://github.com/sp00nznet/N64ModernRuntime "$DEPS/N64ModernRuntime"
fi
NR="$DEPS/N64Recomp"
RT="$DEPS/N64ModernRuntime"
[ -d "$NR/include" ] || die "N64Recomp not found in $DEPS"
[ -d "$RT/ultramodern" ] || die "N64ModernRuntime not found in $DEPS"
echo "     deps: $DEPS"

if [ ! -x "$NR/build/N64Recomp" ] || [ ! -x "$NR/build/RSPRecomp" ]; then
    command -v cmake >/dev/null || die "cmake is required to build N64Recomp (apt install cmake / pip install cmake)"
    gen=""
    command -v ninja >/dev/null && gen="-G Ninja"
    echo "     building N64Recomp (this takes a few minutes)"
    cmake -S "$NR" -B "$NR/build" $gen -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$NR/build" -j"$JOBS" >/dev/null
fi
echo "     $(basename "$(dirname "$NR")")/N64Recomp: $("$NR/build/N64Recomp" 2>&1 | head -1 || true)"

# ---------------------------------------------------------------- 3. patches
say "3/6  ARMv6K patches for the runtime and recomp.h"
python3 "$here/patch_runtime.py" --runtime "$RT" --n64recomp "$NR" | sed 's/^/     /'

# ---------------------------------------------------------------- 4. recompile
say "4/6  N64Recomp: ROM -> RecompiledFuncs/"
mkdir -p "$game"
cp "$cfg/recomp.toml" "$cfg/symbols.toml" "$game/"
cp "$ROM" "$game/baserom.z64"
( cd "$game" && "$NR/build/N64Recomp" recomp.toml ) | sed 's/^/     /'
funcs=$(ls "$game"/RecompiledFuncs/*.c | wc -l)
echo "     $funcs C files"

# ---------------------------------------------------------------- 5. rsp
say "5/6  RSPRecomp: aspMain -> C++"
cp "$cfg/rsp/aspMain.toml" "$game/"
# RSPRecomp cannot create an empty parent directory, so give it a real one.
sed -i 's|output_file_path = "aspMain.cpp"|output_file_path = "gen/aspMain.cpp"|' "$game/aspMain.toml"
sed -i 's|rom_file_path = "../baserom.z64"|rom_file_path = "baserom.z64"|' "$game/aspMain.toml"
( cd "$game" && mkdir -p gen && "$NR/build/RSPRecomp" aspMain.toml ) | sed 's/^/     /'
echo "     $(grep -c 'void aspMain' "$game"/gen/aspMain.cpp 2>/dev/null || echo 0) microcode entry point(s)"

# ---------------------------------------------------------------- 6. cross-compile
say "6/6  devkitARM: generated C -> ARMv6K objects"
: "${DEVKITARM:?set DEVKITARM (source /etc/profile.d/devkit-env.sh)}"
export PATH="$DEVKITARM/bin:$PATH"
mkdir -p "$game/build"
ok=0; fail=0
for f in "$game"/RecompiledFuncs/*.c; do
    o="$game/build/$(basename "$f" .c).o"
    if arm-none-eabi-gcc -c -O2 -march=armv6k -mtune=mpcore -mfloat-abi=hard -std=gnu11 \
        -ffunction-sections -fdata-sections -D__3DS__ \
        -I"$RT/ultramodern/include" -I"$RT/librecomp/include" -I"$NR/include" \
        -I"$game" -I"$game/RecompiledFuncs" \
        "$f" -o "$o" 2>"$game/build/err.txt"; then
        ok=$((ok+1))
    else
        fail=$((fail+1)); echo "     FAILED: $(basename "$f")"; head -5 "$game/build/err.txt"
    fi
done
arm-none-eabi-gcc -c -O2 -march=armv6k -fno-exceptions -fno-rtti -mfloat-abi=hard -std=gnu++20 \
    -D__3DS__ -I"$RT/ultramodern/include" -I"$RT/librecomp/include" -I"$NR/include" \
    "$game/gen/aspMain.cpp" -o "$game/build/aspMain.o" && echo "     aspMain.cpp -> aspMain.o"
echo "     compiled $ok/$((ok+fail)) generated files"

if [ "$RELEASE" = 1 ]; then
    say "release: runtime objects"
    "$here/build-runtime.sh" --jobs "$JOBS" | sed 's/^/     /'
fi

say "done"
cat <<EOF
     objects : $game/build/*.o
     recompile report: $game/recompile-report.txt (written below)

The last link step needs the 3DS game entry point - the platform callbacks that
recomp::start() takes (renderer on PICA200, audio into ndsp, HID into the pad). That is
game.cpp in BUILDING.md, "The game build". Until it lands, this script's output is the
recompiled game logic, verified to compile for ARMv6K.

Generated code is ROM-derived: keep it out of git and never redistribute it.
EOF
{ echo "Racer 3D - host recompilation report"
  echo "date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "rom         : $ROM ($size bytes)"
  echo "functions   : $funcs files from $(grep -c '\[\[section.functions\]\]' "$cfg/symbols.toml") symbol entries"
  echo "aspMain     : $game/gen/aspMain.cpp"
  echo "objects     : $ok compiled, $fail failed"
} > "$game/recompile-report.txt"
