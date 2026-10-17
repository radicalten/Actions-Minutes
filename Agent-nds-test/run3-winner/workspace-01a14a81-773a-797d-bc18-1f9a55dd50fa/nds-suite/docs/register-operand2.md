# Milestone 9 — register-controlled ARM operand2

**Historical milestone-9 baseline below.** Milestone10 adds256 flag-only cases
per CPU; current totals6498/5174, with unchanged320/23 failures. See
[flag-only operand2](compare-operand2.md) for the current addition and capacity.


Added **256 cases per CPU**, preserving all prior IDs:
ARM9 **5987–6242**; ARM7 **4663–4918**. These common architectural cases are appended
to each CPU's existing profile rather than renumbering the shared prefix.

## Exact matrix

- ADCS, SBCS, ANDS, EORS.
- Register-controlled LSL, LSR, ASR, ROR operand2.
- Shift counts 0,1,31,32,33,255,256,257; only the low eight bits select the shift.
- Both incoming carry values.
- Fixed Rn=`7FFFFFFF`, Rm=`80000001`; old V alternates by shift-count row.
- 4 operations ×4 shifts ×8 counts ×2 carry values =256 cases /16 helpers per CPU.

`initial` contains both the incoming flag bits and the shift-register value.
The helper writes CPSR_f from it and supplies the **unmasked full register** as Rs.
Its high flag bits and count bit8 deliberately verify that Rs's high bits are ignored.
A zero register count preserves shifter carry (unlike immediate LSR/ASR zero
encodings). ROR count32 is a rotation by a full word with carry from bit31.

ADC/SBC use **incoming CPSR.C**, not shifter carry, for arithmetic. Their output C/V
come from arithmetic. AND/EOR take shifter carry and preserve old V. All four check
NZ. The helper captures flags before restoring the caller's original flag field.
No PC operand, register alias, unpredictable encoding or timing assumption is used.

Python expectations use explicit unbounded integer arithmetic, signed overflow and
separate shifter-carry computation. Reference:
[GBATEK ARM data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu).
Source generator: `scripts/register_operand2.py`.

## Verification

- DS ARM9: all256 PASS; **6,242 total =5,922 PASS /320 retained FAIL**.
- DS ARM7: all256 PASS; **4,918 total =4,895 PASS /23 retained FAIL**.
- `run_qemu_operand2.py`:256 PASS on ARM946 and ti925t. The latter remains an
  **ARMv4T proxy, not exact ARM7TDMI**. The portable ARM7-targeted oracle binary runs
  on both models; the DS regression verifies that both linked CPU helper bodies
  are byte-identical and execute at the correct respective ELF addresses.
- Primary and NZCV expectation mutations:16 helper-level cases of each kind,
  separately on each CPU. All detected without suppressing prior failures.
- Actual opcode mutations replace ADC with ADD and SBC with SUB. Exactly64 lost
  carry-input cases per CPU become additional failures, as expected.
- Nine new execution/regression scenarios;34 host tests; all existing milestone-8,
  ARM7 attribution, negative, recovery and12-page LCD checks pass their assertions.
- Full supervised suite remains **17/25 groups (68%)**, zero unrun groups. Two native
  division crashes are recovered by host restart, not by the standalone ROM.

This adds a selected operand/flag matrix, **not exhaustive coverage**. Other ALU
operations, operand/register/alias combinations, complete PC/encoding matrices,
ARM7 raw exceptions/FIQ, priority/deeper nesting, CP15 enforcement and cycle timing
remain open. Milestone-8 QEMU MPU limitations and all retained discrepancies are
unchanged; see `arm9-milestone8.md` and `remaining-cases.json`.

## Capacity and storage

Trace slots still hold6552 records: **310 ARM9 /1634 ARM7 slots remain**.
Check capacity before the next expansion; do not silently truncate traces.
Only the newest verified milestone ZIP is retained. Diagnostic blobs are pooled
by content hash; reports continue to reference/hash-verify their original bytes.
Historical design documents and regression tests remain because they describe
coverage still present in the latest ROM, not separate old release copies.
