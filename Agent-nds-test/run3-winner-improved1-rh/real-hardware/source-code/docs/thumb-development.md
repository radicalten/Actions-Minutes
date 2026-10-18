# Thumb expansion — ARM7 and ARM9 development

> **Historical 800-case checkpoint.** The counts and capacity figures in this file predate the branch-boundary matrix and sweep. See the [current suite handbook](guide/README.md) for the live ROM hash, totals, trace map, and outcomes.

**800 new cases per CPU, all passing on the tested DS backend at this checkpoint.**
IDs: ARM9 **7011–7810**, ARM7 **5687–6486**. Old IDs/expectations are unchanged.
This is coverage-first development, not a final release.

## Exact matrix

| Family | Cases per CPU | Scope |
|---|---:|---|
| LSL/LSR/ASR/ROR, separate registers |512|4 values ×8 counts ×4 flag states ×4 shifts|
| Same-register shifts (Rd=Rs) |64|values/counts0,1,32,257 ×4 flags ×4 shifts|
| ADC/SBC |128|8 operand pairs ×4 flags ×2 operations ×distinct/aliased registers|
| NEG |32|8 operand pairs ×4 flag states; result uses source Rs|
| MUL |64|8 operand pairs ×4 flags ×distinct/aliased registers|
| **Total** |**800**|15 helpers per CPU|

Shift values:00000000,00000001,80000001,FFFFFFFF. Counts:0,1,31,32,33,255,256,257.
Initial flag states:00000000,20000000,90000000,F0000000. Shift counts use their low
8 bits. Register count0 preserves carry; large shifts and full-word rotates check
carry boundaries and sign extension. ADC/SBC check incoming carry/borrow, result
and NZCV; NEG checks zero minus Rs. MUL checks low32-bit result and defined flags.

**CPU-specific MUL comparison:** ARM9 preserves C and V, so NZCV are checked.
ARM7's MUL carry is destroyed/unspecified, so its mask is **D0000000 (NZV)**:
C is deliberately not asserted. This is not an ARM7 carry PASS or a fabricated C
expectation. Both initial V values and both C values appear in the selected matrix.

The wrapper sets flags in ARM state, enters the actual16-bit Thumb instruction via
BX, returns to ARM via BX without changing NZCV, captures results, then restores
caller flags. Source/destination alias forms are explicit. No Thumb2 instruction
or undefined/unpredictable encoding is introduced by these new helpers.

Generator: `scripts/thumb_edges.py`. Reference:
[GBATEK Thumb register ALU operations](https://mgba-emu.github.io/gbatek/#thumbopcodesregisteroperationsalubx),
particularly the THUMB.4 opcode table and affected-flags description. The cached
reference's MUL distinction is preserved rather than imposing ARM9 behavior on ARM7.

## Executed verification

- `reports/development.json`: current ARM9 **7810 =7490 PASS /320 FAIL**;
  ARM7 **6486 =6463 PASS /23 FAIL**. All1600 new DS executions pass.
- `reports/qemu-thumb.json`:800 ARM9-profile cases PASS on ARM946;
  800 ARM7-profile cases PASS on both ARM946 and ti925t. The actual CPU profiles
  have different MUL masks. ti925t remains an **ARMv4T proxy**, not exact ARM7TDMI.
- `reports/thumb-tests.json`:11 regression scenarios. Checks actual ELF helper
  addresses, exact Thumb opcode halfwords and BX return encodings, and byte-identical
  helper code across CPUs.15 primary and15 checked-flag expectation mutations per
  CPU are detected. ADC/SBC→ADD/SUB opcode mutations expose64 lost-carry cases per
  CPU. Two MUL carry-expectation mutations fail on ARM9 but correctly remain outside
  ARM7's comparison mask. No baseline mismatch is suppressed.
- Existing QEMU subsets were rerun (shared, portable ARM9, privileged, CP15 aliases,
  original CPU groups and both prior register-operand2 expansions). Raw exception
  oracle remains PARTIAL_UNSUPPORTED for128 MPU-abort cases; this is not PASS.
- Prior512-case ARM operand2 regression,16 trace-protocol/runtime-boundary checks,
  all12 LCD pages and all3 recovery scenarios rerun successfully.
- Supervised group score remains **17/25 =68%**, zero unrun groups, with two native
  division crashes explicitly recovered by host restart.

## Limits and next work

This expands selected Thumb operand/flag/alias matrices, not exhaustive Thumb
coverage. More register choices, immediate encodings, high-register/PC/SP forms,
load/store address/alias/bus-region matrices, branch ranges, stack edge cases and
CPU-specific control behavior remain. Physical hardware and timing are unverified.

At this historical checkpoint, trace v2 capacity9828 per CPU left **2018 ARM9 /3342 ARM7 slots**. The current suite uses trace v5 with 13,822-record slots; see the [current trace map](guide/architecture-and-traces.md). Every vector remains represented in the current generated manifest. The final-release gate remains BLOCKED; broader coverage and a full current-release regression/package review are still required.
