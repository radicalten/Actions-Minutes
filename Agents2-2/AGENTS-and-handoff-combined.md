# AGENTS.md — NooDS-Wii: ARMv4T/ARMv5TE → Wii PowerPC 750CL JIT

This is the single consolidated guide for the project. It replaces the former `AGENTS.md` and `Handoff.md` pair. Statuses were re-checked on **2026-10-10** in a fresh sandbox; each row says whether it was re-run today or carried forward from an earlier session.

---

## 0. Agent directives

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs with `flock` (`tools/run_dolphin.sh` does this).
- **Conserve context.** Inspect targeted line ranges. Redirect logs to disk and inspect with `grep`/`tail`/`sed`. Never `cat` a Dolphin log (NUL bytes and ALSA noise; `run_dolphin.sh` strips them).
- **Report honestly.** Use `PASS`, `FAIL`, `HANG`, or `NOT_RUN` with exact evidence (commit, hash, counts, exit code, `END: X/Y`). Missing files or captures are `NOT_RUN`, never zero mismatches.
- **Do not overstate performance.** The interpreter (`JIT=0 BLOCKS=0`) is currently faster than both JIT variants. Never claim a JIT speedup over the interpreter.
- **Keep evidence in the repository** under `evidence/`, and built DOLs under `deliverables/`. Do not fabricate output.
- **Do not destroy work.** Run `git status` before any switch or reset. No `rm -rf` on a checkout or reference tree unless the user asks.
- **Target Wii only** (`-mrvl`). The `vbagx` clone is read-only study material. Never build, link, or include it.
- **Preserve the interpreter** as the correctness oracle and fallback.
- **Do not commit or push** unless the user asks. Nothing from the 2026-10-10 re-verification has been pushed.

---

## 1. Repository layout (corrected)

The git repository and the project are nested. Every path in this guide is relative to one of these roots:

| Name | Path (in this sandbox) | Contents |
|---|---|---|
| `REPO` | `/home/user/NooDS-Wii` | Git root: `https://github.com/radicalten/Actions-Minutes2.git`, branch `main` (HEAD `e5f5b5f` at re-verification). Holds `AGENTS.md`, `Handoff.md`, and the `NooDS-Wii/` project directory. |
| `PROJ` | `$REPO/NooDS-Wii` | `Makefile`, `tools/`, `evidence/`, `nds/`, `gba/`, `README.md`, `LICENSE`. Run every build and tool command from here. |
| `SRC` | `$PROJ/NooDS-Wii` | Emulator and JIT sources (`arm_jit.h`, `arm_jit.cpp`, `jit_bridge.S`, `memory.cpp`, `interpreter*.cpp`, `main.cpp`, …). |

Corrections against the old docs:

- The old guide used `NooDS-Wii/<file>` for source paths. The correct location is `SRC/<file>`.
- The docs say the work branch is `feature/arm-ppc-jit`. **That branch does not exist on the remote.** Only `main` exists. Its recent history includes commits that deleted and re-added `AGENTS.md`, `Handoff.md`, and the project directory.
- Base commit `1c995b4` is **not in the Actions-Minutes2 history**. It is HEAD of upstream `https://github.com/radicalten/NooDS-Wii.git` `main`, and it builds and reproduces (see §3).
- The docs say ROMs are git-ignored. **They are tracked** (`nds/rockwrestler.nds`, `gba/suite.gba`), and no `.gitignore` exists.
- All `tools/*.sh` and `tools/*.py` are tracked as mode `100644`. Run them with `bash tools/...`, or `chmod +x` them locally. Do not commit mode changes unless the user asks.

### Artifacts the old docs cite that are NOT in the repository

The old docs cited the following, but they are absent from `main`:

- `deliverables/` (all DOLs, `.sha256` files, and `base_1c995b4_unmodified.dol`)
- `WORKSPACE_MANIFEST.md`
- `local_changes_2026-10-10.patch`
- Most of `evidence/`: `g0_*` build logs and hashes, `g1_stub_disasm.log`, `g2_summary.md`, `g2_compare_*.json`, `g3_summary.md`, `g3_compare_*.json`, `gbaout_*.log`, `mgba_summary_*.json`, `g4_timing_summary.md`, `g5_arm7_blocks_summary.md`, `g6_arm9_blocks_summary.md`, `poll_skip_2026-10-10.md`, `jit_review_2026-10-10.md`, `reverify_2026-10-10.md`

