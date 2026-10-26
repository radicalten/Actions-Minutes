#!/bin/bash
# Final measurement matrix: 4 workloads x 2 builds
for TAG in v1pt5 v1p980; do
  D=/home/user/results/release/$TAG.dol
  printf "%-7s" "$TAG"
  for W in "mgba:sd:/roms/mgba-suite-auto.gba:30000" "nds2d:sd:/roms/nds-2d-agent-suite.nds:9000" "poly:sd:/roms/suite-poly.nds:4000" "varoom:sd:/roms/varoom-3d.gba:3000"; do
    N=${W%%:*}; R=$(echo $W|cut -d: -f2-3|sed 's/:[0-9]*$//'); F=${W##*:}
    ./tools/run_test.sh $D "$R" $F $F "${TAG}_m_$N" 600 >/dev/null 2>&1
    CU=$(grep -oP 'PERF frame=\d+ accum_us=\K\d+' results/${TAG}_m_$N.state.log)
    printf " %s=%s" "$N" "$CU"
  done
  echo
done
