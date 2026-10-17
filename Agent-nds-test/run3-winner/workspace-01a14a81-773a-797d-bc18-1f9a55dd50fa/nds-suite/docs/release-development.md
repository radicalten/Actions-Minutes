# Coverage-first final-release work — development status

User decision: close remaining CPU coverage before final release. Automation must
also be suitable for AI agents. **This workspace is NOT a final release.** Only
milestone10.zip is retained as the last verified packaged recovery point. Current
source and `suite.nds` are development files; previous primary release reports are
not valid evidence for this ROM unless their SHA-256 matches.

## Current update — Thumb PC-relative address and literal load

Added216 cases per CPU, all PASS;14 execution/fault scenarios PASS.
Current ARM9:9466 (9146/320); ARM7:8142 (8119/23);17/27 groups,0 unrun.
See [PC-relative matrix](thumb-pc.md). Trace headroom362/1686.
Focused regressions and deterministic rebuild refreshed; full release gauntlet
and Rockwrestler redistribution clearance remain incomplete.

## Earlier batch (historical counts)

## Current update — MIT approval and Thumb SP matrix

Original project contributions are now MIT-licensed; see root LICENSE and NOTICE.md.
Rockwrestler redistribution clearance remains blocked. Coverage-first policy remains.
Added192 SP cases per CPU, all PASS;18 fault/execution scenarios PASS.
Current ARM9:9250 (8930/320); ARM7:7926 (7903/23);17/27 groups,0 unrun.
See [Thumb SP details](thumb-sp.md). Trace headroom578/1902. Current evidence
and deterministic rebuild refreshed; full release gauntlet remains PENDING.

## Previous batch (historical counts)

## Current update — Thumb memory and per-record provenance

Added1248 aligned Thumb memory cases per CPU, all PASS. Current totals:
ARM9 **9058 (8738 PASS /320 FAIL)**; ARM7 **7734 (7711 PASS /23 FAIL)**.
Full score17/27=62.96%,0 unrun; no new baseline discrepancies.
See [matrix and fault tests](thumb-memory.md) and [CPU groups](cpu-groups.md).
Registry/score/manifest3; trace4. Current trace headroom770/2094.

Focused tests refreshed:24 memory scenarios,13 CPU-group scenarios, mixed-record
resume provenance, protocol,12 LCD pages,3 recovery scenarios, prior Thumb800 and
ARM512 matrices. Focused QEMU memory/Thumb/remaining subsets pass; exact ARM7
and hardware validation remain unavailable. ROM rebuild is byte-identical; see
`reports/development-rebuild.sha256`. Full release gauntlet remains PENDING.

## Earlier development steps (historical counts and protocol versions)

## Current update — four scored CPU groups

See [CPU-group migration](cpu-groups.md). CPU vector counts remain7810/6486, but
four independent generated-vector groups replace the previous two. Full score now
17/27=62.96% (same vector failures),0 unrun. Registry/score/trace schema3. All four
selectors, fault isolation and restart into Thumb after ARM-only hangs are tested.
Older step-specific scores/layout descriptions below are historical.

## Latest update — Thumb coverage

Added800 Thumb edge/alias cases per CPU, all PASS. Current totals **7810 ARM9
(7490 PASS /320 FAIL),6486 ARM7 (6463 PASS /23 FAIL)**. See
[Thumb development details](thumb-development.md) for matrices, CPU-specific MUL
flag masks, QEMU verification and11 fault-injection/execution scenarios. The older
step-specific counts below are historical. Current trace headroom is2018/3342.


## Reference review

Reference release: https://github.com/mattrbeck/mgba-suite-auto/releases/tag/latest
Release r101 identifies commit2b239cfe63f15ec8ab9cbedb4c09498fc98e7b70, matching the
pinned vendor snapshot. Public release metadata is saved in
`reports/automation-reference-release.json`. Source audit:
`vendor/mgba-suite-auto/src/main.c` lines232–260.

Observed design: unattended boot, BEGIN/END per suite, explicit SKIP for an absent
interactive runner, IRQ reinitialization between suites, terminal ALL DONE. Adopt
these properties without copying GBA-only debug hardware or assuming completion
means PASS. NDS already boots unattended, resets hardware between groups, publishes
score/ledger mailboxes, and retains per-vector evidence; native crashes require
host checkpoint/restart. The reference does not itself prove full NDS coverage.

## Implemented this development step