Present in `evidence/`: `vbagx_ref_commit.txt`, `vbagx_jit_files.txt`, `vbagx_jit_grep.txt`, `vbagx_jit_notes.md`, `vbagx_arch_map.md`, `toolchain_bootstrap_result.txt`. Generated outputs from this re-verification are untracked (see §9).

Any result that depends on an absent artifact stays `CARRIED FORWARD` until the artifact is regenerated.

---

## 2. Environment

### Sandbox facts (2026-10-10)

- Debian 13 x86_64, user `user` with passwordless `sudo`, **2 cores, ~2 GB RAM, ~21 GB free**. `apt-get` works.
- Files outside `/home/user` do not persist between sessions. This includes `/opt/devkitpro` and apt-installed packages.

### Host dependencies

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"
```

Verified: `dolphin-emu 2503+dfsg-1+deb13u1`, `binutils-arm-none-eabi 2.44`, and `python3-pyelftools` installed. `mkfs.vfat` is in `/usr/sbin`.

### devkitPPC / libogc toolchain

```bash
cd $PROJ
sudo bash tools/bootstrap_toolchain.sh        # NOTE: must be run through bash (mode 100644)
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
```

- Installs and SHA-256-pins: devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4, devkitppc-rules 1.2.1, devkitppc-crtls 2.1.0-1, libogc 3.1.0, libfat-ogc 2.1.0-4, gamecube-tools 1.0.7 (`elf2dol`), and general-tools 1.4.4. Source: `https://wii.leseratte10.de/devkitPro/`.
- Ends with a link smoke test. Writes `evidence/toolchain_bootstrap_result.txt`.
- **Verified 2026-10-10: `STATUS=PASS`**, GCC `16.1.0`, AS `2.46.0.20260210`, about 3 s runtime with a warm package cache.

### ROM test assets (git-tracked in `main`)

| File | Size | SHA-256 |
|---|---:|---|
| `nds/rockwrestler.nds` | 39,433 B | `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` |
| `gba/suite.gba` | 524,288 B | `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` |

**Verified 2026-10-10: both hashes match.** If they are missing, re-download with:

```bash
cd $PROJ && mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ]        || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
```

---

## 3. Verification status (2026-10-10 re-verification)

Status key: **VERIFIED** = re-run today in this sandbox. **CARRIED** = claimed by an earlier session; its evidence is not in the repo, so it was not re-checked. **NOT_RUN** = not executed.

