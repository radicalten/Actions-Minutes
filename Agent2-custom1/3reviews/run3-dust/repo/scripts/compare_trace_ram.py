#!/usr/bin/env python3
"""Compare NooDS suite trace memory from the test harness's mem_dump.bin files.

The dump format is the harness layout: 0x25000 bytes of WRAM/TCM/palette/OAM,
then the first 4 MiB of NDS main RAM. Trace offsets below follow suite-readme's
trace-v5 table. This is a raw evidence comparator; it does not infer vector IDs
or decide whether a registered expectation is architecturally correct.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

PREFIX = 0x25000
RAM_SIZE = 0x400000
TRACE_SLOT_SIZE = 0x6C000
TRACE_HEADER_SIZE = 64
TRACE_RECORD_SIZE = 32
TRACES = {
    "ARM9": {"offset": 0x300000, "cpu": 9},
    "ARM7": {"offset": 0x36C000, "cpu": 7},
}
ASSERTION = {"offset": 0x3D8000, "size": 0x14040}
TRACE_MAGIC = 0x43545235  # bytes "5RTC", little-endian spelling of TRC5


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_dump(path: Path) -> bytes:
    data = path.read_bytes()
    required = PREFIX + RAM_SIZE
    if len(data) < required:
        raise ValueError(f"{path}: dump is {len(data)} bytes; need at least {required}")
    return data[PREFIX:PREFIX + RAM_SIZE]


def trace_header(ram: bytes, offset: int) -> dict:
    region = ram[offset:offset + TRACE_SLOT_SIZE]
    if len(region) != TRACE_SLOT_SIZE:
        raise ValueError("trace slot runs past captured RAM")
    words = struct.unpack_from("<16I", region, 0)
    if words[0] != TRACE_MAGIC:
        raise ValueError(f"bad trace-v5 magic at {offset:#x}: {words[0]:08X}")
    if words[1] != 5 or words[9] != TRACE_RECORD_SIZE:
        raise ValueError(f"unsupported trace version/record size at {offset:#x}")
    total = words[3]
    written = words[4]
    if written > (TRACE_SLOT_SIZE - TRACE_HEADER_SIZE) // TRACE_RECORD_SIZE:
        raise ValueError(f"trace record count exceeds slot capacity: {written}")
    return {
        "cpu": words[2],
        "planned_records": total,
        "written_records": written,
        "pass_count": words[5],
        "fail_count": words[6],
        "record_size": words[9],
        "manifest_crc32": f"{words[10]:08X}",
        "header_sha256": sha256(region[:TRACE_HEADER_SIZE]),
        "slot_sha256": sha256(region),
    }


def compare_trace(name: str, a: bytes, b: bytes, offset: int) -> dict:
    info_a = trace_header(a, offset)
    info_b = trace_header(b, offset)
    count = min(info_a["written_records"], info_b["written_records"])
    ra = a[offset:offset + TRACE_SLOT_SIZE]
    rb = b[offset:offset + TRACE_SLOT_SIZE]
    changed = []
    changed_states = 0
    same_status_data_changes = 0
    transitions = {"fail_to_pass": 0, "pass_to_fail": 0, "other_status": 0}
    first_transition = {key: None for key in transitions}
    changed_record_count = 0
    for i in range(count):
        pos = TRACE_HEADER_SIZE + i * TRACE_RECORD_SIZE
        xa = ra[pos:pos + TRACE_RECORD_SIZE]
        xb = rb[pos:pos + TRACE_RECORD_SIZE]
        if xa == xb:
            continue
        changed_record_count += 1
        wa = struct.unpack("<8I", xa)
        wb = struct.unpack("<8I", xb)
        if wa[0] == wb[0]:
            same_status_data_changes += 1
        else:
            changed_states += 1
            key = (
                "fail_to_pass" if wa[0] == 3 and wb[0] == 2
                else "pass_to_fail" if wa[0] == 2 and wb[0] == 3
                else "other_status"
            )
            transitions[key] += 1
            if first_transition[key] is None:
                first_transition[key] = {
                    "record_index": i,
                    "baseline_state": wa[0],
                    "candidate_state": wb[0],
                    "fingerprint": f"{wa[7]:08X}",
                }
        if len(changed) < 64:
            changed.append({
                "record_index": i,
                "baseline_words": [f"{x:08X}" for x in wa],
                "candidate_words": [f"{x:08X}" for x in wb],
            })
    return {
        "baseline_header": info_a,
        "candidate_header": info_b,
        "header_equal": ra[:TRACE_HEADER_SIZE] == rb[:TRACE_HEADER_SIZE],
        "records_compared": count,
        "changed_records": changed_record_count,
        "changed_status_words": changed_states,
        "status_transitions": transitions,
        "first_status_transition": first_transition,
        "same_status_data_changes": same_status_data_changes,
        "slot_equal": ra == rb,
        "first_changed_records": changed,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline", type=Path)
    ap.add_argument("candidate", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--max-ram-diffs", type=int, default=64)
    args = ap.parse_args()

    a = load_dump(args.baseline)
    b = load_dump(args.candidate)
    result = {
        "baseline_dump": str(args.baseline),
        "candidate_dump": str(args.candidate),
        "captured_main_ram_bytes": RAM_SIZE,
        "main_ram_equal": a == b,
        "main_ram_sha256": {"baseline": sha256(a), "candidate": sha256(b)},
        "traces": {
            name: compare_trace(name, a, b, spec["offset"])
            for name, spec in TRACES.items()
        },
    }
    ao, bo = ASSERTION["offset"], ASSERTION["offset"] + ASSERTION["size"]
    result["assertion_journal"] = {
        "offset": f"{ao:#08x}",
        "bytes": ASSERTION["size"],
        "equal": a[ao:bo] == b[ao:bo],
        "baseline_sha256": sha256(a[ao:bo]),
        "candidate_sha256": sha256(b[ao:bo]),
    }
    excluded = [(0x300000, 0x3EC040)]
    diffs = []
    diff_count = 0
    for i, (x, y) in enumerate(zip(a, b)):
        if x == y or any(lo <= i < hi for lo, hi in excluded):
            continue
        diff_count += 1
        if len(diffs) < args.max_ram_diffs:
            diffs.append({"ram_offset": f"{i:#08x}", "baseline": f"{x:02X}", "candidate": f"{y:02X}"})
    result["different_bytes_outside_trace_and_assertion_regions"] = diff_count
    result["first_different_bytes_outside_trace_and_assertion_regions"] = diffs

    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
