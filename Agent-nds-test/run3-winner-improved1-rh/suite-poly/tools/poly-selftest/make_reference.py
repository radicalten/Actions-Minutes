#!/usr/bin/env python3
"""Independent decoder for data/rastertest.data, used to cross-check the ROM's
C++ span reader. Writes reference frames + per-scene pixel counts."""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "..", "Src", "src9", "poly", "rastertest.data")

class Reader:
    def __init__(self, data):
        self.d = data; self.N = len(data); self.pointer = 0; self.shift = 0; self.buf = 0
    def _byte(self):
        if self.pointer >= self.N: return 0
        b = self.d[self.pointer]; self.pointer += 1; return b
    def _align(self):
        while self.shift >= 8:
            self.buf = ((self.buf << 8) | self._byte()) & 0xFFFFFFFF
            self.shift -= 8
    def bit(self):
        self._align(); return (self.buf >> (31 - self.shift)) & 1
    def byte(self):
        self._align(); v = (self.buf >> (24 - self.shift)) & 0xFF; self.shift += 8; return v
    def col(self, bits, mask):
        while bits + self.shift > 32:
            self.buf = ((self.buf << 8) | self._byte()) & 0xFFFFFFFF
            self.shift -= 8
        v = (self.buf >> ((32 - bits) - self.shift)) & mask
        self.shift += bits
        return v

def decode_scene(r, colormode, multispan):
    mask = {1: 0x1F, 2: 0x3FF, 3: 0x7FFF}.get(colormode, 0)
    bits = colormode * 5
    frame = [None] * (256 * 192)  # None = void, else recorded colour
    count = 0
    for y in range(192):
        r._align()
        if not ((r.buf >> (31 - r.shift)) & 1):
            r.shift += 1
            continue
        r.shift += 1
        while True:
            r._align()
            start = r.byte(); end = r.byte()
            for x in range(start, end + 1):
                c = r.col(bits, mask) if colormode else 0x7FFF
                frame[y * 256 + x] = c
                count += 1
            if multispan:
                if not r.bit():
                    r.shift += 1
                    break
                r.shift += 1
            else:
                break
    return frame, count

def main():
    meta = []
    info = open(os.path.join(HERE, "..", "..", "Src", "src9", "poly", "poly_info.h")).read()
    for line in info.splitlines():
        if line.strip().startswith('{"'):
            meta.append(line)
    if len(meta) != 77:
        sys.exit("expected 77 scenes in poly_info.h, found %d" % len(meta))

    data = open(DATA, "rb").read()
    r = Reader(data)
    version = (data[0] << 8) | data[1]
    r.pointer = 2
    r.buf = 0
    for _ in range(4):
        r.buf = ((r.buf << 8) | r._byte()) & 0xFFFFFFFF

    counts = []
    frames = []
    for i in range(len(meta)):
        # Colour mode / multispan are taken from the same rule the ROM uses.
        cm, multi = SCENE_META[i]
        frame, n = decode_scene(r, cm, multi)
        counts.append(n)
        frames.append(frame)

    with open(os.path.join(HERE, "ref_frames.bin"), "wb") as f:
        for frame in frames:
            f.write(struct.pack("<%dH" % len(frame), *[0xFFFF if v is None else v for v in frame]))
    with open(os.path.join(HERE, "ref_counts.txt"), "w") as f:
        f.write("data_version %d\n" % version)
        for i, n in enumerate(counts):
            f.write("%d %d\n" % (i, n))
    print("version", version, "scenes", len(meta), "total expected px", sum(counts))

if __name__ == "__main__":
    SCENE_META = []
    for line in open(os.path.join(HERE, "scene_meta.txt")):
        parts = line.split()
        if len(parts) == 3:
            SCENE_META.append((int(parts[1]), int(parts[2])))
    main()