| Gate | What it checks | Status | Result / evidence |
|---|---|---|---|
| Repo checkout | `Actions-Minutes2` `main` clones and builds | **VERIFIED** | HEAD `e5f5b5f`. Project in `NooDS-Wii/`. |
| Base build | Unmodified upstream `1c995b4` (`make -j2`) | **VERIFIED** | `NooDS-Wii.dol` 1,317,888 B, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`. Matches the old doc. ~21 s wall. |
| vbagx pin (G-1) | Pinned reference commit exists | **VERIFIED** (commit exists) | `4d5b9984e9b7457c9146e37dace5b95fa04b73ef` resolves in `https://github.com/dborth/vbagx.git`. Study files present in `evidence/vbagx_*`. Content of the study was not re-audited. |
| Toolchain | devkitPPC bootstrap | **VERIFIED** | `STATUS=PASS` (see §2). |
| ROMs | SHA-256 of test assets | **VERIFIED** | Both match (see §2). |
| **G0** Builds | Three JIT configs compile | **VERIFIED (builds) / DIGESTS DO NOT MATCH DOCS** | All three build, exit 0. Rebuilding `JIT=0 BLOCKS=0` gave identical bytes. The digests in the old doc (§5) could **not** be reproduced from this source. Current digests are in §5. |
| **G1** Encoder oracle | 52 PowerPC 750CL forms vs `powerpc-eabi-as`/`objdump` | **VERIFIED** | `FORMS_MATCHED=52/52`, `REQUIRED_MINIMUM=44/44`, `STATUS=PASS` (`tools/check_g1_encoder.sh`). In-situ MEM2 disassembly log is absent, so that part is CARRIED. |
| **G2** Differential, rockwrestler | Checkpoints 15/30/45 vs interpreter; pool-wrap `POOL_LIMIT_WORDS=1024` | **VERIFIED for rockwrestler** | `rw_jit1_blocks0`, `rw_jit1_blocks1`, plus their `_wrap` runs: each `mismatch_count = 0`, `status = PASS`. Dumps: 413,696 B mem, 393,216 B fb. Interpreter `_wrap` also `PASS` (sanity). |
| **G2** Differential, suite.gba | Frames 50/60/120 | **NOT_RUN this session** | No command in the source docs. CARRIED from earlier session only. |
| **G3** mGBA auto-suite | `gba/suite.gba` to `ALL DONE`, three variants | **VERIFIED** | All three: `13` suites, `1` SKIP, `ALL DONE`, `6,998` tests (`3,506` passed, `3,492` failed). `gbaout.log` 411,393 B, SHA-256 `97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff` in every variant. `mismatch_count = 0` for `jit1_blocks0` and `jit1_blocks1`. |
| **G4** Phase 1 timing | Phase 1 vs interpreter | **CARRIED FORWARD (not re-measured)** | Earlier: Phase 1 ≈ 1.74× slower on rockwrestler, ≈ 3.4× on suite.gba. Summary file absent. |
| **G5** Phase 2 ARM7 timing | Phase 2 vs Phase 1 and interpreter | **CARRIED FORWARD (not re-measured)** | Earlier: about 1.5–1.7× faster than Phase 1; still ≈ 1.12× (rockwrestler) to ≈ 1.5× (suite.gba) **slower than the interpreter**. |
| **G6** ARM9 blocks | ARMv5TE native blocks + CP15 exits | **CARRIED FORWARD** | Earlier counters: `blkComp9 = 80`, `blkHit9 = 5,011,952`, `blkInsn9 = 14,955,280`, `cp15Exit9 = 21`, 0 mismatches. Not re-run this session. |

### Re-measured timing (single run, this session)

- G3 wall-clock on `suite.gba`: interpreter 14 s, `jit1_blocks0` 39 s, `jit1_blocks1` 19 s.
- Rockwrestler G4/G5 timing was not re-measured. The `accum_us` figures in old docs are session-relative and must not be compared across sessions.

### Performance headline (for planning, not a claim)

- Frame 45 rockwrestler, earlier session, `accum_us`: interpreter 4,606,860; baseline JIT 5,569,304; fast path 4,954,587; final default 4,914,235.
- The fast path is about 11% faster than the baseline JIT. The default JIT is still about 7% **slower** than the interpreter on this measurement.

---

## 4. Build commands (verified)

```bash
cd $PROJ
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

for cfg in "0 0" "1 0" "1 1"; do
  set -- $cfg
  make clean JIT=$1 BLOCKS=$2
  make -j2 JIT=$1 BLOCKS=$2
done
# Outputs in PROJ: NooDS-Wii_jit0_blocks0.dol, NooDS-Wii_jit1_blocks0.dol, NooDS-Wii_jit1_blocks1.dol
sha256sum NooDS-Wii_jit*.dol
```

Each `make` call also writes `NooDS-Wii.dol`, `.elf`, and `.elf.map` copies in `PROJ`. These are regenerable.

Build time on 2 cores is about 20–25 s per variant (`~70 s` for all three in this session).

---

## 5. Digests (current source, 2026-10-10)

