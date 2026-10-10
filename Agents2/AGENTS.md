# AGENTS.md — NooDS-Wii: ARMv4T/ARMv5TE → Wii PowerPC JIT

**Refreshed 2026-10-10 (Phases 1 & 2 complete; Gates G-1 through G6 verified PASS).** This guide and verification record accompanies `Handoff.md` (`handoff.md`) and the repository uploaded to `https://github.com/radicalten/Actions-Minutes2/tree/main` (built on top of `https://github.com/radicalten/NooDS-Wii.git` base commit `1c995b48c37ebf3645646968c416958f79264137` on branch `feature/arm-ppc-jit`). Everything marked **PASS** below was executed in this sandbox and its evidence is committed under `evidence/` and `deliverables/`.

## 0. Agent directives

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs with `flock` (the provided `tools/run_dolphin.sh` does this).
- **Conserve context.** Inspect targeted line ranges; redirect logs to disk and inspect with `grep`/`tail`/`sed`. Never `cat` a Dolphin log (it contains NUL bytes and ALSA noise; `run_dolphin.sh` strips both).
- **Report honestly.** Use `PASS`, `FAIL`, `HANG`, or `NOT_RUN` with exact evidence (commit, hash, counts, exit code, `END: X/Y`). Missing files or captures are `NOT_RUN`, never zero mismatches.
- **Keep evidence in the repository** under `evidence/`; built DOLs and hashes under `deliverables/`. Do not fabricate output.
- **Do not destroy work.** Inspect `git status` before switching/resetting. No `rm -rf` on a checkout or reference tree unless the user asks.
- **Target Wii only** (`-mrvl`). The vbagx clone is read-only study material; never build, link, or include it.
- Work on branch `feature/arm-ppc-jit` (or `main` when cloned from `https://github.com/radicalten/Actions-Minutes2/tree/main`). Preserve the interpreter as the correctness oracle and fallback.
- **Sandbox facts (this environment):** Debian 13 x86_64, user `user` with passwordless `sudo`, 2 cores, ~2 GB RAM, ~20 GB free disk. `apt-get` works. Files outside `/home/user` (notably `/opt/devkitpro` and apt-installed packages) **do not persist** between sessions; see §13 for the ~2-minute re-provisioning sequence.

## 1. Verified state (2026-10-10, this sandbox)

