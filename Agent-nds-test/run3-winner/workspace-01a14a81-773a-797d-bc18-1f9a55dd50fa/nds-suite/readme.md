> **v1.0 target: coverage first.** See [release plan](docs/v1.0-release.md).
> Original project contributions are MIT-licensed; upstream redistribution permission still blocks public release.

> **CPU GROUP MIGRATION:** [ARM7 / ARM7Thumb / ARM9 / ARM9Thumb](docs/cpu-groups.md)
> are now separately executed/scored. Registry/score schema3, trace4;27 groups,
> current17/27=62.96% with unchanged vector results. Prefer group names over old IDs.

> **DEVELOPMENT — NOT FINAL:** The working ROM now has9466 ARM9 /8142 ARM7 cases
> and trace protocol v4. Milestone10 is only the last packaged recovery release.
> Read [AGENTS.md](AGENTS.md) and [current development status](docs/release-development.md)
> before running automation. Older release counts/layouts below are historical;
> full current-release regression/package refresh is pending.
> Latest addition: [216 Thumb PC-relative cases per CPU](docs/thumb-pc.md), all passing.

# suite.nds — automated Nintendo DS test suite

**Milestone 10: flag-only ARM operand2 on both CPUs. Still not exhaustive.**

This devkitARM C++/assembly ROM includes dual-screen diagnostics, QEMU runners,
source/support scripts and retained execution evidence. Exact ARM7TDMI QEMU and
physical-hardware validation remain unavailable; many original GBA source ports
and exhaustive encoding/exception matrices are unfinished.

## New in this release

Added **256 tests on each CPU** for TST/TEQ/CMP/CMN with register-controlled
LSL/LSR/ASR/ROR: low-byte shift-count boundaries, shifter versus arithmetic flags, and preservation of the source register. All **512 new DS executions pass**. The portable
sequence also passes QEMU ARM946 and ti925t (ARMv4T proxy, not exact ARM7TDMI).

| CPU | Total | PASS | FAIL |
|---|---:|---:|---:|
| ARM9 | 6,498 | 6,178 | 320 |
| ARM7 | 5,174 | 5,151 | 23 |

The previous failures remain unchanged: ARM9 **160 CP15 alias +32 Thumb undefined
+128 MPU-abort nonentry**, ARM7 **23 alignment/PC discrepancies**. These are
emulator/reference results, not hardware-confirmed findings. Full supervised score
remains **17/25 (68%)**, zero groups unrun, including recovery from two native crashes.

Read [exact new matrix, independent verification and fault injections](docs/compare-operand2.md),
[prior system/exception coverage](docs/arm9-milestone8.md), and the
[remaining-work ledger](docs/remaining-cases.json). The QEMU MPU-abort capability
limitation remains explicit; those cases are not independently verified PASS.

Only the **latest verified milestone archive** is retained to conserve workspace
storage. Historical design notes describe tests still included in the current ROM.

## New: detailed correctness debugging

CPU group PASS/FAIL now expands into **every vector and helper family**, with inputs, runtime expectations, observed primary/auxiliary values, masks, XOR differences, executed helper PC, and unrun/interrupted states. Failures do not stop later CPU vectors.

- **12 lower-screen pages**, including ARM9/ARM7 explorers and an original C++ assertion journal. Up/Down browses, A switches family/case mode, B finds the next failure.
- **Self-contained [offline debugger](reports/debugger.html)** with filters/search and full CSV export. JSON includes per-family totals and every mismatch.
- **117 instrumented C++ assertion sites**, producing 160 events on the recorded run. Uninstrumented assembly/multiline assertions remain group-only/unknown; no fabricated passing subchecks.
- Immutable diagnostics survive supervised native-crash restarts and are labeled prior-boot evidence.
- **96 new ARM9-only LDRD/STRD vectors**: six aligned addressing modes, data and writeback checks, four operand pairs. These are not executed on ARM7 or its QEMU proxy.

See [debugging guide and binary protocols](docs/debugging.md). Export with `python3 scripts/export_debug.py reports/resilient.json` after running the suite.

## This update fixes the earlier stop-on-failure policy

**Returned assertions and ROM timeouts now continue to later groups, including hardware groups.** The framework restores its ARM9 context and hardware baseline, and resets ARM7 cooperatively before IPC/WRAM/VRAM groups. It no longer deliberately ends the suite at the IPCFIFO failure.

A ROM cannot recover after its emulator process crashes, and a hard CPU hang may never reach a failure handler. The supplied **checkpointed host runner** handles these cases by retaining the observed ledger, classifying the interrupted group as a crash/timeout, and booting a fresh emulator at the next group.

```sh
python3 scripts/run_desmume.py --sequential \
  --output reports/sequential.json --screenshot reports/score-screen.png
```