| Config | Output | Size (B) | SHA-256 | Reproducible? |
|---|---|---:|---|---|
| Base `1c995b4` (no JIT) | `NooDS-Wii.dol` | 1,317,888 | `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c` | Yes (matches old doc) |
| `JIT=0 BLOCKS=0` (interpreter) | `NooDS-Wii_jit0_blocks0.dol` | 1,338,816 | `99e6b49275d40f8a8af55298992e15a5a986d8e055ddd9d856f549db1a9b3873` | Yes (rebuilt, identical) |
| `JIT=1 BLOCKS=0` (Phase 1) | `NooDS-Wii_jit1_blocks0.dol` | 1,336,992 | `bb0853bd2a865b53324200a479071a29bf55b19ee1dc9a3da5f69fa94ac7d70d` | Not separately rebuilt |
| `JIT=1 BLOCKS=1` (Phase 2) | `NooDS-Wii_jit1_blocks1.dol` | 1,364,064 | `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e` | Not separately rebuilt |

These are the digests of the current `main` source. They are the ones the G2/G3 runs used. The old §2 digests (`12f073…`, `6d09df…`, `c69f71…`) are **superseded and not reproducible** from this source. Those DOLs predate the DISPSTAT fast path and the skip-flag work. Do not cite them as current.

---

## 6. Architecture

### 6.1 Goal

A Wii PowerPC 750CL (Broadway) dynamic recompiler inside NooDS-Wii for **both ARM7 (ARMv4T) and ARM9 (ARMv5TE)**, covering **both ARM and Thumb**. The interpreter (`JIT=0 BLOCKS=0`) stays the exact reference oracle.

### 6.2 Phase 1 — 1:1 stub JIT (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=0`)

- One eligible guest dispatch maps to one cached native stub in MEM2 (`stubTable[cpuIdx][8192]`).
- The stub calls the resolved `Interpreter` member handler (`resolveArmHandler` / `resolveThumbHandler`) via `bctrl`, with `(Interpreter *cpu, uint32_t opcode)`. The opcode was already fetched by `Interpreter::runOpcode`. It returns the exact cycle cost through `jit_exit`.
- It does not refetch, double-advance `PC`, refill the pipeline twice, skip condition handling, or change scheduler timing.
- Condition-false (`case 0`) and reserved-condition (`case 2`, `handleReserved()`) opcodes stay on the interpreter.

### 6.3 Phase 2 — native emitters and multi-instruction blocks (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=1`)

- **Native emitters** (`emitNativeArm`, `emitNativeThumb`) translate ALU ops, immediate shifts (`LSL`/`LSR`/`ASR`/`ROR`, with shifter carry-out on logical `S==1` forms), `MOV`/`MVN`, `NEG`, `CMP`/`CMN`/`TST`/`TEQ`, `ADD`/`SUB`/`RSB`, non-flag `ADC`/`SBC`/`RSC`, `AND`/`EOR`/`ORR`/`BIC`, ARM9 `CLZ`, PC/SP-relative adds, and Thumb `BL` setup. Output is Broadway machine words in MEM2.
- **Flags**: exact NZCV in `Interpreter::cpsr` bits 31..28. `N` from result bit 31. `Z` from `cntlzw`. `C` from XER CA after `addc`/`subfc`. `V` from signed-overflow expressions.
- **Block compiler** (`compileBlock`, `tryExecuteBlock`): up to `MAX_BLOCK_INSTRS = 24` contiguous instructions within one 4 KB page. A block may start with a single-register load (`firstIsLoad`, called via `bctrl`). It may end with `B`, `BL`, `BX`, `BLX`, or `Bcc`, including 2-instruction `[Load, Branch]` blocks.
- **Optimizations**: backward CPSR flag-liveness pass (`thumbWrittenFlagsMask`, `armWrittenFlagsMask`, `armReadsCarry`). Branch-terminated blocks use a tail-call epilogue (`execBranchArm` / `execBranchThumb`) that skips dead `pcData`/`pipeline` updates, with a same-4 KB-page fast path.
- **Safety**: blocks never cross `core->events[0].cycles`. ARM9 CP15 (`MCR`/`MRC`) exits to the interpreter (`cp15Exit9`).
- **Invalidation**: `alignas(32) BlockEntry` (one Broadway cache line). A 1 KB `pageHasBlockBits` bitmap filters writes on non-code pages. `tailHash` verification on page-epoch changes stops false SMC recompiles from stack/data writes sharing an IWRAM page (`0x03000000`).