| Check / Gate | Result |
|---|---|
| Repository & Base | **PASS** — Upstream base `https://github.com/radicalten/NooDS-Wii.git` pinned at `1c995b48c37ebf3645646968c416958f79264137`; workspace uploaded to `https://github.com/radicalten/Actions-Minutes2/tree/main` (work branch `feature/arm-ppc-jit`). |
| Toolchain bootstrap | **PASS** — `tools/bootstrap_toolchain.sh` installs devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4, rules 1.2.1, libogc 3.1.0, libfat-ogc 2.1.0-4, gamecube-tools 1.0.7 (`elf2dol`), general-tools 1.4.4 from `https://wii.leseratte10.de/devkitPro/`, plus **devkitppc-crtls v2.1.0-1** (`devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst`). All artifacts SHA-256 pinned; ends with a link smoke test. Runtime ≈ 5 s. |
| Baseline build (`1c995b4`) | **PASS** — unmodified base `make -j2` ≈ 17 s wall. `deliverables/base_1c995b4_unmodified.dol` 1,317,888 bytes, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`. Log: `evidence/g0_baseline_build.log`. |
| Host dependencies | **PASS** — `dolphin-emu 2503+dfsg-1+deb13u1` (`/usr/games/dolphin-emu-nogui`, "Dolphin [master] 2503"), `mtools 4.0.48`, `dosfstools 4.2`, `zstd 1.5.7`, `binutils-arm-none-eabi 2.44`, `python3-pyelftools 0.32`, `xvfb`. `mkfs.vfat` is in `/usr/sbin`. |
| ROM downloads | **PASS** — `nds/rockwrestler.nds` (39,433 B, `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`) and `gba/suite.gba` (524,288 B, `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495`). Git-ignored via `/nds/` and `/gba/`; re-downloadable via §9.1. |
| **G-1 (`vbagx` study)** | **PASS** — Pinned at `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`. Complete 10-point study and NooDS architecture mapping committed in `evidence/vbagx_ref_commit.txt`, `evidence/vbagx_jit_files.txt`, `evidence/vbagx_jit_grep.txt`, `evidence/vbagx_jit_notes.md`, and `evidence/vbagx_arch_map.md`. |
| **G0 (`JIT=0/1` builds)** | **PASS** — Clean reproducible per-config builds (`build_jit$(JIT)_blocks$(BLOCKS)`):<br>• `JIT=0 BLOCKS=0` (`deliverables/interp.dol`, 1,338,752 B): `12f07335f643488779e3f3548d2ccaa9fd068549d79c110ceeed23504480e6b8`<br>• `JIT=1 BLOCKS=0` (`deliverables/ref.dol`, 1,335,872 B): `6d09dfcc403a473699bbdfec7d9f0b5422ff393ca7f449e47a3bcd8b1c89cf7d`<br>• `JIT=1 BLOCKS=1` (`deliverables/jit.dol`, 1,356,992 B): `c69f71097e6365b2656e27b6ad2ad66732d62ff3d5344b1597f4b368be2273bb`<br>Evidence: `evidence/g0_all_hashes.txt`, `evidence/g0_jit0_build.log`, `evidence/g0_jit1_blocks0_build.log`, `evidence/g0_jit1_blocks1_build.log`. |
| **G1 (PPC encoder oracle)** | **PASS** — `52/52` instruction forms verified bit-exact against `powerpc-eabi-as` and `powerpc-eabi-objdump` (`REQUIRED_MINIMUM=44/44`) via `tools/check_g1_encoder.sh` (`evidence/g1_encoder_oracle.log`), plus in-situ MEM2 stub/block disassembly at `0x93129ee0`, `0x93129f18`, `0x930affc8`, and `0x930afdc0` (`evidence/g1_stub_disasm.log`). |
| **G2 (Differential parity)** | **PASS** — `mismatch_count = 0` across all 8 comparison runs (`rockwrestler.nds` at frames `15, 30, 45` and `suite.gba` at frames `50, 60, 120`, both default 262,144-word pool and `POOL_LIMIT_WORDS=1024` pool-wrap stress runs). Exact cycle, register, CPSR, memory dump (`413,696` B), and framebuffer dump (`393,216` B) parity. Evidence: `evidence/g2_summary.md`, `evidence/g2_compare_*.json`. |
| **G3 (mGBA auto-suite)** | **PASS** — All 3 variants (`interp`, `jit1_blocks0`, `jit1_blocks1`) complete 13 `END: X/Y` suites, 1 `SKIP` (`Video tests`), and `ALL DONE` across 6,998 tests (`3,506` passed, `3,492` failed) with identical `gbaout.log` (`411,393` B, SHA-256 `97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff`, `mismatch_count = 0`). Evidence: `evidence/g3_summary.md`, `evidence/g3_compare_*.json`. |
| **G4 (Phase 1 baseline)** | **PASS** — Phase 1 (`JIT=1 BLOCKS=0`) measured honestly against `JIT=0 BLOCKS=0` using both guest Broadway timebase ticks (`PPCTicksToUs`) and host wall-clock (`evidence/g4_timing_summary.md`). |
| **G5 (ARM7 blocks)** | **PASS** — Phase 2 (`JIT=1 BLOCKS=1`) compiles native ARM & Thumb multi-instruction blocks (`blkComp7 = 10,386`, `blkHit7 = 1,166,665`, `blkInsn7 = 3,381,479` at frame 120 of `suite.gba`), achieving **2.58× faster** full-suite completion (`19s` vs `49s` wall) and **1.55×–1.70× faster** `rockwrestler.nds` execution (`5,520,078 us` vs `8,551,813 us` / `9,375,851 us`) over Phase 1 with `0` mismatches (`evidence/g5_arm7_blocks_summary.md`). |
| **G6 (ARM9 parity)** | **PASS** — Dual-CPU NDS9+NDS7 `rockwrestler.nds` runs with native ARMv5TE (`CLZ`) and multi-instruction blocks (`blkComp9 = 80`, `blkHit9 = 5,011,952`, `blkInsn9 = 14,955,280`), CP15 interpreter exits (`cp15Exit9 = 21`), ITCM/DTCM/map invalidations (`mapInv9 = 7`), and `0` mismatches (`evidence/g6_arm9_blocks_summary.md`). |

## 2. Mission and implementation architecture

Built a Wii PowerPC 750CL (Broadway) dynamic recompiler inside `NooDS-Wii` for **both ARM7 (ARMv4T) and ARM9 (ARMv5TE)** supporting both **ARM and Thumb** instruction sets, keeping the interpreter (`JIT=0 BLOCKS=0`) as the exact reference oracle.

### Phase 1 — 1:1 fallback JIT (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=0`)

