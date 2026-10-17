# Build, test, and inspect results

This guide applies to the current source tree and the ROM hash in the [handbook](README.md). The NDS image requires **devkitARM** and `ndstool`; host GCC is not a substitute.

## Fresh Linux setup

The repository setup script is intended for Debian/Ubuntu-style Linux. Review it first: it uses `sudo`, apt, network access, installs devkitPro packages, and creates `.venv`.

```sh
python3 scripts/fetch_upstreams.py
bash scripts/setup.sh
make build
```

The fetcher installs exact revisions from `upstream-lock.json`; `python3 scripts/fetch_upstreams.py --check` validates existing clean checkouts without fetching. Upstream source retrieval is for local development and **does not grant redistribution permission**.

The current successful build used devkitARM GCC 16.1.0 and `ndstool` 2.3.1. It generated 3,390 shared, 7,414 ARM9-only, and 6,090 ARM7-specific vectors, instrumented 117 C++ assertion sites, and rebuilt `suite.nds`. Existing compiler/assembler/linker warnings were emitted for volatile assignments, unpredictable legacy instructions, ARM7 `r15` uses, and an RWX load segment; the build completed. Warnings are not validation of those legacy cases.

## Fast host-only checks

```sh
make host
```

This runs Python compilation, manifest/ROM/source contracts, the branch-matrix generator invariants, upstream-fetcher fixture tests, and release-gate regression tests. **It does not execute CPU vectors in an emulator or on hardware.** The GitHub workflow is host-only for the same reason.

## Branch displacement regression

After setup, this target runs QEMU comparisons and actual NDS-ROM DeSmuME/fault-injection checks for all **1,338 branch vectors per profile**:

```sh
make thumb-branch
```

Equivalent commands after `make build`:

```sh
python3 scripts/run_qemu_thumb_branch.py
.venv/bin/python scripts/test_thumb_branch.py
```

The tests check all 306 helper encodings/targets per profile in the final ROM, execute branch vectors in the ARM7Thumb and ARM9Thumb groups, and apply deliberate expected-value and opcode mutations. The QEMU `ti925t` result is an ARMv4T proxy, not exact ARM7TDMI verification. The result files include `reports/qemu-thumb-branch.json` and `reports/thumb-branch-tests.json`. See [Thumb branch sweep](thumb-branch-boundaries.md), especially the non-terminating −4/−6 taken cases and the −2 target/fallthrough alias.

## Broader QEMU comparison target

```sh
make qemu
```

The current aggregate QEMU target passed its shared-vector, upstream portable, ARM9 extension, privileged, CP15-alias, register-controlled operand2/compare, and branch subsets. This includes the 1,338 branch vectors per profile. ARM7-profile runs on `arm946` or `ti925t` remain functional cross-checks/proxy results—not an exact ARM7TDMI oracle. QEMU Integrator/CP does not validate NDS BIOS/MMIO or hardware timing.

## Full ROM, trace, and recovery checks

Run all 27 groups in isolated emulator boots:

```sh
.venv/bin/python scripts/run_desmume.py \
  --output reports/development-after-thumb-sweep.json
```

Run the checkpointed sequential/recovery path and save a resumable provenance report:

```sh
.venv/bin/python scripts/run_resilient.py \
  --output reports/development.json
```

The current full run records **17 PASS, 8 FAIL, 2 emulator crashes**; the recovery score folds the two crashes into its 10 non-passing groups. The result is not a clean full-suite PASS. The rebuilt ARM9 trace declares 10,804 rows and ARM7 declares 9,480; the new capacity is 11,057 each.

After generating `reports/development.json`, run the trace and provenance boundary checks:

```sh
.venv/bin/python scripts/test_trace_protocol.py
.venv/bin/python scripts/test_trace_provenance.py
```

These validate the expanded runtime capacity, guard-page rejection at slot 11,058, malformed-trace handling, restore preflight, historical/current record provenance, and assertion-journal separation. Other focused ROM regressions include:

```sh
.venv/bin/python scripts/test_thumb_edges.py
.venv/bin/python scripts/test_remaining.py
```

The `reports/` directory is generated local evidence, not part of the ROM or a pre-certified release bundle.

For a focused group:

```sh
.venv/bin/python scripts/run_desmume.py \
  --group ARM7Thumb --output reports/arm7-thumb.json
.venv/bin/python scripts/run_desmume.py \
  --group ARM9Thumb --output reports/arm9-thumb.json
```

Those groups still have pre-existing failures (10 on ARM7Thumb and 96 on ARM9Thumb), even though all 1,338 added branch vectors pass. See the exact profile/group breakdown in [coverage results](coverage-matrix.md).

### Exit-code interpretation

- `make host` exit 0 means the defined host-side tests passed; it is not CPU execution evidence.
- QEMU and focused ROM-test scripts return success only when their selected evidence passes.
- `run_desmume.py` exit 1 can mean the harness completed and recorded test failures or emulator crashes; inspect JSON/XML and traces, not just the shell status.
- Missing, stale, incomplete, unsupported, or unselected vectors are never converted to PASS.
- Do not suppress a failing score with `|| true` or alter expected values to match an emulator.

## Release gate

```sh
python3 scripts/release_gate.py
```

The gate writes a JSON report beneath `reports/`; exit codes are 0 READY, 2 BLOCKED, 3 ERROR. The current checkout is expected to remain **BLOCKED** until its real acceptance and licensing requirements are resolved. A host-test pass does not override the gate.

## Reproducibility notes

- Preserve the exact ROM SHA-256 alongside execution evidence.
- Build outputs under `artifacts/`, upstream checkouts under `vendor/`, and local run evidence under `reports/` are generated files.
- For CPU vector evidence, keep CPU, ISA group, vector ID, observed primary/auxiliary result, comparison mask, execution PC, and ROM hash together.
- QEMU `ti925t` results must be labeled as proxy comparisons, not exact ARM7TDMI results.
- No physical DS test or firmware/BIOS redistribution is claimed for the current snapshot.
