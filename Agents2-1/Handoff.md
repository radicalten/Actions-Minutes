# Handoff Report — NooDS-Wii ARMv4T/ARMv5TE → Wii PowerPC 750CL JIT (`feature/arm-ppc-jit`)

**Repository:** `https://github.com/radicalten/Actions-Minutes2/tree/main` (built on `https://github.com/radicalten/NooDS-Wii.git` base commit `1c995b48c37ebf3645646968c416958f79264137`, work branch `feature/arm-ppc-jit`)
**Companion Guide:** `AGENTS.md`

> **Current status (2026-10-10, latest session; supersedes the older statements below where they conflict).**
> - **Default build = fast path.** `memory.cpp` has the DISPSTAT / VCOUNT / IPC_SYNC I/O read fast path ON. `NOODS_JIT_IDLE_SKIP` = 0 (ARM9 idle-loop skip; measured net loss). `NOODS_JIT_POLL_SKIP` = 0 (joint ARM9/ARM7 poll skip; implemented, G2 clean, but skips=0 on rockwrestler and ~1.7x slower when enabled). See `evidence/poll_skip_2026-10-10.md`.
> - **Gates on the default build:** G2 rockwrestler frames 15/30/45 vs a fresh interpreter build: PASS, `mismatch_count` 0 (`evidence/g2_compare_fin_def45.json`). G3 suite.gba vs interpreter: PASS, `mismatch_count` 0, gbaout sha `97b08848…` (`evidence/g3_compare_dflt.json`). The enabled poll-skip build also passes G2 at frames 15/30/45/120 (`evidence/g2_compare_fin_poll120.json`, skips=0).
> - **Performance, frame 45 rockwrestler, same session** (`accum_us`, wall-clock based, so compare within one session only): interpreter 4,606,860; baseline JIT (`base_prev`) 5,569,304; fast path 4,954,587; final default 4,914,235. Result: the fast path is about 11% faster than the baseline JIT, but **the JIT is still about 7% slower than the interpreter** on this measurement. The earlier "11% gain" is relative to baseline only.
> - **Deliverables are stale for the fast path.** `deliverables/*.dol` (hashes verified this session) predate the fast-path and skip-flag changes. The current default DOL is not stored; rebuild with `make JIT=1 BLOCKS=1`.
> - **Evidence gaps (not new):** `evidence/g0_baseline_build.log`, `evidence/g0_jit0_build.log`, `evidence/g0_all_hashes.txt` and `evidence/g1_stub_disasm.log` cited in §1 are absent from this workspace (they were absent before this session). The G0 hash claims rest on `deliverables/*.sha256`, which match.
> - **Cleanup (2026-10-10):** raw Dolphin logs, per-run memory/framebuffer dumps, SD images, root-level DOL/ELF copies, `build_*` dirs and scratch dirs (profiling copy, raw timing repeats, DOL snapshots) were deleted. Kept: compare JSONs, `result.txt`, `g2_state.log`, `autoboot.txt`, summaries, deliverables, ROMs. Mem-dump hashes remain in the compare JSONs.
> - **ROM path:** the canonical autoboot entry is `sd:/nds/rockwrestler.nds`. Some local G2 runs used the bare `nds/rockwrestler.nds`; their memory and framebuffer hashes match the `sd:/` runs.
> - **Uncommitted work:** the full diff is saved as `local_changes_2026-10-10.patch` in the repo root. `WORKSPACE_MANIFEST.md` lists the workspace layout. Nothing has been pushed.
> - **Executable bit:** `tools/*.sh` and `tools/*.py` are tracked as `100644`. `run_suite_gba.sh` calls `build_sd_image.sh` and `run_dolphin.sh` directly, so `chmod +x` them first or run via `bash`.

