#!/usr/bin/env python3
"""Host-only invariants for Thumb branch boundary and displacement-sweep matrices."""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from thumb_branch import (  # noqa: E402
    CONDITIONS,
    FLAGS,
    CONDITIONAL_SWEEP_DISPLACEMENTS,
    CONDITIONAL_SWEEP_FLAGS,
    NZCV_SP_MASK,
    UNCONDITIONAL_OFFSETS,
    UNCONDITIONAL_SWEEP_OFFSETS,
    condition_passed,
    conditional_forms,
    extend_thumb_branch,
    extend_thumb_branch_sweep,
)


def expected_truth(code: int, flags: int) -> bool:
    n, z, c, v = ((flags >> bit) & 1 for bit in (31, 30, 29, 28))
    expected = (
        z == 1,
        z == 0,
        c == 1,
        c == 0,
        n == 1,
        n == 0,
        v == 1,
        v == 0,
        c == 1 and z == 0,
        c == 0 or z == 1,
        n == v,
        n != v,
        z == 0 and n == v,
        z == 1 or n != v,
    )
    return expected[code]


def main() -> int:
    assert len(CONDITIONS) == 14
    assert [code for _, code in CONDITIONS] == list(range(14))
    assert len(FLAGS) == 16 and len(set(FLAGS)) == 16
    assert len(conditional_forms()) == 16
    assert len(UNCONDITIONAL_OFFSETS) == 4
    assert len(CONDITIONAL_SWEEP_DISPLACEMENTS) == 254
    assert len(UNCONDITIONAL_SWEEP_OFFSETS) == 32
    assert not set(UNCONDITIONAL_SWEEP_OFFSETS) & set(UNCONDITIONAL_OFFSETS)
    for code in range(14):
        for flags in FLAGS:
            assert condition_passed(code, flags) == expected_truth(code, flags)

    assembly: list[str] = []
    declarations: list[str] = []
    rows: list[dict] = []
    extend_thumb_branch(assembly, declarations, rows, base=5000, cpu=9)
    assert len(rows) == 320
    extend_thumb_branch_sweep(assembly, declarations, rows, base=5000, cpu=9)
    assert len(rows) == 1338
    assert len(declarations) == 306
    assert [row["id"] for row in rows] == list(range(5001, 6339))
    assert len({row["name"] for row in rows}) == 306
    assert Counter(row["verification_scope"] for row in rows) == {
        "thumb_branch": 320,
        "thumb_branch_sweep": 1018,
    }
    assert Counter(row["operation"] for row in rows) == {
        "conditional_branch": 762,
        "unconditional_branch": 576,
    }
    assert all(row["mask"] == NZCV_SP_MASK for row in rows)
    assert all(row["branch_base"] == "instruction_address + 4" for row in rows)
    assert all(row["source"].endswith("#thumbopcodesjumpsandcalls") for row in rows)
    assert sum(line.endswith("_site:") for line in assembly) == 306
    assert sum(line.endswith("_target:") for line in assembly) == 306
    assert sum(line.endswith("_fallthrough:") for line in assembly) == 306

    edge = [row for row in rows if row["verification_scope"] == "thumb_branch"]
    sweep = [row for row in rows if row["verification_scope"] == "thumb_branch_sweep"]
    edge_conditional = [row for row in edge if row["operation"] == "conditional_branch"]
    assert Counter(row["condition"] for row in edge_conditional) == {
        name: (32 if name in ("eq", "ne") else 16) for name, _ in CONDITIONS
    }
    sweep_conditional = [row for row in sweep if row["operation"] == "conditional_branch"]
    assert {row["displacement_bytes"] for row in sweep_conditional} == set(CONDITIONAL_SWEEP_DISPLACEMENTS)
    for displacement in CONDITIONAL_SWEEP_DISPLACEMENTS:
        group = [row for row in sweep_conditional if row["displacement_bytes"] == displacement]
        expected_states = (0,) if displacement in (-4, -6) else CONDITIONAL_SWEEP_FLAGS
        assert tuple(row["initial"] for row in group) == expected_states
        assert all(row["condition"] == "eq" and row["condition_code"] == 0 for row in group)
        for row in group:
            assert row["condition_taken"] == expected_truth(0, row["initial"])
            if displacement in (-2, -4, -6):
                assert row["result"] == 2 and row.get("target_path_note")
            else:
                assert row["result"] == (1 if row["condition_taken"] else 2)
    minus_two = [row for row in sweep_conditional if row["displacement_bytes"] == -2]
    assert len(minus_two) == 2 and all(row["target_equals_fallthrough"] for row in minus_two)
    assert {row["condition_taken"] for row in minus_two} == {False, True}

    sweep_unconditional = [row for row in sweep if row["operation"] == "unconditional_branch"]
    assert {row["displacement_bytes"] for row in sweep_unconditional} == set(UNCONDITIONAL_SWEEP_OFFSETS)
    assert all(sum(row["displacement_bytes"] == offset for row in sweep_unconditional) == 16
               for offset in UNCONDITIONAL_SWEEP_OFFSETS)

    for row in rows:
        displacement = row["displacement_bytes"]
        assert displacement == row["displacement_halfwords"] * 2
        assert displacement % 2 == 0
        if row["operation"] == "conditional_branch":
            code = row["condition_code"]
            assert code is not None and 0 <= code <= 13
            assert row["expected_thumb_opcode"] == 0xD000 | (code << 8) | ((displacement // 2) & 0xFF)
            assert -256 <= displacement <= 254
        else:
            assert row["expected_thumb_opcode"] == 0xE000 | ((displacement // 2) & 0x7FF)
            assert -2048 <= displacement <= 2046
            assert row["result"] == 1
        assert row["flags"] == row["initial"]

    print(
        "PASS: 1,338 vectors, 306 helpers, all 256 conditional offsets (safe path cases), "
        "14 NZCV boundary predicates, 32 stratified unconditional offsets"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
