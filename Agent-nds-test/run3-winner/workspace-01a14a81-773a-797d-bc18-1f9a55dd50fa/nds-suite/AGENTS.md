> **v1.0 target: coverage first.** See [release plan](docs/v1.0-release.md).
> Original project contributions are MIT-licensed; upstream redistribution permission still blocks public release.

# AI-agent operating contract

## Current state / release policy

**Coverage-first development, NOT a final release.** Read `docs/release-development.md`
and `docs/remaining-cases.json`. Current development ROM:9466 ARM9 /8142 ARM7 cases.
Last packaged release is milestone10; keep that single recovery archive until a
new release is fully verified, then remove it. Do not publish a final label simply
because a finite matrix or automation script passes.

## CPU group migration — schema3

Read `docs/cpu-groups.md`. Four independently executed/scored groups:0 ARM7,
1 ARM7Thumb,2 ARM9,3 ARM9Thumb. Original upstream IDs shift by+2. Total27 groups;
score17/27=62.96%, with unchanged per-vector failures. Prefer name selectors:
`run_desmume.py --group ARM7Thumb` or `--group ARM9Thumb`; discover via `--list-groups`.
Unselected vectors are NOT_SELECTED, never PASS. CSV/JSON carry cpu_group,group_id,
isa,isa_basis. ARM return instructions targeting Thumb remain classified ARM.
Legacy ARM7 mailbox describes the last ISA group; use score.cpu7 for aggregates.

## Unattended operation

Use `.venv/bin/python` for emulator runners; `bash scripts/setup.sh` restores dependencies.
`bash scripts/build.sh` regenerates both CPU profiles and uses devkitARM. No button
press or display/OCR is needed for execution or result collection.

```
.venv/bin/python scripts/run_resilient.py --output reports/development.json
.venv/bin/python scripts/run_qemu_remaining.py
.venv/bin/python scripts/run_qemu_thumb_memory.py
.venv/bin/python scripts/test_thumb_memory.py
.venv/bin/python scripts/test_trace_provenance.py
.venv/bin/python scripts/run_qemu_thumb.py
.venv/bin/python scripts/test_remaining.py
.venv/bin/python scripts/test_thumb_edges.py
.venv/bin/python scripts/test_cpu_groups.py
.venv/bin/python scripts/test_trace_protocol.py
.venv/bin/python scripts/release_gate.py
```

Runner exit1 with a valid completed report means observed nonpasses; never erase or
rewrite expectations to make CI green. A Python traceback, missing/stale report,
malformed protocol or tool failure is not a test result. Bound all runs by wall/frame
limits. Read runner `--help`; don't chain expected-nonzero runs using `&&`.
The readiness gate emits JSON only:0 READY,2 BLOCKED,3 ERROR. It currently MUST be
BLOCKED. Group PASS counts are not opcode or executed-assertion counts.

## Authoritative evidence

Validate `base_rom_sha256` against the actual ROM. Identity is `(CPU,id)`, not id alone.
CPU traces are v4; addresses/capacity have one source: `src/trace_layout.h`, read by
`scripts/trace_layout.py`. Never restore v1/v2/v3 traces into the v4 layout. Host decoding
validates version, CPU, fingerprint, record width/count, states and completion.
Restore validates the whole input set before any emulator memory write.

Use JSON plus hashed binary traces, not screenshots or scraped log text. Historical
traces are explicitly marked; never call a resumed run uninterrupted. Unknown,
unrun, interrupted, unsupported and failed cases cannot become PASS. Raw-exception
sentinels are not captured CPSR values. Physical timing and exact ARM7TDMI QEMU are
unverified. ti925t is only a proxy; QEMU Integrator/CP is not Nintendo DS hardware.

## Reference design

The pinned mGBA-auto release boots unattended, logs BEGIN/END, explicitly SKIPs
interactive-only tests, resets IRQ state between suites and emits ALL DONE. Follow
these principles, not GBA-specific debug transport or hardware expectations.
NDS uses score/ledger mailboxes and lossless per-CPU traces; native emulator crashes
need checkpoint/restart supervision. ALL DONE/completion is never synonymous with
all assertions passing. See `docs/release-development.md` for exact provenance.

## Tests and change discipline

Never edit vendor assertions to hide a backend discrepancy. Preserve old case IDs.
Append cases, update fingerprints/counts/capacity checks and oracle scopes. Keep
legal semantics separate from undefined/unpredictable characterization. For every
new family, execute the target CPU, independently check when possible, and inject
primary/auxiliary/instruction faults. Do not mistake expected baseline failures for
regression-test failures; compare exact changed IDs.

Current focused evidence is listed in the development document. Older primary
release reports and fixed-count host tests describe milestone10 and are NOT proof
for the current ROM; refresh the complete release suite before packaging. Existing
package guards intentionally reject stale evidence. Do not bypass those guards.

Keep one verified ZIP, pool immutable trace blobs with `compact_evidence.py`, and
remove only unreferenced/redundant generated data. Keep source, reference provenance,
and all blobs referenced by retained reports. Never silently trim trace records.

## Latest Thumb memory and resume update

Read `docs/thumb-memory.md`. All2496 new executions pass; manifest/CSV describe
addressing, width, aliases, opcode and guard policy. State bit0x100 is per-record
history; the header history bit does not label newly executed records. ARM9 must
reattach its runtime trace writer after partial restore. Mixed-history regression
is `test_trace_provenance.py`. Full release gauntlet is still pending.

## v1.0 repository readiness

`release/v1.0-plan.json` is a target plan, not a published version declaration.
Run `scripts/test_release_readiness.py` for gate regression. Never change approval
or milestone fields merely to obtain READY: link review/validation evidence first.
Licensing clearance is a human review, not something a boolean can establish.
Do not invoke the historical milestone10 package script for v1.0.

## Thumb SP batch

See `docs/thumb-sp.md`; scope `thumb_sp`,192 cases/12 helpers per CPU.
Run `scripts/test_thumb_sp.py` and `scripts/run_qemu_thumb_sp.py`.
Trace headroom:578 ARM9/1902 ARM7; check capacity before the next large batch.
Root MIT license covers original contributions, not unresolved upstream rights.

## Latest PC-relative batch

See `docs/thumb-pc.md`; scope `thumb_pc`,216 cases/54 helpers per CPU.
Run `scripts/test_thumb_pc.py` and `scripts/run_qemu_thumb_pc.py`.
Current headroom362 ARM9/1686 ARM7 supersedes prior batch counts.
Review trace capacity before branch/stack expansion; never truncate evidence.

Secondary regression reports may omit replayable `breakdown.arm*/families` and
`failures` arrays to conserve storage; they explicitly declare this omission.
Aggregate outcomes and hashed raw traces remain. Reconstruct with
`diagnostics.summarize(report['diagnostics'])` using the matching manifest.
The primary development report and offline debugger keep full details.
`compact_derived_reports.py` performs this lossless-with-respect-to-raw-evidence
compaction after `compact_evidence.py`; do not infer absent arrays mean no failures.
