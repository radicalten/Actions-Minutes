#!/usr/bin/env python3
"""Decode the suite-poly result block from the NooDS-Wii full-RAM dump."""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

DUMP_PREFIX = 0x25000
GUEST_RAM_BASE = 0x02000000
GROUP_TABLE = 0x023F0100
POLY_BLOCK = 0x023F0400
SCENE_COUNT = 77
HEADER_WORDS = 20
RECORD_WORDS = 12
GROUP_NAMES = ("WAIT", "PASS", "FAIL", "TIME", "SKIP", "RUN", "CRASH", "HTIME")
POLY_STATES = ("not run", "running", "complete", "aborted")
SCENE_STATES = ("pending", "pass", "fail", "error")
CATEGORIES = (
    "fill rules primary",
    "fill rules unusual",
    "vertical right-edge shift",
    "anti-aliasing",
    "edge marking",
    "clipping",
)


def file_offset(guest_address: int) -> int:
    return DUMP_PREFIX + guest_address - GUEST_RAM_BASE


def words_at(blob: bytes, offset: int, count: int) -> list[int]:
    end = offset + count * 4
    if end > len(blob):
        raise ValueError(f"dump is truncated: need byte 0x{end:X}, have 0x{len(blob):X}")
    return list(struct.unpack_from("<" + "I" * count, blob, offset))


def label(labels: tuple[str, ...], value: int) -> str:
    return labels[value] if value < len(labels) else f"unknown({value})"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mem_dump", type=Path, help="mem_dump.bin from the test DOL")
    parser.add_argument("--scenes", action="store_true", help="list every scene record")
    parser.add_argument("--require-pass", action="store_true", help="exit nonzero unless all 77 scenes pass")
    args = parser.parse_args()

    blob = args.mem_dump.read_bytes()
    poly_off = file_offset(POLY_BLOCK)
    header = words_at(blob, poly_off, HEADER_WORDS)
    if header[0] != 0x504C5935:
        raise SystemExit(f"unexpected polygon block magic 0x{header[0]:08X}; expected 0x504C5935 (PLY5)")
    if header[2] != SCENE_COUNT:
        raise SystemExit(f"unexpected scene count {header[2]}; expected {SCENE_COUNT}")

    print(f"PLY5 v{header[1]} · {header[2]} scenes · data version {header[3]} · tag 0x{header[4]:08X}")
    print(f"State: {label(POLY_STATES, header[5])}; passed {header[6]}, failed {header[7]}, errors {header[8]}")
    print(f"Pixels: expected {header[9]}, missing {header[10]}, overdraw {header[11]}, wrong-colour {header[12]}")
    first_fail = "none" if header[13] == 0xFFFFFFFF else str(header[13])
    last_result = "none" if header[14] == 0xFFFFFFFF else str(header[14])
    print(f"First failing scene: {first_fail}; last result scene: {last_result}; flags 0x{header[15]:08X}")

    records_off = poly_off + HEADER_WORDS * 4
    records: list[list[int]] = []
    for index in range(SCENE_COUNT):
        record = words_at(blob, records_off + index * RECORD_WORDS * 4, RECORD_WORDS)
        records.append(record)

    counts = [0, 0, 0, 0]
    for record in records:
        state = record[1]
        if state < len(counts):
            counts[state] += 1
    print("Scene records: " + ", ".join(f"{SCENE_STATES[i]} {n}" for i, n in enumerate(counts)))

    category_totals: dict[int, list[int]] = {}
    for record in records:
        totals = category_totals.setdefault(record[8], [0, 0, 0, 0, 0, 0, 0])
        totals[0] += 1
        totals[1] += record[1] == 1
        totals[2] += record[1] == 2
        totals[3] += record[1] == 3
        totals[4] += record[4]
        totals[5] += record[5]
        totals[6] += record[6]
    print("Category totals (scenes, pass, fail, error, missing, overdraw, wrong-colour):")
    for category_id in sorted(category_totals):
        name = label(CATEGORIES, category_id)
        totals = category_totals[category_id]
        print(f"  {name}: " + ", ".join(str(value) for value in totals))

    if args.scenes:
        print("\nscene state expected actual missing overdraw wrong first-mismatch category flags")
        for index, r in enumerate(records):
            state = label(SCENE_STATES, r[1])
            if r[7] == 0xFFFFFFFF:
                mismatch = "-"
            else:
                x = r[7] & 0xFF
                y = (r[7] >> 8) & 0xFF
                kind = (r[7] >> 16) & 0xFF
                mismatch = f"({x},{y},kind={kind})"
            category = label(CATEGORIES, r[8])
            print(f"{index:02d} {state:7} {r[2]:8} {r[3]:8} {r[4]:7} {r[5]:8} {r[6]:5} {mismatch:20} {category:28} 0x{r[9]:08X}")

    group_off = file_offset(GROUP_TABLE) + 27 * 8
    group_state, group_code = words_at(blob, group_off, 2)
    print(f"Group 27: {label(GROUP_NAMES, group_state)}; code 0x{group_code:08X}")

    if args.require_pass:
        if header[5] != 2 or header[6] != SCENE_COUNT or header[7] != 0 or header[8] != 0:
            return 1
        if any(r[1] != 1 for r in records):
            return 1
        if group_state != 1:
            return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
