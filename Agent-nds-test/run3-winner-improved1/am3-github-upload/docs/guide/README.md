# Current suite handbook

This is the canonical, current documentation set for the generated `suite.nds` CPU-vector suite. It supersedes checkpoint-era counts and layouts in older milestone notes. Historical documents may remain in the full development repository for context, but are intentionally omitted from this trimmed staging tree. Use this guide for the current matrix, trace map, test commands, results, and limitations.

**Snapshot date:** 2026-10-17  
**ROM:** `suite.nds`  
**SHA-256:** `fa7509bfafb6cfffaba83ec591efad698953b997c073bce7cf08f19d1a0142b4`  
**Trace protocol:** v4 · **Group/score schema:** v3 · **Registered groups:** 27

## Current snapshot

| Profile | ARM vectors | Thumb vectors | Total vectors | DeSmuME observed PASS | DeSmuME observed FAIL |
|---|---:|---:|---:|---:|---:|
| ARM7TDMI profile | 4,816 | 4,664 | **9,480** | **9,457** | **23** |
| ARM946E-S profile | 6,113 | 4,691 | **10,804** | **10,484** | **320** |

The profiles share 3,390 vectors. ARM9 adds 7,414 profile-specific vectors; ARM7 adds 6,090. A new branch expansion contributes **1,338 vectors per profile**: the earlier 320-case boundary matrix plus a 1,018-case displacement sweep. All 1,338 new branch vectors passed their focused DeSmuME Thumb-group runs on both profiles.

The full isolated DeSmuME run attempted all 27 groups: **17 PASS, 8 FAIL, 2 emulator crashes**. The checkpointed run completed the group ledger but preserves those same 8 failures and 2 crashes as non-passing outcomes. Its score summary reports 17 passed and 10 failed because it folds the two crashes into the non-pass count. The full-suite issues were present before the sweep; this update did not resolve them. See [coverage and current results](coverage-matrix.md).

## Trace capacity

Trace v4 retains its 64-byte header and 40-byte records. The page-aligned slots are now `0x6c000` bytes, allowing **11,057 records per CPU**. The current ROM uses 10,804 on ARM9 (253 free slots) and 9,480 on ARM7 (1,577 free slots). The relocated assertion journal still ends below the fixed restore-acknowledgement gate. The layout, runtime guard-page test, full ROM traces, and checkpoint/restore provenance test all passed; details are in [architecture and trace layout](architecture-and-traces.md).

## Read this first

1. [Coverage matrix and current results](coverage-matrix.md) — vector totals, per-group outcomes, and what the full runs actually established.
2. [Thumb branch sweep](thumb-branch-boundaries.md) — exact encoding, the per-offset matrix, edge cases that cannot safely take a backward branch, and test evidence.
3. [Build and test guide](build-and-test.md) — reproducible setup, commands, outputs, and exit-code interpretation.
4. [Architecture and trace layout](architecture-and-traces.md) — generators, stable IDs, trace v4 memory map, and record model.
5. [Limitations and release status](limitations-and-release.md) — oracle gaps, unimplemented coverage, known failures, and redistribution status.

## Evidence and scope rules

- A generated vector is planned coverage; it is not a PASS until a valid execution trace records it.
- DeSmuME NDS-ROM execution, QEMU functional comparison, host-only checks, and physical-hardware testing are separate evidence classes.
- QEMU `arm946` provides an ARM946-class functional comparison; `ti925t` is only an ARMv4T proxy, not an exact ARM7TDMI model. QEMU Integrator/CP is not a Nintendo DS system model.
- No physical DS test was run for this snapshot. No DS BIOS/firmware is bundled.
- The branch suite encodes and audits all 256 legal conditional-B displacements, but the taken paths for −4 and −6 bytes are non-terminating by construction and therefore only EQ-false rows are executed. The −2-byte target aliases fallthrough. Unconditional B uses 32 stratified interior offsets plus its range/zero boundaries; it is not an exhaustive sweep.
- The release-readiness gate remains BLOCKED. Combined-ROM redistribution clearance is unresolved; read [`NOTICE.md`](../../NOTICE.md).

## Source of truth

The generated per-vector manifest is `src/generated/debug-manifest.json`. The trace constants are centralized in `src/trace_layout.h`; scripts derive the same constants from that file. Vector identity is `(CPU, ID)`, because profile-specific IDs can overlap.

The legacy `docs/gbatek-coverage.md`, `docs/debugging.md`, `docs/release-development.md`, and milestone pages are historical material in the full development repository and are intentionally not included here. Their old totals/layout descriptions are not current unless explicitly restated in this handbook.
