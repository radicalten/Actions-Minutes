# GBATEK-guided CPU coverage — milestones 2–10

> **Historical milestone record.** Its totals and “remaining” lists are checkpoint-era snapshots. For the current suite matrix, branch additions, trace layout and tested outcomes, use the [current-suite handbook](guide/README.md).

Reference: [GBATEK ARM CPU Reference](https://mgba-emu.github.io/gbatek/#armcpureference), retrieved 2026-10-17. `gbatek-source.json` records the page SHA-256 and source anchors. GBATEK is a technical reference, not an executable oracle or proof that our interpretation is correct. Selected subsets are independently executed through QEMU; DS-only cases and explicit oracle limitations are documented per milestone.

## Milestone 2 additions (retained)

| Reference section | Added checks | Scope / limits |
|---|---|---|
| [ARM data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu) | ARM register LSL/LSR/ASR/ROR, immediate shifts, RRX | Register counts 0, 1, 31, 32, 33, 255, 256, 257; immediate boundary counts; both incoming carry values. No full ALU operand2 cross-product or PC operands. |
| [Thumb instruction summary](https://mgba-emu.github.io/gbatek/#thumbinstructionsummary) | Thumb immediate LSL/LSR/ASR; word register/immediate loads/stores; byte/halfword signed/unsigned loads; byte/halfword stores | Defined, aligned forms in low registers. High-register, branch, exception and stack matrices still pending. |
| [Multiply](https://mgba-emu.github.io/gbatek/#armopcodesmultiplyandmultiplyaccumulatemulmla) | MLA; UMULL/SMULL/UMLAL/SMLAL full 64-bit results and separate NZ checks | Accumulator high word zero, low word 0 or FFFFFFFF. This is not a full port of the GBA multiply-long table. Does not assert ARM7 long-multiply C/V behavior. |
| [Single data transfer](https://mgba-emu.github.io/gbatek/#armopcodesmemorysingledatatransferldrstrpld) | ARM LDR/STR immediate offset, positive/negative pre/post writeback, shifted register offset | Result and base-register delta checked. No unprivileged T forms, abort behavior, or exhaustive register aliases. |
| [Halfword/signed transfers](https://mgba-emu.github.io/gbatek/#armopcodesmemoryhalfworddoublewordandsigneddatatransfer) | ARM/Thumb LDRB/LDRH/LDRSB/LDRSH; STRB/STRH byte-lane preservation | Aligned cases only; guard words checked. Milestone 5 adds 96 ARM9-only aligned LDRD/STRD vectors; full alias/offset matrices remain open. |
| [SWP](https://mgba-emu.github.io/gbatek/#armopcodesmemorysingledataswapswp) | SWP and SWPB | Checks both the returned old value and new memory contents. Aligned, single-core scratch storage only; no bus atomicity test. |
| [Block transfers](https://mgba-emu.github.io/gbatek/#armopcodesmemoryblockdatatransferldmstm) | ARM LDM/STM IA, IB, DA, DB | Two registers; base excluded from transfer list; correct register/memory ordering. Writeback occurs but these cases do not separately assert its resulting value. |

**528 additional vectors**, bringing the total from 653 to **1,181 per CPU**. There are 70 ARM test functions and 37 Thumb test functions, with multiple inputs per function. These counts are not counts of all legal ARM/Thumb encodings.

Every added vector contains a source URL in `src/generated/vectors.json`. The old carry-vector IDs 561–653 remain stable. Newly added IDs begin at 654. Some helpers use the second result word for a high product, writeback delta, guard word or second register instead of CPSR; `aux_kind` identifies it. Historical manifest keys `flags`/`mask` hold the expected second word and its comparison mask in these cases.

## QEMU verification added

`run_qemu_rock.py` runs eleven upstream CPU groups independently on QEMU `arm946`:

1. ARM condition codes
2. CLZ
3. QADD/QSUB
4. QDADD/QDSUB
5. SMULxy
6. SMLAxy
7. SMULWy
8. SMLAWy
9. SMLALxy
10. BLX
11. LDR/POP/LDM PC interworking

All eleven pass in the recorded run. The original assertions, rather than rewritten expectations, are linked into the oracle. Each group has a separate ELF/process so Q flags and other state cannot leak from one group to another. ARMv5 instructions are not executed on the ARMv4T proxy.

The Rockwrestler LDM/STM characterization group is deliberately excluded from the QEMU oracle: it contains architecturally unpredictable base-in-register-list cases and banked-register behavior. It remains included in the DS ROM and scored normally as an upstream group. This distinction is not a silent skip to obtain green results; it is recorded in `reports/qemu-rock.json`.

## CPU-specific distinctions not yet implemented

[GBATEK alignment notes](https://mgba-emu.github.io/gbatek/#armcpumemoryalignments) explicitly distinguish ARM7 and ARM9 misaligned halfword behavior. This milestone avoids those cases rather than using a shared expectation. A later implementation needs separate CPU-specific expectations and real-hardware evidence for implementation-specific behavior.

GBATEK also describes instructions from architectures later than the DS processors. For example, UMAAL is ARMv6, not a missing legal DS opcode. Future encoding manifests must distinguish legal ARMv4T/ARMv5TE forms, later-generation instructions, undefined encodings, and unpredictable operands. Merely seeing an instruction name in GBATEK does not make it applicable to both DS CPUs.

## Milestone 3 additions — 353 Thumb vectors

At milestone 3, the total was **1,534 vectors per CPU** across **144 functions (70 ARM, 74 Thumb)**. Of these, 781 vectors execute ARM test instructions and 753 execute Thumb test instructions. IDs 1–1,181 are unchanged; milestone 3 appends IDs 1,182–1,534.

The new cases are based on [GBATEK's Thumb instruction summary](https://mgba-emu.github.io/gbatek/#thumbinstructionsummary) and implemented in `scripts/thumb_extensions.py`:

| Family | New coverage | Important boundaries |
|---|---|---|
| Conditional B | All 14 legal conditions × all 16 NZCV combinations = **224 cases** | Short local branch targets; no maximum-range/encoding-bit sweep or invalid condition 14/15 forms. |
| High-register MOV/ADD/CMP | High source/destination forms using R8; MOV/ADD flag preservation; CMP NZCV | Representative R8 cases, not all high registers, PC writes, or every alias. |
| B and BL | Forward/backward branches; BL call/return in both directions; LR Thumb bit | Local targets; not the full ±4 MiB displacement space or independent malformed BL halves. |
| PC-relative LDR and ADR | Literal pool value and independently ARM-calculated label address | One aligned local literal/address, not every PC alignment/offset. |
| SP-relative LDR/STR/ADD | Word access at offset 12, guard word, address calculation | No full immediate-range sweep. |
| SP adjustment | Positive and negative 508-byte adjustment and restoration | Address differences only; no access outside the allocated scratch area. |
| PUSH/POP | Two low registers; LR push placement; POP PC valid Thumb target and restored SP | Representative register lists, not all masks or CPU-specific interworking edges. |
| Thumb LDMIA/STMIA | Data ordering and separate base-writeback checks | Two low registers, base outside the list; unpredictable base-in-list cases excluded. |

The other **129 cases** cover the non-conditional families above. All 1,534 shared vectors pass in the recorded DS ARM9/ARM7 execution and both QEMU comparison models. This does not upgrade `ti925t` into an exact ARM7 model.

Fault-injection tests deliberately corrupt expectations for new `branch_eq`, `push_lr`, and `pop_pc` vectors and verify the exact failing vector ID plus 1,533 passed / 1 failed counts. The new cases are not merely entries in a coverage list.

Milestone 3 approached the ARM7 private-WRAM stack boundary. Milestone 4 relocates the ARM7 code/data image to a separate main-RAM region at 0x02200000 while keeping private-WRAM stacks. This removes that immediate capacity limit but changes instruction-fetch timing; timing equivalence is not claimed.

## Milestone 4 additions — 1,856 ARM vectors

Milestone-4 total: **3,390 shared vectors per CPU**, comprising 2,637 ARM and 753 Thumb vectors across 186 ARM and 74 Thumb helpers. Prior IDs 1–1,534 remain stable; new IDs are 1,535–3,390.

- **240 conditional execution cases:** 15 legal ARM conditions (including AL) across all 16 NZCV combinations. Tests check the conditional result and preservation of flags. The ARMv5 unconditional encoding space is not treated as a universally legal NV condition.
- **1,600 immediate-shift operand2 cases:** AND/EOR/ORR/BIC/MOV/MVN/ADD/SUB/ADC/SBC; ten shift forms (LSL 0/1/31, LSR 1/32, ASR 1/32, ROR 1/31, RRX); eight operand pairs; both incoming carry values. Logical operations check shifter carry while arithmetic checks its own carry/overflow, including RRX plus ADC/SBC interactions.
- **16 flags-only MSR/MRS cases:** write NZCV combinations and verify readback plus preservation of non-NZCV bits. These do not cover control-field changes, SPSR, privilege transitions or exceptions.

Sources: [ARM flags/conditions](https://mgba-emu.github.io/gbatek/#armcpuflagsconditionfieldcond), [ARM data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu), and [PSR transfers](https://mgba-emu.github.io/gbatek/#armopcodespsrtransfermrsmsr). Implementation is in `scripts/arm_extensions.py`; each vector records its relevant source anchor.

All shared cases pass both emulated DS CPUs, QEMU ARM946 and the labeled ti925t proxy. Additional fault-injection runs corrupt new conditional, shifted-operand ADC/RRX and PSR expectations, verifying the exact failing ID and 3,389 passed / 1 failed counters. Exact ARM7TDMI verification remains unavailable.

These are selected legal forms and operand classes, not the full shifter/register/PC/immediate-encoding cross-product, exception/control-state matrix or exhaustive encoding coverage.

## Still incomplete

- Every instruction form/register alias/condition/addressing combination.
- Full original mGBA shifter and multiply-long source ports.
- Exhaustive Thumb branch-range, register-alias, SP/PC-relative, stack and exception matrices (representative legal forms are now implemented).
- ARM exceptions/banked registers, complete PSR/CP15 tests, aborts, SWI and undefined instructions.
- Remaining DSP/interworking and LDRD/STRD edge matrices, chip-specific misalignment.
- Hardware timing, DS memory-controller/BIOS integration and the remaining GBA hardware groups.
- Exact ARM7TDMI QEMU verification: stock QEMU here still has no such CPU model. `ti925t` remains a labeled proxy, not a replacement.

The reference narrows and documents implementation work; it does not eliminate these verification gaps.


## Milestone 5 additions — 96 ARM9-only doubleword vectors

ARM9 now runs **3,486** vectors; ARM7 still runs the **3,390 shared** vectors. Added IDs 3,391–3,486 cover LDRD and STRD with aligned immediate-offset, pre-index increment/decrement with writeback, post-index increment/decrement, and register-offset forms. For each of the six addressing modes, four data pairs exercise data and writeback separately: 2 opcodes × 6 modes × 2 result checks × 4 pairs = 96 vectors / 24 helpers.

Generated by `scripts/generate_v5.py`, based on GBATEK's halfword/doubleword/signed-transfer section. Scratch addresses are eight-byte aligned, paired registers legal, base and destination non-overlapping. Misalignment, aborts, undefined aliases, all register/offset encodings and timing remain untested. No ARMv5 cases are dispatched on ARM7 or ti925t. The separate `run_qemu_v5.py` oracle and actual DS ARM9 execution both pass all 96 cases.

Full CPU debugging is now available; this improves evidence granularity, not the claim of exhaustive opcode coverage. See `docs/debugging.md` and `coverage.json` for remaining limitations.

## Milestone 6 — ARM7-targeted semantics

Added 188 ARM7-specific profile cases / 23 helpers, linked only into ARM7. ARM7 total is 3,578: 3,555 PASS and 23 retained GBATEK-reference mismatches on the tested DeSmuME core. Alignment tests include ARM7 odd LDRH rotation and odd LDRSH byte sign extension; PC tests include pipeline/store values and non-interworking loads. See [ARM7 audit](arm7-specific.md) for exact counts, execution-address proof, fault/hang injections and limits. No exact ARM7 QEMU or physical hardware oracle is claimed; the new cases are excluded from the ti925t proxy comparison. ARM9 total remains 3,486.

## Milestone 7 — privileged state and real IRQ

Added 1,052 ARM7 cases: 956 bank/PSR/software-return cases and 96 real IRQ/User/mask-gating cases. All new cases pass; ARM7 total 4,630 = 4,607 PASS / 23 retained mismatches. Portable cases also pass QEMU ARM946 and the ti925t proxy; NDS IRQ cases do not run in that oracle. See [exact matrices and caveats](arm7-privileged.md) and [remaining work](remaining-cases.json). SWI/undefined/abort/FIQ entry and timing are not declared covered.

## Milestone 8 — ARM9 system/correctness and ARM7 nesting

Added 2,500 ARM9 and 32 ARM7 cases. Current counts are 5,986 / 4,662; full coverage remains incomplete. [Exact DSP, CP15, exception and nesting matrices, oracle scopes, observed failures and safety](arm9-milestone8.md). Earlier sections retain historical counts, not current totals.

## Milestone 9 — register-controlled operand2

Added256 cases per CPU; all pass on DS and the ARM946/ti925t portable oracle. Current totals6,242 ARM9 /4,918 ARM7. [Exact scope and verification](register-operand2.md). This is a selected matrix, not completion of all operand/encoding/alias combinations.

## Milestone 10 — flag-only register operand2

Added256 TST/TEQ/CMP/CMN cases per CPU; all pass on DS and the ARM946/ti925t portable oracle. Current totals6498/5174, with320/23 retained failures. [Exact scope and verification](compare-operand2.md). ARM9 trace capacity has54 slots left; expansion must respect or revise that limit.
