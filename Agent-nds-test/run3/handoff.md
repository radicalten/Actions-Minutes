# Agent handoff — suite.nds milestone 1

## Read this first

The requested final product is NOT finished. Preserve that distinction. We have a compiled, tested automation baseline, all 23 Rockwrestler asserting groups wired up, 653 ARM/Thumb ALU/carry vectors on each CPU, reproducible build scripts, JSON/JUnit reporting, and a narrowly scoped QEMU oracle. We do NOT have all opcodes, most GBA groups, an exact ARM7TDMI QEMU model, or hardware validation.

User chose **emulator-first** automation. They want brief progress/decision notes while working and a summary at the end of every turn. Do not expose private chain-of-thought; report concrete actions, findings, and decisions.

## Workspace and persistence

Project root: `/home/user/nds-suite`. Main files:

- `readme.md`: coverage, results, protocol, limitations.
- `suite.nds`: compiled current baseline, not copied upstream ROM.
- `src/auto.cpp`: replaces the Rockwrestler interactive menu in a temporary staging tree.
- `src/fail.s`: nonreturning automatic failure/timeout hooks.
- `src/cpu7.cpp`: startup ALU execution and dedicated result mailbox.
- `scripts/generate_cpu.py`: independent arithmetic model + extraction of upstream carry expectations + assembly generation.
- `src/generated/`: assembly, C++ table/checker, count header, detailed vector manifest.
- `src/qemu/`: minimal semihosted bare-metal oracle.
- `vendor/`: pinned source snapshots, with upstream sources left unchanged.
- `artifacts/`: built ELF/bin/disassembly/map files and startup-observation symbol addresses.
- `reports/`: retained actual reports, not synthetic examples.
- `coverage.json`, `upstream-lock.json`: scope/provenance ledger.

System tools under `/opt`, installed Python packages, processes, and `.venv` may not persist across Arena snapshots. Source, ROM, reports, artifacts, and vendored files under `/home/user` do persist. Never depend on a previous turn's shell exports.

## Reconstruct the environment

Prerequisites: x86-64 Debian/Ubuntu with network access, sudo, Python, and adequate disk space. Review `scripts/setup.sh` before running; it modifies apt sources and installs the official signed devkitPro packages.

```sh
cd /home/user/nds-suite
bash scripts/setup.sh
source .venv/bin/activate
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
```

`py-desmume==0.0.9` had a working Python 3.13 manylinux x86-64 wheel in this session. If another platform has no wheel, use a compatible Python/platform or build that backend separately. Its native library is an older DeSmuME 0.9.12 core; do not confuse it with the Debian `desmume-cli` 0.9.13 package. No SDL window, X server, proprietary firmware, or BIOS files are required for this HLE-boot harness.

Build environment actually used:

- devkitARM r68-1, GCC 16.1.0-1
- devkitarm-binutils 2.46.0-1
- ndstool 2.3.1-1
- QEMU 10.0.13 (Debian package revision in `reports/qemu.json`)
- py-desmume 0.0.9

`reports/devkitpro-packages.txt` is the complete installed package list. The setup script installs current versions, not a reproducibly archived toolchain. For byte-identical rebuilds, preserve/reinstall the recorded package versions from trusted archives or a pinned container, maintaining signature verification. Do not disable package verification to obtain an old version. Source-reproducible builds with later tools still require rerunning all checks.

If vendor snapshots are absent, clone the URLs and checkout exact commits in `upstream-lock.json`. Never silently follow moving `latest` or HEAD. Rockwrestler has no license file in this revision; investigate permission before external publication.

## Rebuild and validate

```sh
bash scripts/build.sh > reports/build.log 2>&1
python3 scripts/run_qemu.py
python3 scripts/run_desmume.py             # expected nonzero today
python3 scripts/test_negative.py
python3 scripts/test_host.py
sha256sum suite.nds
```

Build stages:

