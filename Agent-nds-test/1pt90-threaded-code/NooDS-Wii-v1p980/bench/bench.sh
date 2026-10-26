#!/bin/bash
# bench.sh <dol> <prefix>  -- runs the full benchmark/accuracy matrix for one build
# Emits one result line per workload: <prefix>,<workload>,<core_us>,<fbhash>,<score>
set -u
DOL=$1; PRE=$2
R=/home/user/results
T=/home/user/tools

line() { echo "$PRE,$1,$2,$3,$4" >> $R/bench_$PRE.csv; }

# 1. mGBA suite (GBA interpreter path) - 30000 slices
$T/run_test.sh "$DOL" "sd:/roms/mgba-suite-auto.gba" 30000 30000 ${PRE}_gba >/dev/null 2>&1
CU=$(grep -oP 'PERF frame=\d+ accum_us=\K\d+' $R/${PRE}_gba.state.log 2>/dev/null)
FH=$(grep -oP 'FB_HASH top=\w+ bottom=\w+ combined=\K\w+' $R/${PRE}_gba.state.log 2>/dev/null)
export MTOOLS_SKIP_CHECK=1
mcopy -o -i /home/user/dolphin/Load/WiiSD.raw ::/gbaout.log $R/${PRE}_gba.gbaout 2>/dev/null
SC=$(python3 - <<EOF
import re
try:
    L=open("$R/${PRE}_gba.gbaout",errors='replace').read().splitlines()
except: print("NA"); raise SystemExit
p=t=0; n=0
for i,l in enumerate(L):
    if l.startswith('BEGIN:'):
        for j in range(i+1,min(i+40,len(L))):
            if L[j].startswith('END:'):
                m=re.match(r'END:\s*(\d+)\s*/\s*(\d+)',L[j])
                if m: p+=int(m.group(1)); t+=int(m.group(2)); n+=1
                break
done=any(l.strip()=='ALL DONE' for l in L)
print(f"{p}/{t}{'=DONE' if done else ''}")
EOF
)
line gba_mgba_suite "$CU" "$FH" "$SC"

# 2. NDS 2D raster suite (full results screen, 9000 slices)
$T/run_test.sh "$DOL" "sd:/roms/nds-2d-agent-suite.nds" 9000 9000 ${PRE}_2d >/dev/null 2>&1
CU=$(grep -oP 'PERF frame=\d+ accum_us=\K\d+' $R/${PRE}_2d.state.log 2>/dev/null)
FH=$(grep -oP 'FB_HASH top=\w+ bottom=\w+ combined=\K\w+' $R/${PRE}_2d.state.log 2>/dev/null)
line nds2d_suite "$CU" "$FH" "see_png"

echo "== $PRE =="
cat $R/bench_$PRE.csv
