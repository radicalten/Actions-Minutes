#!/bin/bash
# Run ALL 3 test suites (suite.gba, nds-2d-agent-suite.nds, suite-poly.nds) with frameskip=0.
set -e
DOL=${1:-/home/user/NooDS-Wii-v1pt63/NooDS-Wii.dol}
echo "=========================================================="
echo " 1/3  GBA ACCURACY SUITE: suite.gba (frameskip=0)"
echo "=========================================================="
/home/user/test/run_gba.sh "$DOL"

echo ""
echo "=========================================================="
echo " 2/3  NDS 2D ACCURACY SUITE: nds-2d-agent-suite.nds (frameskip=0)"
echo "=========================================================="
cd /home/user/test
cat > stage/autoboot.txt <<'EOF'
sd:/noods/roms/nds-2d-agent-suite.nds
MAX_FRAMES=6000
EOF
rm -f sd.img && bash make-sd.sh 64 >/dev/null
./run-dolphin.sh "$DOL" /home/user/test/sd.img /home/user/test/user2D_RUN 90 /home/user/test/cur_2d.log Null >/dev/null
mcopy -o -i sd.img ::/fb_dump.bin /home/user/test/cur_2d_fb.bin
python3 - <<'EOF'
import numpy as np
from PIL import Image
raw = open('/home/user/test/cur_2d_fb.bin', 'rb').read()
arr = np.frombuffer(raw, dtype='>u4').reshape((384, 256))
rgb = np.stack([(arr & 0xFF).astype(np.uint8), ((arr >> 8) & 0xFF).astype(np.uint8), ((arr >> 16) & 0xFF).astype(np.uint8)], axis=-1)
lum = rgb.max(axis=-1)
bw = np.where(lum > 100, 0, 255).astype(np.uint8)
Image.fromarray(bw[:192]).resize((1024, 768), Image.NEAREST).save('/home/user/test/cur_2d_bw.png')
same_as_bl = (raw == open('/home/user/test/bl_2d_fb.bin', 'rb').read())
print("Exact framebuffer match vs verified 8/8 (100.00%) baseline:", same_as_bl)
EOF
tesseract /home/user/test/cur_2d_bw.png stdout --psm 6 2>/dev/null | grep -v "^$" | head -16

echo ""
echo "=========================================================="
echo " 3/3  NDS CPU + 3D SUITE: suite-poly.nds (frameskip=0)"
echo "=========================================================="
cd /home/user/test
cat > stage/autoboot.txt <<'EOF'
sd:/noods/roms/suite-poly.nds
MAX_FRAMES=1300
EOF
rm -f sd.img && bash make-sd.sh 64 >/dev/null
./run-dolphin.sh "$DOL" /home/user/test/sd.img /home/user/test/userPOLY_RUN 90 /home/user/test/cur_poly.log Null >/dev/null
mcopy -o -i sd.img ::/mem_dump.bin /home/user/test/cur_poly_mem.bin
python3 - <<'EOF'
import struct
mem = open('/home/user/test/cur_poly_mem.bin', 'rb').read()
ram_off = 0x25000
box = struct.unpack_from('<26I', mem, ram_off + 0x3F0000)
poly = struct.unpack_from('<20I', mem, ram_off + 0x3F0400)
states = {0:'WAIT', 1:'PASS', 2:'FAIL', 3:'TIME', 4:'SKIP', 5:'RUN', 6:'CRSH', 7:'HTIM'}
fails = []
for g in range(28):
    st, code = struct.unpack_from('<2I', mem, ram_off + 0x3F0100 + g * 8)
    if st != 1:
        fails.append(f"Group {g} ({states.get(st, str(st))}, code=0x{code:08X})")
print(f"Groups passed: {box[5]}/{box[6]}  Failed: {box[8]}  Non-pass groups: {fails if fails else 'NONE (ALL 28 PASS!)'}")
print(f"Poly 3D scenes: {poly[6]}/{poly[2]} passed, failed={poly[7]}, errored={poly[8]}, expectedPx={poly[9]}, missing={poly[10]}, overdraw={poly[11]}, wrongCol={poly[12]}")
EOF