- One eligible guest dispatch maps to one cached native stub in MEM2 (`stubTable[cpuIdx][8192]`). The stub invokes the resolved `Interpreter` member handler (`resolveArmHandler` / `resolveThumbHandler`) directly via `bctrl` with `(Interpreter *cpu, uint32_t opcode)` (already fetched by `Interpreter::runOpcode`) and returns its exact cycle cost through `jit_exit`.
- Does not refetch the opcode, advance the guest PC twice, refill the pipeline twice, skip condition handling, desynchronize `pcData`, or alter scheduler timing.
- Condition-false (`case 0`) and reserved-condition (`case 2`, `handleReserved()`) opcodes stay on the interpreter fast path.

### Phase 2 — Native ARM/Thumb emitter & multi-instruction basic blocks (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=1`)

- **Native PowerPC 750CL Emitter (`emitNativeArm`, `emitNativeThumb`)**: Translates ARM and Thumb ALU, immediate shifts (`LSL`, `LSR`, `ASR`, `ROR` including shifter carry-out on logical `S==1` instructions `ANDS`, `EORS`, `TST`, `TEQ`, `ORRS`, `MOVS`, `BICS`, `MVNS`), `MOV`/`MVN`, `NEG`, `CMP`/`CMN`/`TST`/`TEQ`, `ADD`/`SUB`/`RSB`, non-flag `ADC`/`SBC`/`RSC`, `AND`/`EOR`/`ORR`/`BIC`, ARM9 `CLZ`, PC/SP-relative adds, and Thumb `BL` setup directly into Broadway machine words in MEM2 (`0x93129ee0` / `0x930afdc0`).
- **Multi-Instruction Block Compiler (`compileBlock`, `tryExecuteBlock`)**:
  - Translates up to `MAX_BLOCK_INSTRS = 24` contiguous instructions within a 4 KB page, including blocks starting with a single-register memory load (`firstIsLoad` at `m == 0`, calling the resolved `Interpreter` member handler via `bctrl`) and optionally terminated by a conditional or unconditional branch (`Bcc`, `B`, `BL`, `BX`, `BLX`, including 2-instruction `[Load, Branch]` blocks).
  - Performs a backward `CPSR` flag-liveness pass (`thumbWrittenFlagsMask`, `armWrittenFlagsMask`, `armReadsCarry`) to eliminate dead intermediate `CPSR` flag computations inside blocks.
  - Emits a compact 10-word tail-call epilogue (`execBranchArm` / `execBranchThumb`) on branch-terminated blocks that skips dead `pcData`/`pipeline[0..1]` updates on taken branches and uses a same-4KB-page `pcData` fast path.
  - Guards execution against `core->events[0].cycles` so blocks never cross a scheduled hardware event, and exits to the interpreter on ARM9 CP15 (`MCR`/`MRC`, `cp15Exit9`).
  - Uses `alignas(32) BlockEntry` (32 bytes = 1 Broadway cache line) with packed `epochAndFlags`, a 1 KB `pageHasBlockBits` bitmap to filter `invalidateSharedAddr` writes on non-code pages, and `tailHash` verification on 4 KB page-epoch changes so stack/data writes sharing a 4 KB IWRAM page (`0x03000000`) do not cause false SMC recompilations.