### 6.4 Key files (paths relative to `SRC`)

- **`arm_jit.h`**: `namespace JitPpc` (52-form encoder); `class ArmPpcJit` with `stubTable[2][8192]`, `blockTable[2][8192]`, `pageEpoch[2][4096]`, `pageHasBlockBits[2][128]`, `setPoolLimitWords`, invalidation API, `dumpStats`. `DEFAULT_POOL_BYTES = 1 MiB`, `MAX_BLOCK_INSTRS = 24`, `NOODS_JIT_POLL_SKIP` default `0`.
- **`arm_jit.cpp`**: MEM2 code pool via `Noods_MEM2_Alloc` (`0x93129ee0` GBA mode, `0x930afdc0` NDS mode). `flushCodeRange` runs `dcbst; sync; icbi; sync; isync` on every emitted stub or block. Core is about 46.37 MiB inside a ~51.87 MiB MEM2 arena, so 1 MiB for the pool fits.
- **`jit_bridge.S`**: `jit_enter`/`jit_exit`. 32-byte frame, saves `LR` at `36(r1)` and `r28..r31` at `16(r1)..28(r1)`, holds `Interpreter *cpu` in `r31`, dispatches via `mtctr r3; bctr`.
- **`interpreter.h` / `interpreter.cpp`**: hooks into `init`, `loadState`, `directBoot`, and `runOpcode()`.
- **`memory.h` / `memory.cpp` / `dldi.cpp`**: `invalidateSharedAddr` in `Memory::write<T>`/`writeFallback<T>` (includes VRAM); `invalidateCpu` in `updateMap9`/`updateMap7`; `invalidateAll` in `Dldi::patchRom`. The **DISPSTAT / VCOUNT / IPC_SYNC read fast path** lives in `memory.cpp` (around line 630) and is ON.
- **`main.cpp`**: `NoodsTestHarness` reads `sd:/autoboot.txt` (`MAX_FRAMES`, `CHECKPOINTS`, `POOL_LIMIT_WORDS`). It writes per-frame timebase profiling (`PPCGetTickCount` / `PPCTicksToUs`) and deterministic dumps (`sd:/g2_state.log`, `sd:/mem_dump.bin`, `sd:/fb_dump.bin`). It calls `fatUnmount("sd"); fatInitDefault();` before printing the sentinel, so FAT caches flush.

### 6.5 Runtime flags

| Flag | Default | Status |
|---|---|---|
| `NOODS_JIT` / `NOODS_JIT_BLOCKS` | per Makefile (`JIT`, `BLOCKS`) | Build config. `JIT=0` is the oracle. |
| `NOODS_JIT_IDLE_SKIP` | `0` | ARM9 idle-loop skip. Measured net loss (`accum_us` 5,948,401 vs 5,569,302 baseline). Compiled out. |
| `NOODS_JIT_POLL_SKIP` | `0` | Joint ARM9/ARM7 fixed-point poll skip with a global `pollEpoch`. G2-clean at frames 15/30/45/120. Never fires on rockwrestler (`skips = 0`) and ~1.7× slower when enabled. **Leave off.** Redesign the invalidation (address-aware, not global) before reconsidering. Enable experimentally with `CXXFLAGS=-DNOODS_JIT_POLL_SKIP=1 make JIT=1 BLOCKS=1`. |
| DISPSTAT / VCOUNT / IPC_SYNC fast path (`memory.cpp`) | ON | Verified by G2 (rockwrestler, this session) and G3. |

---

## 7. Gate definitions

