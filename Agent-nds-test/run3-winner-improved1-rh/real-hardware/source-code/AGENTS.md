# AI-agent operating contract

Read [`suite-readme.md`](suite-readme.md) before building, running, comparing, or interpreting tests. It is the single canonical testing handbook; the old `docs/guide/` URLs redirect there. Read [`NOTICE.md`](NOTICE.md) before packaging or discussing redistribution.

## Current workspace state — verify before acting

- Current `suite.nds`: SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`, 2,036,233 bytes; packaged for hardware testing at `real-hardware/suite.nds`.
- `src/generated/debug-manifest.json` has 13,380 ARM9 and 12,056 ARM7 vectors; it matches the trace-v5 source after the successful `make build`.
- The current build uses the pinned upstream commits from `upstream-lock.json`; `python3 scripts/fetch_upstreams.py --check` passes. The old invalid vendor snapshots were preserved at `/home/user/Actions-Minutes3-vendor-backup`.
- `make build` succeeded with devkitARM GCC 16.1.0 and `ndstool` 2.3.1. The NDS header and logo CRCs report OK. ARM9 disassembly confirms a halfword `STRH` at POWCNT1 (`0x04000304`).
- `make host` passes, including five POWCNT1 regression tests and the generator, source-contract, trace, comparator, upstream and release-gate tests. No DeSmuME run has been made against this new ROM.
- `reports/development.json` is still historical for ROM `fa7509…` / trace v4; immediate, branch and release reports identify `e72f3987…`. Neither report set validates current ROM `8259d5…`.
- Release status remains **BLOCKED**. The user reported both screens white immediately on a DSi/3DS via TWiLight Menu++/nds-bootstrap using e72; the new 8259 ROM has **not** yet been physically tested, so do not claim the issue is fixed. Exact ARM7TDMI coverage and combined-ROM redistribution clearance remain unresolved.

For exact result labels, evidence hashes, test commands, scope, and recovery steps, use [`suite-readme.md`](suite-readme.md). Do not combine records from different ROM hashes or trace/manifest schemas.

## Build and test discipline

1. Review `scripts/setup.sh` before running it; it uses `sudo`, apt, and network access. Re-fetch exact upstream pins from `upstream-lock.json`, set up the supported toolchain, then rebuild before collecting fresh evidence.
2. Run `make host`, `make thumb-branch`, and `make immediate-alu` against the same newly built ROM; then regenerate full DeSmuME, trace, recovery, and release-gate reports. Keep each report and binary trace tied to the exact ROM hash.
3. `make host` is host-only; it is not CPU execution evidence. QEMU, DeSmuME, and physical hardware are separate evidence classes. QEMU Integrator/CP is not an NDS system model.
4. A valid completed emulator run may exit nonzero because recorded vectors fail or a group crashes. Inspect JSON/XML and trace blobs; never suppress outcomes with `|| true` or change expected values just to make a backend green.
5. Preserve stable IDs and append vectors only. Use `(CPU, ID)` because ARM7/ARM9 IDs can overlap. Keep ARM and Thumb groups distinct.
6. Do not exceed the v5 capacity of 13,822 records per CPU. Trace v4 is historical/read-only; current differential comparison requires current v5 reports.
7. Run emulator comparisons only on the same ROM hash, manifest, schemas, and compatible trace-backed reports. Use separate `--output`/`--log-dir` paths. The comparator reports relative deltas; it is not an architectural oracle. QEMU aggregate reports are not comparator inputs.
8. Distinguish ARM9 `arm946` results from ARM7-profile proxies. `ti925t` is not exact ARM7TDMI validation. No physical DS result is claimed unless one is actually run and documented.
9. Keep open cases in `docs/remaining-cases.json`; read the release caveats and licensing notice. Do not edit approval or milestone data to obtain READY.
10. The `.patch` file was intentionally deleted. Do not recreate it; maintain the updated source, documentation, and staging files directly in the workspace.

## Useful entry points

- Testing and reproducibility: [`suite-readme.md`](suite-readme.md)
- Open-scope ledger: [`docs/remaining-cases.json`](docs/remaining-cases.json)
- Redistribution notice: [`NOTICE.md`](NOTICE.md)
- Handoff / next steps: [`handoff.md`](handoff.md)
