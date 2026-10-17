#!/bin/sh
# Cross-compile the jit3 differential test for PowerPC and run it under qemu-ppc.
#   PPC_TC / PPC_QEMU  override the toolchain and emulator paths.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TC=${PPC_TC:-$HOME/.local/tc/powerpc-linux-musl-cross/bin/powerpc-linux-musl-g++}
QEMU=${PPC_QEMU:-$HOME/.local/tc/quser/usr/bin/qemu-ppc}
OUT=${OUT:-/tmp/jit3_test_ppc}

# the emitter must keep building with the host toolchain too
${HOST_CXX:-g++} -std=c++17 -O1 -Wall -c -I "$ROOT/NooDS-Wii/jit3" \
    -o /tmp/jit3_armjit_host.o "$ROOT/NooDS-Wii/jit3/armjit.cpp"

"$TC" -O2 -std=c++17 -static -Wall -I "$ROOT/NooDS-Wii/jit3" -I "$ROOT/tools/host" \
      -o "$OUT" "$ROOT/tools/host/jit3_test.cpp" "$ROOT/NooDS-Wii/jit3/armjit.cpp"
exec "$QEMU" "$OUT" "$@"
