# AGENTS.md — NooDS-Wii: ARMv4T/ARMv5TE → Wii PowerPC 750CL JIT

This is the consolidated local guide for the project. It was corrected and re-verified on **2026-10-10** against a fresh checkout of `Actions-Minutes2` `main` at `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`. The current remote commit deletes `AGENTS.md`; this attached, updated copy is therefore **not tracked in that checkout**. Each verification status below distinguishes tests re-run in this audit from claims carried forward.

---

## 0. Agent directives

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs with `flock` (`tools/run_dolphin.sh` does this).
- **Conserve context.** Inspect targeted line ranges. Redirect logs to disk and inspect with `grep`/`tail`/`sed`. Never `cat` a Dolphin log (it can contain NUL bytes and ALSA noise; `run_dolphin.sh` strips them).
- **Report honestly.** Use `PASS`, `FAIL`, `HANG`, `NOT_RUN`, or `CARRIED` with exact evidence (commit, hash, counts, exit code, `END: X/Y`). Missing files or captures are `NOT_RUN`, never zero mismatches.
- **Do not overstate performance.** Earlier timing runs found the interpreter faster than both JIT variants. G4–G6 were not re-measured in this audit; do not make current speed claims without repeating those measurements.
- **Keep evidence in the repository** under `evidence/`. The Makefile writes gate DOLs in `PROJ`; copy selected final DOLs to `deliverables/` only when packaging deliverables. Do not imply a normal build populates that absent directory, and do not fabricate output.
- **Do not destroy work.** Run `git status` before any switch or reset. No `rm -rf` on a checkout or reference tree unless the user asks. `run_g2_case.sh` itself deletes and recreates its `evidence/g2_<LABEL>/` directory; use a new label.
- **Target Wii only** (`-mrvl`). The `vbagx` clone is read-only study material. Never build, link, or include it.
- **Preserve the interpreter** as the correctness oracle and fallback.
- **Do not commit or push** unless the user asks. This audit made no commits and pushed nothing.

---

## 1. Repository layout (corrected for current `main`)

The git repository and the project are nested. Every path in this guide is relative to one of these roots:

| Name | Path (in this sandbox) | Contents |
|---|---|---|
| `REPO` | `/home/user/NooDS-Wii` | Git root: `https://github.com/radicalten/Actions-Minutes2.git`, branch `main`, HEAD `c79fd20` at re-verification. |
| `PROJ` | `$REPO/NooDS-Wii` | `Makefile`, `tools/`, `evidence/`, `nds/`, `gba/`, `README.md`, `LICENSE`. Run project builds/tools from here. |
| `SRC` | `$PROJ/NooDS-Wii` | Emulator and JIT sources (`arm_jit.h`, `arm_jit.cpp`, `jit_bridge.S`, `memory.cpp`, `interpreter*.cpp`, `main.cpp`, …). |

Corrections against earlier docs:

- The old guide used `NooDS-Wii/<file>` for source paths. The correct location is `SRC/<file>`.
- At this re-verification, the remote advertises only `main`; `feature/arm-ppc-jit` does not exist on the remote.
- Current `main` HEAD is `c79fd20` (`Delete AGENTS.md`). The preceding commit `f13c0f4` deletes `Handoff.md`. Both files existed in earlier history, but neither is present/tracked at current HEAD. Do not assume this attached AGENTS copy is present in a fresh clone.
- Base commit `1c995b4` is **not in the Actions-Minutes2 history** (checked after fetching full history). It is the current HEAD of upstream `https://github.com/radicalten/NooDS-Wii.git` `main`; its DOL build and hash were reproduced (see §3).
- ROMs are tracked (`nds/rockwrestler.nds`, `gba/suite.gba`); there is no `.gitignore` in the current project tree.
- All `tools/*.sh` and `tools/*.py` are tracked as mode `100644`. Invoke scripts with `bash` (or `python3` for Python tools). **Exception:** `run_suite_gba.sh` directly executes two child scripts, so `bash tools/run_suite_gba.sh` alone fails on a clean checkout; see §8.

### Artifacts not committed in current `main`