> **Correction block (2026-10-10, local re-verification in a fresh sandbox; not pushed).**
> - Rebuilt all 3 variants: SHA-256 match the committed deliverables. G1 52/52, G2 8/8 (0 mismatches), G3 identical gbaout logs (0 mismatches). See `evidence/reverify_2026-10-10.md`.
> - **Performance correction:** the speedups below are Phase 2 vs Phase 1 only. Against the interpreter, Phase 1 is slower (rockwrestler guest accum 8.66 s vs 4.98 s, ~1.74×; suite.gba wall 42–49 s vs 13–14 s). Phase 2 is still slower than the interpreter (rockwrestler 5.57 s vs 4.98 s, ~1.12×; suite.gba 20–22 s vs 13–14 s, ~1.5×). G4 is a measured baseline, not a performance pass.
> - **Branch/base:** `feature/arm-ppc-jit` and base commit `1c995b4` are not present on the `main` remote. Base-build hash is unverified.
> - **Evidence:** the original G0 build logs, G2/G3 summaries, and G4–G6 summaries cited below were absent from the repo. Replacements were regenerated locally (`evidence/g1_encoder_oracle.log`, `evidence/g2_*`, `evidence/gbaout_*`, `evidence/mgba_summary_*`, `evidence/g{4,5,6}_*_summary.md`) and are not committed.
> - Doc fixes: the companion guide is `Handoff.md` (not `handoff.md`); the stray duplicate "# 7" block in §4 is removed.
> - **Tool fixes (2026-10-10):** `check_g1_encoder.sh` now cds relative to its own location (was a hardcoded clone path); `compare_runs.py` compares only frames present in both runs and reports absent frames as mismatches instead of crashing (`KeyError`); `Makefile` and tools honor `DEVKITPRO` (default `/opt/devkitpro`); `run_g2_case.sh` calls sub-tools via `bash`; `bootstrap_toolchain.sh` no longer `chmod`s tracked files. Verified: builds byte-identical, G1 52/52, all 8 G2 comparisons PASS, G3 log hash unchanged, negative controls FAIL as expected.
> - **Executable bit:** git records `tools/*.sh` and `tools/*.py` as `100644`, so the docs' direct `tools/run_*.sh` invocations need `chmod +x` (done locally) or `git update-index --chmod=+x` before any commit.
> - **Generated summaries:** `evidence/g2_summary.md` and `evidence/g3_summary.md` (previously cited but absent) are now regenerated from the JSONs.
> - **Poll-skip result (2026-10-10):** `NOODS_JIT_POLL_SKIP` (joint ARM9/ARM7 fixed-point skip) is implemented, default 0. It is G2-clean (PASS at frames 15, 30, 45, 120) but never fires on rockwrestler (skips=0) and is about 1.7x slower when enabled. Default build keeps the fast path with the skip compiled out: G2 PASS (15/30/45) and G3 PASS. Details: `evidence/poll_skip_2026-10-10.md`.
> - **Idle-skip finding:** `NOODS_JIT_IDLE_SKIP` is default 0 because it is a measured net loss (see the review evidence).

---

## 1. Per-Gate Status Summary

