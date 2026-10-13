# AGENTS.md — NooDS-Wii: Wii PowerPC 750CL (Broadway) Dynamic Recompiler for ARM7/ARM9 (ARM + Thumb)

## 0. Mission & scope

This project builds and improves a dynamic recompiler ("JIT") inside **NooDS-Wii** that translates
Nintendo DS/GBA guest CPU code — **ARM7 (ARMv4T)** and **ARM9 (ARMv5TE)**, in **both ARM and Thumb**
instruction sets — into native **PowerPC 750CL ("Broadway")** code, executed on the Wii.

The work has two layers, both covered by this document:

- **Process/infrastructure layer** (§§1–6, 11–14): how to check out, build, and verify the project;
  what is actually proven vs. merely claimed; tooling and reproduction steps.
- **Technical improvement layer** (§§7–10): the JIT's internal conventions and invariants, and a
  concrete, ordered backlog of correctness fixes and performance work in
  `arm_jit.cpp` / `arm_jit.h` / `jit_bridge.S`.

This file consolidates two prior agent guides (an infrastructure/verification guide re-verified on
**2026-10-10** against `Actions-Minutes2` `main` at `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`, and a
JIT-internals task guide). Where the two disagreed on file-path conventions they have been
reconciled (see §2). Status markers (`VERIFIED` / `CARRIED` / `NOT_RUN`) are preserved from the
infrastructure audit; the technical task descriptions are preserved from the internals guide but are
explicitly flagged as **not re-audited line-by-line** against current source — see §7.5.

---

## 1. Agent directives (read first)

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs
  with `flock` (`tools/run_dolphin.sh` does this).
- **Conserve context.** Inspect targeted line ranges. Redirect logs to disk and inspect with
  `grep`/`tail`/`sed`. Never `cat` a Dolphin log (it can contain NUL bytes and ALSA noise;
  `run_dolphin.sh` strips them).
- **Report honestly.** Use `PASS`, `FAIL`, `HANG`, `NOT_RUN`, or `CARRIED` with exact evidence
  (commit, hash, counts, exit code, `END: X/Y`). Missing files or captures are `NOT_RUN`, never zero
  mismatches.
- **Do not overstate performance.** Earlier timing runs found the interpreter faster than both JIT
  variants. G4–G6 were not re-measured in the last audit; do not make current speed claims without
  repeating those measurements, and do not claim a JIT speedup from one-shot wall-clock runs.
- **Keep evidence in the repository** under `evidence/`. The Makefile writes gate DOLs in `PROJ`;
  copy selected final DOLs to `deliverables/` only when packaging deliverables. Do not imply a normal
  build populates that (currently absent) directory, and do not fabricate output.
- **Do not destroy work.** Run `git status` before any switch or reset. No `rm -rf` on a checkout or
  reference tree unless the user asks. `run_g2_case.sh` itself deletes and recreates its
  `evidence/g2_<LABEL>/` directory; use a new label each run.
- **Target Wii only** (`-mrvl`). The `vbagx` clone is read-only study material. Never build, link, or
  include it.
- **Preserve the interpreter** as the correctness oracle and fallback — the design must always fail
  toward correctness (see §8).
- **Do not commit or push** unless the user asks.
- **One task per commit** when doing JIT improvement work (§9). Correctness fixes (T1–T2) must not
  be bundled with performance work — each needs its own regression pass.
- **Never widen a JIT classifier without adding a matching, correct emitter**, and **never inline
  raw guest-memory pointer reads/writes** — always go through the guarded bus helpers (see §8).

---

## 2. Repository layout and path conventions

The git repository and the project are nested. Every path below is relative to one of these roots:

| Name | Path (in this sandbox) | Contents |
|---|---|---|
| `REPO` | `/home/user/NooDS-Wii` | Git root: `https://github.com/radicalten/Actions-Minutes2.git`, branch `main`. |
| `PROJ` | `$REPO/NooDS-Wii` | `Makefile`, `tools/`, `evidence/`, `nds/`, `gba/`, `README.md`, `LICENSE`. Run project builds/tools from here. |
| `SRC` | `$PROJ/NooDS-Wii` (i.e. `REPO/NooDS-Wii/NooDS-Wii`) | Emulator and JIT sources: `arm_jit.h`, `arm_jit.cpp`, `jit_bridge.S`, `memory.cpp`, `interpreter*.cpp`, `main.cpp`, `defines.h`, … |

> **Path reconciliation:** an earlier version of the internals guide referred to JIT sources as
> `NooDS-Wii/NooDS-Wii/arm_jit.h` etc. (relative to `REPO`). That is identical to `SRC/arm_jit.h`.
> This document uses `SRC/<file>` consistently; translate as `REPO/NooDS-Wii/NooDS-Wii/<file>` if
> needed.

Corrections against older docs (re-verified 2026-10-10, HEAD `c79fd20`):

- At current HEAD the remote advertises only `main`; `feature/arm-ppc-jit` does not exist on the
  remote. Create it only if explicitly requested, or stop referencing it.
- Current `main` HEAD `c79fd20` (`Delete AGENTS.md`) follows `f13c0f4` (`Delete Handoff.md`). Neither
  file is tracked at current HEAD — this document is therefore a **local/attached** copy, not
  tracked in a fresh checkout, unless an agent is explicitly asked to restore one.
- Base commit `1c995b4` is **not in the Actions-Minutes2 history**. It is the current HEAD of
  upstream `https://github.com/radicalten/NooDS-Wii.git` `main`; its build was reproduced separately
  (§5).
- ROMs are tracked in Git (`nds/rockwrestler.nds`, `gba/suite.gba`); there is no `.gitignore` in the
  current project tree. (An older doc said ROMs were ignored — that is stale; see Open Issue §14.5.)
