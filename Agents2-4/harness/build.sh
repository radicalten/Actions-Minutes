#!/usr/bin/env bash
# Host build of the NooDS-Wii emulation core for big-endian PowerPC Linux.
# Usage: ./build.sh <interp|jit> [outdir]
set -eo pipefail
EXTRA_DEFS="${EXTRA_DEFS:-}"
MODE="${1:-interp}"
SR="${SRC:-/home/user/repo/NooDS-Wii/NooDS-Wii}"
OUT="${2:-bin_$MODE}"
CXX=powerpc-linux-gnu-g++
# Mirror NooDS-Wii/Makefile MACHDEP + CXXFLAGS for the Wii target (ENDIAN_BIG is
# load-bearing: U8TO32/U8TO16 byte-swap semantics depend on it).
FLAGS="-std=c++17 -O2 -g -DENDIAN_BIG -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions -mcpu=750 -meabi -mhard-float"
JITDEF="-DNOODS_JIT=0 -DNOODS_JIT_BLOCKS=0"
if [ "$MODE" = "jit" ]; then JITDEF="-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=1"; fi
if [ "$MODE" = "jit_nb" ]; then JITDEF="-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=0"; fi
FILES="arm_jit core interpreter interpreter_alu interpreter_branch interpreter_transfer \
memory gpu gpu_2d gpu_3d gpu_3d_renderer spu timers dma ipc rtc spi cp15 div_sqrt \
cartridge dldi hle_bios hle_arm7 nds_icon save_states settings action_replay screen_layout wifi input interpreter_lookup"
mkdir -p "$OUT/obj"
for f in $FILES; do
  o="$OUT/obj/$f.o"
  if [ ! -f "$o" ] || [ "$SR/$f.cpp" -nt "$o" ]; then
    $CXX $FLAGS -include shims/gccore.h $JITDEF $EXTRA_DEFS -c "$SR/$f.cpp" -o "$o" -I shims -I "$SR"
  fi
done
$CXX $FLAGS -include shims/gccore.h $JITDEF $EXTRA_DEFS -c src/harness_main.cpp -o "$OUT/obj/harness_main.o" -I shims -I "$SR"
$CXX -c "$SR/jit_bridge.S" -o "$OUT/obj/jit_bridge.o" -mbig-endian
$CXX $FLAGS -c shims/shims.cpp -o "$OUT/obj/shims.o" -I shims
$CXX "$OUT/obj"/*.o -o "$OUT/noods_host" -static -lpthread -lm
echo "BUILT $OUT/noods_host"