At the checked-out `c79fd20`, the older docs' `deliverables/`, `WORKSPACE_MANIFEST.md`, `local_changes_2026-10-10.patch`, and most earlier `evidence/` files are not tracked. Missing historical evidence includes G0 build logs/hashes, in-situ G1 disassembly, G2/G3 summaries/comparisons, G4–G6 timing summaries, poll-skip notes, and prior review/reverification reports.

Tracked evidence present at HEAD: `vbagx_ref_commit.txt`, `vbagx_jit_files.txt`, `vbagx_jit_grep.txt`, `vbagx_jit_notes.md`, `vbagx_arch_map.md`, and `toolchain_bootstrap_result.txt`.

This audit generated fresh, **untracked** captures in `evidence/`; see `evidence/reverify_2026-10-10.md` and §11. They are local evidence, not committed history. A result tied to an absent old artifact remains `CARRIED` unless re-generated.

---

## 2. Environment

### Sandbox facts (2026-10-10)

- Debian 13.7 x86_64, user `user` with passwordless `sudo`, **2 cores, about 1.9 GiB RAM, about 21 GiB free** at start. `apt-get` works.
- Files outside `/home/user` do not persist in workspace snapshots. This includes `/opt/devkitpro` and apt-installed packages.

### Host dependencies

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"
```

**Re-verified:** the package command succeeded. This environment installed `dolphin-emu 2503+dfsg-1+deb13u1`, `binutils-arm-none-eabi 2.44-3+23+b1`, `python3-pyelftools 0.32-1`, `mtools 4.0.48-1`, and `dosfstools 4.2-1.2`. `mkfs.vfat` is `/usr/sbin/mkfs.vfat`; the PATH export and the image scripts make it available.

Python caveat for this sandbox: `python3` resolves to `/usr/local/bin/python3`, which does **not** import Debian's `elftools`; `/usr/bin/python3` does. The current G0–G3 tools do not import `elftools`. Use `/usr/bin/python3` for any future step that needs the apt-installed pyelftools package.

### devkitPPC / libogc toolchain

```bash
cd "$PROJ"
sudo bash tools/bootstrap_toolchain.sh        # scripts are mode 100644; invoke through bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
```

- `bootstrap_toolchain.sh` SHA-256-pins devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4, devkitppc-rules 1.2.1, devkitppc-crtls 2.1.0-1, libogc 3.1.0, libfat-ogc 2.1.0-4, gamecube-tools 1.0.7 (`elf2dol`), and general-tools 1.4.4 from `https://wii.leseratte10.de/devkitPro/`.
- Ends with a Wii link smoke test and writes `evidence/toolchain_bootstrap_result.txt`.
- **Verified 2026-10-10: `STATUS=PASS`**, GCC `16.1.0`, AS `2.46.0.20260210`, `elf2dol` present. The smoke-test DOL linked successfully.

### ROM test assets (git-tracked in `main`)

| File | Size | SHA-256 |
|---|---:|---|
| `nds/rockwrestler.nds` | 39,433 B | `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` |
| `gba/suite.gba` | 524,288 B | `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` |

**Verified 2026-10-10: both hashes match.** If the tracked assets are missing, the earlier download URLs were:

```bash
cd "$PROJ" && mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ]        || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
```

Always compare the hashes above; do not treat a successful download as verified content.

---

## 3. Verification status (2026-10-10 re-verification)

Status key: **VERIFIED** = re-run against the stated checkout in this audit. **CARRIED** = claimed by earlier work but not re-measured/reproduced here. **NOT_RUN** = not executed.