- **G-1**: vbagx reference study pinned to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`. Artifacts in `evidence/vbagx_*`. To re-clone the read-only reference (not tracked):
  ```bash
  mkdir -p /home/user/reference
  git clone --depth 1 https://github.com/dborth/vbagx.git /home/user/reference/vbagx
  git -C /home/user/reference/vbagx fetch --depth 1 origin 4d5b9984e9b7457c9146e37dace5b95fa04b73ef
  git -C /home/user/reference/vbagx checkout FETCH_HEAD
  ```
- **G0**: all three configs build. Hashes are in §5.
- **G1**: 52/52 encoder forms bit-exact vs GNU `as`/`objdump`. In-situ disassembly checks are CARRIED.
- **G2**: checkpoint parity vs interpreter at frames `15,30,45` (rockwrestler) and `50,60,120` (suite.gba). `mismatch_count = 0` across exact cycle, register, CPSR, memory (413,696 B) and framebuffer (393,216 B) dumps. Includes `POOL_LIMIT_WORDS=1024` wrap runs.
- **G3**: mGBA suite to `ALL DONE`. Identical `gbaout.log` across variants.
- **G4**: Phase 1 timing measured (not a speed claim). Result: slower than interpreter.
- **G5**: Phase 2 ARM7 blocks vs Phase 1. Faster than Phase 1, still slower than interpreter.
- **G6**: Phase 2 ARM9 blocks and CP15 exits. Counters in §3 (carried).

---

## 8. Tooling (`PROJ/tools/`)

All scripts are mode `100644`. Run them with `bash` (or `chmod +x` locally).

1. `bootstrap_toolchain.sh`: pinned devkitPro/libogc/crtls install plus a link smoke test. Requires `sudo`.
2. `build_sd_image.sh <out.img> [--autoboot sd:/...]`: builds a 128 MiB FAT16 SD image with `suite.gba`, `rockwrestler.nds`, and optional `autoboot.txt`. Needs `mtools`/`dosfstools`.
3. `run_dolphin.sh DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL]`: `flock`-serialized, timeout-bounded, sentinel-aware headless Dolphin runner. Writes `result.txt`, `dolphin.log`.
4. `check_g1_encoder.sh` and `test_encoder.cpp`: G1 encoder oracle.
5. `run_g2_case.sh DOL LABEL ROM CHECKPOINTS MAX_FRAMES TIMEOUT [POOL_LIMIT]`: runs one G2 case into `evidence/g2_<LABEL>/`. **It deletes and recreates that directory each run.**
6. `run_suite_gba.sh DOL LABEL OUT_DIR [TIMEOUT]`: full mGBA suite to `ALL DONE`. Writes `gbaout_<LABEL>.log` and `mgba_summary_<LABEL>.json` into `OUT_DIR`.
7. `compare_runs.py --mode checkpoints|suites --ref ... --cand ... --out ...`: G2 and G3 comparator. Reports `status`, `mismatch_count`, and `mismatches`. Reports absent frames as mismatches instead of crashing.

The old docs describe these tool fixes: `Makefile` and tools honor `DEVKITPRO` (default `/opt/devkitpro`); `check_g1_encoder.sh` cds relative to its own location; `run_g2_case.sh` and `run_suite_gba.sh` call sub-tools via `bash`; `bootstrap_toolchain.sh` no longer `chmod`s tracked files. **Verified by use this session:** `DEVKITPRO` honored, `run_g2_case.sh` and `run_suite_gba.sh` run their sub-tools correctly, and the bootstrap runs without `chmod`. The `check_g1_encoder.sh` cd behavior was exercised (it ran from `PROJ`) but not tested from another directory.

---

## 9. Re-verification sequence (tested 2026-10-10)

```bash
# A. Checkout
REPO=/home/user/NooDS-Wii
git clone https://github.com/radicalten/Actions-Minutes2.git $REPO      # skip if present; check git status first
PROJ=$REPO/NooDS-Wii && cd $PROJ

# B. Host packages and toolchain (§2)
sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools
sudo bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# C. ROM hashes (§2)
sha256sum nds/rockwrestler.nds gba/suite.gba

# D. G0 builds (§4) and digests (§5)
for cfg in "0 0" "1 0" "1 1"; do set -- $cfg; make clean JIT=$1 BLOCKS=$2; make -j2 JIT=$1 BLOCKS=$2; done
sha256sum NooDS-Wii_jit*.dol

# E. G1
bash tools/check_g1_encoder.sh > /tmp/g1.log 2>&1 && tail -3 /tmp/g1.log      # expect FORMS_MATCHED=52/52, STATUS=PASS

