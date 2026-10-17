"""ARMv4T Thumb branch decision, signed-offset, boundary, and NZCV-preservation tests."""
from __future__ import annotations

CONDITIONS = (
    ("eq", 0),
    ("ne", 1),
    ("cs", 2),
    ("cc", 3),
    ("mi", 4),
    ("pl", 5),
    ("vs", 6),
    ("vc", 7),
    ("hi", 8),
    ("ls", 9),
    ("ge", 10),
    ("lt", 11),
    ("gt", 12),
    ("le", 13),
)
FLAGS = tuple(nibble << 28 for nibble in range(16))
UNCONDITIONAL_OFFSETS = (-2048, -2046, 0, 2046)
CONDITIONAL_SWEEP_DISPLACEMENTS = tuple(range(-254, 254, 2))
CONDITIONAL_SWEEP_FLAGS = (0, 0x40000000)  # EQ false / true
UNCONDITIONAL_SWEEP_HALFWORDS = (
    -1022, -768, -512, -511, -257, -256, -255, -129,
    -128, -127, -65, -64, -63, -33, -32, -31,
    1022, 768, 512, 511, 257, 256, 255, 129,
    128, 127, 65, 64, 63, 33, 32, 31,
)
UNCONDITIONAL_SWEEP_OFFSETS = tuple(2 * value for value in UNCONDITIONAL_SWEEP_HALFWORDS)
NZCV_SP_MASK = 0xF0000001
SOURCE = "https://mgba-emu.github.io/gbatek/#thumbopcodesjumpsandcalls"


def condition_passed(code: int, flags: int) -> bool:
    """Evaluate one of the 14 legal ARM condition predicates from NZCV."""
    n = (flags >> 31) & 1
    z = (flags >> 30) & 1
    c = (flags >> 29) & 1
    v = (flags >> 28) & 1
    return (
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
    )[code]


def conditional_forms() -> list[tuple[str, int, int]]:
    """All conditions at one signed boundary, plus EQ/NE at the opposite edge."""
    forms = []
    for name, code in CONDITIONS:
        edge = -256 if code % 2 == 0 else 254
        forms.append((name, code, edge))
    forms.extend((("eq", 0, 254), ("ne", 1, -256)))
    return forms


def _thumb_guard_padding(asm: list[str], size_bytes: int) -> None:
    """Emit two-byte guards so a mis-target exits instead of drifting into the target."""
    if size_bytes < 0 or size_bytes % 2:
        raise ValueError(f"Thumb padding must be a nonnegative even byte count: {size_bytes}")
    pairs, remainder = divmod(size_bytes, 4)
    for _ in range(pairs):
        # A branch landing on either halfword leaves result 2 or the initialized 0,
        # then exits to ARM cleanup. It cannot fall through and reach the real target.
        asm.extend(["mov r8,r2", "bx r5"])
    if remainder:
        asm.append("bx r5")


def _append_wrapper(asm: list[str], decl: list[str], name: str) -> None:
    decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
    asm.extend(
        [
            ".arm",
            ".balign 4",
            f".global {name}",
            f".type {name}, %function",
            f"{name}:",
            "push {r4-r10,lr}",
            "mov r4,r3",
            "mrs r7,cpsr",
            "mov r6,r2",
            "mov r9,sp",
            "mov r8,#0",
            "mov r1,#1",
            "mov r2,#2",
            "ldr r5,=2f",
            "msr cpsr_f,r6",
            f"ldr r3,={name}_site+1",
            "bx r3",
            ".thumb",
            ".balign 2",
        ]
    )


def _append_cleanup(asm: list[str]) -> None:
    asm.extend(
        [
            ".arm",
            ".balign 4",
            "2:",
            "mov r0,r8",
            "mrs r1,cpsr",
            "and r1,r1,#0xf0000000",
            "cmp sp,r9",
            "orrne r1,r1,#1",
            "str r0,[r4]",
            "str r1,[r4,#4]",
            "msr cpsr_f,r7",
            "pop {r4-r10,lr}",
            "bx lr",
            ".ltorg",
        ]
    )


def _append_target(asm: list[str], name: str) -> None:
    asm.extend([f".global {name}_target", f"{name}_target:", "mov r8,r1", "bx r5"])


