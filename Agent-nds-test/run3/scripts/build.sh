#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
python3 scripts/generate_cpu.py
mkdir -p work artifacts
rm -rf work/rock
cp -a vendor/rockwrestler work/rock
cp src/auto.cpp work/rock/src9/framework/menu.cpp
cp src/fail.s work/rock/src9/framework/menu.s
cp src/cpu7.cpp work/rock/src7/framework/
# adapter lives one directory deeper than its includes
sed -i 's/"inc.h"/"..\/inc.h"/;s/"tests\/tests.h"/"..\/tests\/tests.h"/' work/rock/src9/framework/menu.cpp
python3 - <<'PATCH'
from pathlib import Path
p=Path('work/rock/src7/framework/main.s')
p.write_text(p.read_text().replace('bl init_stackpointers', 'bl init_stackpointers\n    bl auto_cpu7', 1))
PATCH
# Source timestamp intentionally fixed for reproducibility.
printf '.text\n.global txt_buildtime\ntxt_buildtime: .asciz "AUTOMATED BASELINE"\n' > work/rock/src9/framework/time.s
cp src/generated/cpu.* work/rock/common/
cd work/rock
FLAGS='-O2 -marm -nostartfiles -fno-exceptions -fno-rtti -Wno-narrowing -fno-delete-null-pointer-checks -fno-strict-aliasing -fno-unwind-tables -fno-asynchronous-unwind-tables'
arm-none-eabi-g++ -mcpu=arm946e-s $FLAGS -o ../../artifacts/arm9.elf src9/entry.s src9/*.cpp src9/framework/*.cpp src9/framework/*.s src9/tests/*.cpp src9/tests/*.s common/*.cpp common/*.s -T linker9.ld -Wl,-Map,../../artifacts/arm9.map
arm-none-eabi-g++ -mcpu=arm7tdmi $FLAGS -o ../../artifacts/arm7.elf src7/entry.s src7/*.cpp src7/framework/*.cpp src7/framework/*.s src7/tests/*.cpp common/*.cpp common/*.s -T linker7.ld -Wl,-Map,../../artifacts/arm7.map
for cpu in 7 9; do
 arm-none-eabi-objcopy -O binary ../../artifacts/arm$cpu.elf ../../artifacts/arm$cpu.bin
 arm-none-eabi-objdump -d ../../artifacts/arm$cpu.elf > ../../artifacts/arm$cpu.disasm
 done
ndstool -c ../../suite.nds -r9 0x02000100 -e9 0x02000100 -r7 0x03800100 -e7 0x03800100 -9 ../../artifacts/arm9.bin -7 ../../artifacts/arm7.bin
cd ../..
sha256sum suite.nds > reports/rom.sha256
arm-none-eabi-g++ --version > reports/compiler.txt
python3 scripts/symbols.py
