# suite.nds — automated Nintendo DS conformance baseline

**Status: milestone 1 / incomplete.** This is a real devkitARM-built ROM with executable tests and a headless emulator runner, not the finished exhaustive suite requested for this project. Do not advertise all-opcode coverage, a full GBA-suite port, exact ARM7 QEMU verification, or hardware certification.

## Deliverables

- `suite.nds`: compiled, self-starting Nintendo DS ROM (ARM9 + ARM7).
- `src/`: C++ adapter, ARM/Thumb assembly, generated CPU vectors, and QEMU entry point.
- `scripts/`: build, environment setup, vector generation, emulator/QEMU runners, and host/negative tests.
- `vendor/`: pinned upstream source snapshots; original test assertions are retained.
- `reports/`: actual execution results, JUnit XML, build logs, tool versions, and ROM hash.
- `coverage.json`: machine-readable implemented and missing coverage.
- `handoff.md`: environment reconstruction and next-agent instructions.

## Current coverage

### Rockwrestler

All **23 asserting menu groups** are callable without button presses:

| Area | Groups |
|---|---|
| ARMv4 | condition codes |
| ARMv5 | CLZ; QADD/QSUB; QDADD/QDSUB; SMULxy; SMLAxy; SMULWy; SMLAWy; SMLALxy; BLX; LDR/POP/LDM PC interworking; LDM/STM |
| IPC | IPCSYNC; IPCFIFO; IPCFIFO IRQ |
| DS math | SQRT32; SQRT64; DIV32/32; DIV64/32; DIV64/64 |
| Memory | WRAMCNT; VRAMCNT; TCM |

The two startup-state screens were observations, not assertions. Their stored ARM7/ARM9 IPC, interrupt, CPSR, and ARM9 CP15 values are exported in JSON as `startup_observations`. No fabricated expected values or PASS labels are assigned to these observations.

Each group retains upstream fail-fast assertion behavior: reaching an assertion failure stops that group. Therefore a failed group does **not** imply all its later assertions executed. The host starts a fresh emulator process and ROM boot for each group, so failures, hangs, or emulator crashes do not prevent subsequent groups from running.

### CPU vectors on both DS CPUs

Each CPU executes **653 vectors**:

- 560 deterministic vectors across 17 ARM and 18 Thumb ALU test functions.
- 93 imported checks from all 31 rows of `mgba-suite-auto/src/carry.c`: ADC, SBC, RSC.
- Result and NZCV checks; multiply only checks architecturally useful NZ flags here.
- Boundary operands, signed overflow, carry/borrow, and Thumb register shift counts 0, 1, 31, 32, 33, 255, 256, 257.
- ARM operations: AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN, MUL.
- Thumb operations: AND, EOR, SUB, ADD, ADC, SBC, TST, CMP, CMN, ORR, BIC, MVN, MUL, NEG, LSL, LSR, ASR, ROR.

The imported carry expectations compare NZCV rather than GBA startup mode/interrupt bits. This is a semantic vector port, not a binary port of the GBA UI and inline-assembly wrappers. `src/generated/vectors.json` maps one-based CPU failure IDs to inputs, expectations, and upstream case names.

**Not exhaustive:** these are selected instruction forms and operands, not every encoding, register combination, condition, addressing mode, or instruction family. The Rockwrestler ARMv5 tests provide additional DS-only coverage but have not been run through the QEMU oracle in this milestone.

### GBA-suite port status

Carry-table arithmetic is ported. The other 13 groups—memory, IO read, timing, timers, timer IRQ, shifter, multiply-long, BIOS math, DMA, SIO read, SIO timing, miscellaneous edges, and video—are **not ported**. GBA MMIO, BIOS, memory maps, waitstates, serial hardware, and video expectations must be reviewed for DS applicability; directly reusing their expected values would be incorrect. Overlap with Rockwrestler is not counted as porting those source tests.

## Actual verification results

Final baseline run: **25 groups: 19 PASS, 4 FAIL, 2 EMULATOR_CRASH**.

| Group | Observed result |
|---|---|
| CPU ARM9 / ARM7 vectors | PASS, 653 vectors each |
| LDM/STM | FAIL at assertion `0x004` |
| IPCFIFO | FAIL at assertion `0x025` |
| DIV32/32 | FAIL at assertion `0x015` |
| DIV64/32 and DIV64/64 | backend processes terminated by signal 8 (SIGFPE) |
| TCM | FAIL at assertion `0x000` |
| Other 17 Rockwrestler groups | PASS |

Backend: `py-desmume==0.0.9`, whose bundled core identifies itself as **DeSmuME 0.9.12**, interpreter mode, HLE boot, no external Nintendo BIOS/firmware. The separately installed system `desmume-cli` is not this runner's backend. These results are specific to the tested backend, not proof of hardware defects or definitive emulator bug attribution.