## 3. Checkout and workspace setup

When starting from `https://github.com/radicalten/Actions-Minutes2/tree/main` (or existing `/home/user/NooDS-Wii`):

```bash
cd /home/user
# If cloning fresh from Actions-Minutes2:
# git clone https://github.com/radicalten/Actions-Minutes2.git NooDS-Wii
cd /home/user/NooDS-Wii
git status -s
git log --oneline -n 5
```

Because ROM binaries (`nds/rockwrestler.nds` and `gba/suite.gba`) are git-ignored via `/nds/` and `/gba/`, download and verify them if missing:

```bash
cd /home/user/NooDS-Wii && mkdir -p nds gba
curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
# Expect:
# f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  nds/rockwrestler.nds
# 63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495  gba/suite.gba
```

## 4. vbagx reference study (Gate G-1 — Complete)

All G-1 study artifacts are committed under `evidence/`:
- `evidence/vbagx_ref_commit.txt` (`4d5b9984e9b7457c9146e37dace5b95fa04b73ef`)
- `evidence/vbagx_jit_files.txt` (18 files)
- `evidence/vbagx_jit_grep.txt` (29 lines)
- `evidence/vbagx_jit_notes.md` (10-point architectural extraction with exact `file:line` citations)
- `evidence/vbagx_arch_map.md` (NooDS-Wii dual-CPU ARM7+ARM9, ARM+Thumb adaptation and non-goals)

If you ever need to re-clone the read-only `vbagx` reference tree locally (not tracked in git):

```bash
mkdir -p /home/user/reference
git clone --depth 1 https://github.com/dborth/vbagx.git /home/user/reference/vbagx
git -C /home/user/reference/vbagx fetch --depth 1 origin 4d5b9984e9b7457c9146e37dace5b95fa04b73ef
git -C /home/user/reference/vbagx checkout FETCH_HEAD
```

## 5. Acceptance gates summary

| Gate | Description | Status | Primary Evidence |
|---|---|---|---|
| **G-1** | `vbagx` Reference Architecture Study (`4d5b9984e9b7457c9146e37dace5b95fa04b73ef`) | **PASS** | `evidence/vbagx_jit_notes.md`, `evidence/vbagx_arch_map.md` |
| **G0** | Clean Reproducible Builds (`JIT=0 BLOCKS=0`, `JIT=1 BLOCKS=0`, `JIT=1 BLOCKS=1`) | **PASS** | `evidence/g0_all_hashes.txt`, `evidence/g0_jit*_build.log` |
| **G1** | PowerPC 750CL Encoder Oracle (`52/52` forms) + In-Situ MEM2 Disassembly | **PASS** | `evidence/g1_encoder_oracle.log`, `evidence/g1_stub_disasm.log` |
| **G2** | Differential Parity vs. Interpreter (`rockwrestler.nds` & `suite.gba`, normal + `POOL_LIMIT_WORDS=1024` wrap runs) | **PASS** | `evidence/g2_summary.md`, `evidence/g2_compare_*.json` |
| **G3** | mGBA Auto-Suite (`gba/suite.gba` r101) — 13 `END: X/Y` suites, 1 `SKIP`, `ALL DONE`, 6,998 tests, 0 mismatches | **PASS** | `evidence/g3_summary.md`, `evidence/g3_compare_*.json`, `evidence/gbaout_*.log` |
| **G4** | Phase 1 (`JIT=1 BLOCKS=0`) Performance Baseline vs. Interpreter (`JIT=0 BLOCKS=0`) | **PASS** | `evidence/g4_timing_summary.md` |
| **G5** | Phase 2 ARM7 Native Translation & Multi-Instruction Blocks (`JIT=1 BLOCKS=1`) | **PASS** | `evidence/g5_arm7_blocks_summary.md` |
| **G6** | Phase 2 ARM9 (ARMv5TE) Native Translation, Multi-Instruction Blocks & CP15 Interpreter Exits | **PASS** | `evidence/g6_arm9_blocks_summary.md` |

