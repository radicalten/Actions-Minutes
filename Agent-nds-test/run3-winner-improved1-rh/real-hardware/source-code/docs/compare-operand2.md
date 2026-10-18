# Milestone 10 — flag-only ARM operand2

Adds256 cases per CPU: **ARM9 IDs6243–6498; ARM7 IDs4919–5174**.
All previous IDs/expectations remain stable.

## Exact scope

TST,TEQ,CMP,CMN ×register-controlled LSL,LSR,ASR,ROR ×counts
0,1,31,32,33,255,256,257 ×both incoming carry values =256 cases /16 helpers.
Operands remain Rn=7FFFFFFF, Rm=80000001. Old V alternates by count row.
The full `initial` argument supplies both flags and Rs; its high bits must not
participate in the shift count. This is a selected fixed-operand matrix, not all
operand/register/encoding cross-products.

- TST/TEQ set NZ from AND/XOR, C from the shifter, and preserve V.
- CMP/CMN set NZCV from subtraction/addition, independent of incoming carry.
- Every case also verifies that Rn is unchanged: test/compare instructions do not
  write their arithmetic/logical result into the source register.
- Captured flags precede restoration of the caller's original flag field.

Reference: [GBATEK ARM data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu),
including its returned-CPSR-flags and register-shift descriptions.
Generator: `scripts/compare_operand2.py`, using the integer shift/arithmetic model
in `register_operand2.py`. QEMU is an independently executed implementation, not
physical hardware verification.

## Evidence

- All256 new cases PASS on each DS CPU.
- All256 portable cases PASS QEMU ARM946 and ti925t, the explicitly labeled ARMv4T
  proxy. Exact ARM7TDMI QEMU remains unavailable.
- Linked helper machine-code bodies match across both DS CPUs. Every new case's
  recorded entry address is checked against its corresponding ELF symbol.
-16 primary and16 NZCV expectation mutations per CPU are detected.
- Actual opcode mutations replace TST/TEQ/CMP/CMN with ANDS/EORS/SUBS/ADDS targeting
  Rn. Flags stay identical, but **212 cases per CPU detect an unintended source
  write**. The other44 produce a value coincidentally equal to Rn; they are not
  claimed as detected faults. All256 mutated cases retain the expected flags.
- Nine new regression scenarios,36 host tests, retained negative/ARM7 attribution/
  privileged/milestone8/operand2 regressions, recovery checks and all12 LCD pages.

Current totals: **ARM9 6498 =6178 PASS /320 FAIL; ARM7 5174 =5151 PASS /23 FAIL**.
All existing discrepancies and the independent MPU-oracle limitation remain.
Supervised group score: **17/25 =68%**, zero unrun groups; two native crashes still
require host restart. This is not one uninterrupted execution.

## Capacity and remaining work

Every current CPU vector is retained in the diagnostics. Capacity remains6552
records per CPU, leaving **54 ARM9 /1378 ARM7 slots**. A larger ARM9 expansion
requires an explicit trace-layout/capacity change, with matching runtime, host,
restore, UI and corruption-check regressions—not silent truncation.

RSC and other untested operand2 forms, broader operand/register/alias matrices,
ARM7 raw exceptions/FIQ, raw IRQ LR/priority/deeper nesting, MPU/cache/TCM behavior,
remaining mGBA source ports, and physical timing verification remain open.
No exhaustive-coverage claim is made. Only the newest verified archive is retained.
