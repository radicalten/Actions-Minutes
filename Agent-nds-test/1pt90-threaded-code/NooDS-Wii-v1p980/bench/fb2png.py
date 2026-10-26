#!/usr/bin/env python3
"""Convert a NooDS-Wii fb_dump.bin (2 x 256x192 uint32 screens) to a PNG.

Layout: tempBuffer[0 .. 49151]  = top screen
        tempBuffer[49152 .. ...] = bottom screen
Each word is 0xAARRGGBB in host (big-endian Wii) order written as native u32,
so on the little-endian host the file is big-endian RGBX.
"""
import struct, sys, zlib

W, H = 256, 192

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)

def main(src, dst, scale=2):
    data = open(src, 'rb').read()
    n = len(data) // 4
    words = struct.unpack('>%dI' % n, data[:n*4])
    top = words[:W*H]
    bot = words[W*H:2*W*H] if n >= 2*W*H else [0]*(W*H)
    # stack top over bottom
    out = bytearray()
    for row in range(H*2):
        scr = top if row < H else bot
        y = row if row < H else row - H
        line = bytearray()
        for x in range(W):
            v = scr[y*W + x]
            r, g, b = (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff
            line += bytes((r, g, b)) * scale
        for _ in range(scale):
            out += line
    write_png(dst, W*scale, H*2*scale, bytes(out))
    print(f"{dst}: {W*scale}x{H*2*scale}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2)