1. Generate 560 new vectors and import 93 GBA carry checks.
2. Copy Rockwrestler to `work/rock`, leaving vendor untouched.
3. Replace only its menu/failure adapters; inject ARM7 CPU call once after initial stack setup.
4. Compile ARM9 with `-mcpu=arm946e-s`, ARM7 with `-mcpu=arm7tdmi`, ARM state, C++ exceptions/RTTI off.
5. Link with original bare-metal linker scripts, extract binaries, package with ndstool.
6. Save maps/disassembly, ROM hash, compiler information, and observation symbol addresses.

Do not migrate to libnds without reviewing tests that deliberately modify TCM, interrupt state, VRAM, shared WRAM, and IPC. Framework dependencies on the hardware being tested can cause false failures or hangs.

Known tool warnings: original volatile chained assignments, RWX load segment, and deliberately unpredictable LDM/STM encodings. They are not suppressed evidence of a failed build. Deterministic build timestamp replaces upstream wall-clock text.

### Expected current results

- 653-vector oracle: PASS on QEMU arm946 and ti925t proxy.
- DS CPU groups: PASS on both emulated processors.
- Overall emulator group summary: 19 PASS / 4 FAIL / 2 EMULATOR_CRASH.
- Failure IDs: LDM/STM 4; IPCFIFO 37 (`0x25`); DIV32/32 21 (`0x15`); TCM 0.
- DIV64/32 and DIV64/64 processes die with return code -8/SIGFPE.
- Eight host checks pass, including reports from three negative scenarios.

Do not change upstream expected values merely to get green results. LDM/STM assertion 4 concerns base-in-list writeback (architecturally unpredictable); IPCFIFO assertion 0x25 has an upstream comment about emulator divergence. Investigate other failures on another backend and real hardware before attribution.

`python3 scripts/test_negative.py` injects an incorrect first expected result into a TEMPORARY ROM, proves assertion 1 fails, tests an invalid selector, and tests a one-frame timeout. It leaves the published ROM intact. `test_host.py` verifies these retained negative reports plus layout/CRC/counts/oracle report. These host checks are not a replacement for executing the ROM.

### Runner details

`python3 scripts/run_desmume.py --group N --output reports/name.json` selects one group. Without `--group`, each of 25 groups runs in a separate subprocess and clean ROM boot. `--frames` defaults 600, `--timeout` defaults 30 seconds per subprocess. Every group writes a log; results include JSON and JUnit XML. The bundled emulator may crash; process isolation is intentional.

`--child` is an internal worker mode and returns JSON rather than asserting that its test passed via process status. Use the parent runner for CI exit semantics. A negative worker exit is classified EMULATOR_CRASH; a positive Python/launch failure is HARNESS_ERROR. Wall and frame timeouts are distinct. Fail-fast inside a group means later assertions in that group are not covered by that execution.

The standalone ROM runs sequentially and halts on failure. The host selector patch is necessary to collect later groups after a failure. Runtime reset/recovery inside the DS ROM is not implemented and cannot safely be faked after arbitrary memory/IPC/TCM failures.

Mailbox addresses:

- `0x023f0000`: ARM9 protocol, seven u32 words; format in readme.
- `0x023ef000`: ARM7 ALU result and diagnostic details.
- `0x02000004`: upstream ARM7 startup observations.

Reserved result areas are outside the current ARM9 code image. If the test image expands, add explicit linker reservations/assertions before approaching them. Current host layout check is only a coarse size bound; strengthen it when changing layout. The existing upstream linker/runtime is intentionally minimal.

## QEMU limitation — do not mislabel it

The installed QEMU's CPU list has `arm946`, not `arm7tdmi`. `ti925t` supports an ARMv4T comparison but is not the DS ARM7TDMI. QEMU's Integrator/CP board is not a DS. This oracle currently tests only the shared generated ALU/carry subset; it does not validate Rockwrestler's ARMv5 DSP tests or hardware tests.

To satisfy exact ARM7 verification, choose and document one of:

1. Implement/audit an actual ARM7TDMI QEMU CPU model, with chip-specific behavior validated independently.
2. Obtain a suitable independently maintained QEMU fork and audit it, rather than trusting its CPU name.
3. Ask the user to accept an explicit alternative reference such as real hardware plus a separately validated CPU interpreter, while preserving that it is not QEMU verification.

Do not rename `ti925t`, use ARM926, or call ARMv4T compiler flags an exact CPU oracle.

## Next implementation priorities

1. **Define “all opcodes” rigorously.** Create per-CPU ARM/Thumb encoding-family manifests with legal/unallocated/unpredictable/implementation-defined classifications. Coverage requires instruction families, forms, flags, operand classes, register aliases, condition outcomes, privilege modes, and exceptions—not enumerating all 2^32 words indiscriminately.
2. **Port more GBA CPU groups.** Start with shifter and multiply-long expected tables. Shifter PC tests encode GBA addresses and need semantic relocation. Preserve source IDs and provenance. Long multiply C/V behavior differs/has undefined portions; use architecture- and CPU-specific masks rather than blindly retaining GBA CPSR bits.
3. **Expand generated CPU cases.** ARM/Thumb loads/stores, all addressing modes, signed transfers, block transfers, branches, stack operations, interworking, long multiply, DSP saturation/Q flag, PSR access, exceptions/banked registers, SWI and undefined instructions. Execute legal ARMv4T cases on both CPUs, ARMv5TE-only cases on ARM9; add explicit ARM7 undefined-instruction expectations for invalid v5 encodings only after an exception harness exists.
4. **Extend the QEMU runner.** Capture observed values/flags and per-vector results, not just first failure; add v5TE legal instruction cases. Separate QEMU model disagreement from NDS-only behavior. Keep exact ARM7 status unresolved until genuinely solved.
5. **Audit the remaining GBA groups.** Record each test's DS counterpart or justified non-applicability. DS memory maps, BIOS SWIs, DMA/timer clocks, serial protocols, and video hardware are not drop-in GBA equivalents. Overlapping Rockwrestler areas do not count as a source-level port.
6. **Add another DS backend.** Current py-desmume core is old. Use a pinned current melonDS or DeSmuME headless adapter capable of reading the mailbox, with isolated boots and signal/timeout handling. Never fabricate a pass when an emulator lacks an automation interface.
7. **Improve failure diagnostics.** ARM9 observed vs expected values, per-assertion events, startup sync handshakes instead of conservative busy delays, assertion subcase counts, and snapshots before crash-prone IO writes. Avoid using IPC itself as the sole reporting channel for IPC tests.
8. **Real hardware and distribution.** Confirm chip-specific expectations on DS hardware, review undefined behavior in C/C++ and upstream assembly, establish a safe host-export path if requested, resolve Rockwrestler licensing, and then produce a final release with a signed/pinned provenance manifest.

## Lessons from this implementation

- A Thumb `bx pc` return trampoline needs correct word alignment. An initial misplaced NOP caused both CPUs to fail the first Thumb vector; disassembly exposed it. The generated sequence now aligns the BX and both DS/QEMU runs pass.
- ARM7 startup CPU checks must be inserted only at initial stack setup, not every command-loop reset.
- QEMU 10 here needs `-audio driver=none`; merely declaring an unused `-audiodev` did not silence board initialization.
- The official devkitPro bootstrap script prompted interactively and aborted with closed stdin. `scripts/setup.sh` uses explicit noninteractive apt commands instead.
- A backend crash or missing completion is not a failed CPU assertion and certainly not PASS. Retain these classifications and the failing logs.

## Publication checklist

Before calling this the finished suite, close the coverage ledger, run the full applicable matrix on both CPUs, justify every skip/unpredictable case, resolve exact ARM7 reference verification, document backend/hardware evidence, and verify licensing. Current `suite.nds` is a useful working milestone only.