- All `tools/*.sh` and `tools/*.py` are tracked as mode `100644`. Invoke scripts with `bash` (or
  `python3` for Python tools). **Exception:** `run_suite_gba.sh` directly executes two child
  scripts, so `bash tools/run_suite_gba.sh` alone fails on a clean checkout — see §12.6.

### Artifacts not committed in current `main`

`deliverables/`, `WORKSPACE_MANIFEST.md`, `local_changes_2026-10-10.patch`, and most earlier
`evidence/` files (G0 build logs/hashes, in-situ G1 disassembly, G2/G3 summaries, G4–G6 timing
summaries, poll-skip notes, prior review reports) are **not tracked** at `c79fd20`.

Tracked evidence present at HEAD: `vbagx_ref_commit.txt`, `vbagx_jit_files.txt`,
`vbagx_jit_grep.txt`, `vbagx_jit_notes.md`, `vbagx_arch_map.md`, `toolchain_bootstrap_result.txt`.

Fresh, **untracked** captures from the 2026-10-10 audit live under `evidence/` (see
`evidence/reverify_2026-10-10.md` and §13). They are local evidence, not committed history. A result
tied to an absent old artifact remains `CARRIED` unless re-generated.

---

## 3. Environment

### Sandbox facts (as of 2026-10-10)

- Debian 13.7 x86_64, user `user` with passwordless `sudo`, ~2 cores, ~1.9 GiB RAM, ~21 GiB free at
  start. `apt-get` works.
- Files outside `/home/user` do not persist across workspace snapshots (includes
  `/opt/devkitpro` and apt-installed packages — re-run bootstrap each fresh session).

### Host dependencies

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"
```

Verified installed versions: `dolphin-emu 2503+dfsg-1+deb13u1`, `binutils-arm-none-eabi 2.44-3+23+b1`,
`python3-pyelftools 0.32-1`, `mtools 4.0.48-1`, `dosfstools 4.2-1.2`. `mkfs.vfat` is
`/usr/sbin/mkfs.vfat`.

**Python caveat:** `python3` resolves to `/usr/local/bin/python3`, which does **not** import
Debian's `elftools`; `/usr/bin/python3` does. Current G0–G3 tools don't import `elftools`. Use
`/usr/bin/python3` for any future step that needs apt's pyelftools.

### devkitPPC / libogc toolchain

```bash
cd "$PROJ"
sudo bash tools/bootstrap_toolchain.sh        # tracked mode 100644; invoke through bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
```

- SHA-256-pins devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4,
  devkitppc-rules 1.2.1, devkitppc-crtls 2.1.0-1, libogc 3.1.0, libfat-ogc 2.1.0-4,
  gamecube-tools 1.0.7 (`elf2dol`), general-tools 1.4.4, from
  `https://wii.leseratte10.de/devkitPro/`.
- Ends with a Wii link smoke test and writes `evidence/toolchain_bootstrap_result.txt`.
- **Verified 2026-10-10: `STATUS=PASS`**, GCC `16.1.0`, AS `2.46.0.20260210`, `elf2dol` present.

### ROM test assets (git-tracked in `main`)

| File | Size | SHA-256 |
|---|---:|---|
| `nds/rockwrestler.nds` | 39,433 B | `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` |
| `gba/suite.gba` | 524,288 B | `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` |

**Verified 2026-10-10:** both hashes match. If assets are missing:

```bash
cd "$PROJ" && mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ]        || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
```

Always compare against the hashes above; a successful download is not verified content.

---

## 4. Verification status (process layer, as of 2026-10-10)

Status key: **VERIFIED** = re-run against stated checkout in the last audit. **CARRIED** = claimed
earlier, not re-measured here. **NOT_RUN** = not executed.

| Gate | What it checks | Status | Result / evidence |
|---|---|---|---|
| Repo checkout | `Actions-Minutes2` `main` + nested project | **VERIFIED** | HEAD `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`; only remote branch `main`. |
| Base build | Upstream `1c995b4` (`make -j2`) | **VERIFIED** | `NooDS-Wii.dol`: 1,317,888 B, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`. |
| vbagx pin (G-1) | Pinned reference commit exists | **VERIFIED (pin only)** | Fetch resolved exactly to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`; content not re-audited. Do not build it. |
| Toolchain | devkitPPC bootstrap + Wii link smoke test | **VERIFIED** | `STATUS=PASS`; see §3. |
| ROMs | Test asset hashes | **VERIFIED** | Both match §3. |
| **G0** Builds | Three JIT configs compile | **VERIFIED** | All exit 0; hashes in §6. Clean rebuild of `JIT=0 BLOCKS=0` reproduced identical bytes. |
| **G1** Encoder oracle | 52 PowerPC 750CL forms vs `powerpc-eabi-as`/`objdump` | **VERIFIED** | `FORMS_MATCHED=52/52`, `REQUIRED_MINIMUM=44/44`, `STATUS=PASS` (run from `/tmp`). In-situ MEM2 disassembly remains **CARRIED**. |
| **G2** Differential, `rockwrestler.nds` | Frames 15/30/45; normal and `POOL_LIMIT_WORDS=1024` | **VERIFIED** | Both JIT variants, normal + wrap: all 4 comparisons `PASS`, `mismatch_count=0`. |
| **G2** Differential, `suite.gba` | Frames 50/60/120; normal and wrap | **VERIFIED** | Both JIT variants, normal + wrap: all 4 comparisons `PASS`, `mismatch_count=0`. |
| **G3** mGBA auto-suite | `gba/suite.gba` → `ALL DONE`, three variants | **PASS WITH LOCAL MODE WORKAROUND** | All 3 runs reached `ALL DONE`; 13 suites, 1 SKIP, 6,998 tests (3,506 passed / 3,492 failed per the suite itself). All `gbaout.log` SHA-256 identical; both comparisons `PASS`, zero mismatches. **Clean-checkout wrapper fails exit 126 before the workaround — see §12.6.** |
| **G4** Phase 1 timing | Phase 1 vs interpreter | **CARRIED** | Earlier: Phase 1 slower than interpreter. Not re-measured. |
| **G5** Phase 2 ARM7 timing | Phase 2 vs Phase 1 / interpreter | **CARRIED** | Earlier: ~1.5–1.7× faster than Phase 1, still slower than interpreter. Not re-measured. |
| **G6** ARM9 blocks | ARMv5TE native blocks + CP15 exits | **CARRIED** | Earlier counters: `blkComp9=80`, `blkHit9=5,011,952`, `blkInsn9=14,955,280`, `cp15Exit9=21`, 0 mismatches. Not re-run. |