| Gate | What it checks | Status | Result / evidence |
|---|---|---|---|
| Repo checkout | `Actions-Minutes2` `main` and nested project | **VERIFIED** | HEAD `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`; only remote branch is `main`. Current HEAD deletes `AGENTS.md`; this updated attachment is not tracked there. |
| Base build | Upstream `1c995b4` (`make -j2`) | **VERIFIED** | Upstream `main` resolved to `1c995b48c37ebf3645646968c416958f79264137`. `NooDS-Wii.dol`: 1,317,888 B, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`. |
| vbagx pin (G-1) | Pinned reference commit exists | **VERIFIED (pin only)** | Fetch resolved exactly to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`; content of the earlier study was not re-audited. Do not build the reference. |
| Toolchain | devkitPPC bootstrap and Wii link smoke test | **VERIFIED** | `STATUS=PASS`; GCC `16.1.0`, AS `2.46.0.20260210`; see §2 and `evidence/toolchain_bootstrap_result.txt`. |
| ROMs | Test asset hashes | **VERIFIED** | Both match §2. |
| **G0** Builds | Three JIT configs compile | **VERIFIED** | All exited 0; DOL hashes/sizes are in §5. Rebuilding `JIT=0 BLOCKS=0` after clean reproduced the same bytes. |
| **G1** Encoder oracle | 52 PowerPC 750CL forms vs `powerpc-eabi-as`/`objdump` | **VERIFIED** | `FORMS_MATCHED=52/52`, `REQUIRED_MINIMUM=44/44`, `STATUS=PASS`; run from `/tmp` to verify path handling. The in-situ MEM2 disassembly check remains **CARRIED**. |
| **G2** Differential, `rockwrestler.nds` | Frames 15/30/45; normal and `POOL_LIMIT_WORDS=1024` | **VERIFIED** | Both `jit1_blocks0` and `jit1_blocks1`, normal and wrap, compared against the interpreter: all 4 comparisons `PASS`, `mismatch_count=0`. Memory dump 413,696 B; framebuffer 393,216 B. |
| **G2** Differential, `suite.gba` | Frames 50/60/120; normal and `POOL_LIMIT_WORDS=1024` | **VERIFIED** | Both JIT variants, normal and wrap, compared against the interpreter: all 4 comparisons `PASS`, `mismatch_count=0`. Same dump sizes as above. |
| **G3** mGBA auto-suite | `gba/suite.gba` to `ALL DONE`, three variants | **PASS WITH LOCAL MODE WORKAROUND** | All 3 runs reached `ALL DONE`; 13 suites, 1 SKIP, 6,998 tests (3,506 passed / 3,492 failed as reported by the suite). All `gbaout.log` hashes were `97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff`; both interpreter-vs-JIT comparisons `PASS`, zero mismatches. **The clean-checkout wrapper fails exit 126 before the workaround; see §8.** This does not mean every upstream suite test passes. |
| **G4** Phase 1 timing | Phase 1 vs interpreter | **CARRIED** | Earlier run reported Phase 1 slower; timing summary is absent and was not re-measured. |
| **G5** Phase 2 ARM7 timing | Phase 2 vs Phase 1 and interpreter | **CARRIED** | Earlier: roughly 1.5–1.7× faster than Phase 1, but still slower than the interpreter. Not re-measured. |
| **G6** ARM9 blocks | ARMv5TE native blocks + CP15 exits | **CARRIED** | Earlier counters: `blkComp9 = 80`, `blkHit9 = 5,011,952`, `blkInsn9 = 14,955,280`, `cp15Exit9 = 21`, 0 mismatches. Not re-run. |

### G2/G3 interpretation notes

- Each G2 Dolphin `result.txt` reported `STATUS=PASS` and `SENTINEL_HIT=1`. The runner terminates Dolphin after the sentinel; `RC=143` in that result is expected here. Do not use `RC` alone as the pass/fail signal.
- G2/G3 used the default `NOODS_JIT_POLL_SKIP=0`; these parity passes do not verify the optional poll-skip optimization. `NOODS_JIT_IDLE_SKIP` is not present in the current source despite its earlier documentation.
- `run_g2_case.sh` can exit 0 once dump files exist even if the Dolphin result is not a pass. Check the runner `result.txt`, then require the comparator JSON to say `status=PASS` and `mismatch_count=0`.
- The G3 test suite itself reports 3,492 failed test cases in each variant. The verified result is matching suite output and successful completion, **not** complete mGBA compatibility.
- One-shot G3 elapsed times were interpreter 13 s, `jit1_blocks0` 39 s, `jit1_blocks1` 19 s. These are not controlled timing results and are not a performance claim.
- G4–G6 timing/counters were not re-measured. Historical frame-45 `accum_us` values from the earlier session were interpreter 4,606,860; baseline JIT 5,569,304; fast path 4,954,587; final default 4,914,235. Keep these **CARRIED**, not current performance evidence.