| Gate | Description | Status | Primary Evidence Artifacts |
|---|---|---|---|
| **G-1** | `vbagx` Reference Architecture Study (`4d5b9984e9b7457c9146e37dace5b95fa04b73ef`) | **PASS** | `evidence/vbagx_ref_commit.txt`, `evidence/vbagx_jit_files.txt`, `evidence/vbagx_jit_grep.txt`, `evidence/vbagx_jit_notes.md`, `evidence/vbagx_arch_map.md` |
| **G0** | Clean Reproducible Builds (hashes reproduced 2026-10-10; original logs absent from repo) (`JIT=0 BLOCKS=0`, `JIT=1 BLOCKS=0`, `JIT=1 BLOCKS=1`) | **PASS** | `evidence/g0_baseline_build.log`, `evidence/g0_baseline_hashes.txt`, `evidence/g0_jit0_build.log`, `evidence/g0_jit1_blocks0_build.log`, `evidence/g0_jit1_blocks1_build.log`, `evidence/g0_all_hashes.txt` |
| **G1** | PowerPC 750CL Encoder Oracle (`52/52` forms vs `powerpc-eabi-as`/`objdump`) + In-Situ MEM2 Stub & Block Disassembly | **PASS** | `evidence/g1_encoder_oracle.log` (`52/52 PASS`), `evidence/g1_stub_disasm.log` |
| **G2** | Differential Parity vs. Interpreter (`rockwrestler.nds` @ frames `15,30,45`; `suite.gba` @ frames `50,60,120`; `POOL_LIMIT_WORDS=1024` wrap tests) | **PASS** | `evidence/g2_summary.md`, `evidence/g2_compare_rw_jit1_blocks0.json`, `evidence/g2_compare_rw_jit1_blocks0_wrap.json`, `evidence/g2_compare_rw_jit1_blocks1.json`, `evidence/g2_compare_rw_jit1_blocks1_wrap.json`, `evidence/g2_compare_suite_jit1_blocks0.json`, `evidence/g2_compare_suite_jit1_blocks0_wrap.json`, `evidence/g2_compare_suite_jit1_blocks1.json`, `evidence/g2_compare_suite_jit1_blocks1_wrap.json` |
| **G3** | mGBA Auto-Suite (`gba/suite.gba` r101) — 13 `END: X/Y` suites, 1 `SKIP`, `ALL DONE`, 6,998 tests, 0 mismatches | **PASS** | `evidence/g3_summary.md`, `evidence/gbaout_interp.log`, `evidence/gbaout_jit1_blocks0.log`, `evidence/gbaout_jit1_blocks1.log`, `evidence/mgba_summary_interp.json`, `evidence/mgba_summary_jit1_blocks0.json`, `evidence/mgba_summary_jit1_blocks1.json`, `evidence/g3_compare_jit1_blocks0.json`, `evidence/g3_compare_jit1_blocks1.json` |
| **G4** | Phase 1 (`JIT=1 BLOCKS=0`) Performance Baseline vs. Interpreter (`JIT=0 BLOCKS=0`) — **measured: Phase 1 is slower than the interpreter (~1.74× rockwrestler, ~3.4× suite.gba)** | **PASS (as baseline measurement)** | `evidence/g4_timing_summary.md` |
| **G5** | Phase 2 ARM7 Native Translation & Multi-Instruction Blocks (`JIT=1 BLOCKS=1`) — 2.58x speedup over **Phase 1** (still ~1.5× slower than the interpreter on suite.gba) on full `suite.gba` (`19s` vs `49s`) and 1.55x–1.70x speedup on `rockwrestler.nds` (`5.52s` vs `8.55s`/`9.38s` guest `accum_us`) vs. Phase 1 with 0 mismatches | **PASS** | `evidence/g5_arm7_blocks_summary.md`, `evidence/g3_compare_jit1_blocks1.json`, `evidence/g2_compare_suite_jit1_blocks1.json` |
| **G6** | Phase 2 ARM9 (ARMv5TE) Native Translation, Multi-Instruction Blocks & CP15 Interpreter Exits (`cp15Exit9=21`, `blkInsn9=14,955,280`) | **PASS** | `evidence/g6_arm9_blocks_summary.md`, `evidence/g2_compare_rw_jit1_blocks1.json`, `evidence/g2_compare_rw_jit1_blocks1_wrap.json` |

---

## 2. Deliverables & SHA-256 Digests

| Deliverable | Path | Size (Bytes) | SHA-256 |
|---|---|---:|---|
| Unmodified Base (`1c995b4`) | `deliverables/base_1c995b4_unmodified.dol` | `1,317,888` | `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c` |
| Interpreter (`JIT=0 BLOCKS=0`) | `deliverables/interp.dol` | `1,338,752` | `12f07335f643488779e3f3548d2ccaa9fd068549d79c110ceeed23504480e6b8` |
| Phase 1 Reference JIT (`JIT=1 BLOCKS=0`) | `deliverables/ref.dol` | `1,335,872` | `6d09dfcc403a473699bbdfec7d9f0b5422ff393ca7f449e47a3bcd8b1c89cf7d` |
| Phase 2 Block JIT (`JIT=1 BLOCKS=1`) | `deliverables/jit.dol` | `1,356,992` | `c69f71097e6365b2656e27b6ad2ad66732d62ff3d5344b1597f4b368be2273bb` |

---

## 3. Architecture & Implementation Summary

### 3.1 Source Files Added / Modified

- `NooDS-Wii/arm_jit.h`:
  - `namespace JitPpc`: 52-form PowerPC 750CL / Broadway instruction encoder verified against `powerpc-eabi-as` and `powerpc-eabi-objdump` (`tools/test_encoder.cpp`, `tools/check_g1_encoder.sh`).
  - `class ArmPpcJit`: Per-CPU direct-mapped `stubTable[2][8192]`, `alignas(32) BlockEntry blockTable[2][8192]` (32 bytes = 1 Broadway cache line with packed `epochAndFlags`), `pageEpoch[2][4096]`, `pageHasBlockBits[2][128]`, runtime-configurable pool size (`setPoolLimitWords`), SMC/memory-map invalidation API, and telemetry (`dumpStats`).
