# Maintainer / agent handoff

## Canonical documentation

Use [`suite-readme.md`](suite-readme.md) as the single testing handbook. It consolidates the coverage/results, branch and immediate matrices, build/test instructions, trace and ID architecture, emulator-comparison workflow, limitations, and release status. Old `docs/guide/` URLs remain as short pointers to it. [`AGENTS.md`](AGENTS.md) is the operating contract; [`NOTICE.md`](NOTICE.md) covers redistribution constraints.

## Current state and next action

A new hardware-test build is now present and packaged:

- `suite.nds` / `real-hardware/suite.nds`: SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`, 2,036,233 bytes.
- `src/generated/debug-manifest.json`: 13,380 ARM9 and 12,056 ARM7 vectors; current trace protocol is v5.
- `make build` succeeded from clean, exact pinned upstream snapshots. `ndstool -i` reports logo and header CRCs OK. ARM9 disassembly confirms `STRH` at POWCNT1 `0x04000304`, reflecting the `vu16*` header fix.
- `make host` passes, including the five POWCNT1 regression tests. No DeSmuME run or physical hardware test has been performed against this new hash.
- `reports/development.json` remains historical for `fa7509…` / trace v4; immediate/branch/release reports identify `e72f3987…`. None is runtime evidence for `8259d5…`.
- The user’s physical report is that e72 showed both screens white immediately on DSi/3DS through TWiLight Menu++/nds-bootstrap. Do not claim the new build fixes it until the user tests this exact ROM. Release and redistribution status remain blocked.

**Next:** ask the user to load `real-hardware/suite.nds` with the same hardware and TWiLight/nds-bootstrap settings, then record the result and exact versions/settings. If the symptom persists, use the supplied source snapshot and investigate earlier boot/display initialization. After device feedback, run a fresh emulator/full report tied to `8259d5…`, refresh the release gate, and update upload staging only as a separate step. Keep old e72/fa7509 reports labeled as historical.

## Testing, evidence, and release guardrails

- Test commands, exact recorded outcomes, trace-v5 capacity, comparator usage, evidence limits, and release blockers are documented in [`suite-readme.md`](suite-readme.md). Check the snapshot-integrity section first.
- Use separate report/log directories for reference and candidate runs; compare only same-ROM, same-manifest, current-schema trace-backed reports. The comparator is differential evidence, not an oracle.
- Keep host, QEMU, DeSmuME, proxy CPU, and physical-hardware evidence separate. `ti925t` is an ARMv4T proxy, not exact ARM7TDMI; no physical DS test is claimed.
- Known vector failures and two DIV emulator crashes must remain visible. Recovery does not turn them into passes. The exception probe's 128 unsupported MPU cases are not passes or hardware results.
- Release gate remains **BLOCKED**; combined-ROM redistribution permission is unresolved. Root MIT applies only to original project work. Do not publish the ROM as a release until acceptance and licensing blockers are resolved.
- The requested `.patch` file was deleted and must not be recreated. Keep direct source/doc edits and the staging folder for the user's upload review.