---

## 4. Build commands (verified on current `main`)

```bash
cd "$PROJ"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

set -euo pipefail
for cfg in '0 0' '1 0' '1 1'; do
  read -r JIT BLOCKS <<< "$cfg"
  make clean JIT="$JIT" BLOCKS="$BLOCKS"
  make -j2 JIT="$JIT" BLOCKS="$BLOCKS"
done
# Outputs in PROJ: NooDS-Wii_jit0_blocks0.dol, NooDS-Wii_jit1_blocks0.dol,
#                  NooDS-Wii_jit1_blocks1.dol
sha256sum NooDS-Wii_jit*.dol
```

Each build also writes `NooDS-Wii.dol`, `.elf`, and `.elf.map` copies in `PROJ`; these generic copies are overwritten by the next configuration. Config-tagged DOLs remain. Build artifacts are regenerable; check `git status` before cleaning any unrelated output.

---

## 5. Digests (current source at `c79fd20`, 2026-10-10)

| Config | Output | Size (B) | SHA-256 | Reproducible? |
|---|---|---:|---|---|
| Base upstream `1c995b4` (no JIT) | `NooDS-Wii.dol` | 1,317,888 | `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c` | Yes; rebuilt from upstream `main` at `1c995b48…` |
| `JIT=0 BLOCKS=0` (interpreter) | `NooDS-Wii_jit0_blocks0.dol` | 1,338,816 | `99e6b49275d40f8a8af55298992e15a5a986d8e055ddd9d856f549db1a9b3873` | Yes; rebuilt after clean, identical bytes |
| `JIT=1 BLOCKS=0` (Phase 1) | `NooDS-Wii_jit1_blocks0.dol` | 1,336,992 | `bb0853bd2a865b53324200a479071a29bf55b19ee1dc9a3da5f69fa94ac7d70d` | Built once in this audit |
| `JIT=1 BLOCKS=1` (Phase 2) | `NooDS-Wii_jit1_blocks1.dol` | 1,364,064 | `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e` | Built once in this audit |

These are the hashes of the current `main` source used for the G2/G3 runs. Older DOL digests (`12f073…`, `6d09df…`, `c69f71…`) are superseded and not reproducible from this source; do not cite them as current.

---

## 6. Architecture

This section is an implementation inventory carried forward from the prior review. The builds and G2/G3 parity gates were re-run against `c79fd20`; G4–G6 and every low-level architecture claim were not separately audited line by line in this session.

### 6.1 Goal

A Wii PowerPC 750CL (Broadway) dynamic recompiler inside NooDS-Wii for **both ARM7 (ARMv4T) and ARM9 (ARMv5TE)**, covering **both ARM and Thumb**. The interpreter (`JIT=0 BLOCKS=0`) stays the correctness oracle and fallback.

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
- **`arm_jit.cpp`**: MEM2 code pool via `Noods_MEM2_Alloc`; `flushCodeRange` runs `dcbst; sync; icbi; sync; isync` on emitted stubs/blocks. The prior review recorded GBA/NDS pool addresses and MEM2 sizing; re-check free MEM2 headroom before changing allocation sizes.
- **`jit_bridge.S`**: `jit_enter`/`jit_exit`. 32-byte frame, saves `LR` at `36(r1)` and `r28..r31` at `16(r1)..28(r1)`, holds `Interpreter *cpu` in `r31`, dispatches via `mtctr r3; bctr`.
- **`interpreter.h` / `interpreter.cpp`**: hooks into `init`, `loadState`, `directBoot`, and `runOpcode()`.
- **`memory.h` / `memory.cpp` / `dldi.cpp`**: `invalidateSharedAddr` in `Memory::write<T>`/`writeFallback<T>` (includes VRAM); `invalidateCpu` in `updateMap9`/`updateMap7`; `invalidateAll` in `Dldi::patchRom`. Correction: `memory.cpp` around line 630 has an explicit ARM9 halfword-read fast path for DISPSTAT (`0x04000004`) only. VCOUNT and IPCSYNC go through the normal I/O read dispatch. Separately, `arm_jit.cpp::armLoadAddrOk` whitelists DISPSTAT, VCOUNT, and IPCSYNC addresses for ARM load classification; that whitelist is not a `memory.cpp` fast path. G2/G3 parity ran with the DISPSTAT optimization active and `NOODS_JIT_POLL_SKIP=0`.
- **`main.cpp`**: `NoodsTestHarness` reads `sd:/autoboot.txt` (`MAX_FRAMES`, `CHECKPOINTS`, `POOL_LIMIT_WORDS`). It writes per-frame timebase profiling (`PPCGetTickCount` / `PPCTicksToUs`) and deterministic dumps (`sd:/g2_state.log`, `sd:/mem_dump.bin`, `sd:/fb_dump.bin`). It calls `fatUnmount("sd"); fatInitDefault();` before printing the sentinel so FAT caches flush.