- `NooDS-Wii/arm_jit.cpp`:
  - **MEM2 Code Pool Allocation**: Allocates a 32-byte-aligned 1 MiB (`262,144` words) executable pool in MEM2 via `Noods_MEM2_Alloc(DEFAULT_POOL_BYTES)` (`0x93129ee0` in GBA mode, `0x930afdc0` in NDS mode). Because `Core` itself is `0x02e5fed8` bytes (~46.37 MiB) allocated from the ~51.87 MiB MEM2 arena (`0x90002000..0x933e0000`), sizing `DEFAULT_POOL_BYTES` to `1 MiB` allows both `Core` and the JIT code pool to reside in MEM2 with ~1.4 MiB of headroom for ROM sections.
  - **Broadway Cache Coherency (`flushCodeRange`)**: Executes explicit 32-byte line `dcbst; sync; icbi; sync; isync` sequences over every newly emitted stub or block before execution.
  - **Phase 1 (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=0`)**: Compiles a 1:1 PowerPC stub per guest instruction that passes `(Interpreter *cpu, uint32_t opcode)` (already fetched by `Interpreter::runOpcode`) to the resolved `Interpreter` member function (`resolveArmHandler` / `resolveThumbHandler`), returning exact dynamic cycle counts without double-fetching or double-advancing `PC`/`pcData`.
  - **Phase 2 (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=1`)**:
    - Native ARM and Thumb emitters (`emitNativeArm`, `emitNativeThumb`) translating ALU, immediate shifts (`LSL`, `LSR`, `ASR`, `ROR` including shifter carry-out on logical `S==1` instructions `ANDS`, `EORS`, `TST`, `TEQ`, `ORRS`, `MOVS`, `BICS`, `MVNS`), `MOV`/`MVN`, `NEG`, `CMP`/`CMN`/`TST`/`TEQ`, `ADD`/`SUB`/`RSB`, non-flag `ADC`/`SBC`/`RSC`, `AND`/`EOR`/`ORR`/`BIC`, ARM9 `CLZ`, PC/SP-relative adds, and Thumb `BL` setup into native PowerPC 750CL instructions.
    - Exact ARM NZCV flag synthesis in `Interpreter::cpsr` bits 31..28 (`N` from result bit 31, `Z` from `cntlzw` bit 5, `C` from `mfxer` CA bit 29 after `addc`/`subfc`, and `V` from signed overflow bit 31 of `~(op1 ^ op2) & (res ^ op2)` for addition or `(op1 ^ op2) & ~(res ^ op2)` for subtraction).
    - Multi-instruction basic block compiler (`compileBlock`, `tryExecuteBlock`) translating up to 24 contiguous instructions within a 4 KB page, including blocks starting with a single-register memory load (`firstIsLoad` at `m == 0`, calling the resolved `Interpreter` member handler directly via `bctrl`) and optionally terminated by a conditional or unconditional branch (`B`, `BL`, `BX`, `BLX`, `Bcc`).
    - Performs a backward `CPSR` flag-liveness pass to eliminate dead intra-block flag updates, coalesces `R15`, `pcData`, and `pipeline[0..1]` updates (`lwbrx`/`lhbrx`), tail-calls `execBranchArm`/`execBranchThumb` on branch-terminated blocks to skip dead `pcData`/`pipeline` sync on taken branches and use a same-4KB-page `pcData` fast path, and guards execution against `core->events[0].cycles`.
    - Compact 1 KB `pageHasBlockBits` bitmap filtering `invalidateSharedAddr` writes on non-code pages, plus content-hash (`tailHash`) verification on 4 KB page-epoch changes so stack/data writes sharing a 4 KB page in GBA IWRAM (`0x03000000`) do not trigger false SMC recompilations or `icbi` flushes.
    - Direct interpreter exits for ARM9 CP15 instructions (`MCR`/`MRC`, `cp15Exit9`) and non-native memory/system instructions.
- `NooDS-Wii/jit_bridge.S`:
  - PowerPC EABI assembly bridge (`jit_enter` / `jit_exit`) establishing a 32-byte stack frame, saving `LR` at `36(r1)` and nonvolatile registers `r28..r31` at `16(r1)..28(r1)`, placing `Interpreter *cpu` in `r31`, and dispatching via `mtctr r3; bctr`.
