#!/usr/bin/env python3
"""Decode optional VRAM-A frame records written by the instrumented test DOL."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image

VRAM_SIZE = 0x20000
SCREEN_WIDTH = 256
SCREEN_HEIGHT = 192
SCREEN_BYTES = SCREEN_WIDTH * SCREEN_HEIGHT * 2
HEADER_BYTES = 20 * 4
RECORD_BYTES = 4 + HEADER_BYTES + VRAM_SIZE


def rgb5(value: int) -> tuple[int, int, int]:
    r = value & 31
    g = (value >> 5) & 31
    b = (value >> 10) & 31
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


def frame_image(vram: bytes) -> Image.Image:
    pixels = bytearray(SCREEN_WIDTH * SCREEN_HEIGHT * 3)
    for i in range(SCREEN_WIDTH * SCREEN_HEIGHT):
        value = struct.unpack_from("<H", vram, i * 2)[0] & 0x7FFF
        pixels[i * 3 : i * 3 + 3] = bytes(rgb5(value))
    return Image.frombytes("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), bytes(pixels))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--out", type=Path, default=Path("vram-scenes"))
    parser.add_argument("--scenes", default="", help="comma-separated scene IDs; default writes a progress CSV only")
    args = parser.parse_args()

    blob = args.capture.read_bytes()
    if len(blob) % RECORD_BYTES:
        raise SystemExit(f"capture length {len(blob)} is not a whole number of {RECORD_BYTES}-byte records")

    wanted = {int(s) for s in args.scenes.split(",") if s.strip()}
    args.out.mkdir(parents=True, exist_ok=True)
    written: dict[tuple[int, str], int] = {}
    progress_lines = ["frame\tstate\tpassed\tfailed\terrors\texpected\tmissing\toverdraw\twrong\tlast_scene"]

    for offset in range(0, len(blob), RECORD_BYTES):
        frame = struct.unpack_from("<I", blob, offset)[0]
        words = struct.unpack_from("<20I", blob, offset + 4)
        vram_offset = offset + 4 + HEADER_BYTES
        state, passed, failed, errors = words[5], words[6], words[7], words[8]
        last_scene = words[14]
        progress_lines.append(
            f"{frame}\t{state}\t{passed}\t{failed}\t{errors}\t{words[9]}\t{words[10]}\t{words[11]}\t{words[12]}\t{last_scene}"
        )
        if last_scene not in wanted or passed + failed + errors == 0:
            continue
        vram = blob[vram_offset : vram_offset + VRAM_SIZE]
        for which in ("first", "last"):
            key = (last_scene, which)
            if which == "first" and key in written:
                continue
            frame_image(vram[:SCREEN_BYTES]).save(args.out / f"scene-{last_scene:02d}-{which}-frame-{frame:04d}.png")
            written[key] = frame

    (args.out / "progress.tsv").write_text("\n".join(progress_lines) + "\n")
    print(f"Captured {len(blob) // RECORD_BYTES} frames; progress: {args.out / 'progress.tsv'}")
    for (scene, which), frame in sorted(written.items()):
        print(f"scene {scene:02d} {which}: frame {frame}")


if __name__ == "__main__":
    main()
