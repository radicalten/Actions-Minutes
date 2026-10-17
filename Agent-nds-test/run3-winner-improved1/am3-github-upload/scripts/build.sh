#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

# Fail before generation or file mutation when the clean-checkout prerequisites
# are absent. This ROM is built with devkitARM's cross compiler, not host GCC.
for tool in \
  "$DEVKITARM/bin/arm-none-eabi-g++" \
  "$DEVKITARM/bin/arm-none-eabi-objcopy" \
  "$DEVKITARM/bin/arm-none-eabi-objdump" \
  "$DEVKITPRO/tools/bin/ndstool"; do
  if [[ ! -x "$tool" ]]; then
    printf 'Missing required build tool: %s\n' "$tool" >&2
    printf 'Install devkitARM and ndstool with: bash scripts/setup.sh\n' >&2
    exit 2
  fi
done
if ! python3 scripts/fetch_upstreams.py --check; then
  printf '\nPinned upstream sources are missing, modified, or at the wrong revision.\n' >&2
  printf 'Fetch clean snapshots with: python3 scripts/fetch_upstreams.py\n' >&2
  exit 2
fi
mkdir -p work artifacts reports
python3 scripts/generate_cpu.py
rm -rf work/rock
cp -a vendor/rockwrestler work/rock
cp src/bottom.cpp src/bottom.h src/generated/count.h work/rock/src9/framework/
cp src/generated/v5.cpp src/generated/v5.s src/generated/arm7meta.cpp work/rock/src9/framework/
cp src/generated/arm7.cpp src/generated/arm7.s work/rock/src7/framework/
cp src/trace.cpp src/trace_layout.h work/rock/common/
cp src/trace_layout.h work/rock/src9/framework/
cp src/trace_layout.h work/rock/src7/framework/
cp src/auto.cpp work/rock/src9/framework/menu.cpp
cp src/fail.s work/rock/src9/framework/menu.s
cp src/generated/count.h work/rock/src7/framework/
cp src/cpu7.cpp src/reset7.cpp src/reset7.s work/rock/src7/framework/
cp src/reset9.cpp src/assert_log.cpp work/rock/src9/framework/
cp src/linker/arm7.ld work/rock/linker7.ld
cp src/linker/arm9.ld work/rock/linker9.ld
# adapter lives one directory deeper than its includes
sed -i 's/"inc.h"/"..\/inc.h"/;s/"tests\/tests.h"/"..\/tests\/tests.h"/' work/rock/src9/framework/menu.cpp
python3 - <<'PATCH'
from pathlib import Path
p=Path('work/rock/src7/framework/main.s')
p.write_text(p.read_text().replace('bl init_stackpointers', 'bl init_stackpointers\n    bl auto_cpu7', 1).replace('.global arm7_main', '.global arm7_main, arm7_main_post_inittest'))
p=Path('work/rock/src7/inc.h');p.write_text(p.read_text()+'\nextern "C" void service_poll7();\n')
# Cooperative reset checks only in ARM7 busy/wait loops. Keep original assertions.
for name in ['framework/waitloop.cpp','framework/rw.cpp','tests/ipcfifo_irq.cpp','tests/ipcsync.cpp']:
 p=Path('work/rock/src7')/name;s=p.read_text()
 s=s.replace('while (true)\n    {','while (true)\n    {\n        service_poll7();')
 s=s.replace('while (REG_IPCFIFOCNT & (1 << 8)) { }','while (REG_IPCFIFOCNT & (1 << 8)) { service_poll7(); }')
 s=s.replace('while (magic != 89)\n    {','while (magic != 89)\n    {\n        service_poll7();')
 s=s.replace('while (magic != 0x69)\n    {','while (magic != 0x69)\n    {\n        service_poll7();')
 s=s.replace('while ((REG_IPCSYNC & 0xF) != value)\n    {','while ((REG_IPCSYNC & 0xF) != value)\n    {\n        service_poll7();')
 if name=='framework/waitloop.cpp':
  s=s.replace('REG_IPCFIFOCNT = 1 << 15;', 'REG_IPCFIFOCNT = 1 << 15;\n    ((vu32*)0x023ee000)[3]=((vu32*)0x023ee000)[1];')
 p.write_text(s)
PATCH
python3 scripts/instrument_assertions.py
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
python3 - <<'LAYOUT'
from pathlib import Path
# ARM7 code/data live in reserved main RAM; its stacks remain in private WRAM.
# Keep the ARM9 image below the ARM7 image and ARM7 below 02300000.
assert Path('../../artifacts/arm7.bin').stat().st_size < 0x100000, 'ARM7 image exceeds reserved main RAM'
assert Path('../../artifacts/arm9.bin').stat().st_size < 0x1eff00, 'ARM9 image encroaches on MPU scratch at 021f0000'
LAYOUT
ndstool -c ../../suite.nds -r9 0x02000100 -e9 0x02000100 -r7 0x02200000 -e7 0x02200000 -9 ../../artifacts/arm9.bin -7 ../../artifacts/arm7.bin
cd ../..
sha256sum suite.nds > reports/rom.sha256
arm-none-eabi-g++ --version > reports/compiler.txt
python3 scripts/symbols.py
