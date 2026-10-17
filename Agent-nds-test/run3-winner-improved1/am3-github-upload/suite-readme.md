# `suite.nds` — consolidated testing handbook

**Canonical testing documentation.** This file brings together the coverage/results, agent emulator workflow, immediate-ALU matrices, Thumb branch sweep, build/test instructions, architecture/IDs/traces, and limitations/release-status guides. The old guide URLs remain as short pointers here.

> [!CAUTION]
> **The workspace currently contains a mixed snapshot. Do not treat every result below as evidence for the ROM file currently on disk.** As inspected on 2026-10-17, `suite.nds` hashes to `fa7509bfafb6cfffaba83ec591efad698953b997c073bce7cf08f19d1a0142b4`. The checked-in generated manifest has 9,786 ARM9 and 8,462 ARM7 rows, while the current generators/source contracts expect the later 13,380 / 12,056-vector, trace-v5 build. The local full-run report has the `fa7509…` ROM hash and trace v4, but declares 10,804 / 9,480 rows; newer immediate/branch/release reports refer to ROM hash `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`. That newer ROM is not the `suite.nds` file currently present. `make host` was run during this consolidation and currently fails three source-contract checks because generated artifacts are stale. See [Snapshot integrity and evidence status](#snapshot-integrity-and-evidence-status) before using any result.

This project remains a **coverage-first development suite, not a release**. The combined ROM has not been cleared for public redistribution.

## Contents

1. [Snapshot integrity and evidence status](#snapshot-integrity-and-evidence-status)
2. [How to read the test evidence](#how-to-read-the-test-evidence)
3. [Coverage and recorded results](#coverage-and-recorded-results)
4. [Thumb branch boundary and displacement sweep](#thumb-branch-boundary-and-displacement-sweep)
5. [ARM and Thumb immediate-ALU matrices](#arm-and-thumb-immediate-alu-matrices)
6. [Build and test workflow](#build-and-test-workflow)
7. [Agent emulator workflow and differential comparison](#agent-emulator-workflow-and-differential-comparison)
8. [Architecture, stable IDs, and trace protocol](#architecture-stable-ids-and-trace-protocol)
9. [Limitations and release status](#limitations-and-release-status)

## Snapshot integrity and evidence status

### Files actually present in this workspace

| Item | Observed identity/state | Interpretation |
|---|---|---|
| `suite.nds` | SHA-256 `fa7509bfafb6cfffaba83ec591efad698953b997c073bce7cf08f19d1a0142b4` | This is the binary currently on disk. It is not the later `e72f…` binary named in some newer reports and root notes. |
| `src/generated/debug-manifest.json` | ARM9 9,786 rows; ARM7 8,462 rows; CRC32 `0xb1347d9d` | Stale relative to the current source-contract expectations. The immediate rows and the 1,018-case branch sweep are absent from these generated files. |
| `reports/development.json` | Base ROM hash `fa7509…`; trace protocol 4; trace headers declare ARM9 10,804 and ARM7 9,480 rows | Its ROM hash matches the binary, but its row counts do not match the checked-in manifest, and its trace is historical v4. Keep it as a diagnostic record, not current-schema evidence against the present manifest. |
| `reports/immediate-alu-rom-tests.json`, `reports/thumb-branch-tests.json`, `reports/release-readiness.json`, `reports/rom.sha256` | Identify ROM hash `e72f3987…` | These are records for the later snapshot, not the `suite.nds` currently present. Do not compare them to the current binary. |
| Current source | `src/trace_layout.h` is trace v5; generators include immediate matrices and the branch sweep; source tests expect 13,380 / 12,056 vectors | Source describes the later intended snapshot, but the checked-in generated files and ROM have not been refreshed to it. |
| Current host check | `make host` exits 2 at `scripts/test_source_contract.py`; 3 of its 9 checks fail | It reports wrong generated counts (9,786 instead of 13,380), missing immediate rows (0 instead of 528 ARM / 2,048 Thumb), and a missing 1,018-row branch sweep. Later host targets were not reached by that `make host` invocation. |
| Build environment | `.venv`, devkitARM and `ndstool` are absent; `python3 scripts/fetch_upstreams.py --check` reports checkout-root mismatches for both pinned upstreams | This workspace cannot currently rebuild the ROM as-is. Set up/re-fetch before claiming a clean build. |

The latest previously recorded **intended** development snapshot used SHA-256 `e72f3987c3e1563b98142442bd5a2dc72c38b5e12072d9f98a80734bffaa0b99`, trace v5, and the 13,380 / 12,056-vector matrix described below. Earlier work recorded its focused immediate/branch tests, comparator checks, and full-run totals. Some of those per-family reports remain in `reports/`, but the matching ROM and a matching full trace-v5 `reports/development.json` are not present in this workspace. They are preserved here as **hash-labelled recorded results**, not re-certified against the current `suite.nds`.

### To restore one coherent snapshot

1. Review `scripts/setup.sh` before running it; it uses `sudo`, apt/network access, and installs the toolchain and Python environment.
2. Re-fetch the exact pinned upstreams in `upstream-lock.json`; the current `--check` finds checkout-root mismatches. Do not interpret the fetched source as redistribution permission.
3. Run `make build`, then verify `sha256sum suite.nds`, `src/generated/debug-manifest.json`, and the trace version/capacity all agree.
4. Run `make host`, `make thumb-branch`, `make immediate-alu`, and the required trace/diagnostic tests. Regenerate a full report against the rebuilt ROM and keep its trace blobs.
5. Re-run the release gate and any emulator comparison only with reports that match the same ROM hash, manifest, group/score schema, and trace protocol.

Until those steps are done, use the snapshot distinction in this handbook and do not describe the current workspace as a fresh, fully validated e72 build.

## How to read the test evidence

The suite has distinct evidence sources; one must not be used to imply another:

| Evidence source | What it can establish | What it cannot establish |
|---|---|---|
| Host tests (`make host`, Python/C++ contracts) | Generator invariants, manifest/source relationships, parser/comparator rules, trace guards, fetcher/release-gate logic | CPU execution, emulator correctness, physical hardware behavior |
| QEMU | Functional execution of selected portable CPU cases under the named model | Nintendo DS BIOS/MMIO/bus/timing behavior; exact ARM7TDMI behavior from a proxy model |
| DeSmuME | Execution of the packaged NDS ROM in the emulator, including selected CPU/ISA groups and trace capture | Physical DS behavior or independent architectural truth |
| Physical DS | Hardware evidence, if actually run and recorded | No hardware result is claimed for this snapshot |

`arm946` is the relevant QEMU model used for ARM9-profile functional checks. QEMU `ti925t` is an **ARMv4T proxy only**, not an exact ARM7TDMI model. QEMU Integrator/CP is not a Nintendo DS system model.

Interpret outcomes conservatively:

- `PASS` means the executed observation matched the registered expected primary result and the masked auxiliary bits for that case.
- `FAIL` is a retained mismatch, not permission to change an expectation to match a backend.
- `EMULATOR_CRASH` is not a vector `FAIL` and is never a `PASS`; it means the selected group did not finish normally.
- `NOT_RUN`, `INTERRUPTED`, `NOT_SELECTED`, unsupported, missing, stale, malformed, or unverified records are not passes.
- A resumed run must retain restored-record provenance. Recovery can finish a ledger without erasing the original crash or making the result uninterrupted.
- A generated vector is planned coverage. Runtime evidence requires a validated trace tied to the exact ROM and manifest.

## Coverage and recorded results

### Later intended snapshot (`e72f3987…`, trace v5) — recorded, not present on disk

The latest previously recorded intended matrix was:

| Profile / CPU group | Vectors | PASS | FAIL |
|---|---:|---:|---:|
| ARM7 / ARM | 5,344 | 5,331 | 13 |
| ARM7 / Thumb | 6,712 | 6,702 | 10 |
| **ARM7 total** | **12,056** | **12,033** | **23** |
| ARM9 / ARM | 6,641 | 6,417 | 224 |
| ARM9 / Thumb | 6,739 | 6,643 | 96 |
| **ARM9 total** | **13,380** | **13,060** | **320** |

The four CPU/ISA groups are ARM7, ARM7Thumb, ARM9, and ARM9Thumb. The Thumb-side matrix on the later intended snapshot decomposes as follows; the 2,048-vector immediate family is the later addition to the branch-era matrix:

| Thumb family | ARM7 vectors | ARM9 vectors | Scope note |
|---|---:|---:|---|
| Register ALU edges | 800 | 800 | Selected shift/count edges, aliases, and carry-sensitive operations |
| Aligned memory | 1,248 | 1,248 | Selected loads/stores, offsets, aliases, and guard checks; not all regions/alignment/aborts |
| SP/address generation | 192 | 192 | Selected SP adjustment and SP-relative address cases |
| PC-relative ADD/literal loads | 216 | 216 | Selected PC alignment/offset/register cases and static literals |
| Earlier shared Thumb vectors | 753 | 753 | Original shared matrix |
| Other profile-specific Thumb/system vectors | 117 | 144 | Earlier CPU-specific and system/exception/IRQ vectors |
| Conditional/unconditional branches | 1,338 | 1,338 | 320 boundaries + 1,018 displacement sweep |
| Thumb imm8 MOVS/CMP/ADDS/SUBS | 2,048 | 2,048 | Complete imm8 field for these four operations only |
| **Thumb total** | **6,712** | **6,739** | Defined matrices, not exhaustive ISA enumeration |

The matching ARM-side totals are 5,344 ARM7 vectors and 6,641 ARM9 vectors; each includes the 528-vector ARM rotated-immediate addition. CPU-specific vector IDs can overlap; use `(CPU, ID)` as the identity. The immediate matrices add passing vectors to the ARM and Thumb groups; they do not remove the pre-existing failures shown above.

The recorded trace-v5 checkpointed full run attempted all 27 registered groups: **17 PASS, 8 FAIL, and 2 emulator crashes**. Recovery completed the group ledger but preserved all non-pass results. The score summary reports 17 passed and 10 failed because it folds the two crashes into the non-pass count; this is not a clean full-suite pass.

| Group IDs | Recorded outcome | Groups / notes |
|---|---|---|
| 0–3 | FAIL | ARM7, ARM7Thumb, ARM9, ARM9Thumb; counts above |
| 4–14 | PASS | CONDITION CODES; CLZ; QADD/QSUB; QDADD/QDSUB; SMULxy; SMLAxy; SMULWy; SMLAWy; SMLALxy; BLX; LDR r15 / POP {r15} / LDM {r15} |
| 15 | FAIL | LDM / STM |
| 16 | PASS | IPCSYNC |
| 17 | FAIL | IPCFIFO |
| 18 | PASS | IPCFIFO IRQ |
| 19–20 | PASS | SQRT 32 and SQRT 64 |
| 21 | FAIL | DIV 32/32 |
| 22–23 | EMULATOR_CRASH | DIV 64/32 and DIV 64/64 |
| 24–25 | PASS | WRAM CNT and VRAM CNT |
| 26 | FAIL | TCM |

### Current on-disk `fa7509…` report — historical v4 / manifest mismatch

The local `reports/development.json` is a sequential checkpoint/recovery report with `base_rom_sha256=fa7509…` and trace protocol 4. It records 17 passed groups, 10 non-passing groups (including the two DIV emulator crashes), ARM9 **10,484 PASS / 320 FAIL** and ARM7 **9,457 PASS / 23 FAIL**, for trace-declared totals of 10,804 and 9,480. The group ledger records 17 PASS, 8 FAIL, and 2 crashes. Trace v4 uses 40-byte records in the same `0x6c000`-byte slots, so its per-CPU capacity is 11,057; the report's trace-declared headroom is 253 ARM9 and 1,577 ARM7 records. Those figures are useful forensic context for the `fa7509…` binary, but the report's row totals disagree with the currently checked-in generated manifest (9,786 / 8,462), so do not use it as a current validated comparison input until the build/manifest/report set is reconciled.

### Focused test records

The later `e72f3987…` recorded focused checks include:

- `make thumb-branch`: all 1,338 branch vectors per profile passed their selected DeSmuME ROM runs; QEMU comparisons, final-ROM opcode/target audits, and fault-injection checks were recorded as passing. Existing Thumb failures remained 10 on ARM7 and 96 on ARM9.
- `make immediate-alu`: 528 ARM and 2,048 Thumb immediate vectors per profile passed focused DeSmuME execution; QEMU functional checks and final-ROM encoding audits passed; result/flags expectation mutations were detected.
- The aggregate `make qemu` target's selected shared-vector, portable-extension, ARM9, privileged/CP15/operand2, and branch comparisons were recorded as passing on the newer build; this remains QEMU functional evidence, not DS-system or hardware validation.
- `make diagnostics`, trace protocol/provenance checks, and `scripts/test_remaining.py` were recorded as passing on that newer build.
- `make host` was recorded as passing on the newer build: 9 source-contract, 11 comparator, 5 upstream-fetch, and 7 release-gate tests passed, along with the configured generator/trace checks. This does **not** describe the current stale generated files; the actual current `make host` result is listed above.
- The comparator's recorded self-comparison processed 25,436 executed vectors with zero behavioral differences and zero non-comparable vectors. A self-comparison checks plumbing/consistency; it is not independent correctness evidence.

The current `reports/` directory mixes records from multiple ROM hashes. Check the report's `base_rom_sha256` or `rom_sha256`, its trace protocol, and the selected ROM before interpreting any JSON/XML or binary trace.

## Thumb branch boundary and displacement sweep

The recorded branch suite has **1,338 vectors per CPU profile**: the original 320 boundary cases plus a 1,018-case sweep, implemented by `scripts/thumb_branch.py` and integrated in `scripts/generate_v5.py` (ARM9) and `scripts/generate_arm7.py` (ARM7). IDs append after earlier vectors: ARM9 boundary IDs **9,467–9,786**, sweep IDs **9,787–10,804**; ARM7 boundary IDs **8,143–8,462**, sweep IDs **8,463–9,480**. Always qualify these IDs with CPU.

### Encoding and coverage

For a Thumb branch instruction at address `A`, these ARMv4T forms calculate from `A + 4`:

```text
conditional B: 1101 | cond[3:0] | imm8
                target = A + 4 + sign_extend(imm8, 8) * 2
                legal byte displacements: -256 through +254

unconditional B: 11100 | imm11
                  target = A + 4 + sign_extend(imm11, 11) * 2
                  legal byte displacements: -2048 through +2046
```

The immediate counts halfwords. The generator emits explicit `.hword` opcodes, and the final-ROM checks inspect the encoded instruction and symbol-derived target rather than trusting only assembler labels or generated metadata.

| Subset | Construction | Cases per CPU |
|---|---|---:|
| Conditional boundaries | 14 legal conditions × 16 NZCV states at alternating −256 / +254 endpoints | 224 |
| Opposite conditional edges | EQ at +254 and NE at −256, each × 16 NZCV states | 32 |
| Unconditional boundaries | −2048, −2046, 0, +2046 bytes, each × 16 NZCV states | 64 |
| Original boundary matrix | 20 helpers × 16 rows | **320** |
| Conditional interior sweep | EQ at every even displacement −254 through +252; two states except −4/−6, which use EQ-false only | 506 |
| Unconditional interior sweep | 32 stratified offsets × 16 NZCV states | 512 |
| Added sweep | 286 helpers | **1,018** |
| **Total** | 306 helpers per CPU | **1,338** |

All **256 legal conditional-B displacement values** are encoded and audited across the boundary matrix and sweep. The unconditional test samples 32 stratified interior offsets plus its boundaries and zero; the 16 sampled absolute interior byte magnitudes are 62, 64, 66, 126, 128, 130, 254, 256, 258, 510, 512, 514, 1,022, 1,024, 1,536, and 2,044 (both signs where representable). It is **not** exhaustive across every legal unconditional immediate. The conditions are EQ, NE, CS/HS, CC/LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, and LE; condition codes 14 and 15 are not included as legal conditional-B cases. The condition evaluator is host-tested against all 16 NZCV nibbles.

### Special short backward branches

- **−2 bytes:** target `A+2` aliases the fallthrough instruction. The two paths cannot be distinguished by a target marker; opcode and target are still audited.
- **−4 bytes:** taken EQ branches to the branch itself and loops. Only the EQ-false row executes.
- **−6 bytes:** the target NOP falls into the branch and loops if taken. Only the EQ-false row executes.

Those taken paths are explicit untested cases, not passes. Normal helpers enter in ARM state, save caller state, install an NZCV pattern, switch into Thumb at the branch site, and use distinct target/fallthrough markers plus exit guards. They restore the caller's flags and stack. The auxiliary mask checks NZCV preservation plus a stack-pointer sentinel (`0xF0000001`).

Recorded final-ROM checks covered 306 helper encodings/targets per profile (612 total); DeSmuME branch rows passed; QEMU `arm946` passed its ARM9-profile run, while ARM7-profile `arm946`/`ti925t` results are functional compatibility/proxy checks only. Four result and four flag expectation mutations per CPU were detected; opcode/offset mutations covered EQ→NE at −256 bytes, unconditional B +2046→+2044, and sweep EQ −2→0 (33 affected IDs per CPU). Implementation/tests: `scripts/thumb_branch.py`, `scripts/run_qemu_thumb_branch.py`, `scripts/test_thumb_branch.py`, `scripts/test_thumb_branch_generator.py`; reports include `reports/thumb-branch-tests.json` and `reports/qemu-thumb-branch.json`. Reference: [GBATEK Thumb Jumps and Calls](https://mgba-emu.github.io/gbatek/#thumbopcodesjumpsandcalls) and [Thumb instruction summary](https://problemkaputt.de/gbatek-thumb-instruction-summary.htm); these references inform expectations but are not execution oracles. Check the test report's ROM hash before interpreting it.

## ARM and Thumb immediate-ALU matrices

The later recorded immediate addition contributes **2,576 vectors per CPU profile** and stays intentionally bounded rather than claiming every possible operand/register cross-product.

### ARM rotated-immediate operand 2

- 16 ARM data-processing opcodes: AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN.
- Two encoded imm8 values (`0x01`, `0x81`) × all 16 rotate fields (`0..15`) × all 16 opcodes = **512 base vectors and 512 helpers**.
- 16 additional C/V companion rows exercise independent carry/overflow preservation at the ROR-0 logical/test boundary, for **528 vectors per profile** total.
- Representative source operands and both carry inputs for arithmetic are selected; this is not the entire imm8 × rotate × register/alias cross-product.

### Thumb imm8 data processing

- MOVS, CMP, ADDS, and SUBS are tested for every imm8 value `0..255`.
- Two input/flag cases per opcode/immediate produce **2,048 vectors per profile**, using **1,024 helpers** (one encoded instruction helper per opcode/immediate).
- The matrix checks the primary result and NZCV; MOVS cases also exercise C/V preservation.

### Recorded immediate results and limits

For the later `e72f3987…` snapshot, reports record all 528 ARM and 2,048 Thumb vectors passing in focused DeSmuME runs for both CPU profiles. Final-ROM audits checked 512 ARM and 1,024 Thumb instruction encodings per profile. Result and flags expectation fault injections were detected for ARM and Thumb on both profiles (eight mutations total). QEMU recorded 2,576/2,576 passing on the `arm946` model for the ARM9 and ARM7-generated profiles; `ti925t` also passed the ARM7-profile functional run as a proxy only. Implementation/tests: `scripts/arm_immediate.py`, `scripts/thumb_immediate.py`, `scripts/run_qemu_immediate_alu.py`, `scripts/test_immediate_alu.py`, and `scripts/test_immediate_alu_generator.py`; recorded outputs include `reports/immediate-alu-rom-tests.json` and `reports/qemu-immediate-alu.json`. Instruction references include [GBATEK ARM data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu) and [Thumb register/ALU summary](https://mgba-emu.github.io/gbatek/#thumbopcodesregisteroperationsalu); they are not a substitute for independent execution evidence. These report hashes and the currently present ROM do not match; rebuild and rerun before treating them as evidence for the on-disk binary.

## Build and test workflow

### Fresh setup and build

The intended setup path is Debian/Ubuntu-style Linux. Review `scripts/setup.sh` first because it uses `sudo`, apt, network access, and installs devkitPro packages plus the Python environment. A valid NDS ROM build requires **devkitARM and `ndstool`**; host GCC alone is not a substitute.

```sh
python3 scripts/fetch_upstreams.py
bash scripts/setup.sh
make build
sha256sum suite.nds
```

`python3 scripts/fetch_upstreams.py --check` checks existing clean pinned checkouts without fetching. The pinned revisions live in `upstream-lock.json`. Fetching source for local testing is not a grant to redistribute it. The last recorded successful build used devkitARM GCC 16.1.0 and `ndstool` 2.3.1; the current workspace does not have those tools available.

### Test targets

```sh
make host           # host-only source, generator, trace, comparator, fetch and release-gate tests
make qemu           # aggregate selected QEMU functional comparisons
make thumb-branch   # QEMU + focused DeSmuME + final-ROM audit + mutation tests
make immediate-alu  # ARM/Thumb immediate QEMU + DeSmuME + ROM audit + mutation tests
make diagnostics    # diagnostic, ARM7-specific, and privileged focused tests
make qemu-exceptions
```

The host target does not execute CPU vectors. The focused targets rebuild first and require their documented toolchains/runtime. Their passing results apply only to selected cases and a matching build hash.

`make qemu-exceptions` is not a clean pass in the recorded run: it returned **PARTIAL_UNSUPPORTED**, with 128 low-RAM-adapted ARM946-model entry cases passing and 128 MPU cases unsupported because MPU-enable readback was zero. It exits 2. This is neither physical hardware evidence nor an ARM7 result.

### DeSmuME focused and full-suite runs

```sh
.venv/bin/python scripts/run_desmume.py --list-groups
.venv/bin/python scripts/run_desmume.py \
  --group ARM9Thumb --output reports/arm9thumb.json \
  --log-dir reports/arm9thumb-logs
.venv/bin/python scripts/run_resilient.py \
  --output reports/development.json
```

`run_desmume.py` boots the NDS ROM in DeSmuME and captures group results/traces. Worker logs are separated by output stem by default; `--log-dir` provides an explicit per-run directory. Use different output and log directories for reference/candidate runs. A normal full run may return nonzero because it records failures or emulator crashes; inspect its JSON/XML, trace blobs, selected groups, and ROM hash. Do not hide expected nonzero results with `|| true`.

After a matching full report, the trace/recovery checks include:

```sh
.venv/bin/python scripts/test_trace_protocol.py
.venv/bin/python scripts/test_trace_provenance.py
.venv/bin/python scripts/test_remaining.py
```

Other focused tests live in `scripts/test_*.py`; use the target/test corresponding to the matrix being changed. Keep JSON/XML and hash-verified trace blobs together. `reports/` is generated local evidence, not an automatic release certificate.

### Release gate and exit codes

```sh
python3 scripts/release_gate.py
```

The gate returns 0 READY, 2 BLOCKED, or 3 ERROR. The recorded later report is **BLOCKED** with 13 blockers. Do not change approval/milestone fields or suppress non-pass outcomes to obtain READY.

- `make host` exit 0 means only the defined host checks passed.
- A completed emulator run may exit 1 when selected vectors/groups fail or crash; inspect the report.
- `PARTIAL_UNSUPPORTED` is not PASS.
- Missing, stale, incomplete, malformed, or unselected evidence is not PASS.

## Agent emulator workflow and differential comparison

The comparator is a trace-backed differential debugger, **not an architectural oracle**. Use it to locate behavioral changes between emulator builds while retaining provenance and scope.

### Collect comparable runs

1. Rebuild and pin one `suite.nds` hash. Use the same binary, manifest, selected CPU/ISA groups, and run configuration for reference and candidate.
2. Run each backend with separate report and log paths, for example:

   ```sh
   .venv/bin/python scripts/run_desmume.py \
     --group ARM9Thumb --output reports/reference.json \
     --log-dir reports/reference-logs
   .venv/bin/python scripts/run_desmume.py \
     --group ARM9Thumb --output reports/candidate.json \
     --log-dir reports/candidate-logs
   ```

3. Compare the two trace-backed reports:

   ```sh
   python3 scripts/compare_emulator_runs.py \
     reports/reference.json reports/candidate.json \
     --rom suite.nds \
     --reference-label "reference build" \
     --candidate-label "candidate build" \
     --output reports/emulator-diff.json
   ```

   Or use the Make target:

   ```sh
   make compare-emulators \
     REFERENCE_REPORT=reports/reference.json \
     CANDIDATE_REPORT=reports/candidate.json \
     DIFF_OUTPUT=reports/emulator-diff.json
   ```

The comparator validates the ROM SHA-256, group schema, score protocol, trace protocol, manifest CRC, trace blob hashes/headers, CPU identity, and per-vector metadata before comparing. It accepts current-schema trace-v5 reports only; stale ROMs, v4 traces, conflicting manifest rows, or QEMU aggregate reports without per-vector traces are rejected. Reports must use the **same ROM hash and manifest**.

Vectors are paired by `(CPU, ID)`, never by ID alone. Executed pairs are classified as unchanged, checked-output divergence, unmasked auxiliary divergence, status transition, or provenance-only change. Missing, unselected, incomplete, or non-executed rows are listed as `NOT_COMPARABLE`, not silently discarded or scored as passes. The diff includes source, input tuple, expected values, mask, function PC, observations, status, and provenance.

A `PASS`/`FAIL` transition is relative to the other run and the registered expectation. It does not say which emulator or expectation is correct. Inspect the exact source/CPU/ISA, input, mask, trace provenance, final-ROM encoding where relevant, an independent reference, and hardware evidence separately. QEMU model/proxy results are a separate evidence class and are not accepted as inputs to this trace comparator.

The earlier e72 self-comparison recorded 25,436 executed vectors compared, zero behavioral differences, and zero non-comparable vectors. That result validates comparator plumbing only; self-comparison is not an independent oracle. The current `reports/development.json` is v4 and the current manifest is stale, so the comparator is expected to reject current workspace inputs until a clean v5 rebuild/run is made.

## Architecture, stable IDs, and trace protocol

### Generation and identity

The build path is `scripts/build.sh` → generated sources/manifests → ARM7 and ARM9 cross-compiled images → `ndstool`-packaged `suite.nds`.

- `scripts/generate_cpu.py` builds the shared vector prefix.
- `scripts/generate_v5.py` appends ARM9-specific cases; `scripts/generate_arm7.py` appends ARM7-specific cases.
- `scripts/thumb_branch.py`, `scripts/arm_immediate.py`, and `scripts/thumb_immediate.py` define the newer branch and immediate matrices.
- `src/generated/debug-manifest.json` records CPU-qualified IDs, expectations, comparison masks, ISA/group, source, and scope metadata.
- `src/trace_layout.h` is the canonical trace layout; `scripts/trace_layout.py` reads it rather than keeping a second address map.

IDs are append-only. Adding vectors must not renumber earlier vectors. CPU-specific IDs can overlap, so the stable identity is `(CPU, ID)`. Keep CPU profile, ISA group, ROM SHA-256, manifest identity, trace protocol, and run scope together in every report.

### Trace v5 layout

The intended current source layout uses a 64-byte (16-word) header and compact 32-byte (8-word) records in fixed `0x6c000`-byte slots. This gives **13,822 records per CPU**. On the e72 matrix, ARM9 has 442 records of headroom and ARM7 has 1,766; stop before exceeding capacity and review the layout before further growth.

| Region | Base | Reserved size | Contents |
|---|---:|---:|---|
| ARM9 trace | `0x02300000` | `0x6c000` | v5 header + up to 13,822 × 32-byte records |
| ARM7 trace | `0x0236c000` | `0x6c000` | v5 header + up to 13,822 × 32-byte records |
| Assertion journal | `0x023d8000` | `0x14040` | 64-byte header + up to 4,096 × 20-byte events |
| Restore acknowledgement | `0x023ed000` | one word | Emulator/host restore handshake |
| Group mailbox / ledger / UI | `0x023f0000` onward | fixed areas | Score, group, navigation, and display metadata |

The trace record stores state, observed primary and auxiliary values, helper PC, expected primary and auxiliary values, comparison mask, and a 32-bit FNV-1a fingerprint of `(ID, A, B, initial flags)`. The host checks that fingerprint against the manifest, so operands need not be duplicated in every compact record. Missing, malformed, over-capacity, or incomplete records must fail closed. A historical/restored-record provenance marker remains distinct from live execution.

The assertion journal ends at `0x023ec040`, `0xfc0` bytes before the restore gate. Trace-v4 has a 64-byte header and 40-byte historical records (capacity 11,057 per slot); it is readable by the legacy decoder but read-only, and the current comparator does not accept v4 as current evidence. Recorded v5 checks wrote through all 13,822 capacity slots and verified that record 13,823 is rejected, exercised full ARM9/ARM7 traces without overflow, malformed/truncated headers, restore preflight, live-versus-restored provenance, and assertion-journal separation. These checks were recorded as passing on the newer build; rerun them after rebuilding the current workspace.

The build instruments 117 C++ assertion sites. This is not the number of assembly assertions, vectors, or hardware-validated behaviors.

## Limitations and release status

### Open test scope

- The recorded emulator failures remain visible: ARM9 has 320 vector failures and ARM7 has 23 in the later intended matrix. They need trace-level investigation and an independent oracle; they are not automatically bad expectations.
- The two DIV 64/32 and DIV 64/64 emulator crashes remain unresolved. Checkpoint recovery does not turn them into passes.
- No exact ARM7TDMI QEMU model is available. `ti925t` is only an ARMv4T proxy; no physical Nintendo DS test was performed.
- QEMU Integrator/CP does not model the NDS BIOS, MMIO, memory controller, IPC/system behavior, display, or hardware timing.
- The QEMU exception probe is partial: 128 low-RAM-adapted ARM946 entry cases passed; 128 MPU cases were unsupported because MPU-enable readback was zero. No hardware conclusion follows.
- Branch coverage has explicit non-terminating −4/−6 taken paths, a −2 target/fallthrough alias, and a stratified rather than exhaustive unconditional-B interior sweep. BL/BLX/BX, timing, and every register/encoding cross-product are not covered by that matrix.
- The ARM immediate matrix uses two representative imm8 values and selected source operands; it does not exhaust all ARM imm8/rotate/alias combinations. The Thumb imm8 sweep is exhaustive only for MOVS/CMP/ADDS/SUBS' imm8 field, not for every register/memory/condition cross-product.
- Exception/abort entry, FIQ entry, IRQ LR/priority/deep nesting, CP15/cache/TCM corner behavior, all bus-region/alignment/abort combinations, and cycle timing remain incomplete or require better references. See `docs/remaining-cases.json` for the machine-readable open-case ledger.
- The installed compiler emitted warnings for existing legacy/unpredictable code in the recorded builds. A successful link does not resolve those warnings.

### Release and licensing

The recorded release gate is **BLOCKED** (13 blockers):

1. Missing CPU uncovered-work/coverage map.
2. Missing v1.0 plan and approval evidence.
3. Incomplete full current-ROM release regression/package refresh.
4. ARM7 SWI/undefined/abort entry strategy and reference not implemented.
5. Physical FIQ entry stimulus/verification not implemented.
6. IRQ LR, priority, and deeper/full nesting matrices are partial (32 ARM7 timer3-to-timer2 nested cases exist).
7. Operand encoding, register-alias, PC-use, and full cross-product coverage is partial.
8. Bus-region, alignment, and abort behavior matrices are partial.
9. Cycle timing is not validated against a controlled hardware baseline.
10. Exact ARM7TDMI oracle is unavailable; `ti925t` is only a proxy and hardware was not run.
11. ARM9 raw exception verification is partial (128 low-RAM-adapted QEMU entry cases passed; 128 MPU cases were unsupported).
12. Original-code license approval is incomplete.
13. Upstream/combined-ROM redistribution permission is unresolved.

The root MIT license applies only to original project contributions. It does not relicense imported code, generated test contents, or the combined ROM. Redistribution permission for the pinned Rockwrestler source/assets/ROM remains unresolved; fetching upstream commits for local development is not a license grant. No Nintendo BIOS/firmware is bundled. **Do not publish or package the combined ROM as a redistributable release until all licensing and acceptance blockers are resolved.**

A clean focused test or host-test pass applies only to its defined scope; none alone makes this project release-ready.