The LDM/STM failing case intentionally exercises base-in-register-list writeback, which the assembler warns is UNPREDICTABLE. Treat it as a chip-specific characterization case, not a universal architectural requirement. Upstream already notes an emulator discrepancy at IPCFIFO `0x025`. Other failures need independent investigation; expected values were not weakened to get a green report.

### QEMU

`reports/qemu.json` records QEMU 10.0.13 results for the same 653 vectors and assembly functions:

- `-cpu arm946`: PASS.
- `-cpu ti925t`: PASS, **ARMv4T proxy only**.
- Exact `arm7tdmi`: unavailable in the installed QEMU CPU list (`reports/qemu-cpus.txt`).

The oracle is a bare-metal ELF on QEMU's Integrator/CP board. It is **not** NDS emulation and does not verify DS MMIO, BIOS behavior, caches/TCM integration, bus timings, interrupts, or chip-specific unpredictable behavior. QEMU is an independent execution comparison, not hardware ground truth. No stock ARM7-equivalence claim is made.

Eight host checks pass. Negative tests prove invalid-selector rejection, frame-budget timeout reporting, and failure on a deliberately corrupted expected CPU result. Two successive builds with the recorded toolchain produced identical ROM SHA-256 values; see `reports/rom.sha256`.

## Build and run

On Debian/Ubuntu, review and run:

```sh
bash scripts/setup.sh
source .venv/bin/activate
bash scripts/build.sh
python3 scripts/run_qemu.py
python3 scripts/run_desmume.py   # currently exits 1: known failures/crashes are real results
python3 scripts/test_negative.py
python3 scripts/test_host.py
```

`make`, `make qemu`, `make test`, and `make host` are shortcuts. ROM construction needs devkitARM and ndstool, not libnds: Rockwrestler supplies its own bare-metal runtime. Recorded build: devkitARM r68, GCC 16.1.0, ndstool 2.3.1. The setup script installs current signed packages; see handoff for reproducibility caveats.

Run one group:

```sh
python3 scripts/run_desmume.py --group 0 --output reports/arm9.json
python3 scripts/run_desmume.py --group 15 --frames 600 --timeout 30
```

`src/groups.json` lists stable IDs. Results go to JSON and companion JUnit XML. Exit 0 means every selected group completed successfully; assertion failures, ROM timeouts, host frame/wall timeouts, backend crashes, and harness errors cause nonzero status. Group execution uses subprocess isolation. The internal `--child` mode is not a CI success indicator; it returns its result in JSON.

The unpatched `suite.nds` starts all groups sequentially and halts on the first failure, displaying its group/assertion IDs. On the tested backend this stops at LDM/STM. Use the host runner to collect every group. There is no automatic recovery from a failed hardware-state-changing group within one ROM boot.

## Automation interface

Caches are disabled by the upstream runtime. ARM9 publishes seven little-endian 32-bit words at `0x023f0000`:

| Word | Meaning |
|---|---|
| 0 | magic `0x4e445354` |
| 1 | protocol version 1 |
| 2 | state: 1 running, 2 group passed, 3 failed, 4 ROM timeout, 5 selected run complete |
| 3 | active group (25 at normal loop completion) |
| 4 | first failing assertion, or 0; `0xffffffff` for unnumbered/invalid selection |
| 5 | number of completed groups |
| 6 | total registered groups, currently 25 |

State is committed last. A single-group host run requires state 5 and completed count 1; state 2 alone is not success. Emulator wall/frame limits cover failures that cannot reach the mailbox.

ARM7 CPU status is at `0x023ef000`: magic `0x43505537`, first failing vector ID or zero, followed by observed result, expected result, observed CPSR, expected flags on failure. ARM7 completes these checks during startup before entering the upstream command loop. CPU ARM9 failures expose the vector ID; full ARM9 observed-value diagnostics are a future extension.

A unique ROM selector structure contains `0x53454c31`, `0x4e445341`, and a group ID. Default `0xffffffff` means all groups. The runner patches only a temporary copy of the ROM's ARM9 payload; the distributed ROM and DS header are unchanged. Reports contain the base and selected-ROM hashes. Tests must not reuse the reserved mailbox addresses.

## Source provenance and licensing

Pinned revisions and URLs are in `upstream-lock.json`. The fetched `latest` GBA release resolves to the same commit as the vendored source. The release metadata is retained in `reports/mgba-release.json`.

mGBA-suite's MIT notice is retained in `vendor/mgba-suite-auto/LICENSE`, and applies to imported carry data. Rockwrestler's pinned repository has **no LICENSE/COPYING file**; public hosting is not itself a redistribution license. No blanket license is asserted for that code or the combined ROM. Resolve upstream permission before public redistribution. See `NOTICE.md`.

No real Nintendo DS hardware validation has been performed. See `handoff.md` for the remaining implementation and verification work.
