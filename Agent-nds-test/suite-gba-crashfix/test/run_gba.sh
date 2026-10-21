#!/bin/bash
# Run suite.gba against a .dol and print per-suite + total pass/total counts.
set -e
DOL=${1:-/home/user/NooDS-Wii-v1pt63/NooDS-Wii.dol}
cd /home/user/test
cat > stage/autoboot.txt <<'EOF'
sd:/noods/roms/suite.gba
MAX_FRAMES=500000
EOF
rm -f sd.img && bash make-sd.sh 64 >/dev/null
./run-dolphin.sh "$DOL" /home/user/test/sd.img /home/user/test/userGBA_RUN 60 /home/user/test/cur_gba.log Null >/dev/null
python3 - <<'EOF'
import re
lines = open('/home/user/test/cur_gba.log', errors='replace').read().splitlines()
cur_name = None
tot_p = tot_n = 0
print(f"{'Sub-suite':<28} {'Score':>12} {'Pct':>8}")
print("-" * 50)
done = False
for l in lines:
    m = re.search(r'\[MGBA\] BEGIN: (.+)', l)
    if m: cur_name = m.group(1).strip()
    m = re.search(r'\[MGBA\] END: (\d+)/(\d+)', l)
    if m and cur_name:
        p, n = int(m.group(1)), int(m.group(2))
        tot_p += p; tot_n += n
        print(f"{cur_name:<28} {p:>5}/{n:<6} {100.0*p/n:>7.2f}%")
        cur_name = None
    if '[MGBA] ALL DONE' in l:
        done = True
print("-" * 50)
print(f"{'TOTAL (ALL DONE=' + str(done) + ')':<28} {tot_p:>5}/{tot_n:<6} {100.0*tot_p/max(1,tot_n):>7.2f}%")
EOF
mcopy -o -i sd.img ::/gbaout.log /home/user/test/cur_gbaout.log 2>/dev/null || true
