#!/usr/bin/env python3
"""Decode suite-poly.nds machine-readable results out of mem_dump.bin.

Layout documented in POLY-TESTS.md:
  0x023F0000  base suite box  (+ CPU group table)
  0x023F0400  poly block: 20-word header + 77 records x 12 words
mem_dump.bin = wram(0x8000) itcm(0x8000) dtcm(0x4000) wram7(0x10000)
               palette(0x800) oam(0x800) ram(0x400000)
so main RAM 0x00000000 is at file offset 0x25000.
"""
import struct, sys

RAM_OFF = 0x25000
POLY = 0x3F0400
MAGIC = 0x504C5935


def main(path):
    d = open(path, 'rb').read()
    base = RAM_OFF + POLY
    hdr = struct.unpack_from('<20I', d, base)
    if hdr[0] != MAGIC:
        print(f"poly block magic missing at {path} (got 0x{hdr[0]:08X}) -- suite not finished")
        return 1
    (magic, sver, scenes, dver, tag, state, passed, failed, errored,
     expPx, missing, overdraw, wrongCol, firstFail, lastScene, flags) = hdr[:16]
    stateName = {0: 'not run', 1: 'running', 2: 'complete', 3: 'aborted'}.get(state, state)
    print(f"state={stateName} scenes={scenes} passed={passed} failed={failed} errored={errored}")
    print(f"expectedPx={expPx:,} missing={missing} overdraw={overdraw} wrongCol={wrongCol}")
    print(f"firstFail={firstFail} lastScene={lastScene} flags={flags}")
    ok = (state == 2 and failed == 0 and errored == 0 and missing == 0
          and overdraw == 0 and wrongCol == 0)
    print("POLY RESULT:", "28/28 CPU + 77/77 3D scenes  PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
