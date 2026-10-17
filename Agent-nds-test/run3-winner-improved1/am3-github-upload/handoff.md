# Maintainer / agent handoff — current suite snapshot

## Status and source of truth

Coverage-first development, **not a release**. Do not package or label this ROM v1.0. The canonical documentation is the [suite handbook](docs/guide/README.md); older milestone pages are historical. `scripts/package.py` retains milestone-10 fixed-count guards and is not a current packager—do not bypass them.

Current `suite.nds` SHA-256: `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`.

- ARM9 / ARM946E-S profile: **13,380 vectors; 13,060 PASS / 320 FAIL**.
- ARM7 / ARM7TDMI profile: **12,056 vectors; 12,033 PASS / 23 FAIL**.
- New Thumb branch suite: **1,338 vectors per CPU** (320 boundary + 1,018 sweep), 306 helpers per CPU; all pass focused DeSmuME runs. QEMU comparisons, final-ROM encoding/target checks, and fault injections pass.
- Branch sweep special cases: −2 bytes aliases fallthrough; taken EQ branches at −4/−6 would loop, so only EQ-false rows execute there. These opcodes/targets are still ROM-audited.
- Immediate-ALU additions: 528 ARM rotated-immediate vectors (all 16 rotate fields across all 16 data-processing opcodes for two imm8 values, plus C/V companions) and 2,048 Thumb imm8 vectors for MOVS/CMP/ADDS/SUBS per CPU. All passed focused DeSmuME and QEMU functional tests; final-ROM opcodes were audited and eight result/flag mutations detected.
- Four CPU/ISA groups plus 23 upstream/system groups: **27 total**. The current trace-v5 checkpointed run recorded 17 PASS, 8 FAIL, 2 emulator crashes. It recovered both crashes and completed the ledger; the crashes remain non-pass outcomes.
- ARM946 QEMU exception-model probe: **PARTIAL_UNSUPPORTED** (128 low-RAM-adapted entry cases passed, 128 MPU cases unsupported because MPU-enable readback is zero). `make qemu-exceptions` exits 2; this is not hardware evidence. See local `reports/qemu-exceptions.json`.
- Trace v5 capacity is **13,822 records per CPU** in unchanged `0x6c000` slots, using 32-byte records with a runtime-input fingerprint. ARM9 has **442 slots** left; ARM7 has **1,766**. Full v5 recovery, guard-page, v4 read-compatibility, restore preflight, and mixed-provenance tests passed. Assertion journal ends `0xfc0` before the restore gate.
- Exact ARM7TDMI QEMU and physical DS testing are unavailable. QEMU `ti925t` is an ARMv4T proxy only.
- Combined-ROM redistribution clearance remains unresolved. Root MIT covers original project work only; see [`NOTICE.md`](NOTICE.md).

The machine-readable open-scope ledger is [`docs/remaining-cases.json`](docs/remaining-cases.json). The release gate is expected to remain BLOCKED.

## Clean build and tests

```sh
python3 scripts/fetch_upstreams.py       # pinned local upstreams; not a license grant
bash scripts/setup.sh                   # review first; sudo/apt/network access
make build                              # DevkitARM + ndstool required
make host                               # host-only contracts; not CPU execution
make thumb-branch                       # QEMU, DeSmuME, final-ROM audit and mutations
make immediate-alu                      # ARM/Thumb immediate suites and fault tests
```

Focused/full-suite and trace commands:

```sh
.venv/bin/python scripts/run_desmume.py --list-groups
.venv/bin/python scripts/run_desmume.py --group ARM9Thumb --output reports/arm9thumb.json
.venv/bin/python scripts/run_resilient.py --output reports/development.json
.venv/bin/python scripts/test_trace_protocol.py
.venv/bin/python scripts/test_trace_provenance.py
.venv/bin/python scripts/test_thumb_edges.py
.venv/bin/python scripts/test_remaining.py
```

To compare reference/candidate emulator builds, run the same ROM hash into separate output/log paths, then use `python3 scripts/compare_emulator_runs.py REF.json CANDIDATE.json`. The comparator validates current ROM, group/score/trace schema, manifest CRC, and v5 blobs; it keeps CPU-qualified differences, masks, and historical provenance separate. It is differential evidence, not an oracle. See [`docs/guide/agent-emulator-workflow.md`](docs/guide/agent-emulator-workflow.md).

The isolated full runner is expected to exit 1 for the recorded failures/crashes. The resilient runner recovers those crashes but also exits 1 because the group ledger retains non-pass results. Inspect JSON/XML and hashed traces; do not suppress outcomes. The current report paths under `reports/` are local generated evidence.

Pinned upstreams and required paths are in `upstream-lock.json`. `python3 scripts/fetch_upstreams.py --check` validates clean snapshots without network. `make host` and host-only CI do not validate CPU semantics.

## Change discipline

1. Preserve stable CPU-qualified vector IDs; append new cases only.
2. Current trace capacity is 13,822 per CPU; do not exceed it or silently truncate records. Any further expansion requires a reviewed memory-layout and runtime-validation change.
3. For a new family, execute the intended target profile, inspect final-ROM instruction/target layout when relevant, compare an independent oracle where possible, and inject expected-value/instruction faults with exact added failing IDs.
4. Separate architectural expectations, emulator observations, unsupported behavior, and physical-hardware evidence. Do not edit imported assertions or expectations merely to make a backend green.
5. Match reports and traces to their ROM SHA-256 and protocol; restored records are explicitly historical.
6. Release-gate exit codes are 0 READY, 2 BLOCKED, 3 ERROR. Never alter approval/milestone data to obtain READY; licensing clearance is a human review.

The requested `.patch` file was deleted and must not be recreated. The source, generated manifests/ROM, and documentation are updated directly in the workspace for the user to upload.