`--sequential` now uses checkpoint/restart supervision by default. Use `--no-restart` to inspect a single unsupervised boot. Opening the ROM directly in another emulator gets the in-ROM assertion recovery, but not a host restart supervisor. The supplied host adapter currently targets py-desmume.

See [recovery design and evidence](docs/recovery.md) for reset boundaries, checkpoints, imported results, timeout limitations and executed fault-injection tests.

## Actual full result on the tested backend

| Outcome | Groups |
|---|---:|
| PASS | 17 |
| Returned group failure | 6 |
| Native emulator crash | 2 |
| Unrun after supervised recovery | **0** |

**Score: 17/25 = 68.00%.** The supervisor restarts after SIGFPE in DIV64/32 and DIV64/64. The final ROM-rendered screen explicitly says **SUITE FINISHED - RESUMED** and shows **8 NONPASS**, including those two crashes. It does not label them as passes or pretend this was one uninterrupted boot.

The unmodified ROM continues beyond the earlier IPCFIFO stop in its first boot; IPCFIFO IRQ and subsequent math groups execute. On this particular old emulator core, it still encounters the native division crash unless supervised. No division test is removed to hide that crash.

The DS CPU totals are shown above. Shared 3,390 cases pass QEMU ARM946 and the
labeled ti925t ARMv4T proxy. Eleven original Rockwrestler CPU groups pass separately
on QEMU ARM946. New oracle subsets and exclusions are recorded separately.

## Downloads / project contents

- `suite.nds`: current self-starting ARM9 + ARM7 ROM.
- `src/`: framework, hardware cleanup, cooperative reset, linker scripts, generated cases and oracle runtime.
- `scripts/`: build/setup, generation, normal/resilient runners, UI/recovery/negative/host tests and packaging.
- `vendor/`: pinned upstream snapshots; original test assertions retained.
- `reports/`: JSON/JUnit, per-boot checkpoints/logs, real screen captures, tool versions and hashes.
- `coverage.json`: coverage and remaining-work ledger.
- `handoff.md`: workspace setup and next-agent instructions.

## Scoring

**Group score = 100 × passed groups / planned groups**, truncated to two decimals. A group is one entry in `src/groups.json`, not one vector, opcode or assertion. CPU-vector counters are displayed separately.

- Default plan: 25 groups. A selected single-group run uses denominator 1.
- Failed, crashed, timed-out and unrun groups never count as passes.
- `GROUPS NONPASS` includes every classified non-pass category; the lower-screen ledger distinguishes them.
- A finished/resolved plan does not mean every internal assertion executed. Original groups stop at their first failing assertion, but the suite now advances to the next group.
- The CPU checker continues after ordinary mismatched values and counts all vectors; an exception or hard hang can still interrupt it before counters are finalized.
- A resumed score is assembled from observed checkpoints and rendered by a fresh ROM boot. It is visibly labeled RESUMED.

The isolated runner reports failures/crashes/timeouts as separate aggregate fields. The ROM's legacy protocol field `failed` now means **non-pass total**. Use the ledger to distinguish individual outcomes.

## Dual-screen results

The top screen keeps the score, counts and last failure. **Left/Right** navigates 12 lower-screen pages:

1. ARM9/ARM7 vector totals and first-failure diagnostics.
2. Groups 00–12.
3. Groups 13–24.
4. Failures/non-pass outcomes 1–9.
5. Outcomes 10–18.
6. Outcomes 19–25.
7. ARM9/ARM7 boot snapshots side by side.
8. Eighteen ARM9 CP15 boot values.
9. Coverage and verification limitations.
10. ARM9 family/case explorer.
11. ARM7 family/case explorer.
12. Executed C++ assertion journal.

Statuses: WAIT, PASS, FAIL, TIME, SKIP, RUN, **CRSH**, **HTIM**. The assertion/cause field records assertion IDs, crash signals, or host-timeout causes as applicable. A zero assertion is valid; inspect the status.

The sub-engine console is enabled during pure CPU groups and terminal reporting, but disabled during hardware groups to avoid competing with VRAM tests. On a resumed boot, prior CPU traces and the C++ journal are restored through the host barrier; CPU explorers label historical evidence. Boot snapshots are observations from the current boot, not passing assertions or historical snapshots from before a crash.

- [Actual completed resumed screen](reports/score-screen.png)
- [Group ledger, including crashes](reports/ui-page-2.png)
- [Failure/cause details](reports/ui-page-3.png)
- [Display implementation and protocols](docs/dual-screen.md)

## CPU coverage

The shared suite has **3,390 vectors**, using **260 test functions: 186 ARM and 74 Thumb**. The shared prefix contains 2,637 ARM vectors and 753 Thumb vectors; CPU-specific profiles are additional.

