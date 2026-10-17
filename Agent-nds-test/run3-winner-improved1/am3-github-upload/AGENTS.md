> **Coverage-first development; not a release.** The current source/ROM snapshot is documented in the [suite handbook](docs/guide/README.md). The combined ROM is not cleared for public redistribution; root MIT applies to original work only.

# AI-agent operating contract

## Current state

- Current `suite.nds` SHA-256: `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`.
- ARM9/ARM946E-S: 13,380 vectors (13,060 PASS / 320 FAIL); ARM7/ARM7TDMI profile: 12,056 (12,033 PASS / 23 FAIL). Shared prefix: 3,390. Trace v5 capacity: 13,822 per CPU in unchanged `0x6c000` slots, leaving **442 ARM9** and 1,766 ARM7 slots. v4 traces are readable but read-only.
- Four independently scored CPU/ISA groups: ARM7, ARM7Thumb, ARM9, ARM9Thumb. Group/score schema 3; trace schema 5; 27 registered groups.
- Branch scope: 320 boundary + 1,018 sweep = **1,338 vectors / 306 helpers per CPU**. Immediate scope: **528 ARM vectors / 512 helpers** plus **2,048 Thumb vectors / 1,024 helpers per CPU**, all focused DeSmuME/QEMU checks passing. All new cases pass focused DeSmuME; QEMU comparisons and branch fault injections pass. The EQ-taken paths at −4/−6 bytes are non-terminating and only their false rows execute; −2 aliases fallthrough. See [branch documentation](docs/guide/thumb-branch-boundaries.md).
- The latest trace-v5 checkpointed full run attempted all groups: 17 PASS, 8 FAIL, 2 emulator crashes; recovery completed the ledger without changing these outcomes. Existing CPU-profile mismatch totals are ARM7 23 and ARM9 320; the two DIV crashes remain unresolved. See [current results](docs/guide/coverage-matrix.md).
- The ARM946 QEMU exception-model probe was PARTIAL_UNSUPPORTED: 128 low-RAM-adapted entry cases passed, 128 MPU cases were unsupported because MPU-enable readback is zero; `make qemu-exceptions` exits 2. This is not hardware evidence (`reports/qemu-exceptions.json`).
- Exact ARM7TDMI QEMU and physical DS validation remain unavailable. `ti925t` is a proxy only. The release-readiness gate must remain BLOCKED until the actual criteria are resolved.

Use `docs/guide/` as the canonical current documentation. The earlier milestone pages and old handoff entries are history, not fresh-ROM evidence. `docs/remaining-cases.json` is the open-scope ledger.

## Build and commands

Fresh Linux setup uses Debian/Ubuntu-style apt, `sudo`, and network access. Review `scripts/setup.sh` first.

```sh
python3 scripts/fetch_upstreams.py
bash scripts/setup.sh
make build
make host
make thumb-branch
make immediate-alu
```

Pinned upstreams are in `upstream-lock.json`; fetch/check does not grant redistribution permission. `make host` is host-only and is not CPU execution evidence. `make thumb-branch` rebuilds, runs the branch QEMU comparisons, executes each DS Thumb group in DeSmuME, and applies result/flags/opcode fault injections. `make immediate-alu` verifies the selected immediate matrices, final-ROM encodings, and expectation mutations.

Full suite and recovery/trace validation:

```sh
.venv/bin/python scripts/run_desmume.py --list-groups
.venv/bin/python scripts/run_desmume.py --group ARM9Thumb --output reports/arm9thumb.json
.venv/bin/python scripts/run_resilient.py --output reports/development.json
.venv/bin/python scripts/test_trace_protocol.py
.venv/bin/python scripts/test_trace_provenance.py
```

For differential debugging, run the same `suite.nds` hash under reference/candidate emulator builds with unique `--output` paths (and optional unique `--log-dir`), then run `python3 scripts/compare_emulator_runs.py REF.json CANDIDATE.json`. It accepts current-schema, trace-backed reports only and rejects stale ROMs, v4 historical traces, or manifest mismatches. A status change is relative evidence, not an architectural verdict; QEMU aggregate summaries are a distinct evidence class and are not inputs to this per-vector comparator.

A valid completed run may exit 1 because current groups fail or crash. Inspect JSON/XML and hashed trace diagnostics. A traceback, missing/stale report, malformed trace, or tool failure is not a test result. Do not chain expected-nonzero runs with `&&`, suppress them with `|| true`, or change expected values to make an emulator green.

## Evidence and identity

- Validate each report's `base_rom_sha256` against the actual ROM.
- Identify cases by `(CPU, ID)`, not ID alone. IDs are stable and new cases must be appended, never renumbered.
- A generated vector is planned coverage; only a valid executed trace supplies runtime evidence. NOT_RUN, INTERRUPTED, NOT_SELECTED, unsupported, stale or malformed cases are never PASS.
- Preserve JSON plus hash-verified binary traces when retaining evidence. Historical/restored records are explicitly marked; never describe a resumed run as uninterrupted.
- Trace bases/capacity have one source: `src/trace_layout.h`, read by `scripts/trace_layout.py`. Never drop records or restore a trace from another protocol/layout. v5 uses 32-byte records and an FNV-1a input fingerprint; the host validates it against the manifest. v4 is read-only. Current headroom is 442 ARM9 / 1,766 ARM7 records; stop ARM9 growth before the final 442 unless the layout is reviewed and revalidated.
- Keep architectural expectations, emulator observations, unsupported behavior and hardware validation distinct. Do not call QEMU Integrator/CP an NDS model, `ti925t` an exact ARM7TDMI oracle, or QEMU a physical timing test.

## Change discipline

- Preserve stable vector IDs, trace capacity, guard behavior, CPU-specific semantics, and source provenance.
- Do not edit vendor assertions to hide a backend discrepancy. Preserve legal vs undefined/unpredictable scope.
- For a new CPU family, execute the target profile, independently compare where possible, inspect actual final-ROM opcodes/targets when relevant, and inject expectation/instruction faults. Confirm exactly which IDs fail.
- `make host` and host-only CI check contracts, not CPU semantics. Run focused emulator tests separately.
- Current CPU failures and native emulator crashes remain visible. Do not automatically classify an expectation mismatch as a bad expected value.
- Read `docs/guide/limitations-and-release.md`, `NOTICE.md`, and the release-readiness report before packaging. Licensing clearance is a human review, not a boolean assertion.
- Do not run the historical `scripts/test_host.py` or milestone10 package script as current-ROM evidence; their fixed-count checks and archived reports predate this snapshot. Use `make host` and the current guide commands.

## Tooling and storage

- `vendor/`, `work/`, `artifacts/`, `reports/`, `.venv/`, and Python caches are local/generated. Do not publish credentials, toolchains, or BIOS/firmware.
- `reports/` contains local run JSON/XML and immutable trace blobs. The concise current results are summarized in `docs/guide/`; do not treat generated reports as reviewed release evidence without matching hashes.
- The build instruments 117 C++ assertion sites. That is not the number of assembly checks, vector cases, or validated hardware behaviors.
- `scripts/release_gate.py` returns 0 READY, 2 BLOCKED, or 3 ERROR. It is expected to be BLOCKED. Never edit approval/milestone fields just to obtain READY.
