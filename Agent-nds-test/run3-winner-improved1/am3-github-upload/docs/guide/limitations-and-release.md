# Limitations, open work, and release status

This is a coverage-first development build, **not a release**. The current ROM hash and observations are in the [handbook](README.md) and [coverage matrix](coverage-matrix.md). The release-readiness gate remains BLOCKED, and the combined ROM has not been cleared for public redistribution.

## What these tests do not establish

- No physical Nintendo DS was used for this snapshot. DeSmuME success is emulator evidence, not hardware confirmation.
- QEMU has no exact ARM7TDMI CPU model in the installed setup. `ti925t` is an ARMv4T proxy; it is not an ARM7TDMI oracle.
- QEMU Integrator/CP does not model the Nintendo DS bus, BIOS, memory controller, IPC, timing, or display hardware.
- The branch sweep audits every legal conditional-B displacement in the final ROM but does not terminate a taken branch at −4 or −6 bytes; those rows exercise EQ-false only. At −2 bytes the target equals fallthrough. Unconditional B tests 32 stratified interior offsets plus boundary/zero cases, not every immediate. BL/BLX/BX and timing remain outside this matrix.
- Existing CPU-profile vector failures remain visible: ARM9 has 320 and ARM7 has 23 in the full profiles, including 96 ARM9Thumb and 10 ARM7Thumb failures. They must be investigated with exact per-vector traces and an independent oracle, not automatically “fixed” by changing expected values.
- Two groups crashed in the latest full DeSmuME run (DIV 64/32 and DIV 64/64). These are unresolved emulator/harness outcomes, not PASS and not vector FAIL counts.
- QEMU MPU-abort limitations, exception-entry gaps, cycle timing, some CP15/cache/TCM behavior, bus-region/alignment matrices, and exhaustive aliases/encoding cross-products remain open.
- The installed toolchain emitted warnings for existing code. A successful link does not prove those legacy cases safe or architecturally resolved.

The current trace layout supports 11,057 records per CPU and has 253 ARM9 / 1,577 ARM7 slots remaining. The new layout and boundary checks passed for the current ROM; further growth beyond those free slots still requires a reviewed memory-layout change. The canonical machine-readable open-case list is [`docs/remaining-cases.json`](../remaining-cases.json). Older milestone notes are retained as history; current counts and outcomes are in this guide.

## Licensing and redistribution

The root MIT license applies to original project contributions only. It does not relicense imported upstream code, generated test contents, or the combined `suite.nds`. The pinned Rockwrestler source was reviewed without an explicit LICENSE/COPYING file in the inspected tree; redistribution clearance is unresolved. Fetching exact upstream commits is for local development, not a redistribution grant. See [`NOTICE.md`](../../NOTICE.md), [`upstream-lock.json`](../../upstream-lock.json), and the release gate report.

Do not publish or package the combined ROM as redistributable until licensing review has resolved every included component and all release acceptance criteria are met.

## Release evidence that is still required

1. Resolve or explicitly scope every remaining case in the coverage ledger.
2. Refresh and adjudicate the full vector failures and the two emulator crashes using independent evidence.
3. Obtain exact ARM7TDMI and physical-hardware validation or an approved, documented scope decision.
4. Complete the required NDS exceptions/abort, timing, bus-region, and system behavior work, or formally remove those requirements with maintainer approval.
5. Re-run all build, host, QEMU, DeSmuME, recovery, trace, UI, and fault-injection checks against the final ROM hash.
6. Finish source/ROM licensing review and update the release-readiness gate without bypassing its checks.

A passing focused branch regression, trace-boundary test, or host suite is evidence only for its defined scope; none makes the project release-ready.