def _append_false_path(asm: list[str], name: str) -> None:
    asm.extend([f".global {name}_fallthrough", f"{name}_fallthrough:", "mov r8,r2", "bx r5"])


def _row(
    rows: list[dict],
    base: int,
    name: str,
    *,
    initial: int,
    result: int,
    opcode: int,
    displacement: int,
    operation: str,
    condition: str | None = None,
    condition_code: int | None = None,
    verification_scope: str = "thumb_branch",
    condition_taken: bool | None = None,
    target_equals_fallthrough: bool = False,
    target_path_note: str | None = None,
) -> None:
    row = {
        "id": base + len(rows) + 1,
        "name": name,
        "a": 0,
        "b": 0,
        "initial": initial,
        "result": result,
        "flags": initial,
        "mask": NZCV_SP_MASK,
        "aux_kind": "NZCV preserved; bit0 SP changed",
        "operation": operation,
        "condition": condition,
        "condition_code": condition_code,
        "displacement_bytes": displacement,
        "displacement_halfwords": displacement // 2,
        "expected_thumb_opcode": opcode,
        "branch_base": "instruction_address + 4",
        "result_interpretation": "1=branch target; 2=fallthrough; other=mis-target/guard exit",
        "guard_policy": "distinct path markers where representable; overlapping short-backward targets documented per row",
        "architecture": "ARMv4T Thumb conditional B and unconditional B",
        "verification_scope": verification_scope,
        "source": SOURCE,
    }
    if operation == "conditional_branch":
        row["condition_taken"] = result == 1 if condition_taken is None else condition_taken
        if target_equals_fallthrough:
            row["target_equals_fallthrough"] = True
        if target_path_note:
            row["target_path_note"] = target_path_note
    rows.append(row)


