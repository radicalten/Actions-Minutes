# suite.nds — ARM7TDMI / ARM946E-S CPU and system test suite

`suite.nds` is a bare-metal Nintendo DS test ROM with host-side generation, emulation, trace extraction, QEMU comparison, and fault-injection tools. This development update adds an immediate-ALU matrix for ARM and Thumb, preserves the Thumb branch sweep, and uses trace v5 compact records with a read-only historical v4 decoder.

> **Pre-release staging snapshot — not v1.0.** Read [`UPLOAD-STATUS.md`](UPLOAD-STATUS.md) before uploading or publishing. ROM SHA-256: `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`. ARM9/ARM946E-S: **13,380 vectors; 13,060 PASS / 320 FAIL**. ARM7/ARM7TDMI: **12,056 vectors; 12,033 PASS / 23 FAIL**. The current trace-v5 checkpointed run recorded 17/27 groups PASS, 8 FAIL, and 2 emulator crashes; it recovered both crashes but retained the non-pass results. The release gate remains BLOCKED; combined-ROM redistribution clearance remains unresolved.

## Current documentation

Start at the [current suite handbook](docs/guide/README.md). The new documentation set contains:

- [Coverage matrix and current results](docs/guide/coverage-matrix.md)
- [Agent emulator workflow and trace-backed differential triage](docs/guide/agent-emulator-workflow.md)
- [ARM and Thumb immediate data-processing matrices](docs/guide/immediate-data-processing.md)
- [Thumb branch boundary and sweep design](docs/guide/thumb-branch-boundaries.md)
- [Build, test, and inspect guide](docs/guide/build-and-test.md)
- [Architecture, stable IDs, and expanded trace layout](docs/guide/architecture-and-traces.md)
- [Limitations and release status](docs/guide/limitations-and-release.md)

Older milestone documents remain in `docs/` as historical records; use the handbook for current totals, test results, and trace addresses.

## Quick start

On Debian/Ubuntu-style Linux, review `scripts/setup.sh` first; it uses `sudo`, apt, and network access. Then run:

```sh
python3 scripts/fetch_upstreams.py
bash scripts/setup.sh
make build
make host
make thumb-branch
make immediate-alu
```

- `make build` generates both CPU profiles, compiles them with devkitARM, and packages `suite.nds` with `ndstool`.
- `make host` checks source, generated metadata, branch/immediate-matrix invariants, trace compatibility, emulator-diff rules, upstream-fetch safety, and release-gate contracts. It does **not** execute CPU vectors.
- `make thumb-branch` runs the 1,338-case-per-CPU branch family on QEMU models, executes the selected groups in DeSmuME, and applies result/flag/opcode fault injections.
- `make immediate-alu` runs 528 ARM rotated-immediate and 2,048 Thumb imm8 cases per CPU on QEMU/DeSmuME, audits final-ROM encodings, and injects result/flag faults.

For the current full-suite checkpoint/recovery run:

```sh
.venv/bin/python scripts/run_resilient.py --output reports/development.json
```

The current suite returns a nonzero status because known vector failures and two emulator crashes remain. The checkpointed runner recovers the crashes but preserves them in the group ledger. Inspect JSON/XML and trace diagnostics; a nonzero result is not automatically a harness error.

## Branch, immediate-ALU, and trace update

The branch suite contains 320 original boundary cases plus a 1,018-case sweep per CPU. All 1,338 pass the focused DeSmuME Thumb-group runs; QEMU comparisons and final-ROM encoding/target audits pass. The conditional sweep encodes and audits all 256 legal branch offsets. At −2 bytes the target aliases fallthrough; taken branches at −4 and −6 would loop, so those paths are not executed.

The immediate addition covers all 16 ARM immediate-rotate fields for two representative imm8 values across all 16 data-processing opcodes, and all 256 Thumb imm8 values for MOVS/CMP/ADDS/SUBS. All new vectors passed focused DeSmuME and QEMU functional comparisons; see the dedicated test document.

Trace v5 uses 32-byte compact records in unchanged `0x6c000`-byte slots, supporting **13,822 records per CPU**. The current ARM9 profile has 442 slots free; ARM7 has 1,766. The v5 guard-page, v4 read-compatibility, restore-preflight, and provenance tests pass. See the [handbook](docs/guide/README.md) for exact evidence scope.

## Important limits

- `ti925t` is only an ARMv4T proxy; no exact ARM7TDMI QEMU model is available here.
- No physical DS was tested. QEMU Integrator/CP is not an NDS hardware/BIOS/MMIO/timing model.
- Existing CPU vector failures, other failing groups, and two DIV emulator crashes remain visible; see the current coverage matrix.
- Fetching pinned upstream code for local development does not grant permission to redistribute it. Root MIT covers original work only; see [`NOTICE.md`](NOTICE.md).
- No Nintendo BIOS/firmware is bundled.

The requested `.patch` file was deleted and is not part of the deliverable. Source, generated manifests/ROM, and updated docs are maintained directly in the workspace for upload.