### Interpretation notes

- Each G2 `result.txt` reports `STATUS=PASS` + `SENTINEL_HIT=1`; `RC=143` is expected (runner kills
  Dolphin after the sentinel). Do not use `RC` alone as pass/fail.
- G2/G3 used `NOODS_JIT_POLL_SKIP=0` (default); these parity passes do **not** verify the poll-skip
  optimization. `NOODS_JIT_IDLE_SKIP` is **not present** in current source despite earlier docs.
- `run_g2_case.sh` can exit 0 once dump files exist even without a passing Dolphin result. Always
  check the runner `result.txt`, then the comparator JSON (`status=PASS`, `mismatch_count=0`).
- G3's suite itself reports 3,492 failing test cases per variant — the verified result is *matching*
  suite output and successful completion, **not** full mGBA compatibility.
- One-shot G3 elapsed times (interpreter 13 s, `jit1_blocks0` 39 s, `jit1_blocks1` 19 s) are **not**
  controlled timing and are not a performance claim.
- Historical frame-45 `accum_us` (not re-measured): interpreter 4,606,860; baseline JIT 5,569,304;
  fast path 4,954,587; final default 4,914,235. Keep **CARRIED**.

---

## 5. Build commands (verified on current `main`)

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
# Outputs: NooDS-Wii_jit0_blocks0.dol, NooDS-Wii_jit1_blocks0.dol, NooDS-Wii_jit1_blocks1.dol
sha256sum NooDS-Wii_jit*.dol
```

Each build also writes generic `NooDS-Wii.dol` / `.elf` / `.elf.map` copies that the next
configuration overwrites; config-tagged DOLs persist. Check `git status` before cleaning unrelated
output.

**Compile hygiene for any JIT source change:** zero warnings. The JIT uses
`#pragma GCC diagnostic ignored "-Winvalid-offsetof"` deliberately, so any warning that does slip
through is real signal and must be investigated, not suppressed.

---

## 6. Digests (current source at `c79fd20`, 2026-10-10)

| Config | Output | Size (B) | SHA-256 | Reproducible? |
|---|---|---:|---|---|
| Base upstream `1c995b4` (no JIT) | `NooDS-Wii.dol` | 1,317,888 | `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c` | Yes |
| `JIT=0 BLOCKS=0` (interpreter) | `NooDS-Wii_jit0_blocks0.dol` | 1,338,816 | `99e6b49275d40f8a8af55298992e15a5a986d8e055ddd9d856f549db1a9b3873` | Yes, identical after clean rebuild |
| `JIT=1 BLOCKS=0` (Phase 1) | `NooDS-Wii_jit1_blocks0.dol` | 1,336,992 | `bb0853bd2a865b53324200a479071a29bf55b19ee1dc9a3da5f69fa94ac7d70d` | Built once |
| `JIT=1 BLOCKS=1` (Phase 2) | `NooDS-Wii_jit1_blocks1.dol` | 1,364,064 | `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e` | Built once |

Older DOL digests (`12f073…`, `6d09df…`, `c69f71…`) are superseded and **not reproducible** from
current source — do not cite them. Any JIT source change invalidates this whole table; re-hash after
every task in §9.

---

## 7. Architecture

This is an implementation inventory. The *build and G2/G3 parity gates* were re-run against
`c79fd20` (§4); the *detailed internals* below (conventions, file roles, exact line positions) came
from an independent source-reading pass and were **not** re-audited line-by-line in the same
session — treat exact bucket names, line numbers, and encoding constants as needing a quick
re-check against current `SRC/arm_jit.{h,cpp}` before relying on them (see §7.5 / §14.7).

### 7.1 Goal

A Wii PowerPC 750CL (Broadway) dynamic recompiler inside NooDS-Wii for **both ARM7 (ARMv4T) and
ARM9 (ARMv5TE)**, covering **both ARM and Thumb**. The interpreter (`JIT=0 BLOCKS=0`) stays the
correctness oracle and fallback — the design **fails toward correctness**: anything unclassifiable
drops to the interpreter.

### 7.2 Phase 1 — 1:1 stub JIT (`-DNOODS_JIT=1 -DNOODS_JIT_BLOCKS=0`)

- One eligible guest dispatch maps to one cached native stub in MEM2 (`stubTable[cpuIdx][8192]`).
- The stub calls the resolved `Interpreter` member handler (`resolveArmHandler` /
  `resolveThumbHandler`) via `bctrl`, with `(Interpreter *cpu, uint32_t opcode)` — opcode already
  fetched by `Interpreter::runOpcode`. Returns the exact cycle cost through `jit_exit`.
- Does not refetch, double-advance `PC`, refill the pipeline twice, skip condition handling, or
  change scheduler timing.
- Condition-false (`case 0`) and reserved-condition (`case 2`, `handleReserved()`) opcodes stay on
  the interpreter.

### 7.3 Phase 2 — native emitters and multi-instruction blocks (`-DNOODS_JIT_BLOCKS=1`)

