#!/usr/bin/env python3
"""Trace v5 capacity, malformed-input, and restore-preflight regressions."""
from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from diagnostics import MANIFEST, checked_blob, cpu_rows, restore, validate_cpu
from trace_layout import (
    ASSERTION_CAPACITY,
    ASSERTION_RECORD_BYTES,
    BASES,
    CAPACITY,
    HEADER_BYTES,
    RECORD_BYTES,
    RECORD_WORDS,
    RESTORE_GATE,
    SLOT_BYTES,
    VERSION,
)

ROOT = Path(__file__).resolve().parents[1]
REPORT = json.loads(Path("reports/development.json").read_text(encoding="utf-8"))
ENTRY = REPORT["diagnostics"]["arm9"]
DATA = checked_blob(ENTRY)
WORDS = list(validate_cpu(DATA))


def rejects(label: str, words: list[int], checks: dict[str, str]) -> None:
    try:
        validate_cpu(struct.pack("<" + "I" * len(words), *words))
    except ValueError:
        checks[label + "_rejected"] = "PASS"
    else:
        raise AssertionError(label)


def main() -> int:
    h = WORDS[:16]
    assert h[1] == VERSION == 5 and h[9] == RECORD_BYTES == 32
    assert h[3] == MANIFEST["arm9_count"] and h[3] <= CAPACITY
    assert len(DATA) == HEADER_BYTES + RECORD_BYTES * h[3]
    assert h[8] == 1 and h[12] == 0
    assert CAPACITY == 13822 and RECORD_WORDS == 8
    assert BASES["arm9"] + SLOT_BYTES <= BASES["arm7"]
    assert BASES["arm7"] + SLOT_BYTES <= BASES["assertions"]
    assert BASES["assertions"] + HEADER_BYTES + ASSERTION_CAPACITY * ASSERTION_RECORD_BYTES < RESTORE_GATE
    checks = {
        "v5_trace_complete_without_capacity_overflow": "PASS",
        "layout_regions_nonoverlapping": "PASS",
        "record_inputs_fingerprinted_against_manifest": "PASS",
    }

    rows = cpu_rows(ENTRY)
    executed = [row for row in rows if row["status"] in ("PASS", "FAIL")]
    assert executed and all(row["runtime_input_fingerprint_matches"] for row in executed)
    mutated = WORDS.copy()
    mutated[16 + 7] ^= 1
    mutated_entry = None
    with tempfile.TemporaryDirectory(prefix="trace-v5-mutation-") as directory:
        path = Path(directory) / "mutated-input-fingerprint.bin"
        raw = struct.pack("<" + "I" * len(mutated), *mutated)
        path.write_bytes(raw)
        mutated_entry = {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest()}
        first = cpu_rows(mutated_entry)[0]
        assert first["runtime_input_fingerprint_matches"] is False
        assert first["runtime_differs_manifest"] is True
    checks["input_metadata_mutation_is_detected"] = "PASS"

    for label, field, value in (
        ("version", 1, 4),
        ("cpu", 2, 8),
        ("count", 3, CAPACITY + 1),
        ("width", 9, 40),
        ("fingerprint", 10, h[10] ^ 1),
        ("runtime_error", 12, 1),
        ("completed_count", 4, h[4] - 1),
        ("active_id", 7, CAPACITY + 1),
        ("state", 16, 5),
        ("state_unknown_bit", 16, 0x202),
        ("empty_historical", 16, 0x100),
        ("plan_count", 14, h[14] - 1),
        ("plan_mask", 15, 0),
    ):
        bad = WORDS.copy()
        bad[field] = value
        rejects(label, bad, checks)
    for label, blob in (
        ("short", b"bad"),
        ("truncated", DATA[:-RECORD_BYTES]),
        ("extra", DATA + b"\0" * RECORD_BYTES),
    ):
        try:
            validate_cpu(blob)
        except ValueError:
            checks[label + "_rejected"] = "PASS"
        else:
            raise AssertionError(label)

    bad_history = WORDS.copy()
    bad_history[11] = 0
    bad_history[16] = (bad_history[16] & 255) | 0x100
    rejects("history_without_header", bad_history, checks)

    class NeverWrite:
        def __getattr__(self, name: str):
            raise AssertionError("Restore touched emulator before validating all traces: " + name)

    with tempfile.TemporaryDirectory(prefix="trace-v5-restore-") as directory:
        root = Path(directory)
        bad = WORDS.copy()
        bad[1] = 1
        bad_data = struct.pack("<" + "I" * len(bad), *bad)
        bad_path = root / "bad.bin"
        bad_path.write_bytes(bad_data)
        bad_entry = {"path": str(bad_path), "sha256": hashlib.sha256(bad_data).hexdigest()}
        try:
            restore(NeverWrite(), {"arm9": ENTRY, "arm7": bad_entry})
        except ValueError:
            checks["entire_restore_validated_before_any_write"] = "PASS"
        else:
            raise AssertionError("unsafe restore")

    Path("artifacts").mkdir(exist_ok=True)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wno-int-to-pointer-cast",
            "tests/trace_bounds.cpp",
            "src/trace.cpp",
            "-o",
            "artifacts/trace-bounds",
        ],
        check=True,
    )
    subprocess.run(["artifacts/trace-bounds"], check=True)
    checks["actual_runtime_v5_capacity_guard_page"] = "PASS"
    Path("reports").mkdir(exist_ok=True)
    Path("reports/trace-protocol-tests.json").write_text(
        json.dumps({"trace_version": VERSION, "record_bytes": RECORD_BYTES, "capacity": CAPACITY, "tests": checks}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    for name, result in checks.items():
        print(result, name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