def _emit_conditional(
    asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int,
    condition: str, code: int, displacement: int, *,
    flag_states: tuple[int, ...] = FLAGS,
    name_group: str = "cond",
    verification_scope: str = "thumb_branch",
) -> None:
    if not -256 <= displacement <= 254:
        raise ValueError(f"Conditional Thumb branch displacement out of range: {displacement}")
    if displacement & 1:
        raise ValueError("Thumb branch displacement must be halfword aligned")
    name = f"cpu_arm{cpu}_thumb_branch_{name_group}_{condition}_d{displacement:+d}".replace("+", "p").replace("-", "m")
    opcode = 0xD000 | (code << 8) | ((displacement // 2) & 0xFF)
    _append_wrapper(asm, decl, name)

    target_path_note = None
    if displacement == -2:
        # PC is site+4, so this branch targets site+2: the same instruction
        # reached by ordinary fallthrough. Give the aliased path one marker.
        asm.extend(
            [
                f".global {name}_site",
                f"{name}_site:",
                f".hword 0x{opcode:04x}",
                f".global {name}_target",
                f".global {name}_fallthrough",
                f"{name}_target:",
                f"{name}_fallthrough:",
                "mov r8,r2",
                "bx r5",
            ]
        )
        target_path_note = "taken target aliases fallthrough at site+2; both states share marker 2"
    elif displacement == -4:
        # The taken target is the branch itself and loops. Only the EQ-false
        # row is emitted; the opcode/target are still checked in the final ROM.
        if any(condition_passed(code, flags) for flags in flag_states):
            raise ValueError("The -4 self-branch may only be exercised with EQ false")
        asm.extend(
            [
                f".global {name}_target",
                f"{name}_target:",
                f".global {name}_site",
                f"{name}_site:",
                f".hword 0x{opcode:04x}",
            ]
        )
        _append_false_path(asm, name)
        target_path_note = "taken target is the branch itself; taken execution would loop"
    elif displacement == -6:
        # The taken target is the halfword immediately before the branch; after
        # that instruction execution reaches this same branch and loops. Test only EQ false.
        if any(condition_passed(code, flags) for flags in flag_states):
            raise ValueError("The -6 branch may only be exercised with EQ false")
        asm.extend(
            [
                f".global {name}_target",
                f"{name}_target:",
                ".hword 0x46c0",
                f".global {name}_site",
                f"{name}_site:",
                f".hword 0x{opcode:04x}",
            ]
        )
        _append_false_path(asm, name)
        target_path_note = "taken target is the preceding NOP then branch, which loops; test only EQ false"
    elif displacement < 0:
        # T = site + 4 + displacement. The four-byte target body plus exact
        # guard padding positions T without relying on assembler branch relaxation.
        _append_target(asm, name)
        _thumb_guard_padding(asm, -displacement - 8)
        asm.extend([f".global {name}_site", f"{name}_site:", f".hword 0x{opcode:04x}"])
        _append_false_path(asm, name)
    else:
        asm.extend([f".global {name}_site", f"{name}_site:", f".hword 0x{opcode:04x}", f"b {name}_fallthrough"])
        _thumb_guard_padding(asm, displacement)
        _append_target(asm, name)
        _append_false_path(asm, name)

    _append_cleanup(asm)
    for initial in flag_states:
        taken = condition_passed(code, initial)
        _row(
            rows,
            base,
            name,
            initial=initial,
            result=2 if displacement in (-2, -4, -6) else (1 if taken else 2),
            opcode=opcode,
            displacement=displacement,
            operation="conditional_branch",
            condition=condition,
            condition_code=code,
            verification_scope=verification_scope,
            condition_taken=taken,
            target_equals_fallthrough=displacement == -2,
            target_path_note=target_path_note,
        )


def _emit_unconditional(
    asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int,
    displacement: int, *, name_group: str = "b",
    verification_scope: str = "thumb_branch",
) -> None:
    allowed = UNCONDITIONAL_OFFSETS if verification_scope == "thumb_branch" else UNCONDITIONAL_SWEEP_OFFSETS
    if displacement not in allowed:
        raise ValueError(f"Unconditional Thumb branch displacement not in matrix: {displacement}")
    name = f"cpu_arm{cpu}_thumb_branch_{name_group}_d{displacement:+d}".replace("+", "p").replace("-", "m")
    opcode = 0xE000 | ((displacement // 2) & 0x7FF)
    _append_wrapper(asm, decl, name)

    if displacement < 0:
        _append_target(asm, name)
        _thumb_guard_padding(asm, -displacement - 8)
        asm.extend([f".global {name}_site", f"{name}_site:", f".hword 0x{opcode:04x}", f"b {name}_fallthrough"])
        _append_false_path(asm, name)
    else:
        # Put the fallback before the branch so the +2046 encoding is still a
        # real target-range test; the fallthrough recovery branch stays nearby.
        _append_false_path(asm, name)
        asm.extend([f".global {name}_site", f"{name}_site:", f".hword 0x{opcode:04x}", f"b {name}_fallthrough"])
        _thumb_guard_padding(asm, displacement)
        _append_target(asm, name)

    _append_cleanup(asm)
    for initial in FLAGS:
        _row(
            rows,
            base,
            name,
            initial=initial,
            result=1,
            opcode=opcode,
            displacement=displacement,
            operation="unconditional_branch",
            verification_scope=verification_scope,
        )


def extend_thumb_branch(asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int) -> None:
    """Append 320 conditional/NZCV and unconditional branch boundary cases."""
    for condition, code, displacement in conditional_forms():
        _emit_conditional(asm, decl, rows, base, cpu, condition, code, displacement)
    for displacement in UNCONDITIONAL_OFFSETS:
        _emit_unconditional(asm, decl, rows, base, cpu, displacement)


def extend_thumb_branch_sweep(asm: list[str], decl: list[str], rows: list[dict], base: int, cpu: int) -> None:
    """Append a full conditional displacement sweep and stratified unconditional offsets."""
    for displacement in CONDITIONAL_SWEEP_DISPLACEMENTS:
        flag_states = (0,) if displacement in (-4, -6) else CONDITIONAL_SWEEP_FLAGS
        _emit_conditional(
            asm,
            decl,
            rows,
            base,
            cpu,
            "eq",
            0,
            displacement,
            flag_states=flag_states,
            name_group="sweep",
            verification_scope="thumb_branch_sweep",
        )
    for displacement in UNCONDITIONAL_SWEEP_OFFSETS:
        _emit_unconditional(
            asm,
            decl,
            rows,
            base,
            cpu,
            displacement,
            name_group="b_sweep",
            verification_scope="thumb_branch_sweep",
        )