- `NooDS-Wii/interpreter.h` & `NooDS-Wii/interpreter.cpp`:
  - Hooked `ArmPpcJit` into `Interpreter::init`, `loadState`, `directBoot`, and `runOpcode()` while keeping condition-false (`case 0`) and reserved-condition (`case 2`, `handleReserved()` for ARM9 `BLX imm`, BIOS HLE IRQ return, and DLDI HLE opcodes) on the interpreter fast path.
- `NooDS-Wii/memory.h`, `NooDS-Wii/memory.cpp`, `NooDS-Wii/dldi.cpp`:
  - Added mGBA debug register handling (`0x4FFF780`, `0x4FFF700`, `0x4FFF600..0x4FFF6FF`) syncing to `sd:/gbaout.log` and `SYS_Report`.
  - Hooked `ArmPpcJit::invalidateSharedAddr` into `Memory::write<T>` and `Memory::writeFallback<T>` (including VRAM mappings), `ArmPpcJit::invalidateCpu` into `Memory::updateMap9` and `Memory::updateMap7`, and `ArmPpcJit::invalidateAll` into `Dldi::patchRom`.
- `NooDS-Wii/main.cpp`:
  - Added `NoodsTestHarness` supporting `sd:/autoboot.txt` (`MAX_FRAMES`, `CHECKPOINTS`, `POOL_LIMIT_WORDS`), per-frame Broadway timebase profiling (`PPCGetTickCount` / `PPCTicksToUs`), deterministic state/memory/framebuffer dumps (`sd:/g2_state.log`, `sd:/mem_dump.bin`, `sd:/fb_dump.bin`), and clean SD FAT cache flushing before termination.

---

### 3.2 Runtime flags and experiments (current)

| Flag | Default | Status |
|---|---|---|
| `NOODS_JIT` / `NOODS_JIT_BLOCKS` | per Makefile (`JIT`, `BLOCKS`) | Build configuration. `JIT=0` is the interpreter oracle. |
| `NOODS_JIT_IDLE_SKIP` | 0 | ARM9 idle-loop skip. Measured net loss (accum_us 5,948,401 vs 5,569,302 baseline). Left compiled out. |
| `NOODS_JIT_POLL_SKIP` | 0 | Joint ARM9/ARM7 fixed-point poll skip, global `pollEpoch`. G2 PASS (15/30/45/120), but it never fires on rockwrestler and runs about 1.7x slower when enabled. Recommended: leave off, then remove or redesign the invalidation (address-aware, not a global epoch). |
| DISPSTAT / VCOUNT / IPC_SYNC I/O fast path (`memory.cpp`) | ON | Verified by G2 and G3 on the default build. |

Enable the experimental skip with `CXXFLAGS=-DNOODS_JIT_POLL_SKIP=1 make JIT=1 BLOCKS=1`.

## 4. Reproducible Build & Verification Commands (Next Agent Onboarding)

```bash
# 1. Clone https://github.com/radicalten/Actions-Minutes2 (if not already in /home/user/NooDS-Wii)
# git clone https://github.com/radicalten/Actions-Minutes2.git /home/user/NooDS-Wii
cd /home/user/NooDS-Wii

# 2. Bootstrap host dependencies & devkitPPC toolchain (if starting a fresh container session)
sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools
sudo tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# 3. Ensure ROM test assets exist (git-ignored under nds/ and gba/)
mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ] || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba

# NOTE (2026-10-10): root-level DOL copies were removed in cleanup. Build before running G2.
# 4. Gate G0: Build all 3 variants
make clean JIT=0 BLOCKS=0 && make -j2 JIT=0 BLOCKS=0
make clean JIT=1 BLOCKS=0 && make -j2 JIT=1 BLOCKS=0
make clean JIT=1 BLOCKS=1 && make -j2 JIT=1 BLOCKS=1

# 5. Gate G1: PowerPC encoder oracle (52/52 forms)
tools/check_g1_encoder.sh

# 6. Gate G2 / G4 / G5 / G6: Differential checkpoint & pool-wrap parity
tools/run_g2_case.sh NooDS-Wii_jit0_blocks0.dol rw_interp "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
tools/run_g2_case.sh NooDS-Wii_jit1_blocks0.dol rw_jit1_blocks0 "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
tools/run_g2_case.sh NooDS-Wii_jit1_blocks1.dol rw_jit1_blocks1 "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_rw_interp --cand evidence/g2_rw_jit1_blocks1 --out evidence/g2_compare_rw_jit1_blocks1.json

```