### 6.5 Runtime flags

| Flag | Default | Status |
|---|---|---|
| `NOODS_JIT` / `NOODS_JIT_BLOCKS` | per Makefile (`JIT`, `BLOCKS`) | Build config. `JIT=0` is the oracle. |
| `NOODS_JIT_IDLE_SKIP` | N/A | **Not referenced in current `main` source** (source search found no implementation or macro use). Earlier notes reported a net loss (`accum_us` 5,948,401 vs 5,569,302 baseline); that historical measurement does not mean the feature can be enabled now. |
| `NOODS_JIT_POLL_SKIP` | `0` | Optional joint ARM9/ARM7 fixed-point poll skip with a global `pollEpoch`. It was not enabled in this audit. Earlier G2 checks were clean, but it never fired on rockwrestler (`skips = 0`) and was about 1.7× slower. **Leave off.** Redesign invalidation (address-aware, not global) before reconsidering. Experimental build: `CXXFLAGS=-DNOODS_JIT_POLL_SKIP=1 make JIT=1 BLOCKS=1`. Historical result, not re-tested here. |
| `NOODS_JIT_PROFILE` | Undefined/0 unless supplied | Optional dispatch profiling in `arm_jit.cpp`; `dumpStats` can print `JIT_PROF` buckets. Not enabled in this audit. |
| ARM9 DISPSTAT halfword-read fast path (`memory.cpp`) | ON | Explicit early return for aligned halfword reads at `0x04000004`; G2/G3 parity ran with it active but did not isolate its performance effect. VCOUNT/IPCSYNC are not this memory fast path. |

---

## 7. Gate definitions

