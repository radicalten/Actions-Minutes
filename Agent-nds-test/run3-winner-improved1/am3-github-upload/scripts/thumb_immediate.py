"""Thumb imm8 data-processing matrices for MOVS/CMP/ADDS/SUBS."""
from __future__ import annotations

M = 0xFFFFFFFF
IMM8_VALUES = tuple(range(256))
OPCODE_BASES = {"movs": 0x2000, "cmp": 0x2800, "adds": 0x3000, "subs": 0x3800}
SOURCE = "https://mgba-emu.github.io/gbatek/#thumbopcodesregisteroperationsalu"
MASK = 0xF0000000


def thumb_opcode(op: str, immediate: int) -> int:
    if op not in OPCODE_BASES or not 0 <= immediate <= 0xFF:
        raise ValueError((op, immediate))
    # r0 is the destination/source register for these immediate encodings.
    return OPCODE_BASES[op] | immediate


def _nz(result: int) -> int:
    result &= M
    return (result & 0x80000000) | (0x40000000 if result == 0 else 0)


def expected(op: str, a: int, immediate: int, initial: int) -> tuple[int, int]:
    """Expected primary result and NZCV for one Thumb imm8 ALU instruction."""
    a &= M
    immediate &= 0xFF
    carry_in = (initial >> 29) & 1
    old_v = (initial >> 28) & 1
    if op == "movs":
        result = immediate
        flags = _nz(result) | (carry_in << 29) | (old_v << 28)
        return result, flags
    if op == "adds":
        full = a + immediate
        result = full & M
        carry = int(full > M)
        signed_sum = (a if a < 0x80000000 else a - 0x100000000) + immediate
        overflow = int(not -0x80000000 <= signed_sum <= 0x7FFFFFFF)
        return result, _nz(result) | (carry << 29) | (overflow << 28)
    if op in ("subs", "cmp"):
        full = a - immediate
        result = full & M
        carry = int(a >= immediate)
        signed_a = a if a < 0x80000000 else a - 0x100000000
        signed_result = signed_a - immediate
        overflow = int(not -0x80000000 <= signed_result <= 0x7FFFFFFF)
        primary = a if op == "cmp" else result
        return primary, _nz(result) | (carry << 29) | (overflow << 28)
    raise ValueError(op)


def _inputs(op: str, immediate: int) -> tuple[tuple[int, int], ...]:
    if op == "movs":
        noisy_input = (0xA5A50000 | immediate) & M
        # Complementary C/V pairs ensure both bits are independently preserved.
        return ((noisy_input, 0x10000000), (noisy_input, 0x20000000))
    if op == "cmp":
        return ((immediate, 0x30000000), ((immediate - 1) & M, 0x90000000))
    if op == "adds":
        carry_edge = (0xFFFFFFFF - immediate + 1) & M
        return ((0x7FFFFFFF, 0x30000000), (carry_edge, 0x90000000))
    if op == "subs":
        return (((immediate + 1) & M, 0x30000000), ((immediate - 1) & M, 0x90000000))
    raise ValueError(op)


def _emit_helper(asm: list[str], decl: list[str], name: str, opcode: int) -> None:
    decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
    asm.extend([
        ".arm", ".balign 4", f".global {name}", f".type {name}, %function", f"{name}:",
        "push {r4-r8,lr}", "mov r4,r3", "mrs r7,cpsr", "mov r6,r2",
        "adr r8,2f", "adr r3,1f", "orr r3,r3,#1", "msr cpsr_f,r6", "bx r3",
        ".thumb", ".balign 2", "1:",
        f".global {name}_instruction", f"{name}_instruction:", f".hword 0x{opcode:04x}",
        "bx r8", ".arm", ".balign 4", "2:",
        "mrs r1,cpsr", "str r0,[r4]", "str r1,[r4,#4]", "msr cpsr_f,r7",
        "pop {r4-r8,lr}", "bx lr",
    ])


def extend_thumb_immediate(asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int) -> None:
    """Append 2,048 vectors / 1,024 helpers, exhausting each imm8 field."""
    for op in ("movs", "cmp", "adds", "subs"):
        for immediate in IMM8_VALUES:
            name = f"cpu_arm{cpu}_thumb_imm_{op}_{immediate:02x}"
            _emit_helper(asm, decl, name, thumb_opcode(op, immediate))
            for case_index, (a, initial) in enumerate(_inputs(op, immediate)):
                result, flags = expected(op, a, immediate, initial)
                rows.append({
                    "id": base + len(rows) + 1,
                    "name": name,
                    "a": a,
                    "b": immediate,
                    "initial": initial,
                    "result": result,
                    "flags": flags,
                    "mask": MASK,
                    "aux_kind": "NZCV",
                    "operation": op,
                    "architecture": "ARMv4T Thumb imm8 data-processing",
                    "verification_scope": "thumb_immediate_alu",
                    "encoded_imm8": immediate,
                    "input_case": case_index,
                    "expected_thumb_opcode": thumb_opcode(op, immediate),
                    "source": SOURCE,
                })
    assert sum(row.get("verification_scope") == "thumb_immediate_alu" for row in rows) >= 2048
