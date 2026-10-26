#!/bin/bash
# Headless Dolphin runner for NooDS-Wii regression/benchmark builds.
#   run_test.sh <dol> <rom-in-sd> <max_frames> <checkpoints-csv> <tag> [timeout_s]
# Artifacts land in /home/user/results/<tag>.*
set -u
DOL=$1; ROM=$2; FRAMES=$3; CPS=$4; TAG=$5; TMO=${6:-300}
SD=/home/user/sd
RES=/home/user/results
IMG=/home/user/dolphin/Load/WiiSD.raw
mkdir -p "$RES"
LOG=$RES/$TAG.rawlog
rm -f "$LOG" "$RES/$TAG".{log,state.log,fb.bin,mem.bin,png}

cat > "$SD/autoboot.txt" <<EOF
$ROM
MAX_FRAMES=$FRAMES
CHECKPOINTS=$CPS
EOF

# pinned benchmark settings: frameskip=0 always, single-threaded renderer
# (threaded2D/3D deadlock under headless Dolphin on this host)
cat > "$SD/noods.ini" <<'EOF'
frameskip=0
fpsLimiter=0
threaded2D=0
threaded3D=0
emulateAudio=1
armJit=1
directBoot=1
arm7Hle=0
EOF

timeout "$TMO" xvfb-run -a -s "-screen 0 640x480x24" \
  /usr/games/dolphin-emu-nogui -u /home/user/dolphin -v Software -e "$DOL" \
  > "$LOG" 2>&1 &
WPID=$!

for i in $(seq 1 $((TMO*4))); do
  if grep -qa "MAX_FRAMES_DONE" "$LOG"; then sleep 1; break; fi
  if ! kill -0 $WPID 2>/dev/null; then break; fi
  sleep 0.25
done
kill -TERM $WPID 2>/dev/null
wait $WPID 2>/dev/null

OUT=$RES/$TAG.log
strings "$LOG" | grep -E "AUTOBOOT|CHECKPOINT frame|MAX_FRAMES_DONE" > "$OUT"
export MTOOLS_SKIP_CHECK=1
mcopy -n -i "$IMG" ::/g2_state.log "$RES/$TAG.state.log" 2>/dev/null
mcopy -n -i "$IMG" ::/fb_dump.bin   "$RES/$TAG.fb.bin"   2>/dev/null
mcopy -n -i "$IMG" ::/mem_dump.bin  "$RES/$TAG.mem.bin"  2>/dev/null
echo "--- $TAG ---"
cat "$OUT"
[ -f "$RES/$TAG.fb.bin" ] && python3 /home/user/tools/fb2png.py "$RES/$TAG.fb.bin" "$RES/$TAG.png" 2 >/dev/null && echo "png: $RES/$TAG.png"