- **G-1**: vbagx reference study pinned to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`. Pin fetch verified; study content not re-audited. To make a fresh read-only clone (do not overwrite an existing path):
  ```bash
  mkdir -p /home/user/reference
  REF=/home/user/reference/vbagx
  [ ! -e "$REF" ] || { echo "Refusing to overwrite existing reference path: $REF" >&2; exit 1; }
  git clone --depth 1 https://github.com/dborth/vbagx.git "$REF"
  git -C "$REF" status --short --branch
  git -C "$REF" fetch --depth 1 origin 4d5b9984e9b7457c9146e37dace5b95fa04b73ef
  git -C "$REF" status --short --branch  # check before switching to FETCH_HEAD
  git -C "$REF" checkout FETCH_HEAD
  ```
- **G0**: all three configs build. Current hashes are in §5.
- **G1**: 52/52 encoder forms bit-exact vs GNU `as`/`objdump`. In-situ MEM2 disassembly remains CARRIED.
- **G2**: checkpoint parity vs interpreter at frames `15,30,45` (`rockwrestler.nds`) and `50,60,120` (`suite.gba`), for both JIT variants. Compare exact cycles, registers, CPSR, memory (413,696 B), and framebuffer (393,216 B). Include matching `POOL_LIMIT_WORDS=1024` wrap runs.
- **G3**: mGBA suite reaches `ALL DONE` in all three variants; compare summaries and verify identical `gbaout.log` SHA-256 manually. This does **not** mean every upstream test passes.
- **G4**: Phase 1 timing vs interpreter; carried result is slower than interpreter.
- **G5**: Phase 2 ARM7 blocks vs Phase 1; carried result is faster than Phase 1 but slower than interpreter.
- **G6**: Phase 2 ARM9 blocks and CP15 exits; counters in §3 are carried.

---

## 8. Tooling (`PROJ/tools/`)

All scripts are tracked mode `100644`. Run shell scripts with `bash` (or `chmod +x` locally, then restore mode); run Python tools with `python3`.

1. `bootstrap_toolchain.sh`: pinned devkitPro/libogc/crtls install plus a link smoke test. Requires `sudo`; run as `sudo bash tools/bootstrap_toolchain.sh`.
2. `build_sd_image.sh <out.img> [--autoboot sd:/...]`: creates a 128 MiB FAT16 SD image with `suite.gba`, `rockwrestler.nds`, and optional `autoboot.txt`. Needs `mtools`/`dosfstools`.
3. `run_dolphin.sh DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL]`: `flock`-serialized, timeout-bounded, sentinel-aware headless Dolphin runner. Writes `result.txt` and a cleaned `dolphin.log`.
4. `check_g1_encoder.sh` and `test_encoder.cpp`: G1 encoder oracle. Relative-path behavior was verified by invoking the script from `/tmp`. It overwrites the fixed `evidence/g1_encoder_oracle.log`; preserve any prior untracked log before rerunning if needed.
5. `run_g2_case.sh DOL LABEL ROM CHECKPOINTS MAX_FRAMES TIMEOUT [POOL_LIMIT]`: runs one G2 case into `evidence/g2_<LABEL>/`. **It deletes and recreates that directory each run.** Inspect its `result.txt` and require `STATUS=PASS`, `SENTINEL_HIT=1`; the helper's exit code alone is insufficient.
6. `run_suite_gba.sh DOL LABEL OUT_DIR [TIMEOUT]`: G3 suite runner, but currently has a clean-checkout bug: it directly executes `tools/build_sd_image.sh` and `tools/run_dolphin.sh`. Because both are tracked mode `100644`, running `bash tools/run_suite_gba.sh ...` fails at line 36 with exit 126 (`Permission denied`). Preferred source fix is to invoke both children through `bash` (as `run_g2_case.sh` already does). Until fixed, temporarily `chmod +x tools/build_sd_image.sh tools/run_dolphin.sh`, run G3, then restore `chmod 644` if you do not intend to commit mode changes. This audit confirmed the clean-checkout failure and tested the local-mode workaround; modes were restored.
7. `compare_runs.py --mode checkpoints|suites --ref ... --cand ... --out ...`: G2/G3 comparator. Reports `status`, `mismatch_count`, and `mismatches`; missing logs/frames are not passes.

`Makefile`, `bootstrap_toolchain.sh`, `check_g1_encoder.sh`, and `run_g2_case.sh` honor `DEVKITPRO` (default `/opt/devkitpro`). `run_g2_case.sh` invokes its child scripts through `bash`. The `run_suite_gba.sh` child-execution bug above is the exception.

---

## 9. Re-verification sequence

Tested on 2026-10-10 against `main` at `c79fd20`. Use unique G2 labels: `run_g2_case.sh` deletes the matching output directory.

```bash
set -euo pipefail

# A. Checkout (skip clone only if this is already the intended Git repository)
REPO=/home/user/NooDS-Wii
if [ ! -d "$REPO/.git" ]; then
  [ ! -e "$REPO" ] || { echo "Refusing to clone over existing non-repo path: $REPO" >&2; exit 1; }
  git clone https://github.com/radicalten/Actions-Minutes2.git "$REPO"
fi
git -C "$REPO" status --short --branch
PROJ="$REPO/NooDS-Wii"
cd "$PROJ"
git -C "$REPO" rev-parse HEAD

# B. Host packages and toolchain (§2)
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
sudo bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# C. ROM hashes (§2)
sha256sum nds/rockwrestler.nds gba/suite.gba

# D. G0 builds (§4) and digests (§5)
set -euo pipefail
for cfg in '0 0' '1 0' '1 1'; do
  read -r JIT BLOCKS <<< "$cfg"
  make clean JIT="$JIT" BLOCKS="$BLOCKS"
  make -j2 JIT="$JIT" BLOCKS="$BLOCKS"
done
sha256sum NooDS-Wii_jit*.dol

