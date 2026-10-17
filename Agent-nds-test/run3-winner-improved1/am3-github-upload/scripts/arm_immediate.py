"""ARM data-processing immediate operand-2 rotate-field test matrix.

The matrix covers every 4-bit rotate field for two representative imm8 values
across all 16 ARM data-processing opcodes. It is a defined subset, not the full
imm8 x rotate x operand/register cross-product.
"""
from __future__ import annotations

from register_operand2 import reference

M = 0xFFFFFFFF
OPS = (
    "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
    "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn",
)
OPCODES = {name: code for code, name in enumerate(OPS)}
IMM8_VALUES = (0x01, 0x81)
ROTATIONS = tuple(range(16))
LOGICAL_OPS = {"and", "eor", "tst", "teq", "orr", "mov", "bic", "mvn"}
FLAG_ONLY = {"tst", "teq", "cmp", "cmn"}
A_VALUES = (
    0x00000000, 0x00000001, 0xFFFFFFFF, 0x7FFFFFFF,
    0x80000000, 0x12345678, 0x87654321, 0x55AA55AA,
    0xAAAAAAAA, 0x01010101, 0x80000001, 0xFFFFFFFE,
)
SOURCE = "https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu"
MASK = 0xF0000000


def ror32(value: int, amount: int) -> int:
    amount &= 31
    value &= M
    return value if amount == 0 else ((value >> amount) | (value << (32 - amount))) & M


def encode_arm_immediate(op: str, imm8: int, rotate: int) -> int:
    """Return the exact AL-immediate ARM data-processing instruction word."""
    if op not in OPCODES or not 0 <= imm8 <= 0xFF or not 0 <= rotate <= 15:
        raise ValueError((op, imm8, rotate))
    # All tests write flags (S=1); test/compare opcodes conventionally encode Rd=0.
    return (
        0xE0000000
        | (1 << 25)
        | (OPCODES[op] << 21)
        | (1 << 20)
        | (0 << 16)  # Rn = r0; ignored for MOV/MVN.
        | (0 << 12)  # Rd = r0; ignored for TST/TEQ/CMP/CMN.
        | (rotate << 8)
        | imm8
    )


def expected(op: str, a: int, immediate: int, initial: int, rotate: int) -> tuple[int, int]:
    """Expected Rd and NZCV for one ARM immediate operand-2 instruction."""
    a &= M
    immediate &= M
    carry_in = (initial >> 29) & 1
    old_v = (initial >> 28) & 1
    shifter_carry = carry_in if rotate == 0 else (immediate >> 31) & 1

    # Test/compare instructions update NZCV but do not write Rd.
    reference_op = {"tst": "and", "teq": "eor", "cmp": "sub", "cmn": "add"}.get(op, op)
    carry_for_reference = shifter_carry if reference_op in {"and", "eor", "orr", "bic", "mov", "mvn"} else carry_in
    alu_result, flags = reference(
        reference_op,
        a,
        immediate,
        "lsl",
        0,
        carry_for_reference,
        old_v,
    )
    result = a if op in FLAG_ONLY else alu_result
    return result & M, flags & MASK


def _initial_flags(imm_index: int) -> int:
    # ADC/SBC/RSC receive both carry values across the two encoded imm8 values.
    # The paired V values also exercise logical-operation V preservation.
    return (0x30000000 if imm_index else 0)


def extend_arm_immediate(asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int) -> None:
    """Append 528 vectors / 512 helpers to the selected CPU-specific profile."""
    helpers: set[str] = set()
    for op in OPS:
        for imm_index, imm8 in enumerate(IMM8_VALUES):
            for rotate in ROTATIONS:
                name = f"cpu_arm{cpu}_immediate_{op}_i{imm8:02x}_r{rotate:02d}"
                word = encode_arm_immediate(op, imm8, rotate)
                if name not in helpers:
                    decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
                    asm.extend([
                        ".arm", ".balign 4", f".global {name}", f".type {name}, %function", f"{name}:",
                        "push {r4-r8,lr}", "mov r4,r3", "mrs r7,cpsr", "msr cpsr_f,r2",
                        f".global {name}_instruction", f"{name}_instruction:", f".word 0x{word:08x}",
                        "mrs r1,cpsr", "str r0,[r4]", "str r1,[r4,#4]",
                        "msr cpsr_f,r7", "pop {r4-r8,lr}", "bx lr",
                    ])
                    helpers.add(name)

                operand = ror32(imm8, 2 * rotate)
                a = A_VALUES[(rotate * 3 + imm_index) % len(A_VALUES)]
                initial = _initial_flags(imm_index)
                result, flags = expected(op, a, operand, initial, rotate)
                row = {
                    "id": base + len(rows) + 1,
                    "name": name,
                    "a": a,
                    "b": operand,
                    "initial": initial,
                    "result": result,
                    "flags": flags,
                    "mask": MASK,
                    "aux_kind": "NZCV",
                    "operation": op,
                    "architecture": "ARMv4T ARM data-processing immediate operand2",
                    "verification_scope": "arm_immediate_rotate",
                    "encoded_imm8": imm8,
                    "rotate_field": rotate,
                    "effective_immediate": operand,
                    "expected_arm_opcode": word,
                    "source": SOURCE,
                }
                rows.append(row)

                # At ROR #0 the shifter preserves C. Add the two missing
                # independent C/V combinations so all four NZCV C/V inputs are
                # checked for every logical/test opcode at that boundary.
                if op in LOGICAL_OPS and rotate == 0:
                    extra_initial = 0x20000000 if imm_index == 0 else 0x10000000
                    extra_result, extra_flags = expected(op, a, operand, extra_initial, rotate)
                    rows.append({**row, "id": base + len(rows) + 1,
                                 "initial": extra_initial, "result": extra_result,
                                 "flags": extra_flags})

    assert len(helpers) == 512
    assert sum(row.get("verification_scope") == "arm_immediate_rotate" for row in rows) >= 512