- **Native emitters** (`emitNativeArm`, `emitNativeThumb`) translate ALU ops, immediate shifts
  (`LSL`/`LSR`/`ASR`/`ROR`, shifter carry-out on logical `S==1`), `MOV`/`MVN`, `NEG`,
  `CMP`/`CMN`/`TST`/`TEQ`, `ADD`/`SUB`/`RSB`, non-flag `ADC`/`SBC`/`RSC`, `AND`/`EOR`/`ORR`/`BIC`,
  ARM9 `CLZ` (prior to T4 in §9, if not yet applied), PC/SP-relative adds, Thumb `BL` setup. Output
  is Broadway machine words in MEM2.
- **Flags:** exact NZCV in `Interpreter::cpsr` bits 31..28. `N` from result bit 31, `Z` from
  `cntlzw`, `C` from XER CA after `addc`/`subfc`, `V` from signed-overflow expressions.
- **Block compiler** (`compileBlock`, `tryExecuteBlock`): up to `MAX_BLOCK_INSTRS = 24` contiguous
  instructions within one 4 KB page. May start with a single-register load (`firstIsLoad`, via
  `bctrl`). May end with `B`, `BL`, `BX`, `BLX`, or `Bcc`, including 2-instruction
  `[Load, Branch]` blocks.
- **Optimizations:** backward CPSR flag-liveness pass (`thumbWrittenFlagsMask`,
  `armWrittenFlagsMask`, `armReadsCarry`). Branch-terminated blocks use a tail-call epilogue
  (`execBranchArm` / `execBranchThumb`) skipping dead `pcData`/`pipeline` updates, with a same-4
  KB-page fast path.
- **Safety:** blocks never cross `core->events[0].cycles`. ARM9 CP15 (`MCR`/`MRC`) exits to the
  interpreter (`cp15Exit9`) — this is **Gate G6**, referenced again in §8.3.
- **Invalidation:** `alignas(32) BlockEntry` (one Broadway cache line). A 1 KB
  `pageHasBlockBits` bitmap filters writes on non-code pages. `tailHash` verification on page-epoch
  changes stops false SMC recompiles from stack/data writes sharing an IWRAM page (`0x03000000`).
  Code pool lives in MEM2, 1 MiB (`DEFAULT_POOL_BYTES`), wraps via `wrapPool()` which bumps
  `currentEpoch` (30-bit); stale entries are rejected by epoch comparison on lookup.

### 7.4 Key files (paths relative to `SRC`)

| File | Role |
|---|---|
| `arm_jit.h` | `namespace JitPpc` (52-form PPC encoder helpers); `class ArmPpcJit` with `stubTable[2][8192]`, `blockTable[2][8192]`, `pageEpoch[2][4096]`, `pageHasBlockBits[2][128]`, `setPoolLimitWords`, invalidation API, `dumpStats`/`Stats`. `DEFAULT_POOL_BYTES = 1 MiB`, `MAX_BLOCK_INSTRS = 24`, `NOODS_JIT_POLL_SKIP` default `0`. |
| `arm_jit.cpp` | Classification, emitters, block compiler, dispatch, SMC invalidation. MEM2 code pool via `Noods_MEM2_Alloc`; `flushCodeRange` runs `dcbst; sync; icbi; sync; isync` on emitted stubs/blocks. Re-check free MEM2 headroom before changing allocation sizes. |
| `jit_bridge.S` | `jit_enter()`/`jit_exit`. 32-byte frame, saves `LR` at `36(r1)` and `r28..r31` at `16(r1)..28(r1)`, holds `Interpreter *cpu` in `r31`, dispatches via `mtctr r3; bctr`. Owns the frame; generated code runs under it. |
| `interpreter.h` / `interpreter.cpp` | Hooks into `init`, `loadState`, `directBoot`, `runOpcode()`. Also the **authoritative reference for semantics** — when in doubt about an instruction, read the real handler here (in `interpreter_alu.cpp` / `interpreter_transfer.cpp` / `interpreter_branch.cpp`) and copy it verbatim rather than reasoning from the ARM ARM from memory. |
| `memory.h` / `memory.cpp` / `dldi.cpp` | `invalidateSharedAddr` in `Memory::write<T>`/`writeFallback<T>` (includes VRAM); `invalidateCpu` in `updateMap9`/`updateMap7`; `invalidateAll` in `Dldi::patchRom`. `memory.cpp` (~line 630) has an explicit ARM9 halfword-read fast path for DISPSTAT (`0x04000004`) only — VCOUNT and IPCSYNC go through normal I/O dispatch. Separately, `arm_jit.cpp::armLoadAddrOk` whitelists DISPSTAT/VCOUNT/IPCSYNC for ARM load classification (not a `memory.cpp` fast path). |
| `main.cpp` | `NoodsTestHarness` reads `sd:/autoboot.txt` (`MAX_FRAMES`, `CHECKPOINTS`, `POOL_LIMIT_WORDS`); writes per-frame timebase profiling (`PPCGetTickCount`/`PPCTicksToUs`) and deterministic dumps (`sd:/g2_state.log`, `sd:/mem_dump.bin`, `sd:/fb_dump.bin`); calls `fatUnmount("sd"); fatInitDefault();` before the sentinel so FAT caches flush. |
| `defines.h` | `U8TO32`/`U8TO16` little-endian guest memory reads. |

### 7.5 JIT register conventions and PPC toolchain constraints (do not break)

These are the *durable* facts about how generated code must look, independent of exact line
numbers:

- **Target toolchain:** devkitPPC (`powerpc-eabi-`), `-mcpu=750` family, GEKKO/Broadway. Code
  emitted at **runtime** must be valid 750CL: no 64-bit integer ops, no `isel` (that's e500/Book-E),
  no AltiVec.
- **Host ABI:** PPC32 SysV. Volatile `r0-r12`, non-volatile `r13-r31`.
- **The JIT's own register convention inside `arm_jit.cpp` (do not reassign these roles):**
  - `R31` = pointer to the guest `Interpreter` object (base for all `lwz`/`stw` of guest state).
  - `R3..R5` = guest operands/result for emitters; `R6` = scratch CPSR; `R7`/`R8`/`R9` = scratch.
  - `R11` = scratch pointer for banked (`registers[i]`) guest-register indirection.
  - `R12` = helper address for `mtctr`/`bctrl` calls.
  - `R28..R30`, `R1` = saved/restore block, owned by `jit_bridge.S`'s frame.
- **Broadway has incoherent split I/D caches.** Any newly emitted code must be flushed via
  `ArmPpcJit::flushCodeRange()` before execution. `compileBlock()`/`compileStub()` already do this —
  **any new compile path you add must call it too**, or you get intermittent garbage execution on
  real hardware that Dolphin may not reproduce (this is why §4's "Hardware" validation step exists).
- Build flags: `NOODS_JIT` (enable), `NOODS_JIT_BLOCKS` (Phase-2 block compiler),
  `NOODS_JIT_POLL_SKIP` (fixed-point poll-loop skip, default off — see §7.6), `NOODS_JIT_PROFILE`
  (timebase profiling buckets).

### 7.6 Runtime flags

| Flag | Default | Status |
|---|---|---|
| `NOODS_JIT` / `NOODS_JIT_BLOCKS` | per Makefile (`JIT`, `BLOCKS`) | Build config. `JIT=0` is the oracle. |
| `NOODS_JIT_IDLE_SKIP` | N/A | **Not referenced in current `main` source** (no implementation or macro use found). Earlier notes reported a net loss; do not document as an available toggle. |
| `NOODS_JIT_POLL_SKIP` | `0` | Optional joint ARM9/ARM7 fixed-point poll skip with a global `pollEpoch`. Not enabled in the last audit; never fired on rockwrestler (`skips=0`) and was ~1.7× slower historically. **Leave off.** Redesign invalidation (address-aware, not global) before reconsidering. Experimental build: `CXXFLAGS=-DNOODS_JIT_POLL_SKIP=1 make JIT=1 BLOCKS=1` (historical, not re-tested). |
| `NOODS_JIT_PROFILE` | Undefined/0 unless supplied | Optional dispatch profiling in `arm_jit.cpp`; `dumpStats` can print `JIT_PROF` buckets. Not enabled in the last audit; use it for the T7 experiment in §9. |
| ARM9 DISPSTAT halfword-read fast path (`memory.cpp`) | ON | Explicit early return for aligned halfword reads at `0x04000004`; G2/G3 parity ran with it active but did not isolate its effect. VCOUNT/IPCSYNC are not this fast path. |

---

## 8. Hard invariants — do NOT change these

Carried from the internals guide; cross-referenced against the gates in §4/§11 where applicable.
The design **fails toward correctness**: anything unclassifiable falls through to the interpreter.
**Do not weaken a classifier to widen coverage — only widen it by adding a correct emitter.**

1. `tryExecuteBlock()` must keep refusing to enter a cached block whose `prefixCycles` would cross
   `core->events[0].cycles`. Breaking this lets a block skip a timer/DMA/IRQ boundary. (See §7.3
   "Safety".)
2. Keep `bit 27 == 1` (LDM/STM, SWI, B/BL, coprocessor/CP15) and `Rd == 15` instructions out of
   block compilation, as today, unless you implement them fully and correctly.
3. Keep ARM9 CP15 ops gated out of blocks — this is the existing **Gate G6** (§4, §7.3).
4. Page-epoch SMC invalidation (`invalidateAddr` / `invalidateRange` / `invalidateSharedAddr`) must
   still fire on every guest write path the JIT doesn't own.
5. Memory accesses must go through the same guarded bus helpers the interpreter uses. **Never
   inline raw pointer reads/writes** — that breaks MMIO and VRAM mirroring.
6. In ARM mode, non-initial instructions in a block must be unconditional (`cond == 0xE`) — unless
   you also implement full condition evaluation for that instruction.

---

## 9. Improvement backlog (ordered)

Work these in order. **T1–T2 are correctness fixes; T3–T7 are additive performance work.** Each is
its own commit with its own regression pass (§10). Function names/line positions/bucket indices
below were read from the source at one point in time and **must be re-verified against current
`SRC/arm_jit.{h,cpp}` before being relied on** — the task *descriptions* and the conventions in §7.5
are the durable part.

### T1 — BUG: `BLX Rm` (register form) never writes LR · ARM9 only · medium impact, low risk

**Where:** `ArmPpcJit::execBranchArm()`, final "BX Rn" tail case.
**Problem:** reached by both `BX` (`0x012FFF1x`) and `BLX` (`0x012FFF3x`, bit 5 set), but only ever
performs BX semantics — LR is silently dropped.
**Fix:** detect bit 5 and write LR first, gated on `!cpu->arm7`. Mirror the already-correct Thumb
`BLX Rs` handling in `execBranchThumb()`.

```cpp
// replacement for the existing "BX Rn" tail in execBranchArm()
const bool isBlxReg = !cpu->arm7 && (opcode & (1u << 5)) != 0;
cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
uint32_t target = *cpu->registers[opcode & 0xFu];
if (isBlxReg) *cpu->registers[14] = cpu->registersUsr[15] - 4u;
cpu->cpsr = (cpu->cpsr & ~(1u << 5)) | ((target & 1u) << 5);
cpu->registersUsr[15] = target;
cpu->flushPipeline();
return prefixCycles + 3;
```

Also confirm `isTerminatorBranchArm()` / `isUncondBranchArm()` treat `BLX Rm` as a terminator so a
block can't be built *through* it.

### T2 — BUG: `BLX <label>` (immediate, `cond == 0xF`) is condition-tested · ARM9 only · medium impact, medium risk

**Where:** top of `execBranchArm()` and the block-termination classifiers.
**Problem:** `cond == 0b1111` is not a condition on ARMv5 — it's the unconditional-extension
encoding space. The current `if (cond != 0xEu)` guard routes it through the 16-entry condition LUT
(accidental behaviour), and the existing `0x0A000000` mask test doesn't even match the BLX encoding.
**Fix:** early-out on `cond == 0xF` before any condition lookup.

```cpp
if (cond == 0xFu) {                       // BLX <label> — ARMv5 unconditional extension space
    if (cpu->arm7) {                      // ARMv4T: UNDEFINED encoding, defer to interpreter
        cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
        cpu->flushPipeline();
        return prefixCycles + cpu->handleReserved(opcode);
    }
    uint32_t r15 = oldR15 + (uint32_t)remDelta;
    int32_t  off  = ((int32_t)(opcode << 8) >> 6) | (int32_t)((opcode >> 23) & 2u); // H -> +2
    *cpu->registers[14] = r15 - 4u;
    cpu->cpsr |= (1u << 5);               // force Thumb
    cpu->registersUsr[15] = r15 + (uint32_t)off;
    cpu->flushPipeline();
    return prefixCycles + 3;
}
```

Verify the offset decode above against `interpreter_branch.cpp`'s real BLX handler — the `H` bit
encoding is easy to get wrong. If it differs, copy the interpreter's version verbatim.

### T3 — Native `STR` / `STRB` fast path · both CPUs · **HIGH impact**, low risk ← biggest win

**Problem:** only `STRH` (immediate, pre-index, no writeback) is fast-pathed today
(`isNativeStoreArm`). Ordinary word/byte stores — the most common store in compiler-generated ARM9
code — always fall back to the interpreter.
**Where:** classification next to `isNativeStoreArm()`, emitter next to `emitNativeStoreArm()`,
registered in `canTranslateNativeArm()` + `compileBlock()`'s per-instruction classification loop.
**Pattern:** mirror `isNativeLoadArm()` exactly — pre-indexed, `W=0`, `Rn != PC`, `Rd != PC`,
register offsets restricted to unshifted/`LSL #imm`, no `Rm == PC`, no bit 4 (reg-shift-by-reg).

```cpp
static inline bool isNativeStoreWordByteArm(uint32_t op) {
    if ((op >> 28) != 0xEu) return false;                // AL only (see invariant §8.6)
    if ((op & 0x0C000000u) != 0x04000000u) return false; // single data transfer
    if (op & (1u << 20)) return false;                   // L = 0
    if (!(op & (1u << 24))) return false;                // P = 1
    if (op & (1u << 21)) return false;                   // W = 0
    if (((op >> 16) & 0xFu) == 15u) return false;        // Rn != PC
    if (((op >> 12) & 0xFu) == 15u) return false;        // Rd != PC
    if (op & (1u << 25)) {                               // register offset
        if (op & (1u << 4)) return false;
        if ((op & 0xFu) == 15u) return false;
        if (((op >> 5) & 3u) != 0u) return false;        // LSL only
    }
    return true;
}
```

The emitter must reuse the **same base+offset computation and the same guarded bus-write helper**
`emitNativeLoadArm()` already calls for its slow path. Do not hand-roll the store (invariant §8.5).

### T4 — Native `CLZ` · ARM9 only · low impact, lowest risk ← do this first as a smoke test

`armWrittenFlagsMask()` already special-cases `0x016F0F10` as "no flags written" — classification is
anticipated, only the emitter is missing. PPC `cntlzw` is a 1:1 match.

```cpp
static inline bool isClzArm(uint32_t op) { return (op & 0x0FFF0FF0u) == 0x016F0F10u; }

// in emitNativeArm(), gated on !cpu->arm7
emitLoadGuestReg(p, JitPpc::R4, opcode & 0xFu);
*p++ = JitPpc::cntlzw(JitPpc::R3, JitPpc::R4);
emitStoreGuestReg(p, JitPpc::R3, (uint8_t)((opcode >> 12) & 0xFu));
outCycles = 1;   // CLZ never updates NZCV — nothing further to emit
```

### T5 — Native `MUL` / `MLA` · both CPUs · medium impact, medium risk

`armPurity()` already recognises the multiply encodings but nothing emits them. `mullw` is 1:1. MLA
adds one accumulator `add`. When `S=1` only **N and Z** are set (C is UNPREDICTABLE on real
hardware; the interpreter leaves it) — reuse `emitUpdateNZ()`.

```cpp
static inline bool isMulArm(uint32_t op) {
    if ((op & 0x0FC000F0u) != 0x00000090u) return false;  // bits 27:22 = 0, 7:4 = 1001
    uint8_t rd = (uint8_t)((op >> 16) & 0xFu), rm = (uint8_t)(op & 0xFu);
    return rd != 15u && rm != 15u && rd != rm;            // rd == rm is UNPREDICTABLE
}
// mullw R3, R4(Rm), R5(Rs)  ->  optional add R3, R3, R6(Rn) for MLA
// -> emitStoreGuestReg -> if (emitFlags) emitUpdateNZ(p, R3)
```

**Caution:** multiply **aliases the data-processing encoding space**, so `isMulArm()` must be
checked *before* the generic ALU switch in `emitNativeArm()`.

### T6 — Native register-specified shift amounts (`LSL Rd, Rm, Rs`) · both · medium impact, medium risk

Currently rejected because `canTranslateNativeArm()` bails on bit 4 = 1. PPC `slw`/`srw`/`sraw`
already produce the correct all-zeros/sign-fill result for shift counts ≥ 32, but ARM reads the
count as **bits 7:0 of Rs**, so mask to a byte then clamp to 32 with `cmplwi` + conditional select.
Carry-out needs one extra shift by `(amount - 1)` guarded for `amount == 0` — that guard is the
trickiest part; get it wrong and every `ADC`/`SBC`/`ROR`-through-shift is wrong.

### T7 — Re-measure native halfword loads now that blocks exist · both · low impact, low risk

`kNativeHalfLoads` is `false` with a comment saying it was measured and gave no speedup — but that
measurement predates the Phase-2 block compiler. Flip it on behind a build flag and re-run
`NOODS_JIT_PROFILE` (§7.6). Chained in a block, the per-dispatch overhead it used to compete against
is gone, so the old conclusion may no longer hold.

### Deferred (don't do unless asked)

`UMULL/UMLAL/SMULL/SMLAL` (needs `mulhw[u]` + `mullw` pairs), `QADD/QSUB/QDADD/QDSUB` (branchless
clamp + sticky Q bit, CPSR bit 27), `SMLAxy/SMULxy/SMLAWy` DSP family, native `LDM`/`STM`.

---

## 10. Validation — required after every task in §9

`ArmPpcJit::dumpStats()` and `NOODS_JIT_PROFILE` buckets already exist; use them, and re-run the
build/G0–G3 process from §§4–6 after each change.

1. **Compile:** full Wii build per §5 (`powerpc-eabi-gcc`). Zero warnings (see §5's hygiene note).
2. **Stats must move in the right direction:**
   - `fbInsn9` / `fbInsn7` (fallback instructions) should **drop** for classes just made native.
   - `natInsn9` / `natInsn7` should rise correspondingly.
   - `blkComp*` / `blkHit*` up, `blkFail*` roughly flat.
   - For T3 specifically: `fbClassArm[cpu][2]` (the LDR/STR/halfword bucket) and the `fbOpc7`
     histogram entries for store opcodes should fall sharply.
   - *(Re-verify these exact bucket/field names against current `arm_jit.h`'s `Stats` struct before
     relying on them — see §7.5/§14.7.)*
3. **Correctness, beyond the standard G2/G3 ROMs (§4):** several titles per CPU, including at least
   one ARM9 game that uses `BLX` trampolines (mixed ARM/Thumb libraries), one GBA-mode title
   (different code paths in `tryExecuteBlock` — note the `gbaMode`/`dsiMode` guards in
   `pollExecArm`), and one with known self-modifying code or DMA-timed code.
4. **Hardware:** final pass on a real Wii or cycle-accurate Dolphin. Broadway `dcbst`/`icbi`
   ordering and alignment issues routinely hide on non-cycle-accurate hosts (this is why §7.5's
   `flushCodeRange()` rule matters).
5. **SMC regression:** after T3 especially, verify DMA-driven code upload (e.g. ARM7 loading ARM9
   code, or decompression stubs) still invalidates correctly — stores are the write path most
   likely to interact with `pageEpoch` invalidation (invariant §8.4).
6. **Re-hash:** update §6's digest table for the new source state; previous hashes are invalidated
   by any JIT source change.
7. **Re-run the G2/G3 gates from §4/§13** against the new build before considering a task complete;
   update the status table honestly (`PASS`/`FAIL`/`CARRIED`, with evidence paths under `evidence/`).

---

## 11. Gate definitions (reference)

- **G-1**: vbagx reference study pinned to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`. Pin fetch
  verified; study content not re-audited. To make a fresh read-only clone (do not overwrite an
  existing path):
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
- **G0**: all three configs build. Current hashes in §6.
- **G1**: 52/52 encoder forms bit-exact vs GNU `as`/`objdump`. In-situ MEM2 disassembly remains
  CARRIED.
- **G2**: checkpoint parity vs interpreter at frames `15,30,45` (`rockwrestler.nds`) and
  `50,60,120` (`suite.gba`), for both JIT variants. Compare exact cycles, registers, CPSR, memory
  (413,696 B), and framebuffer (393,216 B). Include matching `POOL_LIMIT_WORDS=1024` wrap runs.
- **G3**: mGBA suite reaches `ALL DONE` in all three variants; compare summaries and verify
  identical `gbaout.log` SHA-256 manually. This does **not** mean every upstream test passes.
- **G4**: Phase 1 timing vs interpreter; carried result is slower than interpreter.
- **G5**: Phase 2 ARM7 blocks vs Phase 1; carried result is faster than Phase 1 but slower than
  interpreter.
- **G6**: Phase 2 ARM9 blocks and CP15 exits (also invariant §8.3); counters in §4 are carried.

---

## 12. Tooling (`PROJ/tools/`)

All scripts are tracked mode `100644`. Run shell scripts with `bash` (or `chmod +x` locally, then
restore mode); run Python tools with `python3`.

1. `bootstrap_toolchain.sh`: pinned devkitPro/libogc/crtls install plus a link smoke test. Requires
   `sudo`; run as `sudo bash tools/bootstrap_toolchain.sh`.
2. `build_sd_image.sh <out.img> [--autoboot sd:/...]`: creates a 128 MiB FAT16 SD image with
   `suite.gba`, `rockwrestler.nds`, and optional `autoboot.txt`. Needs `mtools`/`dosfstools`.
3. `run_dolphin.sh DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL]`: `flock`-serialized, timeout-bounded,
   sentinel-aware headless Dolphin runner. Writes `result.txt` and a cleaned `dolphin.log`.
4. `check_g1_encoder.sh` + `test_encoder.cpp`: G1 encoder oracle. Relative-path behavior verified by
   invoking from `/tmp`. Overwrites the fixed `evidence/g1_encoder_oracle.log`; preserve any prior
   untracked log before rerunning if needed.
5. `run_g2_case.sh DOL LABEL ROM CHECKPOINTS MAX_FRAMES TIMEOUT [POOL_LIMIT]`: runs one G2 case into
   `evidence/g2_<LABEL>/`. **Deletes and recreates that directory each run.** Inspect its
   `result.txt` and require `STATUS=PASS`, `SENTINEL_HIT=1` — exit code alone is insufficient.
6. `run_suite_gba.sh DOL LABEL OUT_DIR [TIMEOUT]`: G3 suite runner, **currently buggy on a clean
   checkout**: it directly executes `tools/build_sd_image.sh` and `tools/run_dolphin.sh`. Since both
   are tracked mode `100644`, `bash tools/run_suite_gba.sh ...` fails at line 36 with exit 126
   (`Permission denied`). Preferred source fix: invoke both children through `bash` (as
   `run_g2_case.sh` already does — see Open Issue §14.4). Until fixed, temporarily
   `chmod +x tools/build_sd_image.sh tools/run_dolphin.sh`, run G3, then restore `chmod 644`
   afterward if not intending to commit mode changes.
7. `compare_runs.py --mode checkpoints|suites --ref ... --cand ... --out ...`: G2/G3 comparator.
   Reports `status`, `mismatch_count`, and `mismatches`; missing logs/frames are not passes.

`Makefile`, `bootstrap_toolchain.sh`, `check_g1_encoder.sh`, and `run_g2_case.sh` honor `DEVKITPRO`
(default `/opt/devkitpro`). `run_g2_case.sh` invokes its child scripts through `bash`. The
`run_suite_gba.sh` child-execution bug above is the exception.

---

## 13. Re-verification / reproduction sequence

Use unique G2 labels: `run_g2_case.sh` deletes the matching output directory each run.

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

# B. Host packages and toolchain (§3)
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
sudo bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# C. ROM hashes (§3)
sha256sum nds/rockwrestler.nds gba/suite.gba

# D. G0 builds (§5) and digests (§6)
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

The upstream base-commit build/hash check is a separate one-time verification: clone
`https://github.com/radicalten/NooDS-Wii.git` at `main` (currently `1c995b48…`), run `make -j2` from
its root, then check `NooDS-Wii.dol` size/hash against §4. Do not confuse this upstream tree with
the `Actions-Minutes2` checkout.

---

## 14. Open issues (for the next agent)

1. **Old digests are stale.** Use §6, not any older `deliverables/*.sha256` values — and re-hash
   after any JIT source change (per §9/§10).
2. **Committed evidence is still sparse.** G0/G1/G2/G3 outputs from the last audit are
   local/untracked in the verification clone. Decide what to commit only if asked; G4–G6 and in-situ
   G1 evidence are still absent/carried.
3. **Rebuild G4/G5/G6 timing/counters** on the current source before quoting any performance number
   — including before/after numbers for the T3/T5/T6/T7 perf work in §9. The interpreter remains
   the correctness oracle; never claim a JIT speedup from one-shot suite wall times.
4. **Fix the G3 wrapper bug in source.** `run_suite_gba.sh` should invoke `build_sd_image.sh` and
   `run_dolphin.sh` via `bash`; a clean checkout currently exits 126. No source-tool change has been
   made yet.
5. **Decide on ROM tracking policy.** ROMs are tracked in Git; prior docs said they were ignored.
   Update policy/docs deliberately rather than assuming either state.
6. **Branch `feature/arm-ppc-jit` is absent** from the remote. Either create it when explicitly
   requested or remove references to it.
7. **AGENTS/Handoff tracking:** current `main` deletes both files; this merged document is
   attached/local only. Decide separately whether to restore an AGENTS file to the repository.
8. **Poll-skip code** stays off until its invalidation is redesigned and re-verified (§7.6). The old
   `NOODS_JIT_IDLE_SKIP` flag is absent from current source; do not document it as available.
9. **Re-verify all exact identifiers from §7/§9/§10 against current source** (function names, line
   positions, exact table/bucket indices, encoding masks) before relying on them to implement T1–T7
   — they were last confirmed by reading the repo at one point in time, independent of the
   build/gate audit in §4/§13. The task *descriptions*, invariants (§8), and register/ABI
   conventions (§7.5) are the durable part; precise code excerpts are a starting point to verify,
   not to paste blindly.

---

## 15. Session log — 2026-10-10 re-verification

- Cloned/fetched `Actions-Minutes2@main`; current HEAD `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`.
  Full history check confirmed `1c995b4` is not in this repository; current HEAD deletes
  `AGENTS.md`, and `Handoff.md` was deleted in the prior commit.
- Installed documented host packages. `sudo bash tools/bootstrap_toolchain.sh` returned
  `STATUS=PASS`; toolchain versions and link smoke test match §3.
- Built upstream base `1c995b48…`: 1,317,888 B, SHA-256 `cd48c6a4…`, matching the prior report.
- Built all three current JIT variants: exit 0; hashes match §6. Clean rebuild of
  `JIT=0 BLOCKS=0` was byte-identical.
- Ran G1 from `/tmp`: 52/52 PASS.
- Verified ROM hashes and vbagx pin.
- G2: interpreter reference plus both JIT variants, both ROMs, normal and pool-wrap 1,024 runs; all
  eight candidate comparisons PASS with zero mismatches at the specified checkpoints.
- G3: exact clean-checkout `bash tools/run_suite_gba.sh ...` invocation failed with exit 126 because
  its child scripts are not executable. Temporarily enabled execute bits on the two child scripts,
  ran all three variants to `ALL DONE`, confirmed identical logs and zero comparison mismatches,
  then restored both modes to `100644`.
- G4/G5/G6, in-situ MEM2 disassembly, and detailed architecture claims were not re-measured; kept
  CARRIED.
- Generated evidence/logs are under `evidence/reverify_2026-10-10/`, `evidence/g2_audit26_*`, and
  `evidence/g3_audit26/`. No tracked source/mode changes, no commits, and no push. Build/evidence
  outputs remain untracked in the local clone.
- **No JIT improvement work (T1–T7) was attempted in this session** — that backlog (§9) is carried
  forward from a separate internals-reading pass and is the primary queued work for the next agent,
  subject to the re-verification caveat in Open Issue §14.9.