# E. G1
mkdir -p evidence/reverify_2026-10-10
bash tools/check_g1_encoder.sh > evidence/reverify_2026-10-10/g1.log 2>&1
tail -4 evidence/reverify_2026-10-10/g1.log  # expect 52/52 and STATUS=PASS

# F. G2 helper: require runner PASS/sentinel as well as comparator PASS.
TAG="recheck_$(date +%Y%m%d_%H%M%S)_$$"
run_g2() {
  local cfg="$1" label="$2" rom="$3" points="$4" frames="$5" secs="$6" pool="$7"
  bash tools/run_g2_case.sh "NooDS-Wii_${cfg}.dol" "$label" "$rom" "$points" "$frames" "$secs" "$pool"
  local result="evidence/g2_${label}/result.txt"
  grep -q '^STATUS=PASS$' "$result"
  grep -q '^SENTINEL_HIT=1$' "$result"
}
compare_g2() {
  local ref="$1" cand="$2" out="$3"
  python3 tools/compare_runs.py --mode checkpoints \
    --ref "evidence/g2_${ref}" --cand "evidence/g2_${cand}" \
    --out "evidence/${out}.json"
}

# Rockwrestler: normal and 1,024-word wrap
run_g2 jit0_blocks0 "${TAG}_rw_ref" 'sd:/nds/rockwrestler.nds' '15,30,45' 45 90 0
for cfg in jit1_blocks0 jit1_blocks1; do
  run_g2 "$cfg" "${TAG}_rw_${cfg}" 'sd:/nds/rockwrestler.nds' '15,30,45' 45 90 0
  compare_g2 "${TAG}_rw_ref" "${TAG}_rw_${cfg}" "g2_compare_${TAG}_rw_${cfg}"
done
run_g2 jit0_blocks0 "${TAG}_rw_ref_wrap" 'sd:/nds/rockwrestler.nds' '15,30,45' 45 90 1024
for cfg in jit1_blocks0 jit1_blocks1; do
  run_g2 "$cfg" "${TAG}_rw_${cfg}_wrap" 'sd:/nds/rockwrestler.nds' '15,30,45' 45 90 1024
  compare_g2 "${TAG}_rw_ref_wrap" "${TAG}_rw_${cfg}_wrap" "g2_compare_${TAG}_rw_${cfg}_wrap"
done

# GBA suite: normal and 1,024-word wrap
run_g2 jit0_blocks0 "${TAG}_gba_ref" 'sd:/gba/suite.gba' '50,60,120' 120 180 0
for cfg in jit1_blocks0 jit1_blocks1; do
  run_g2 "$cfg" "${TAG}_gba_${cfg}" 'sd:/gba/suite.gba' '50,60,120' 120 180 0
  compare_g2 "${TAG}_gba_ref" "${TAG}_gba_${cfg}" "g2_compare_${TAG}_gba_${cfg}"
done
run_g2 jit0_blocks0 "${TAG}_gba_ref_wrap" 'sd:/gba/suite.gba' '50,60,120' 120 180 1024
for cfg in jit1_blocks0 jit1_blocks1; do
  run_g2 "$cfg" "${TAG}_gba_${cfg}_wrap" 'sd:/gba/suite.gba' '50,60,120' 120 180 1024
  compare_g2 "${TAG}_gba_ref_wrap" "${TAG}_gba_${cfg}_wrap" "g2_compare_${TAG}_gba_${cfg}_wrap"
done

# G. G3 mGBA suite (three variants)
# Current run_suite_gba.sh needs executable children; trap restores the tracked 644 modes.
OUT="$PWD/evidence/g3_${TAG}"
mkdir -p "$OUT"
trap 'chmod 644 tools/build_sd_image.sh tools/run_dolphin.sh' EXIT
chmod +x tools/build_sd_image.sh tools/run_dolphin.sh
for cfg in jit0_blocks0 jit1_blocks0 jit1_blocks1; do
  bash tools/run_suite_gba.sh "NooDS-Wii_${cfg}.dol" "$cfg" "$OUT" 300
  grep -q '^STATUS=PASS$' "$OUT/dolphin_${cfg}/result.txt"
