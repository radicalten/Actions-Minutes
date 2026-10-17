#!/usr/bin/env python3
"""Bake a ROM into a C array so a diagnostic build can carry it into the
emulator (the emulated SD card cannot be filled from outside a headless run).

    tools/mk_embed_rom.py <rom.nds> NooDS-Wii/embed_rom.inc
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
with open(dst, "w") as f:
    for i in range(0, len(data), 16):
        f.write(",".join("0x%02X" % b for b in data[i:i + 16]) + ",\n")
print(f"{dst}: {len(data)} bytes")
