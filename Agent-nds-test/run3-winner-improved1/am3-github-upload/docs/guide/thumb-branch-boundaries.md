# Thumb branch boundaries and displacement sweep

The 2026-10-17 suite update retains the original **320-case boundary matrix** and adds a **1,018-case sweep** to each CPU profile: **1,338 branch vectors per profile**. The implementation is in `scripts/thumb_branch.py`, integrated by `scripts/generate_v5.py` (ARM9) and `scripts/generate_arm7.py` (ARM7). IDs append after existing cases: ARM9 boundary IDs **9,467–9,786**, sweep IDs **9,787–10,804**; ARM7 boundary IDs **8,143–8,462**, sweep IDs **8,463–9,480**.

Reference: [GBATEK — THUMB Jumps and Calls](https://mgba-emu.github.io/gbatek/#thumbopcodesjumpsandcalls) ([GBATEK Thumb instruction summary mirror](https://problemkaputt.de/gbatek-thumb-instruction-summary.htm)). The reference informs expectations; it is not an execution oracle.

## Encoding and target arithmetic

For a Thumb branch instruction at byte address `A`, these ARMv4T branch forms calculate the target from `A + 4`:

```text
conditional B: opcode 1101 | cond[3:0] | imm8
               target = A + 4 + sign_extend(imm8, 8) * 2
               legal displacements: -256 through +254 bytes

unconditional B: opcode 11100 | imm11
                 target = A + 4 + sign_extend(imm11, 11) * 2
                 legal displacements: -2048 through +2046 bytes
```

The low immediate is a signed count of halfwords. The generator writes the intended opcode as an explicit `.hword`, avoiding dependence on assembler relaxation or silently accepting a nearby label. Final-ROM tests read each emitted halfword and use ELF symbols to check `target == site + 4 + displacement`.

## Matrix composition

| Subset | Construction | Cases per CPU |
|---|---|---:|
| Conditional boundary conditions | Each of the 14 legal conditions × all 16 NZCV states at an alternating −256 / +254 byte endpoint | 224 |
| Opposite conditional edges | EQ at +254 and NE at −256, each × all 16 NZCV states | 32 |
| Unconditional boundaries | `B` at −2048, −2046, 0, and +2046 bytes, each × all 16 NZCV states | 64 |
| **Original boundary matrix** | 20 helpers, 16 rows per helper | **320** |
| Conditional interior sweep | EQ over every even displacement from −254 through +252; two states except the special −4 and −6 cases, which use EQ-false only | 506 |
| Unconditional interior sweep | 32 stratified interior offsets × all 16 NZCV states | 512 |
| **Added sweep** | 286 helpers | **1,018** |
| **Total branch suite** | 306 helpers per profile | **1,338** |

The 14 conditions are EQ, NE, CS/HS, CC/LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, and LE. The condition evaluator is host-tested against all 16 NZCV nibbles. Condition codes 14 and 15 are not treated as legal conditional-branch cases.

Across the original matrix and sweep, all **256 legal conditional-B displacement values** are encoded and audited in the final ROM. The conditional sweep uses EQ-false (`Z=0`) and EQ-true (`Z=1`) states for most interior offsets. The unconditional sweep samples the positive and negative byte offsets with magnitudes **62, 64, 66, 126, 128, 130, 254, 256, 258, 510, 512, 514, 1,022, 1,024, 1,536, and 2,044**; all 16 NZCV states execute at each. This is a stratified sample, not every unconditional immediate.

## Special near-backward targets

Three short negative displacements cannot use the ordinary independent target/fallthrough fixture safely:

- **−2 bytes:** `A + 4 − 2 = A + 2`, exactly the fallthrough instruction. The taken target and fallthrough alias, so both flag states share marker 2. The final opcode and target address are still checked.
- **−4 bytes:** the target is the branch instruction itself. A taken EQ branch loops forever, so only the EQ-false row is executed. The exact opcode and self-target are checked in the final ROM.
- **−6 bytes:** the target is the preceding halfword; that NOP falls into the branch, which loops if taken. Only EQ-false is executed. The exact opcode and target address are checked in the final ROM.

These are explicit limitations, not passes for the non-terminating taken paths. The rest of the normal fixture gives target and fallthrough distinct markers and uses exit guards so a mis-target cannot drift into the valid target.

## Runtime fixture and flags

Each helper enters in ARM state, saves caller state, installs an NZCV pattern, and switches into Thumb at the exact branch site. A successful target writes marker 1; the false/fallthrough path writes marker 2. Guards exit on mis-targets. The auxiliary comparison checks unchanged NZCV plus bit 0 as a stack-pointer integrity sentinel (`mask = 0xF0000001`). Helpers restore the caller's CPSR flags and stack before returning. The test is about branch decision, displacement, and flag/SP preservation—not general register side effects.

For unconditional `B`, all 16 NZCV patterns verify that flags remain unchanged. For conditional rows, flags are selected to test both the condition outcome and preservation, except the non-terminating target cases described above.

## Verification performed

The rebuilt ROM SHA-256 is `fa7509bfafb6cfffaba83ec591efad698953b997c073bce7cf08f19d1a0142b4`.

| Evidence | Outcome |
|---|---|
| ARM7Thumb DeSmuME group | All 1,338 branch vectors PASS; the group still has 10 pre-existing non-branch failures. |
| ARM9Thumb DeSmuME group | All 1,338 branch vectors PASS; the group still has 96 pre-existing non-branch failures. |
| Final-ROM instruction/target audit | All 306 helper encodings and target addresses checked for each CPU profile (612 helper instances total). |
| Expected-result and NZCV mutations | Four selected result fields and four selected flags fields corrupted per CPU; exactly the selected vectors fail in each run. |
| Opcode/offset fault injection | EQ→NE at −256 and unconditional `B` +2046→+2044, plus a sweep EQ −2→0 mutation; exactly 33 affected vectors are diagnosed per CPU. |
| QEMU `arm946` | ARM9 profile PASS; ARM7 profile also PASS as a functional ARMv4T/CPU compatibility comparison. |
| QEMU `ti925t` | ARM7 profile PASS as an ARMv4T proxy only; **not** exact ARM7TDMI verification. |

The +2044 mutation lands on a guard instead of the +2046 target. Mutations and layout checks operate on the final NDS ROM, not only generated source or manifest values. Reports are written under local `reports/`.

## Where it lives

- Generator and source-of-truth metadata: `scripts/thumb_branch.py`.
- Profile integration: `scripts/generate_v5.py`, `scripts/generate_arm7.py`.
- Host invariants: `scripts/test_thumb_branch_generator.py`, `scripts/test_source_contract.py`.
- QEMU comparison: `scripts/run_qemu_thumb_branch.py`.
- ROM encoding, execution, and fault injection: `scripts/test_thumb_branch.py`.
- End-to-end target: `make thumb-branch`.

The expanded trace layout has **253 ARM9 slots** and **1,577 ARM7 slots** remaining. Further vector growth still needs a reviewed capacity/layout change before exceeding the current slots. See [architecture and trace layout](architecture-and-traces.md).
