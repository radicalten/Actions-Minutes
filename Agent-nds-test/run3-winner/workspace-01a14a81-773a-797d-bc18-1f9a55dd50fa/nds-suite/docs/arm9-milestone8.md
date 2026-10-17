# Milestone 8 — ARM9 correctness/system expansion and ARM7 nesting

**Historical milestone-8 baseline below.** Milestone9 adds256 passing cases per CPU;
current totals6,242 ARM9 /4,918 ARM7, with unchanged320/23 failures. See
[register-controlled operand2](register-operand2.md) for the current expansion.


This is a finite, GBATEK-guided expansion, not exhaustive ISA or exception coverage.
All earlier IDs are stable. Identity remains **(CPU, ID)**.

## Added matrices

| CPU / IDs | Cases | Scope |
|---|---:|---|
| ARM9 3487–4446 | 960 | QADD/QSUB/QDADD/QDSUB saturation/sticky Q; SMULxy, SMLAxy, SMULWy, SMLAWy, SMLALxy signed halfword/word products and accumulators |
| ARM9 4447–5402 | 956 | Banked SP/LR, FIQ r8–r12, SPSR isolation/field writes, CPSR modes, software-prepared ARM/Thumb exception returns |
| ARM9 5403–5498 | 96 | Real timer IRQ from ARM/Thumb/User; CPSR.I, IME and IE pending-delivery gates; DTCM-derived BIOS callback hook |
| ARM9 5499–5730 | 232 | 24 cacheability/bufferability bitmap checks, 80 region-register mirrors, 128 basic/extended AP aliases; MPU disabled during register-only checks |
| ARM9 5731–5986 | 256 | Raw SWI, undefined, data-abort and prefetch-abort entry/link/controlled return, ARM/Thumb × 16 NZCV combinations |
| ARM7 4631–4662 | 32 | Timer3 outer IRQ permitting timer2 inner IRQ, ARM/Thumb origins; inner SPSR, outer return flags/mode and callback count 2 |

Total additions: **2,500 ARM9 + 32 ARM7**. Current totals: **5,986 ARM9 / 4,662 ARM7**.
DSP integer reference uses signed arithmetic, two-stage QD saturation, sticky Q and
preserved NZCV where defined. SMLALxy checks both halves; selected legal registers
and operands do not exhaust aliases, encodings or all overflow cross-products.

## Raw ARM9 exception mechanics and safety

`arm9_exceptions.py` temporarily enables low ITCM vectors, clears SCTLR.V, disables
MPU/cache, and installs a two-word absolute trampoline at the selected vector.
It saves/restores the old control, ITCM control, vector words, exception bank's
SPSR/LR and caller context. The custom handler uses no exception-mode stack.
The source is SYS with I/F masked; entry CPSR/SPSR/LR and callback count are captured.

SWI/UDF link checks expect ARM +4 / Thumb +2; data abort +8 in both states;
prefetch abort +4 in both. Data abort skips the failed LDR. Prefetch abort captures
the original SPSR, then deliberately clears T and redirects to ARM cleanup:
**this is controlled return, not an unchanged prefetch-abort return path**.

Abort cases save all eight MPU region registers and both AP representations.
Region0 covers all 4 GiB with AP3; region7 overlays **021f0000–021f0fff**, denying
data or instruction access. The scratch word is saved/restored. MPU-enable is
read back. Safe fallthrough/BX stubs let a backend that ignores permissions finish
with FAIL rather than escape into unbounded code. Thumb undefined has two BX guards
because the tested backend treats the reserved conditional encoding like a branch.
The linker and post-build guard reserve the scratch region below the ARM7 image.

Raw auxiliary mask is `F0000FFF`: NZCV/source or returned CPSR plus:

- bit8: handler count was not exactly one;
- bit9: MPU-enable did not read back as set (abort cases only);
- bit10: control/ITCM restoration readback mismatch;
- bit11: vector-word restoration mismatch.

