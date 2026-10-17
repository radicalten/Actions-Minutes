#!/bin/bash
# One A/B run of a diagnostic .dol build.
#
#   tools/run_ab.sh <dol> <seconds> <out-prefix>
#
# Boots the .dol in headless Dolphin with a fresh SD card, lets it run for
# <seconds> of wall clock, then copies the two diagnostic files the build writes
# back out of the card image as <out-prefix>-stage.txt / <out-prefix>-report.txt
# (NUL bytes stripped: the run is SIGKILLed, so the last FAT cluster of an open
# file can be half-written).
#
# Notes that cost real time to rediscover:
#   * Dolphin is killed, not asked to quit - it has no shutdown path in headless
#     mode - so the SD image is always freshly created, never reused.
#   * the emulated SD card is only reachable through mtools (/home/user/dolphin/root/usr/bin/mcopy).
#   * reading a file that was never created makes mcopy exit non-zero, so a
#     missing jit3-report.txt is a real result, not an error to ignore.
set -u
DOL="$1"; SECS="$2"; OUT="$3"
D=/home/user/dolphin
ROM=${ROM:-/home/user/roms/rockwrestler.nds}
MCOPY=$D/root/usr/bin/mcopy
MFORMAT=$D/root/usr/bin/mformat

pkill -9 -f 'dolphin[-]emu' 2>/dev/null
sleep 1

# Dolphin's SD card is a raw image *mirrored* from WiiSDSync/ every time it
# starts (folder sync is on in its config), and the mirror runs before the guest
# sees the card.  So input files go in the folder, and only the outputs are read
# back out of the image - and the image itself is thrown away each run, because
# the process is SIGKILLed and never syncs back.  (Copying into the .raw before
# the run does not work, and pre-creating it does not either: Dolphin re-creates
# and re-formats it, discarding whatever was there.)
SYNC="$D/userdir/Load/WiiSDSync"
rm -f "$D/userdir/Load/WiiSD.raw"
rm -rf "$SYNC"
mkdir -p "$SYNC"
cp "$ROM" "$SYNC/rom.nds"
if [ "${TRACE:-0}" = "1" ]; then
    printf 'trace' > "$SYNC/jit3-trace"
fi
if [ "${VERIFY:-0}" = "1" ]; then
    printf 'verify' > "$SYNC/jit3-verify"
fi

if [ "${HITS:-0}" = "1" ]; then
    printf 'hits' > "$SYNC/jit3-chainhits"
fi
# BLOCKS=<n> asks the build to self-test with <n> guest instructions per dispatch
# instead of one, which is the only setting in which a conditional branch with a
# continuation, a fused BL or a chain appears at all.
if [ -n "${BLOCKS:-}" ]; then
    printf '%s' "$BLOCKS" > "$SYNC/jit3-blocks"
fi

( cd "$D" && yes 2>/dev/null | timeout -s KILL "$SECS" sh run_x.sh -p headless -v Null \
    -u "$D/userdir" -e "$DOL" ) >"$OUT-run.log" 2>&1
pkill -9 -f 'dolphin[-]emu' 2>/dev/null

for f in jit3-stage jit3-report jit3-verify; do
    rm -f "$OUT.tmp"
    if "$MCOPY" -i "$D/userdir/Load/WiiSD.raw" "::/$f.txt" "$OUT.tmp" >/dev/null 2>&1; then
        tr -d '\000' <"$OUT.tmp" >"$OUT-${f#jit3-}.txt"
        rm -f "$OUT.tmp"
    fi
done
echo "report: $(grep -c '^[0-9]' "$OUT-report.txt" 2>/dev/null || echo 0) rows"