## 6. Environment and toolchain

### Host dependencies (re-run at session start if container reset)

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"
```

### devkitPPC / libogc (`tools/bootstrap_toolchain.sh`)

```bash
cd /home/user/NooDS-Wii
sudo tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
```

- Installs devkitPPC r50 (`GCC 16.1.0`, `binutils 2.46.0`), `newlib 4.6.0`, `devkitppc-rules 1.2.1`, `devkitppc-crtls 2.1.0-1` (`rvl.ld` + `libogc_common.ld`), `libogc 3.1.0`, `libfat-ogc 2.1.0-4`, `gamecube-tools 1.0.7` (`elf2dol`), and `general-tools 1.4.4` from `https://wii.leseratte10.de/devkitPro/`.
- Build commands:
  - `make -j2 JIT=0 BLOCKS=0` → `NooDS-Wii_jit0_blocks0.dol` (`deliverables/interp.dol`)
  - `make -j2 JIT=1 BLOCKS=0` → `NooDS-Wii_jit1_blocks0.dol` (`deliverables/ref.dol`)
  - `make -j2 JIT=1 BLOCKS=1` → `NooDS-Wii_jit1_blocks1.dol` (`deliverables/jit.dol`)

## 7. Key implementation details & contracts

- **`NooDS-Wii/jit_bridge.S`:** PowerPC EABI assembly bridge (`jit_enter` / `jit_exit`) saving `LR` at `36(r1)` and `r28..r31` at `16(r1)..28(r1)` inside a 32-byte stack frame while holding `Interpreter *cpu` in `r31` and dispatching via `mtctr r3; bctr`.
- **`NooDS-Wii/arm_jit.h` & `NooDS-Wii/arm_jit.cpp`:**
  - `namespace JitPpc`: 52-form PowerPC 750CL instruction encoder (`PPC` is predefined by `powerpc-eabi-g++`).
  - `DEFAULT_POOL_BYTES = 1 MiB` (`262,144` words) allocated from MEM2 via `Noods_MEM2_Alloc` (`0x93129ee0` in GBA mode, `0x930afdc0` in NDS mode), leaving headroom alongside the ~46.37 MiB `Core` object in MEM2.
  - `flushCodeRange`: 32-byte cache-line `dcbst; sync; icbi; sync; isync` coherency sequence.
  - `Interpreter` offsets verified at compile time and runtime; `registers[0..15]` accessed via `lwz`/`stw` on `Interpreter::usrRegs` (for `R0..R7` and `R15`) or banked pointer table `Interpreter::registers[reg]`.
  - In `libogc 3.1.0`, EXI console logging uses `SYS_Report` (mirrored to Dolphin stdout with `-C Logger.Logs.OSREPORT=True`), and Broadway timebase conversion uses `PPCTicksToUs(PPCGetTickCount())` from `<tuxedo/ppc/clock.h>`.
  - `libfat-ogc 2.1.0` caches dirty FAT sectors in RAM; `NoodsTestHarness` calls `fatUnmount("sd"); fatInitDefault();` before printing the completion sentinel (`MAX_FRAMES_DONE` or `ALL DONE`) so `mcopy` sees all flushed files.

## 8. Tooling (`tools/`)

1. `tools/bootstrap_toolchain.sh` — Pinned devkitPro/libogc/crtls installer + link smoke test.
2. `tools/build_sd_image.sh` — Creates a 128 MiB FAT16 SD image populated with `suite.gba`, `rockwrestler.nds`, and optional `autoboot.txt`.
3. `tools/run_dolphin.sh` — `flock`-serialized, timeout-bounded, sentinel-aware headless Dolphin runner.
4. `tools/check_g1_encoder.sh` & `tools/test_encoder.cpp` — Gate G1 52-form PowerPC 750CL encoder oracle against `powerpc-eabi-as`/`objdump`.
5. `tools/run_g2_case.sh` — Runs a G2 checkpoint & dump test case (`g2_state.log`, `mem_dump.bin`, `fb_dump.bin`) under `evidence/g2_<label>/`.
6. `tools/run_suite_gba.sh` — Runs the full mGBA auto-suite (`suite.gba`) to `ALL DONE` and extracts `gbaout_<label>.log` and `mgba_summary_<label>.json`.
7. `tools/compare_runs.py` — Differential comparator for `--mode checkpoints` (G2) and `--mode suites` (G3).

