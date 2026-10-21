#!/bin/bash
# Run a Wii .dol under Dolphin headless (Xvfb + Null backend = full unthrottled speed).
# Exits automatically as soon as guest emits [NOODS] MAX_FRAMES_DONE (after SD flush).
# Usage: run-dolphin.sh <dol> <sd.img> <userdir> <max_seconds> [logfile] [backend]
set -e
DOL=$1; SDIMG=$2; USERDIR=$3; MAXSEC=${4:-300}; LOG=${5:-/home/user/test/dolphin-run.log}; BACKEND=${6:-Null}

export LC_ALL=C.UTF-8 LANG=C.UTF-8
rm -rf "$USERDIR"; mkdir -p "$USERDIR/Config"

cat > "$USERDIR/Config/Dolphin.ini" <<'EOF'
[Analytics]
Enabled = False
PermissionAsked = True

[Core]
WiiSDCard = True
WiiSDCardAllowWrites = True
EOF

cat > "$USERDIR/Config/Logger.ini" <<'EOF'
[Options]
Verbosity = 5
WriteToFile = True
WriteToConsole = True

[Logs]
OSReport = True
Core = True
EOF

export DISPLAY=:96
Xvfb :96 -screen 0 640x480x24 +extension GLX +render -dpi 96 > /dev/null 2>&1 &
XVFBPID=$!
sleep 1

dolphin-emu --batch \
    -u "$USERDIR" \
    --video_backend="$BACKEND" \
    --config=Dolphin.Core.WiiSDCard=True \
    --config=Dolphin.Core.WiiSDCardAllowWrites=True \
    --config=Dolphin.Core.EmulationSpeed=0 \
    --config=Dolphin.General.WiiSDCardPath="$SDIMG" \
    -e "$DOL" > "$LOG" 2>&1 &
DOLPID=$!

ELAPSED=0
while kill -0 "$DOLPID" 2>/dev/null; do
    if grep -aqE "\[NOODS\] MAX_FRAMES_DONE|\[MGBA\] ALL DONE" "$LOG" 2>/dev/null; then
        sleep 1
        kill -9 "$DOLPID" 2>/dev/null || true
        break
    fi
    if [ "$ELAPSED" -ge "$MAXSEC" ]; then
        echo "TIMEOUT after ${MAXSEC}s"
        kill -9 "$DOLPID" 2>/dev/null || true
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done
wait "$DOLPID" 2>/dev/null || true
kill "$XVFBPID" 2>/dev/null || true
wait "$XVFBPID" 2>/dev/null || true
echo "Finished in ${ELAPSED}s"
