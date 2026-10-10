#!/usr/bin/env bash
set -euo pipefail

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH"

if [ $# -lt 6 ]; then
  echo "Usage: $0 DOL LABEL ROM_PATH CHECKPOINTS MAX_FRAMES TIMEOUT_SECS [POOL_LIMIT_WORDS]" >&2
  exit 2
fi

DOL="$(realpath "$1")"
LABEL="$2"
ROM_PATH="$3"
CHECKPOINTS="$4"
MAX_FRAMES="$5"
TIMEOUT_SECS="$6"
POOL_LIMIT="${7:-0}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EVID_DIR="${REPO_ROOT}/evidence"
RUN_DIR="${EVID_DIR}/g2_${LABEL}"
SD_IMG="${RUN_DIR}/sd.img"
AUTOBOOT_TMP="${RUN_DIR}/autoboot.txt"

rm -rf "$RUN_DIR"
mkdir -p "$RUN_DIR"

bash "$REPO_ROOT/tools/build_sd_image.sh" "$SD_IMG" >/dev/null

cat > "$AUTOBOOT_TMP" <<EOF
${ROM_PATH}
CHECKPOINTS=${CHECKPOINTS}
MAX_FRAMES=${MAX_FRAMES}
POOL_LIMIT_WORDS=${POOL_LIMIT}
EOF

mcopy -o -i "$SD_IMG" "$AUTOBOOT_TMP" ::/autoboot.txt
mcopy -o -i "$SD_IMG" "$AUTOBOOT_TMP" ::/noods/autoboot.txt

set +e
bash "$REPO_ROOT/tools/run_dolphin.sh" "$DOL" "$SD_IMG" "$TIMEOUT_SECS" "$RUN_DIR" "MAX_FRAMES_DONE"
RC=$?
set -e

mcopy -o -i "$SD_IMG" ::/g2_state.log "${RUN_DIR}/g2_state.log" 2>/dev/null || true
mcopy -o -i "$SD_IMG" ::/mem_dump.bin "${RUN_DIR}/mem_dump.bin" 2>/dev/null || true
mcopy -o -i "$SD_IMG" ::/fb_dump.bin  "${RUN_DIR}/fb_dump.bin"  2>/dev/null || true

rm -f "$SD_IMG"

if [ ! -s "${RUN_DIR}/g2_state.log" ] || [ ! -s "${RUN_DIR}/mem_dump.bin" ] || [ ! -s "${RUN_DIR}/fb_dump.bin" ]; then
  echo "ERROR: missing extracted G2 artifacts for ${LABEL}" >&2
  exit 1
fi

echo "G2_CASE=${LABEL} RC=${RC} STATE=$(wc -l < "${RUN_DIR}/g2_state.log")_lines MEM=$(stat -c%s "${RUN_DIR}/mem_dump.bin") FB=$(stat -c%s "${RUN_DIR}/fb_dump.bin")"
