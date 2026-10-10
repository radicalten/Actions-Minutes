# Handoff Report — NooDS-Wii ARMv4T/ARMv5TE → Wii PowerPC 750CL JIT (`feature/arm-ppc-jit`)

**Repository:** `https://github.com/radicalten/Actions-Minutes2/tree/main` (built on `https://github.com/radicalten/NooDS-Wii.git` base commit `1c995b48c37ebf3645646968c416958f79264137`, work branch `feature/arm-ppc-jit`)
**Companion Guide:** `AGENTS.md`

---

## 1. Per-Gate Status Summary

| Gate | Description | Status | Primary Evidence Artifacts |
|---|---|---|---|
| **G-1** | `vbagx` Reference Architecture Study (`4d5b9984e9b7457c9146e37dace5b95fa04b73ef`) | **PASS** | `evidence/vbagx_ref_commit.txt`, `evidence/vbagx_jit_files.txt`, `evidence/vbagx_jit_grep.txt`, `evidence/vbagx_jit_notes.md`, `evidence/vbagx_arch_map.md` |
| **G0** | Clean Reproducible Builds (`JIT=0 BLOCKS=0`, `JIT=1 BLOCKS=0`, `JIT=1 BLOCKS=1`) | **PASS** | `evidence/g0_baseline_build.log`, `evidence/g0_baseline_hashes.txt`, `evidence/g0_jit0_build.log`, `evidence/g0_jit1_blocks0_build.log`, `evidence/g0_jit1_blocks1_build.log`, `evidence/g0_all_hashes.txt` |
| **G1** | PowerPC 750CL Encoder Oracle (`52/52` forms vs `powerpc-eabi-as`/`objdump`) + In-Situ MEM2 Stub & Block Disassembly | **PASS** | `evidence/g1_encoder_oracle.log` (`52/52 PASS`), `evidence/g1_stub_disasm.log` |
| **G2** | Differential Parity vs. Interpreter (`rockwrestler.nds` @ frames `15,30,45`; `suite.gba` @ frames `50,60,120`; `POOL_LIMIT_WORDS=1024` wrap tests) | **PASS** | `evidence/g2_summary.md`, `evidence/g2_compare_rw_jit1_blocks0.json`, `evidence/g2_compare_rw_jit1_blocks0_wrap.json`, `evidence/g2_compare_rw_jit1_blocks1.json`, `evidence/g2_compare_rw_jit1_blocks1_wrap.json`, `evidence/g2_compare_suite_jit1_blocks0.json`, `evidence/g2_compare_suite_jit1_blocks0_wrap.json`, `evidence/g2_compare_suite_jit1_blocks1.json`, `evidence/g2_compare_suite_jit1_blocks1_wrap.json` |
| **G3** | mGBA Auto-Suite (`gba/suite.gba` r101) — 13 `END: X/Y` suites, 1 `SKIP`, `ALL DONE`, 6,998 tests, 0 mismatches | **PASS** | `evidence/g3_summary.md`, `evidence/gbaout_interp.log`, `evidence/gbaout_jit1_blocks0.log`, `evidence/gbaout_jit1_blocks1.log`, `evidence/mgba_summary_interp.json`, `evidence/mgba_summary_jit1_blocks0.json`, `evidence/mgba_summary_jit1_blocks1.json`, `evidence/g3_compare_jit1_blocks0.json`, `evidence/g3_compare_jit1_blocks1.json` |
| **G4** | Phase 1 (`JIT=1 BLOCKS=0`) Performance Baseline vs. Interpreter (`JIT=0 BLOCKS=0`) | **PASS** | `evidence/g4_timing_summary.md` |
| **G5** | Phase 2 ARM7 Native Translation & Multi-Instruction Blocks (`JIT=1 BLOCKS=1`) — 2.58x speedup on full `suite.gba` (`19s` vs `49s`) and 1.55x–1.70x speedup on `rockwrestler.nds` (`5.52s` vs `8.55s`/`9.38s` guest `accum_us`) vs. Phase 1 with 0 mismatches | **PASS** | `evidence/g5_arm7_blocks_summary.md`, `evidence/g3_compare_jit1_blocks1.json`, `evidence/g2_compare_suite_jit1_blocks1.json` |
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

# 7. Gate G3 / G5: Full mGBA auto-suite (6,998 tests)
tools/run_suite_gba.sh NooDS-Wii_jit1_blocks1.dol jit1_blocks1 evidence 180
python3 tools/compare_runs.py --mode suites --ref evidence/mgba_summary_interp.json --cand evidence/mgba_summary_jit1_blocks1.json --out evidence/g3_compare_jit1_blocks1.json
```