done
python3 tools/compare_runs.py --mode suites \
  --ref "$OUT/mgba_summary_jit0_blocks0.json" \
  --cand "$OUT/mgba_summary_jit1_blocks0.json" \
  --out "evidence/g3_compare_${TAG}_jit1_blocks0.json"
python3 tools/compare_runs.py --mode suites \
  --ref "$OUT/mgba_summary_jit0_blocks0.json" \
  --cand "$OUT/mgba_summary_jit1_blocks1.json" \
  --out "evidence/g3_compare_${TAG}_jit1_blocks1.json"
sha256sum "$OUT"/gbaout_*.log  # hashes must all be identical
chmod 644 tools/build_sd_image.sh tools/run_dolphin.sh
trap - EXIT

git status --short --branch  # review generated/untracked evidence; do not commit/push unless asked
```

The build/hash check for upstream base commit is a separate one-time verification: clone `https://github.com/radicalten/NooDS-Wii.git` at `main` (currently `1c995b48…`), run `make -j2` from its root, then check `NooDS-Wii.dol` size/hash in §3. Do not confuse this upstream tree with the Actions-Minutes2 checkout.

---

## 10. Open issues (for the next agent)

1. **Old digests are stale.** Use §5, not the older `deliverables/*.sha256` values.
2. **Committed evidence is still sparse.** The G0/G1/G2/G3 outputs from this audit are local/untracked in the verification clone. Decide which evidence to commit only if asked; G4–G6 and in-situ G1 evidence are still absent/carried.
3. **Rebuild G4/G5/G6 timing/counters** on the current source before quoting any performance number. The interpreter remains the correctness oracle; do not claim a JIT speedup from one-shot suite wall times.
4. **Fix the G3 wrapper bug in source.** `run_suite_gba.sh` should invoke `build_sd_image.sh` and `run_dolphin.sh` via `bash`; a clean checkout currently exits 126. No source-tool change was made in this audit.
5. **Decide on ROM tracking.** The ROMs are tracked in Git; prior docs said they were ignored. Update policy/docs deliberately.
6. **Branch `feature/arm-ppc-jit` is absent** from the remote. Either create it when explicitly requested or remove references.
7. **AGENTS/Handoff tracking:** current `main` deletes both files. This updated copy is attached/local only. Decide separately whether to restore an AGENTS file to the repository; no commit was made here.
8. **Poll-skip code** stays off until its invalidation is redesigned and re-verified. The old `NOODS_JIT_IDLE_SKIP` flag is absent from current source; do not document it as an available toggle.

---

## 11. Session log — 2026-10-10 re-verification

- Cloned/fetched `Actions-Minutes2@main`; current HEAD `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`. Full history check confirmed `1c995b4` is not in this repository; current HEAD deletes `AGENTS.md`, and `Handoff.md` was deleted in the prior commit.
- Installed documented host packages. `sudo bash tools/bootstrap_toolchain.sh` returned `STATUS=PASS`; toolchain versions and link smoke test match §2.
- Built upstream base `1c995b48…`: 1,317,888 B, SHA-256 `cd48c6a4…`, matching the prior report.
- Built all three current JIT variants: exit 0; hashes match §5. Clean rebuild of `JIT=0 BLOCKS=0` was byte-identical.
- Ran G1 from `/tmp`: 52/52 PASS.
- Verified ROM hashes and vbagx pin.
- G2: interpreter reference plus both JIT variants, both ROMs, normal and pool-wrap 1,024 runs; all eight candidate comparisons PASS with zero mismatches at the specified checkpoints.
- G3: exact clean-checkout `bash tools/run_suite_gba.sh ...` invocation failed with exit 126 because its child scripts are not executable. Temporarily enabled execute bits on the two child scripts, ran all three variants to `ALL DONE`, confirmed identical logs and zero comparison mismatches, then restored both modes to `100644`.
- G4/G5/G6, in-situ MEM2 disassembly, and detailed architecture claims were not re-measured; kept CARRIED.
- Generated evidence/logs are under `evidence/reverify_2026-10-10/`, `evidence/g2_audit26_*`, and `evidence/g3_audit26/`. No tracked source/mode changes, no commits, and no push. Build/evidence outputs remain untracked in the local clone.