1. Trace layout v2,9828 records/CPU,384KiB slots:
   - ARM9:02300000–0235ffff
   - ARM7:02360000–023bffff
   - assertion journal:023c0000, maximum81984 bytes, before restore gate023ed000
   - canonical constants in `src/trace_layout.h`; host imports that same definition.
2. Runtime rejects zero/out-of-range IDs, oversized plans and end-without-begin;
   header word12 records errors. Error traces cannot be marked complete.
3. Host validates version,CPU,fingerprint,width/count,byte length,states and completed
   counters. Restore validates every blob before any emulator write. v1 cannot be
   restored into v2. Legacy release reports are not current development evidence.
4.512 new cases on each CPU: ADD/SUB/RSB/RSC/ORR/BIC/MOV/MVN, four register shifts,
   eight count boundaries, both incoming carry values. Same fixed operand pair and
   alternating old-V pattern as milestone9; MOV/MVN omit Rn. With earlier matrices,
   all16 ARM data-processing operations now have this selected register-shift
   matrix. **This does not complete all operand values, aliases or encoding forms.**
5. `AGENTS.md`: unattended commands, evidence/exit-code rules, stable CPU-qualified
   IDs, oracle limitations, restore rules and storage policy.
6. `scripts/release_gate.py`: machine-readable coverage-first readiness; exit0 READY,
   2 BLOCKED,3 ERROR. It currently reports BLOCKED. Known backend failures are not
   automatically coverage gaps; never rewrite expected values to satisfy a backend.

## Executed evidence

- `reports/development.json`: ARM9 **7010:6690 PASS /320 FAIL**; ARM7
  **5686:5663 PASS /23 FAIL**;17/25 groups68%,zero unrun after supervision.
- `qemu-remaining.json`:512 PASS on ARM946 and ti925t ARMv4T proxy. No exact ARM7TDMI
  or physical DS claim. Other prior oracle subsets have not all been refreshed here.
- `remaining-tests.json`:nine scenarios; correct ELF entry addresses, identical
  helper machine-code bytes,32 primary and32 NZCV expectation mutations per CPU,
  and32 reverse-borrow failures per CPU from RSC→RSB opcode mutation.
- `trace-protocol-tests.json`:16 checks including actual7010-record trace, malformed
  version/CPU/count/width/fingerprint/state/length/completion/runtime-error rejection,
  all-input restore preflight and native guard-page execution of the real writer.
  The native test is memory-safety evidence, not ARM hardware timing evidence.
- All12 lower-screen navigation/score-preservation tests and all3 recovery scenarios
  rerun successfully with the new layout. Two backend-native crashes remain visible.

No expectations were changed to suppress320 ARM9/23 ARM7 discrepancies. Trace
capacity now leaves2818 ARM9/4142 ARM7 slots. The new tests cross the previous ARM9
limit, proving that expansion is used in actual execution rather than only a constant.

## Final-release acceptance criteria / open work

Final coverage means documented legal instruction forms and CPU-specific behavior
with defined boundary matrices and provenance—not brute-force enumeration of all
32-bit operand combinations. Do not claim unsupported hardware behavior as verified.

1. Close or explicitly resolve every gap in `remaining-cases.json` and the CPU
   uncovered list: memory/addressing/aliases, PC/encoding forms, exception/FIQ and
   IRQ LR/priority/deeper nesting, CP15/cache/TCM behavior and outstanding source ports.
2. Resolve the ARM7 exact-oracle and physical timing limitations, or obtain an explicit
   scope decision rather than silently removing them from the release requirements.
3. Obtain independent MPU-abort verification; current QEMU ARM946 cannot enable MPU
   in the recorded environment. Raw-abort expectations are not independently PASS.
4. Refresh all count-sensitive legacy host/integration tests and every primary
   report for this ROM. Current focused tests are not the complete release gauntlet.
5. Complete the automation contract across runner error paths: versioned normalized
   results, stale/missing evidence detection, bounded runs, replayable CPU-qualified
   failure selection, genuine SKIP/UNSUPPORTED vs FAIL vs harness-error classification.
6. Deterministic rebuild, full source/report hashes, fresh package guards, licensing
   review, then replace the retained milestone10 archive only after verification.

Next agent: use the focused commands in AGENTS.md first. `scripts/package.py` still
has milestone10 guards and must reject current mixed/stale release evidence; do
not bypass these guards. Read `reports/release-readiness.json` for current blockers.