| Added in | Vectors | Main areas |
|---|---:|---|
| Milestone 1 | 653 | Generated ALU checks plus 93 checks from all 31 GBA carry-table rows |
| Milestone 2 | 528 | Shifts/RRX, long multiplies, MLA, aligned memory transfers, writeback, SWP and defined block transfers |
| Milestone 3 | 353 | Thumb conditions, B/BL, high registers, stack, PC/SP-relative forms, LDMIA/STMIA |
| Milestone 4 | **1,856** | ARM conditional execution, shifted operand2 matrix, flags-only MSR/MRS |

New ARM cases include:

- 15 legal conditions, including AL, across all 16 NZCV combinations: **240 cases**. Reserved/unconditional ARMv5 encoding space is not misclassified as a portable NV condition.
- Ten ALU operations × ten shift forms × eight operand pairs × two carry inputs: **1,600 cases**. This checks logical shifter carry versus arithmetic carry/overflow, including RRX with ADC/SBC.
- Sixteen CPSR flag-transfer cases checking NZCV updates and preservation of non-NZCV state. This is not complete PSR/control/SPSR coverage.

Cases are guided by [GBATEK's ARM CPU Reference](https://mgba-emu.github.io/gbatek/#armcpureference). [Coverage mapping](docs/gbatek-coverage.md) documents exact source anchors and limits. `src/generated/vectors.json` maps one-based IDs to functions, inputs, expectations and auxiliary-result masks. Shared IDs 1–3,390 remain stable. The schema-2 debug manifest supplies separate full ARM9 and ARM7 lists; IDs above 3,390 are CPU-qualified.

These counts are not exhaustive encoding/register/addressing coverage. Exception machinery, many instruction forms, chip-specific misalignment, full CP15, timing and original GBA source ports remain unfinished.

## Original suite coverage

All **23 Rockwrestler asserting groups** are registered without button presses:

| Area | Groups |
|---|---|
| ARMv4 | condition codes |
| ARMv5 | CLZ; QADD/QSUB; QDADD/QDSUB; SMULxy; SMLAxy; SMULWy; SMLAWy; SMLALxy; BLX; LDR/POP/LDM PC interworking; LDM/STM |
| IPC | IPCSYNC; IPCFIFO; IPCFIFO IRQ |
| DS math | SQRT32; SQRT64; DIV32/32; DIV64/32; DIV64/64 |
| Memory | WRAMCNT; VRAMCNT; TCM |

Two original startup-state displays are exported as observations, not fabricated pass/fail tests. Original assertion expressions remain; the staging build adds ARM7 cooperative polling and the framework resets baseline state. This changes scheduling and is not cycle-exact equivalence to the interactive upstream runner.

The GBA carry table is ported semantically, comparing NZCV rather than GBA mode/interrupt-control bits. The other 13 source groups—memory, IO read, timing, timers, timer IRQ, shifter, multiply-long, BIOS math, DMA, SIO read, SIO timing, miscellaneous edges and video—are **not fully ported**. New overlapping checks do not count as a complete source-level port.

## Validation and known failures

Backend: `py-desmume==0.0.9`, native core identifies as **DeSmuME 0.9.12**, interpreter, HLE boot. No Nintendo BIOS/firmware is included or required for these recorded runs.

| Group | Recorded non-pass result |
|---|---|
| LDM/STM | assertion `0x004` |
| IPCFIFO | assertion `0x025` |
| DIV32/32 | assertion `0x015` |
| TCM | assertion `0x000` |
| DIV64/32, DIV64/64 | native SIGFPE; retained as CRSH after restart |

The LDM/STM failure concerns an intentionally unpredictable base-in-list case. Upstream notes an emulator discrepancy at IPCFIFO 0x25. Other failures need independent investigation; no expectations were weakened to obtain green results.

QEMU 10.0.13 runs bare-metal ELF images on Integrator/CP:

- All 3,390 shared vectors PASS on ARM946 and the ti925t proxy.
- Eleven original CPU groups PASS on ARM946 in separate processes.
- Original unpredictable LDM/STM characterization is explicitly excluded from the architectural QEMU comparison, but retained and scored in DS execution.

Stock QEMU here still lacks exact `arm7tdmi`. The proxy is not equivalent verification. QEMU runs do not verify NDS MMIO, BIOS, hardware timings or chip-specific undefined behavior. No real DS hardware validation has been performed.

Twenty-eight host checks pass, including 12-page UI integration, actual negative-vector execution, and recovery evidence. Additional recovery tests prove single-boot continuation after injected returned hardware failures, recovery from an abandoned ARM7 RW service, and host continuation after an injected infinite loop. Test-only injection ROMs are temporary copies; the distributed ROM contains none of those changes.

## Build and run

Review the setup script; it installs signed packages with sudo/network access:

```sh
bash scripts/setup.sh
source .venv/bin/activate
bash scripts/build.sh
python3 scripts/run_qemu.py
python3 scripts/run_qemu_rock.py
python3 scripts/run_qemu_v5.py
python3 scripts/run_qemu_privileged.py
python3 scripts/run_qemu_cp15.py
python3 scripts/run_qemu_operand2.py
python3 scripts/run_qemu_compare2.py
python3 scripts/run_qemu_exceptions.py       # PARTIAL_UNSUPPORTED; returns 1 here
python3 scripts/run_desmume.py               # isolated groups; currently returns 1
python3 scripts/run_desmume.py --sequential --output reports/sequential.json --screenshot reports/score-screen.png
python3 scripts/run_resilient.py --output reports/resilient.json
python3 scripts/run_desmume.py --group 0 --output reports/cpu9.json --screenshot reports/cpu9-screen.png
python3 scripts/test_recovery.py
python3 scripts/test_ui.py
python3 scripts/test_negative.py
python3 scripts/test_diagnostics.py
python3 scripts/test_arm7_specific.py
python3 scripts/test_privileged.py
python3 scripts/test_milestone8.py
python3 scripts/test_operand2.py
python3 scripts/test_compare2.py
python3 scripts/test_host.py
python3 scripts/export_debug.py reports/resilient.json
```

A nonzero suite exit is expected when any group fails, crashes or times out. Do not chain expected-nonzero commands with `&&` and accidentally skip subsequent validation. Build/oracle/harness failures are different from recorded test outcomes.

Runner controls: `--group N`, `--frames 600`, `--timeout 30`, `--output PATH`, `--detail-page 0..11`, `--screenshot PATH`, `--verify-navigation`. In supervised mode the wall/frame limits apply per boot segment. `--no-restart` preserves one-boot debugging. Internal `--child`, `--seed` and `--checkpoint` options are worker interfaces, not ordinary CI success indicators.

Recorded build: devkitARM r68, GCC 16.1.0, ndstool 2.3.1. This uses a bare-metal runtime, not libnds. Setup installs current trusted packages, not an archived exact toolchain; see handoff.

## Memory layout and protocols

The ARM7 image outgrew the safe private-WRAM code space. It now occupies a dedicated main-RAM region:

- ARM9 image starts `0x02000100`, constrained below `0x021f0000`.
- Raw-abort scratch: `0x021f0000`–`0x021f0fff`, excluded from the ARM9 image.
- ARM7 image starts `0x02200000`, reserved through `0x022fffff`.
- ARM7 normal/IRQ stacks remain in private WRAM near `0x0380f000` / `0x0380ff00`.
- Trace slots: `0x02300000` (ARM9), `0x02340000` (ARM7); assertion journal `0x02380000`; restore gate `0x023ed000`.
- ARM7 reset handshake: `0x023ee000`.
- CPU result/diagnostic areas: `0x023ef000`, `0x023ef100`.
- Score mailbox v2: `0x023f0000` (18 words).
- UI metadata: `0x023f0080` (9 words; first five retain the prior layout).
- Group ledger: `0x023f0100` (25 state/cause pairs).

Custom linker scripts are in `src/linker/`. Both link-time region sizes and post-build image-size checks guard against overlap. Execution placement/timing differs from the original ARM7 private-WRAM build; hardware timing equivalence is not claimed.

Score words 0–17: magic, version, state, active group, assertion, passed, registered count, planned, nonpass, unrun, basis points, resolved flag, last nonpass group, last cause, ARM9 pass/fail, ARM7 pass/fail. State 1 is RUN, 2 transition, 3 terminal selected assertion failure, 4 terminal selected ROM timeout, 5 selected plan finished. State 5 is not synonymous with PASS. State 6 is the host diagnostic-restoration barrier.

The default selector remains `0xffffffff` (all groups). A separate disabled-by-default seed block supports host resume. Detailed protocols are documented in [recovery](docs/recovery.md) and [dual-screen reporting](docs/dual-screen.md).

## Provenance / licensing

`upstream-lock.json` pins both upstream repositories. Imported GBA carry data retains the MIT notice in `vendor/mgba-suite-auto/LICENSE`. Rockwrestler's pinned snapshot has no LICENSE/COPYING file; public hosting alone does not grant redistribution rights. No blanket license is asserted over the combined work. Resolve permission before public redistribution; see `NOTICE.md`.

The project remains a validated working milestone, not the finished exhaustive product. The score and recovery improvements do not close the coverage or hardware-reference gaps.
