# Upload staging status

**Status: PRE-RELEASE STAGING — NOT v1.0.** This trimmed upload tree is not a release approval, a clean full-suite result, or legal clearance to publish the combined ROM.

## Identity and current totals

- ROM: `suite.nds`
- SHA-256: `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`
- Trace protocol v5; group/score schema v3; 27 registered groups
- ARM9 / ARM946E-S profile: **13,380 vectors; 13,060 PASS / 320 FAIL**
- ARM7 / ARM7TDMI execution profile: **12,056 vectors; 12,033 PASS / 23 FAIL**
- Trace capacity: **13,822 records per CPU**; ARM9 has 442 free slots, ARM7 has 1,766
- `SHA256SUMS` lists every staged file except itself. From this directory, verify with `sha256sum -c SHA256SUMS`.

## Agent-oriented additions

- `scripts/compare_emulator_runs.py` validates current ROM identity, group/score/trace schemas, manifest CRC, and trace blobs before comparing two trace-backed reports by `(CPU, ID)`. It distinguishes checked output changes, unmasked auxiliary changes, status transitions, incomplete selections, and restored-record provenance. It does not declare either emulator or the registered expectation correct.
- `scripts/run_desmume.py` now isolates worker logs by output stem by default, or accepts `--log-dir`, so separate baseline/candidate runs do not overwrite one another's logs.
- `docs/guide/agent-emulator-workflow.md` documents focused CPU/ISA runs, compatible report inputs, and differential triage. Host tests cover the comparator's hash/schema safety and status/mask/provenance classifications.

## Validation for this ROM snapshot

- `make build`, final `make host`, focused `make thumb-branch`, and `make immediate-alu` completed. The new focused branch and immediate additions passed their defined DeSmuME/QEMU checks, final-ROM encoding audits, and fault-injection tests. `make diagnostics`, trace protocol/provenance checks, and `scripts/test_remaining.py` also passed.
- The immediate matrices comprise 528 selected ARM rotated-immediate and 2,048 Thumb imm8 vectors per CPU. `ti925t` remains an ARMv4T proxy only.
- The current trace-v5 checkpointed full run completed all 27 groups: **17 PASS, 8 FAIL, 2 emulator crashes**. It recovered the crashes but retained all non-pass outcomes. This is not a clean full-suite PASS.
- `make qemu-exceptions` returned **PARTIAL_UNSUPPORTED**: 128 low-RAM-adapted ARM946-model entry cases passed and 128 MPU cases were unsupported because MPU-enable readback is zero. The command exits 2; this is not hardware evidence.
- The release gate is **BLOCKED** for remaining coverage, exception/system, exact-core/hardware, regression-plan, and licensing requirements.

Current suite documentation is in [`docs/guide/`](docs/guide/README.md); the machine-readable scope ledger is [`docs/remaining-cases.json`](docs/remaining-cases.json). Local generated reports and trace blobs are not included in this staging tree.

## Redistribution warning — resolve before public upload

The root [`LICENSE`](LICENSE) applies to original contributions only. [`NOTICE.md`](NOTICE.md) and [`upstream-lock.json`](upstream-lock.json) record that redistribution permission for the pinned Rockwrestler revision is unresolved. The combined ROM includes upstream-derived material. **Do not treat this staging folder, MIT license, or fetch script as permission to publicly distribute the combined ROM or third-party-derived source.** Obtain written permission or complete an approved licensing review first. No Nintendo BIOS or firmware is bundled.

## Included and intentionally omitted

Included: current `src/`, compiled `suite.nds`, current guide/open-case ledger, build/test/QEMU/DeSmuME scripts, trace-backed comparison tooling, host tests, CI workflow, pinned upstream lock, notices/licenses, and future-agent handoff notes.

Omitted: `.git/`, `vendor/`, `work/`, `artifacts/`, `reports/`, `.venv/`, caches, older milestone documents, and obsolete fixed-count `scripts/package.py` / `scripts/test_host.py`. Upstream retrieval is for local development and does not grant redistribution rights.

## Before calling this v1.0

1. Resolve upstream and combined-ROM redistribution permission.
2. Investigate existing vector failures and both emulator crashes with independent evidence.
3. Resolve exact ARM7TDMI and physical-hardware scope or obtain an approved scope decision.
4. Complete release planning and required exception/system tests.
5. Rebuild staging from a clean checkout, verify contents/checksums, and only then label or publish a release.
