#!/bin/sh
# Cross-compile the Thumb probe (and its reference model) for PowerPC and run it
# under qemu-ppc, exactly like run_tests_ppc.sh does for the ARM test.
#
#   run_thumb_probe.sh                     every format sweep (about 30 s)
#   run_thumb_probe.sh -one <hex4>         one halfword x 16 flag nibbles x 10 rotations
#   run_thumb_probe.sh -seq <hex4[,hex4]>  compile+run literal halfwords, dump the block
#   run_thumb_probe.sh -suite <file>       mGBA-suite vectors (gen_suite_vectors.py)
#   PPC_TC / PPC_QEMU / OUT                override paths
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TC=${PPC_TC:-$HOME/.local/tc/powerpc-linux-musl-cross/bin/powerpc-linux-musl-g++}
QEMU=${PPC_QEMU:-$HOME/.local/tc/quser/usr/bin/qemu-ppc}
OUT=${OUT:-/tmp/jit3_thumb_probe}

"$TC" -O2 -std=c++17 -static -Wall -I "$ROOT/NooDS-Wii/jit3" -I "$ROOT/tools/host" \
      -o "$OUT" "$ROOT/tools/host/thumb_probe.cpp" "$ROOT/NooDS-Wii/jit3/armjit.cpp"
exec "$QEMU" "$OUT" "$@"
