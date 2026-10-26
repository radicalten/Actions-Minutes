#!/bin/bash
# wolveslayer with the player walking right + A (needs G2_SCRIPTED_INPUT=1 build)
DOL=$1; TAG=$2; FR=${3:-1500}; TMO=${4:-400}
SD=/home/user/sd; RES=/home/user/results
rm -f "$SD"/g2_state.log
cat > "$SD/autoboot.txt" <<EOT
sd:/roms/wolveslayer.nds
MAX_FRAMES=$FR
CHECKPOINTS=$FR
INPUT_RIGHT_FROM=1
INPUT_RIGHT_TO=999999
INPUT_START_FROM=${START1:-5000}
INPUT_START_TO=${START1E:-5040}
INPUT_START2_FROM=${START2:-5300}
INPUT_START2_TO=${START2E:-5340}
INPUT_RIGHT_FROM=400
INPUT_RIGHT_TO=999999
INPUT_A_FROM=400
INPUT_A_TO=999999
EOT
cat > "$SD/noods.ini" <<'EOT'
frameskip=0
fpsLimiter=0
threaded2D=0
threaded3D=0
emulateAudio=1
armJit=1
directBoot=1
EOT
LOG=$RES/$TAG.rawlog
timeout $TMO xvfb-run -a -s "-screen 0 640x480x24" /usr/games/dolphin-emu-nogui \
  -u /home/user/dolphin -v Software -e "$DOL" > "$LOG" 2>&1 &
W=$!
for i in $(seq 1 $((TMO*4))); do
  grep -qa MAX_FRAMES_DONE "$LOG" && { sleep 1; break; }
  kill -0 $W 2>/dev/null || break
  sleep 0.25
done
kill -TERM $W 2>/dev/null; wait $W 2>/dev/null
strings "$LOG" | grep -E "CHECKPOINT frame" | tee $RES/$TAG.log
export MTOOLS_SKIP_CHECK=1
mcopy -o -i /home/user/dolphin/Load/WiiSD.raw ::/g2_state.log $RES/$TAG.state.log 2>/dev/null
mcopy -o -i /home/user/dolphin/Load/WiiSD.raw ::/fb_dump.bin $RES/$TAG.fb.bin 2>/dev/null
grep -E "SCRIPT_INPUT|PERF|DISPLAY" $RES/$TAG.state.log
python3 /home/user/tools/fb2png.py $RES/$TAG.fb.bin $RES/$TAG.png 2 >/dev/null
