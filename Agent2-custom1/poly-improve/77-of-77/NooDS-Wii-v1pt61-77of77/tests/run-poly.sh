#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${OUT:-"$ROOT/test-output"}
SD_IMAGE=${SD_IMAGE:-"$OUT/sd.raw"}
DOLPHIN=${DOLPHIN:-/usr/games/dolphin-emu-nogui}
MKFS_FAT=${MKFS_FAT:-}
MAX_FRAMES=${MAX_FRAMES:-2000}
DOLPHIN_TIMEOUT=${DOLPHIN_TIMEOUT:-900s}
TEST_JIT=${TEST_JIT:-0}
SKIP_BUILD=${SKIP_BUILD:-0}

if [ -z "$MKFS_FAT" ]; then
    MKFS_FAT=$(command -v mkfs.fat 2>/dev/null || true)
    if [ -z "$MKFS_FAT" ] && [ -x /usr/sbin/mkfs.fat ]; then
        MKFS_FAT=/usr/sbin/mkfs.fat
    fi
fi
[ -n "$MKFS_FAT" ] && [ -x "$MKFS_FAT" ] || {
    echo "mkfs.fat not found (set MKFS_FAT=...)" >&2
    exit 2
}

for tool in make dd mcopy timeout python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing required tool: $tool" >&2
        exit 2
    }
done
[ -x "$DOLPHIN" ] || {
    echo "Dolphin executable not found at $DOLPHIN (set DOLPHIN=...)" >&2
    exit 2
}

mkdir -p "$OUT"
case "$SKIP_BUILD" in
    0)
        make -C "$ROOT" poly-test-dol TEST_JIT="$TEST_JIT"
        ;;
    1)
        [ -s "$ROOT/NooDS-Wii-poly-test.dol" ] || {
            echo "No prebuilt test DOL found at $ROOT/NooDS-Wii-poly-test.dol" >&2
            exit 2
        }
        ;;
    *)
        echo "SKIP_BUILD must be 0 or 1" >&2
        exit 2
        ;;
esac

dd if=/dev/zero of="$SD_IMAGE" bs=1M count=128 status=none
"$MKFS_FAT" -F 32 "$SD_IMAGE" >/dev/null
mcopy -o -i "$SD_IMAGE" "$ROOT/tests/suite-poly.nds" ::suite-poly.nds
cat > "$OUT/autoboot.txt" <<EOF
sd:/suite-poly.nds
MAX_FRAMES=$MAX_FRAMES
CHECKPOINTS=500,600,700,800,1000,1200,1500,2000
INPUT_A_FROM=320
INPUT_A_TO=322
EOF
if [ -n "${DUMP_VRAM_FROM:-}" ]; then
    printf 'DUMP_VRAM_FROM=%s\n' "$DUMP_VRAM_FROM" >> "$OUT/autoboot.txt"
fi
if [ -n "${DUMP_VRAM_TO:-}" ]; then
    printf 'DUMP_VRAM_TO=%s\n' "$DUMP_VRAM_TO" >> "$OUT/autoboot.txt"
fi
mcopy -o -i "$SD_IMAGE" "$OUT/autoboot.txt" ::autoboot.txt

rm -f "$OUT/g2_state.log" "$OUT/mem_dump.bin" "$OUT/fb_dump.bin"
# Timeout is used because the test DOL intentionally remains in its UI after
# writing the final dump at MAX_FRAMES.
set +e
timeout --signal=TERM --kill-after=10s "$DOLPHIN_TIMEOUT" \
    "$DOLPHIN" --platform=headless --video_backend=Null \
    --user="$OUT/dolphin-profile" \
    --exec="$ROOT/NooDS-Wii-poly-test.dol" \
    -C Dolphin.Wii.WiiSDCard=True \
    -C Dolphin.Wii.WiiSDCardAllowWrites=True \
    -C Dolphin.General.WiiSDCardPath="$SD_IMAGE" \
    -C Dolphin.Core.EnableAudio=False \
    -C Dolphin.Interface.UsePanicHandlers=False
DOLPHIN_STATUS=$?
set -e

for file in g2_state.log mem_dump.bin fb_dump.bin vramA_frames.bin; do
    mcopy -o -i "$SD_IMAGE" "::$file" "$OUT/$file" 2>/dev/null || true
done
if [ ! -s "$OUT/mem_dump.bin" ]; then
    echo "No final RAM dump found (Dolphin status $DOLPHIN_STATUS; increase MAX_FRAMES or DOLPHIN_TIMEOUT)." >&2
    exit 1
fi

if [ "${REQUIRE_PASS:-0}" = 1 ]; then
    python3 "$ROOT/tests/decode_poly.py" "$OUT/mem_dump.bin" --scenes --require-pass
else
    python3 "$ROOT/tests/decode_poly.py" "$OUT/mem_dump.bin" --scenes
fi

echo "Dolphin exit status: $DOLPHIN_STATUS (timeout is expected if the UI stays open)."
echo "Artifacts: $OUT"