Capture is initialized to `BAADC0DE`. **`B00000DE` in a no-entry result is a masked
sentinel, not an observed exception CPSR.** Returned flags may still be real when
entry never occurred. Check bit8 and the primary/auxiliary meaning before analysis.
Restoration readback flags are zero in the recorded DS run. This does not prove
all timing/cache behavior or every saved register on physical hardware.

## Observed emulator result

Tested backend: py-desmume 0.0.9 / bundled DeSmuME 0.9.12.

- ARM9 **5,666 PASS / 320 FAIL**: 64 CP15 region mirrors, 96 AP aliases,
  32 Thumb undefined nonentries, 64 data-abort and 64 prefetch-abort nonentries.
- Raw SWI: 64 PASS; ARM undefined: 32 PASS; Thumb undefined: 32 FAIL.
- MPU-enable reads as set on the DS backend, but no abort handler is entered.
- ARM7 **4,639 PASS / 23 FAIL**; all 32 new nested cases pass. Earlier 20 odd
  halfword-load and three PC/pipeline discrepancies remain unchanged.
- **17/25 groups = 68%**, six returned failures, two native division crashes,
  zero unrun after checkpointed supervision. This is not one uninterrupted boot.

Expectations have not been changed to match the backend. MPU-abort expectations
remain reference-derived, without a working independent MPU oracle/hardware run.

## Independent verification and its limits

- `run_qemu_v5.py`: **2,012 PASS**, ARM946 — original 96 doubleword + 960 DSP +
  956 privileged cases. IRQ/raw/CP15 subsets are excluded from this runner.
- `run_qemu_cp15.py`: **208 PASS**, ARM946 — 80 region mirrors + 128 AP aliases.
  Cacheability/bufferability bitmaps and permission enforcement are excluded.
- `run_qemu_exceptions.py`: **128 PASS, 128 UNSUPPORTED** on recorded QEMU 10.0.13.
  The model lacks the NDS ITCM-control register. The oracle-only assembly copy
  replaces its accesses with NOP/zero moves and uses writable low RAM vectors;
  the distributed ROM is unchanged. All 64 SWI and 64 undefined cases pass.
  MPU-enable reads zero for all 128 abort cases; their mismatches are retained in
  the report as capability-limited, not converted to PASS. Runner status is
  `PARTIAL_UNSUPPORTED`, exit 1. The unadapted attempt faulted before its first SWI.
- Shared 3,390 cases still pass ARM946 and ti925t; ARM7's 956 portable privileged
  cases pass both too. **ti925t is an ARMv4T proxy, not ARM7TDMI**.
- Integrator/CP QEMU is not NDS. No QEMU IRQ/BIOS/TCM enforcement claim, no physical
  DS validation, and no cycle-timing claim.

## Regression proof

`test_milestone8.py` runs actual DS code: all new helpers with a passing baseline
case receive a primary-expectation mutation (**220 helper-level injections**).
Separate runs detect a sticky-Q auxiliary mutation, 96 missing ARM9 IRQ counters,
64 missing raw SWI handler counters, and 32 missing second ARM7 IRQ counters.
Baseline 320/23 failures remain visible throughout; no known failure is relabeled.
Existing negative, privileged, ARM7 attribution, recovery, 12-page LCD and host
regressions are retained. Injection ROMs are temporary, not distributed.

## Remaining work

ARM7 BIOS-owned raw SWI/undefined/abort entry remains open. FIQ stimulus/entry,
IRQ raw-LR and priority matrices, deeper nesting, all instruction/alias/bus-region
cross-products, cache/TCM enforcement, and timing remain incomplete. ARM9 raw tests
cover selected SYS-mode entry paths, not all origin modes or all abort instructions.
Use `remaining-cases.json`, not vector totals, as the completion ledger.

References: GBATEK ARM CPU exceptions, PSR transfers, multiply/saturation instructions,
and ARM CP15 protection-unit sections; source URLs are recorded per vector and in
`gbatek-source.json`. QEMU's own results are separate evidence, not hardware truth.
