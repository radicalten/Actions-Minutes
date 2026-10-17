#!/usr/bin/env python3
"""Host invariants and independent arithmetic checks for immediate-ALU matrices."""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import arm_immediate  # noqa: E402
import thumb_immediate  # noqa: E402
from cpu_groups import classify  # noqa: E402

M = 0xFFFFFFFF
NZCV = 0xF0000000
S32_MIN = -0x80000000
S32_MAX = 0x7FFFFFFF


def signed(value: int) -> int:
    value &= M
    return value if value < 0x80000000 else value - 0x100000000


def nzcv(result: int, carry: int, overflow: int) -> int:
    result &= M
    return (result & 0x80000000) | (0x40000000 if result == 0 else 0) | (carry << 29) | (overflow << 28)


def independent_arm(op: str, a: int, operand: int, initial: int, rotate: int) -> tuple[int, int]:
    a &= M
    operand &= M
    carry_in = (initial >> 29) & 1
    old_v = (initial >> 28) & 1
    carry_shifter = carry_in if rotate == 0 else (operand >> 31) & 1
    if op in ("and", "tst"):
        alu = a & operand
        carry, overflow = carry_shifter, old_v
    elif op in ("eor", "teq"):
        alu = a ^ operand
        carry, overflow = carry_shifter, old_v
    elif op in ("orr",):
        alu = a | operand
        carry, overflow = carry_shifter, old_v
    elif op == "bic":
        alu = a & (~operand & M)
        carry, overflow = carry_shifter, old_v
    elif op == "mov":
        alu = operand
        carry, overflow = carry_shifter, old_v
    elif op == "mvn":
        alu = ~operand & M
        carry, overflow = carry_shifter, old_v
    else:
        if op in ("add", "adc", "cmn"):
            x, y = a, operand
            carry_add = carry_in if op == "adc" else 0
            signed_full = signed(x) + signed(y) + carry_add
            full = x + y + carry_add
            alu = full & M
            carry = int(full > M)
        else:
            if op in ("rsb", "rsc"):
                x, y = operand, a
            else:
                x, y = a, operand
            carry_add = carry_in if op in ("sbc", "rsc") else 1
            borrow = 1 - carry_add
            signed_full = signed(x) - signed(y) - borrow
            alu = (x - y - borrow) & M
            carry = int(x >= y + borrow)
        overflow = int(not S32_MIN <= signed_full <= S32_MAX)
    flags = nzcv(alu, carry, overflow)
    return (a if op in ("tst", "teq", "cmp", "cmn") else alu), flags


def independent_thumb(op: str, a: int, imm: int, initial: int) -> tuple[int, int]:
    a &= M
    if op == "movs":
        result = imm
        return result, nzcv(result, (initial >> 29) & 1, (initial >> 28) & 1)
    if op == "adds":
        full = a + imm
        result = full & M
        overflow = int(not S32_MIN <= signed(a) + imm <= S32_MAX)
        return result, nzcv(result, int(full > M), overflow)
    full = a - imm
    result = full & M
    overflow = int(not S32_MIN <= signed(a) - imm <= S32_MAX)
    primary = a if op == "cmp" else result
    return primary, nzcv(result, int(a >= imm), overflow)


def main() -> int:
    arm_asm: list[str] = []
    arm_decl: list[str] = []
    arm_rows: list[dict] = []
    arm_immediate.extend_arm_immediate(arm_asm, arm_decl, arm_rows, base=3000, cpu=7)
    assert len(arm_rows) == 528
    assert len(arm_decl) == 512
    assert len({row["name"] for row in arm_rows}) == 512
    assert [row["id"] for row in arm_rows] == list(range(3001, 3529))
    assert Counter(row["operation"] for row in arm_rows) == {
        **{op: 34 for op in arm_immediate.LOGICAL_OPS},
        **{op: 32 for op in set(arm_immediate.OPS) - arm_immediate.LOGICAL_OPS},
    }
    for op in arm_immediate.OPS:
        for rotate in arm_immediate.ROTATIONS:
            group = [r for r in arm_rows if r["operation"] == op and r["rotate_field"] == rotate]
            assert len(group) == (4 if op in arm_immediate.LOGICAL_OPS and rotate == 0 else 2)
            assert {row["encoded_imm8"] for row in group} == set(arm_immediate.IMM8_VALUES)
            for row in group:
                assert row["expected_arm_opcode"] == arm_immediate.encode_arm_immediate(
                    op, row["encoded_imm8"], rotate
                )
                assert row["b"] == arm_immediate.ror32(row["encoded_imm8"], 2 * rotate)
                assert (row["result"], row["flags"]) == independent_arm(
                    op, row["a"], row["b"], row["initial"], rotate
                )
                assert row["mask"] == NZCV
                assert classify(row)[0] == "ARM"
    for op in arm_immediate.LOGICAL_OPS:
        assert {row["initial"] for row in arm_rows
                if row["operation"] == op and row["rotate_field"] == 0} == {
                    0, 0x10000000, 0x20000000, 0x30000000
                }
    assert sum(line.endswith("_instruction:") for line in arm_asm) == 512
    assert all(f".word 0x{row['expected_arm_opcode']:08x}" in arm_asm for row in arm_rows)

    thumb_asm: list[str] = []
    thumb_decl: list[str] = []
    thumb_rows: list[dict] = []
    thumb_immediate.extend_thumb_immediate(thumb_asm, thumb_decl, thumb_rows, base=9000, cpu=9)
    assert len(thumb_rows) == 2048
    assert len(thumb_decl) == 1024
    assert len({row["name"] for row in thumb_rows}) == 1024
    assert [row["id"] for row in thumb_rows] == list(range(9001, 11049))
    assert Counter(row["operation"] for row in thumb_rows) == {
        "movs": 512, "cmp": 512, "adds": 512, "subs": 512
    }
    for op in thumb_immediate.OPCODE_BASES:
        for imm in thumb_immediate.IMM8_VALUES:
            group = [r for r in thumb_rows if r["operation"] == op and r["encoded_imm8"] == imm]
            assert len(group) == 2
            assert {row["expected_thumb_opcode"] for row in group} == {
                thumb_immediate.thumb_opcode(op, imm)
            }
            for row in group:
                assert (row["result"], row["flags"]) == independent_thumb(
                    op, row["a"], imm, row["initial"]
                )
                assert row["mask"] == NZCV
                assert classify(row)[0] == "THUMB"
    for row in thumb_rows:
        assert row["expected_thumb_opcode"] == 0x2000 + (
            {"movs": 0x0000, "cmp": 0x0800, "adds": 0x1000, "subs": 0x1800}[row["operation"]]
        ) + row["encoded_imm8"]
    for imm in thumb_immediate.IMM8_VALUES:
        mov_rows = [r for r in thumb_rows if r["operation"] == "movs" and r["encoded_imm8"] == imm]
        assert {row["initial"] for row in mov_rows} == {0x10000000, 0x20000000}
    assert sum(line.endswith("_instruction:") for line in thumb_asm) == 1024
    assert all(f".hword 0x{row['expected_thumb_opcode']:04x}" in thumb_asm for row in thumb_rows)

    print(
        "PASS: ARM immediate rotation matrix 528 vectors / 512 helpers; "
        "Thumb imm8 matrix 2,048 vectors / 1,024 helpers; independent result/NZCV formulas"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