## 9. Trimmed workspace & evidence layout

```text
NooDS-Wii/
  AGENTS.md                       This guide and verification record
  Handoff.md / handoff.md         Final handoff report with gate status, hashes, and benchmarks
  Makefile                        Supports JIT=0|1 and BLOCKS=0|1 with per-config build dirs
  NooDS-Wii/                      Emulator + ARM/Thumb PowerPC JIT source (arm_jit.h, arm_jit.cpp, jit_bridge.S, ...)
  tools/                          Toolchain bootstrap, SD builder, Dolphin runner, G1–G3 harnesses
  deliverables/
    base_1c995b4_unmodified.dol   Unmodified base build (cd48c6a4...) + .sha256
    interp.dol                    JIT=0 BLOCKS=0 build (12f07335...) + .sha256
    ref.dol                       JIT=1 BLOCKS=0 Phase 1 build (6d09dfcc...) + .sha256
    jit.dol                       JIT=1 BLOCKS=1 Phase 2 build (c69f7109...) + .sha256
  evidence/
    vbagx_*                       Gate G-1 study notes, architecture map, file/grep lists, pinned commit
    g0_*                          Gate G0 build logs and SHA-256 manifests
    g1_encoder_oracle.log         Gate G1 52/52 PowerPC instruction oracle output
    g1_stub_disasm.log            Gate G1 in-situ MEM2 stub & block disassemblies
    g2_summary.md                 Gate G2 differential parity summary table
    g2_compare_*.json             Gate G2 8 JSON comparison reports (all mismatch_count: 0)
    g2_rw_* / g2_suite_*          Gate G2 checkpoint logs, memory dumps, framebuffer dumps, and Dolphin results
    g3_summary.md                 Gate G3 mGBA auto-suite parity summary
    g3_compare_*.json             Gate G3 suite comparison reports (all mismatch_count: 0)
    gbaout_*.log                  Extracted 411,393-byte mGBA suite logs (interp, jit1_blocks0, jit1_blocks1)
    mgba_summary_*.json           Parsed 6,998-test mGBA suite summaries
    g4_timing_summary.md          Gate G4 Phase 1 timing baseline summary
    g5_arm7_blocks_summary.md     Gate G5 Phase 2 ARM7 block compilation & speedup summary
    g6_arm9_blocks_summary.md     Gate G6 Phase 2 ARM9 block compilation & CP15 exit summary
```

## 10. Quick re-provisioning & verification sequence for the next agent

```bash
# 1. Install host packages & devkitPPC toolchain (~2 min on a fresh container)
sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools
cd /home/user/NooDS-Wii
sudo tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# 2. Download git-ignored ROMs if starting from a fresh git clone
mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ] || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba

# 3. Rebuild all 3 variants and verify hashes against deliverables/*.sha256
make -j2 JIT=0 BLOCKS=0 && make -j2 JIT=1 BLOCKS=0 && make -j2 JIT=1 BLOCKS=1
sha256sum NooDS-Wii_jit0_blocks0.dol NooDS-Wii_jit1_blocks0.dol NooDS-Wii_jit1_blocks1.dol
cat deliverables/*.sha256

# 4. Run Gate G1 encoder oracle and re-verify G2/G3 comparisons
tools/check_g1_encoder.sh
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_rw_interp --cand evidence/g2_rw_jit1_blocks1 --out evidence/g2_compare_rw_jit1_blocks1.json
python3 tools/compare_runs.py --mode suites --ref evidence/mgba_summary_interp.json --cand evidence/mgba_summary_jit1_blocks1.json --out evidence/g3_compare_jit1_blocks1.json
```
