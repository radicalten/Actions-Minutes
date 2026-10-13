#!/usr/bin/env bash
set -euo pipefail

export PATH="$PATH:/usr/sbin:/sbin:/usr/games"

if [ $# -lt 3 ]; then
  echo "Usage: $0 DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL_REGEX]" >&2
  exit 2
fi

DOL="$(realpath "$1")"
SD="$(realpath "$2")"
SECS="$3"
OUT_DIR="${4:-$(mktemp -d /tmp/dolphin_run_XXXXXX)}"
SENTINEL="${5:-}"

mkdir -p "$OUT_DIR"
LOG_RAW="${OUT_DIR}/dolphin_raw.log"
LOG_CLEAN="${OUT_DIR}/dolphin.log"
RESULT="${OUT_DIR}/result.txt"
LOCK_FILE="/tmp/noods_dolphin.lock"

if [ ! -f "$DOL" ] || [ ! -f "$SD" ]; then
  cat > "$RESULT" <<EOF
STATUS=NOT_RUN
REASON=missing_dol_or_sd
DOL=$DOL
SD=$SD
EOF
  exit 1
fi

DOL_SHA_BEFORE="$(sha256sum "$DOL" | awk '{print $1}')"
SD_SHA_BEFORE="$(sha256sum "$SD" | awk '{print $1}')"

exec 9>"$LOCK_FILE"
flock -x 9

USERDIR="$(mktemp -d /tmp/dolphin_user_XXXXXX)"
cleanup() {
  rm -rf "$USERDIR"
}
trap cleanup EXIT

START_TS="$(date +%s)"
SENTINEL_HIT=0
RC=0

: > "$LOG_RAW"

dolphin-emu-nogui \
  -p headless \
  -u "$USERDIR" \
  -v Null \
  -a HLE \
  -C Dolphin.General.WiiSDCard=True \
  -C "Dolphin.General.WiiSDCardPath=$SD" \
  -C Dolphin.General.WiiSDCardAllowWrites=True \
  -C Dolphin.Core.CPUCore=1 \
  -C Dolphin.Core.CPUThread=False \
  -C Dolphin.Core.SyncGPU=True \
  -C Logger.Options.WriteToConsole=True \
  -C Logger.Options.Verbosity=4 \
  -C Logger.Logs.IOS_SD=True \
  -C Logger.Logs.BOOT=True \
  -C Logger.Logs.OSREPORT=True \
  -C Logger.Logs.OSREPORT_HLE=True \
  -C Logger.Logs.CORE=True \
  -e "$DOL" > "$LOG_RAW" 2>&1 &
DPID=$!

DEADLINE=$(( $(date +%s) + SECS ))
while kill -0 "$DPID" 2>/dev/null; do
  if [ -n "$SENTINEL" ]; then
    if tr -d '\000' < "$LOG_RAW" | grep -E -q "$SENTINEL"; then
      SENTINEL_HIT=1
      sleep 0.3
      break
    fi
  fi
  if [ "$(date +%s)" -ge "$DEADLINE" ]; then
    break
  fi
  sleep 0.2
done

if kill -0 "$DPID" 2>/dev/null; then
  kill -TERM "$DPID" 2>/dev/null || true
  sleep 0.2
  kill -TERM "$DPID" 2>/dev/null || true
  sleep 0.3
  kill -KILL "$DPID" 2>/dev/null || true
fi

set +e
wait "$DPID" 2>/dev/null
RC=$?
set -e

END_TS="$(date +%s)"
ELAPSED=$(( END_TS - START_TS ))

tr -d '\000' < "$LOG_RAW" | grep -v -E '^ALSA lib ' > "$LOG_CLEAN" || true
rm -f "$LOG_RAW"

DOL_SHA_AFTER="$(sha256sum "$DOL" | awk '{print $1}')"
SD_SHA_AFTER="$(sha256sum "$SD" | awk '{print $1}')"

BOOT_CNT="$(grep -c 'Booting from executable' "$LOG_CLEAN" || true)"
SD_OPEN_CNT="$(grep -c 'Opening /dev/sdio/slot0' "$LOG_CLEAN" || true)"
SD_INIT_CNT="$(grep -c 'SD card is inserted and initialized' "$LOG_CLEAN" || true)"
DMA_READ_CNT="$(grep -c 'DMA Read' "$LOG_CLEAN" || true)"
DMA_WRITE_CNT="$(grep -c 'DMA Write' "$LOG_CLEAN" || true)"

STATUS="HANG"
if [ -n "$SENTINEL" ]; then
  if [ "$SENTINEL_HIT" -eq 1 ] || grep -E -q "$SENTINEL" "$LOG_CLEAN"; then
    SENTINEL_HIT=1
    STATUS="PASS"
  else
    if [ "$ELAPSED" -ge "$SECS" ]; then
      STATUS="HANG"
    else
      STATUS="FAIL"
    fi
  fi
else
  if [ "$BOOT_CNT" -gt 0 ]; then
    STATUS="PASS"
  else
    STATUS="FAIL"
  fi
fi

cat > "$RESULT" <<EOF
STATUS=$STATUS
RC=$RC
ELAPSED_SECONDS=$ELAPSED
TIMEOUT_SECONDS=$SECS
SENTINEL=$SENTINEL
SENTINEL_HIT=$SENTINEL_HIT
DOL=$DOL
SD=$SD
DOL_SHA256_BEFORE=$DOL_SHA_BEFORE
DOL_SHA256_AFTER=$DOL_SHA_AFTER
SD_SHA256_BEFORE=$SD_SHA_BEFORE
SD_SHA256_AFTER=$SD_SHA_AFTER
BOOT_COUNT=$BOOT_CNT
SD_OPEN_COUNT=$SD_OPEN_CNT
SD_INIT_COUNT=$SD_INIT_CNT
DMA_READ_COUNT=$DMA_READ_CNT
DMA_WRITE_COUNT=$DMA_WRITE_CNT
EOF

cat "$RESULT"
if [ "$STATUS" != "PASS" ]; then
  exit 1
fi
