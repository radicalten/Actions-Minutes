#!/bin/bash
# Restore the v3 sources from the newest source snapshot, and make sure the
# toolchains are present.
#
#   tools/restore_v3.sh            restore + toolchains
#   tools/restore_v3.sh --verify   also rebuild and compare the DOL md5
#
# Why this exists: the workspace gets re-materialised from a snapshot that can
# predate the newest edits (twice now: jit3.cpp/jit3.h and the whole load/store
# slice disappeared, and armjit.cpp came back as an older revision).  Tar
# snapshots under /home/user/v3src-backup have survived both, so they are the
# authority.  **Refresh the tarball after every meaningful change.**
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BK=${BK:-/home/user/v3src-backup}
# Two artifacts, because both are live right now:
#   ba410c11...  v3-jit.dol          ARM-only (pre-Thumb), 1.65x, verified + timed
#   ddc221bb...  dols/v3-thumb-jit.dol  + Thumb (ARM+Thumb, self-test fail=0 both
#                block lengths, VERIFY bad=0 over 29.5M blocks, A/B 1.63-1.66x)
#   34bccfac...  dols/v3-thumb-regshift.dol  + Thumb register shifts (oracle 95/0/0)
#   39b8fe05...  dols/v3-mullong-arm7.dol    + ARM oracle: ARM7TDMI multiply C and
#                RSCS flags fixed (ARM 207/0/0, Thumb 95/0/0, VERIFY bad=0 over
#                29.58M blocks, A/B 1.63-1.66x, cycles identical to the interpreter)
EXPECT=${EXPECT:-39b8fe054c1f7d95cf3ce4efaad39116}   # v3-mullong-arm7.dol, current tree

t=$(ls -t "$BK"/v3-src-*.tar.gz 2>/dev/null | head -1)
if [ -z "$t" ]; then echo "no snapshot in $BK"; exit 1; fi
echo "restoring sources from $(basename "$t")"
tar xzf "$t" -C "$ROOT" || exit 1

need=0
[ -e /home/user/dkpopt/opt/devkitpro/devkitPPC/bin/powerpc-eabi-g++ ] || need=$((need|1))
[ -e "$HOME/.local/tc/powerpc-linux-musl-cross/bin/powerpc-linux-musl-g++" ] || need=$((need|2))
[ -e /home/user/dolphin/root/usr/games/dolphin-emu-nogui ] || need=$((need|4))
if [ $need -ne 0 ]; then
    echo "toolchains missing (mask $need) - fetching"
    bash "$ROOT/tools/agent_env.sh" host dkp dolphin || exit 1
fi

if [ "${1:-}" = "--verify" ]; then
    export DEVKITPRO=/home/user/dkpopt/opt/devkitpro TMPDIR=/home/user/.cache/tmp
    mkdir -p "$TMPDIR"
    rm -f "$ROOT/.jit-flag"
    ( cd "$ROOT" && make -j4 JIT=1 ) >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
    got=$(md5sum "$ROOT/NooDS-Wii.dol" | cut -d' ' -f1)
    if [ "$got" = "$EXPECT" ]; then echo "build matches the shipped DOL ($got)";
    else echo "build DIFFERS from the shipped DOL: got $got want $EXPECT"; exit 1; fi
fi
echo "restore_v3: done"