# F. G2 rockwrestler (interpreter oracle first, then JIT variants)
bash tools/run_g2_case.sh NooDS-Wii_jit0_blocks0.dol rw_interp        "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
bash tools/run_g2_case.sh NooDS-Wii_jit1_blocks0.dol rw_jit1_blocks0  "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
bash tools/run_g2_case.sh NooDS-Wii_jit1_blocks1.dol rw_jit1_blocks1  "sd:/nds/rockwrestler.nds" "15,30,45" 45 90 0
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_rw_interp --cand evidence/g2_rw_jit1_blocks1 --out evidence/g2_compare_rw_jit1_blocks1.json
# Wrap stress (POOL_LIMIT_WORDS=1024): rerun with label suffix _wrap and pool arg 1024, then compare

# G. G3 mGBA suite (each run ~15–40 s)
bash tools/run_suite_gba.sh NooDS-Wii_jit0_blocks0.dol interp      /tmp/g3_interp      300
bash tools/run_suite_gba.sh NooDS-Wii_jit1_blocks0.dol jit1_blocks0 /tmp/g3_jit1_blocks0 300
bash tools/run_suite_gba.sh NooDS-Wii_jit1_blocks1.dol jit1_blocks1 /tmp/g3_jit1_blocks1 300
python3 tools/compare_runs.py --mode suites \
  --ref /tmp/g3_interp/mgba_summary_interp.json --cand /tmp/g3_jit1_blocks1/mgba_summary_jit1_blocks1.json --out /tmp/g3c1.json
# expect status PASS, mismatch_count 0; all gbaout SHA-256 = 97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff
```

---

## 10. Open issues (for the next agent)

1. **Digests in old docs are stale.** Replace all `deliverables/*.sha256` references with §5, or regenerate and commit `deliverables/` deliberately.
2. **Evidence is missing from `main`.** Regenerate the `evidence/` artifacts listed in §1 (G0 logs, G2/G3 summaries, G4–G6 summaries, poll-skip notes) and commit them, or mark the claims they support as CARRIED.
3. **Rebuild G4/G5/G6 timing** on the current source before quoting any performance number. The current sources are slower than the interpreter.
4. **Re-run G2 for `suite.gba`** (frames 50/60/120, plus `_wrap`). This session re-ran rockwrestler only.
5. **Decide on ROM tracking.** The docs say they are git-ignored, but they are tracked. Add a `.gitignore` or update the docs.
6. **Branch `feature/arm-ppc-jit`** does not exist on the remote. Either push it or drop the references.
7. **Executable bits.** Set mode `100755` on `tools/*.sh` and `tools/*.py` with `git update-index --chmod=+x` before commit, or keep using `bash`.
8. **Remove the `NOODS_JIT_POLL_SKIP` code** or redesign its invalidation before enabling. It stays off.

---

## 11. Session log — 2026-10-10 re-verification

- Cloned `Actions-Minutes2@main` (`e5f5b5f`) to `/home/user/NooDS-Wii`. Project found at `NooDS-Wii/`.
- Installed host packages (`apt-get`, succeeded).
- Ran `bootstrap_toolchain.sh` via `sudo tools/...` → failed (`command not found`, mode 644). Re-ran via `sudo bash tools/...` → `STATUS=PASS`.
- Built base `1c995b4` from upstream → `cd48c6a4…`, matches old doc.
- Built three JIT variants → builds OK; digests differ from old doc; `JIT=0` rebuild identical.
- G1 `check_g1_encoder.sh` → 52/52 PASS.
- G2 rockwrestler: `rw_interp`, `rw_jit1_blocks0`, `rw_jit1_blocks1`, plus `_wrap` (1024) → all 0 mismatches.
- G3 suite.gba: all three → `ALL DONE`, identical gbaout SHA `97b08848…`, `compare_runs --mode suites` PASS.
- Cross-referenced old doc claims against the tree. The results are §1 and §10 above.
- **Not pushed. No commits made. Mode changes (`chmod +x` on `tools/*`) and generated outputs are present only in the local clone.**
