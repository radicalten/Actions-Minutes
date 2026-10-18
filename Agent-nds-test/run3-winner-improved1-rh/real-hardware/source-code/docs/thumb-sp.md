# Thumb SP adjustment and address generation

> **Historical SP-matrix checkpoint.** Totals and capacity figures below predate the branch-boundary matrix/sweep. See the [current handbook](guide/README.md) for the current ROM and trace layout.

Development addition: **192 cases /12 helpers per CPU**,384 new DS executions,
all PASS. Stable IDs: ARM9 9059–9250; ARM7 7735–7926.

## Matrix and interpretation

- ADD SP,#imm and SUB SP,#imm: byte immediates0,4,508 (encoded0,1,127).
- ADD Rd,SP,#imm: Rd=r0/r1, byte immediates0,4,1020 (encoded0,1,255).
- Four initial scratch-stack displacements0,4,16,124 and four incoming NZCV
  patterns0,20000000,90000000,f0000000 per helper.
- Result is the unsigned destination delta from the initial SP, not an absolute
  runtime address. For adjustment instructions the destination is SP itself.
- Auxiliary mask f0000001: preserved NZCV and bit0 indicating an unexpected SP
  change during address generation. Address destinations start poisoned.
- Wrapper saves/restores the actual stack independently; it never dereferences
  the adjusted SP. This tests arithmetic/address formation, not stack memory,
  interrupts during adjustment, alignment faults or PUSH/POP behavior.

Source/model: `scripts/thumb_sp.py`. GBATEK Thumb.12 and Thumb.13:
https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryaddressingaddpcsp
PC-relative Thumb.12 forms are **not** covered by this batch.

Metadata includes `verification_scope=thumb_sp`, operation, addressing,
offset_bytes, destination_register, expected_thumb_opcode, result_interpretation,
guard_policy, stable CPU-qualified IDs and group/ISA. All input/result/flag/XOR
fields use the existing agent-readable protocol; no interactive execution needed.

## Validation

`reports/thumb-sp-tests.json`:18 scenarios PASS (nine per CPU): baseline192
and all12 actual instruction encodings;12 primary expectation faults;12 NZCV
expectation faults; actual sign, offset, destination and missing-adjustment
instruction faults; SP and NZCV clobbers after successful address generation.
The latter two retain the correct primary result and fail the auxiliary check.
Fault scenario counts overlap vectors; do not sum them as distinct added tests.

`reports/qemu-thumb-sp.json`:192 PASS on ARM9-profile ARM946 and ARM7-profile
ARM946/ti925t. ti925t remains a proxy, not exact ARM7TDMI or DS hardware evidence.
Existing Thumb-memory, Thumb-ALU, ARM-operand2, CPU-group isolation, mixed-record
resume, trace protocol, UI and recovery regressions were refreshed successfully.
Rebuild is byte-identical: `reports/development-rebuild.sha256`.

## Current results and next work

At this historical checkpoint, ARM7:7,926=7,903 PASS/23 FAIL and ARM9:9,250=8,930 PASS/320 FAIL. The full group score was 17/27=62.96% after supervised crash recovery. Trace capacity9828 then left578 ARM9 and1,902 ARM7 slots; the live suite now has trace v5 13,822-record slots (see the [trace map](guide/architecture-and-traces.md)).

Literal/PC-relative forms and branch boundaries were subsequent work; broader high-register/PC and stack/multiple-transfer matrices remain. Current totals/outcomes are in the [handbook](guide/README.md). Coverage-first v1.0 and upstream redistribution clearance remain incomplete.
