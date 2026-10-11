# AGENTS.md — NooDS-Wii: PowerPC 750CL (Broadway) JIT for ARM7/ARM9 (ARM+Thumb)

## 0. Mission

Build/improve a dynamic recompiler in NooDS-Wii translating NDS/GBA guest CPUs — **ARM7 (ARMv4T)** and **ARM9 (ARMv5TE)**, both **ARM and Thumb** — into native **PowerPC 750CL/Broadway** code on Wii. Two layers:
- **Process layer** (§§1–6, 10–13): checkout/build/verify, what's proven vs. claimed, tooling.
- **Technical layer** (§§7–9): JIT internals, invariants, and an ordered improvement backlog.

Consolidates an infra/verification audit (re-verified **2026-10-10**, `Actions-Minutes2` `main` @ `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`) with a JIT-internals task guide. Internals/tasks (§§7–9) came from an independent source read and are **not** re-audited line-by-line — verify exact names/line numbers/constants before relying on them.

---

## 1. Agent directives

- **Single sequential worker**, no subagents. Serialize Dolphin via `flock` (`run_dolphin.sh` does this).
- **Conserve context**: targeted greps/tails, not full `cat` (Dolphin logs have NUL/ALSA noise; `run_dolphin.sh` strips them).
- **Report honestly**: `PASS/FAIL/HANG/NOT_RUN/CARRIED` with evidence (commit, hash, counts, exit code). Missing data = `NOT_RUN`, never "zero mismatches".
- **No performance overstatement**: interpreter was faster than both JIT variants in earlier timing; G4–G6 not re-measured — don't claim speedups from one-shot wall-clock runs.
- **Evidence** lives under `evidence/`; gate DOLs land in `PROJ` (copy to `deliverables/` only when packaging — don't imply it's auto-populated).
- **No destructive ops** without asking: `git status` before switch/reset/clean; `run_g2_case.sh` deletes/recreates its own `evidence/g2_<LABEL>/` — always use a fresh label.
- **Wii only** (`-mrvl`); `vbagx` clone is read-only reference — never build/link it.
- **Interpreter is the correctness oracle/fallback** — the whole design fails toward correctness.
- **No commit/push** unless asked.
- **One task per commit** for JIT work (§9); never bundle correctness fixes (T1–T2) with perf work.
- **Never widen a classifier without a matching correct emitter; never inline raw guest-memory accesses** — always via guarded bus helpers (§8).

---

## 2. Repo layout & paths

| Name | Path | Contents |
|---|---|---|
| `REPO` | `/home/user/NooDS-Wii` | Git root (`github.com/radicalten/Actions-Minutes2`, `main`). |
| `PROJ` | `$REPO/NooDS-Wii` | `Makefile`, `tools/`, `evidence/`, `nds/`, `gba/`. Run builds/tools here. |
| `SRC` | `$PROJ/NooDS-Wii` | JIT/emulator sources: `arm_jit.h/.cpp`, `jit_bridge.S`, `memory.cpp`, `interpreter*.cpp`, `main.cpp`, `defines.h`. |

Notes (verified @ `c79fd20`):
- Only remote branch is `main`; `feature/arm-ppc-jit` doesn't exist — don't reference it unless creating on request.
- HEAD `c79fd20` deletes `AGENTS.md` (prior commit `f13c0f4` deleted `Handoff.md`) — **this file is a local/attached copy, not tracked in a fresh checkout.**
- Base commit `1c995b4` is **not** in this repo's history; it's upstream `radicalten/NooDS-Wii`'s `main` HEAD (built separately, §5).
- ROMs (`nds/rockwrestler.nds`, `gba/suite.gba`) **are tracked** in Git; no `.gitignore` exists (older docs claiming ROMs are ignored are stale).
- All `tools/*.sh`/`*.py` tracked mode `100644` — invoke via `bash`/`python3`. **Exception:** `run_suite_gba.sh` execs two children directly, so bare invocation fails on clean checkout (§12.6).
- **Not tracked at HEAD:** `deliverables/`, `WORKSPACE_MANIFEST.md`, patch files, most historical `evidence/` (G0 logs, G1 in-situ disasm, G2/G3 summaries, G4–G6 timing, poll-skip notes, prior reviews).
- **Tracked evidence:** `vbagx_ref_commit.txt`, `vbagx_jit_files.txt`, `vbagx_jit_grep.txt`, `vbagx_jit_notes.md`, `vbagx_arch_map.md`, `toolchain_bootstrap_result.txt`.
- Fresh **untracked** 2026-10-10 captures: `evidence/reverify_2026-10-10.md` and files per §13.

---

## 3. Environment

**Sandbox** (2026-10-10): Debian 13.7 x86_64, passwordless sudo, ~2 cores/1.9 GiB RAM/21 GiB free. Files outside `/home/user` don't persist across snapshots (re-run toolchain bootstrap each session).

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"
```
Verified versions: `dolphin-emu 2503+dfsg-1+deb13u1`, `binutils-arm-none-eabi 2.44-3+23+b1`, `python3-pyelftools 0.32-1`, `mtools 4.0.48-1`, `dosfstools 4.2-1.2`. `mkfs.vfat` at `/usr/sbin`.

**Python caveat:** `python3` → `/usr/local/bin/python3` (no apt `elftools`); use `/usr/bin/python3` if a future tool needs it. Current G0–G3 tools don't need it.

**Toolchain:**
```bash
cd "$PROJ"
sudo bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
```
Pins devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4, devkitppc-rules 1.2.1/crtls 2.1.0-1, libogc 3.1.0, libfat-ogc 2.1.0-4, gamecube-tools 1.0.7 (`elf2dol`), general-tools 1.4.4, from `wii.leseratte10.de/devkitPro/`. Ends with Wii link smoke test → `evidence/toolchain_bootstrap_result.txt`. **Verified: `STATUS=PASS`**, GCC 16.1.0, AS 2.46.0.20260210.

**ROMs** (tracked, verified 2026-10-10):

| File | Size | SHA-256 |
|---|---:|---|
| `nds/rockwrestler.nds` | 39,433 B | `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` |
| `gba/suite.gba` | 524,288 B | `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` |

If missing:
```bash
cd "$PROJ" && mkdir -p nds gba
[ -f nds/rockwrestler.nds ] || curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
[ -f gba/suite.gba ]        || curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba  # must match table above
```

---

## 4. Verification status (process layer, 2026-10-10)

Key: **VERIFIED** = re-run this audit. **CARRIED** = claimed earlier, not re-measured. **NOT_RUN** = not executed.

| Gate | Check | Status | Result |
|---|---|---|---|
| Repo | checkout | **VERIFIED** | HEAD `c79fd20bf7118e698c26b2b33a4803e881f8a1cb`; only `main` on remote. |
| Base build | upstream `1c995b4`, `make -j2` | **VERIFIED** | `NooDS-Wii.dol` 1,317,888 B, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`. |
| vbagx pin (G-1) | pin exists | **VERIFIED (pin only)** | Resolves to `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`; content not re-audited — don't build it. |
| Toolchain | bootstrap + link test | **VERIFIED** | `STATUS=PASS` (§3). |
| ROMs | hashes | **VERIFIED** | Match §3. |
| **G0** Builds | 3 JIT configs compile | **VERIFIED** | Exit 0; hashes §6; clean rebuild of `JIT=0 BLOCKS=0` byte-identical. |
| **G1** Encoder oracle | 52 PPC 750CL forms vs `as`/`objdump` | **VERIFIED** | `52/52`, `REQUIRED_MINIMUM=44/44`, `STATUS=PASS` (run from `/tmp`). In-situ MEM2 disasm: **CARRIED**. |
| **G2** `rockwrestler.nds` | frames 15/30/45, normal + `POOL_LIMIT_WORDS=1024` | **VERIFIED** | Both JIT variants × both modes: 4/4 `PASS`, `mismatch_count=0`. |
| **G2** `suite.gba` | frames 50/60/120, normal + wrap | **VERIFIED** | Same: 4/4 `PASS`, 0 mismatches. |
| **G3** mGBA suite | `suite.gba` → `ALL DONE`, 3 variants | **PASS w/ local workaround** | All reach `ALL DONE`; 13 suites, 1 SKIP, 6,998 tests (3,506 pass/3,492 fail *per suite itself*). All `gbaout.log` SHA-256 identical; both comparisons PASS, 0 mismatches. Clean-checkout wrapper fails exit 126 — see §12.6. |
| **G4** Phase1 timing | vs interpreter | **CARRIED** | Earlier: Phase 1 slower than interpreter. |
| **G5** Phase2 ARM7 timing | vs Phase1/interpreter | **CARRIED** | Earlier: ~1.5–1.7× faster than Phase 1, still slower than interpreter. |
| **G6** ARM9 blocks | native blocks + CP15 exits | **CARRIED** | `blkComp9=80`, `blkHit9=5,011,952`, `blkInsn9=14,955,280`, `cp15Exit9=21`, 0 mismatches. |

**Notes:** `RC=143` in G2 `result.txt` is expected (runner kills Dolphin post-sentinel) — check `STATUS=PASS`+`SENTINEL_HIT=1`, not raw RC. G2/G3 ran with `NOODS_JIT_POLL_SKIP=0` (default) — don't treat as poll-skip validation. `NOODS_JIT_IDLE_SKIP` doesn't exist in current source. `run_g2_case.sh` can exit 0 without a pass — always check `result.txt` then comparator JSON (`status=PASS`, `mismatch_count=0`). G3's 3,492 failing cases are intrinsic to the suite, not a regression — the verified claim is *matching* output, not full mGBA compatibility. One-shot G3 wall times (interpreter 13s, jit1_blocks0 39s, jit1_blocks1 19s) and historical frame-45 `accum_us` (interpreter 4,606,860; baseline JIT 5,569,304; fast path 4,954,587; final default 4,914,235) are uncontrolled/CARRIED — not performance evidence.

---

## 5. Build

```bash
cd "$PROJ"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
for cfg in '0 0' '1 0' '1 1'; do
  read -r JIT BLOCKS <<< "$cfg"
  make clean JIT="$JIT" BLOCKS="$BLOCKS"
  make -j2 JIT="$JIT" BLOCKS="$BLOCKS"
done
# -> NooDS-Wii_jit0_blocks0.dol, _jit1_blocks0.dol, _jit1_blocks1.dol (generic NooDS-Wii.dol/.elf/.map overwritten each pass)
sha256sum NooDS-Wii_jit*.dol
```
**Zero warnings required** for any JIT change — `#pragma GCC diagnostic ignored "-Winvalid-offsetof"` is intentional; any other warning is real signal.

---

## 6. Digests (current source @ `c79fd20`, 2026-10-10)

| Config | Output | Size (B) | SHA-256 |
|---|---|---:|---|
| Upstream `1c995b4` (no JIT) | `NooDS-Wii.dol` | 1,317,888 | `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c` |
| `JIT=0 BLOCKS=0` (interpreter) | `_jit0_blocks0.dol` | 1,338,816 | `99e6b49275d40f8a8af55298992e15a5a986d8e055ddd9d856f549db1a9b3873` |
| `JIT=1 BLOCKS=0` (Phase1) | `_jit1_blocks0.dol` | 1,336,992 | `bb0853bd2a865b53324200a479071a29bf55b19ee1dc9a3da5f69fa94ac7d70d` |
| `JIT=1 BLOCKS=1` (Phase2) | `_jit1_blocks1.dol` | 1,364,064 | `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e` |

Older hashes (`12f073…`, `6d09df…`, `c69f71…`) are **stale/not reproducible**. Re-hash this table after every JIT source change (§9/§10).

---

## 7. Architecture

Build/G2-G3 gates re-run @ `c79fd20` (§4); internals below are from an independent read, **not** line-by-line re-audited — verify identifiers before relying on them (§14.9).

**Goal:** full ARM7(ARMv4T)/ARM9(ARMv5TE), ARM+Thumb recompiler; interpreter (`JIT=0 BLOCKS=0`) stays oracle/fallback — design **fails toward correctness**.

**Phase 1** (`NOODS_JIT=1 NOODS_JIT_BLOCKS=0`): 1:1 stub JIT. One guest dispatch → one cached native stub in MEM2 (`stubTable[cpuIdx][8192]`), calling the resolved `Interpreter` handler (`resolveArmHandler`/`resolveThumbHandler`) via `bctrl` with the already-fetched opcode; exact cycle cost returned via `jit_exit`. No PC/pipeline double-handling. Condition-false/reserved-condition opcodes stay interpreted.

**Phase 2** (`NOODS_JIT_BLOCKS=1`): native emitters (`emitNativeArm/Thumb`) cover ALU ops, immediate shifts (with shifter carry-out), `MOV/MVN/NEG/CMP/CMN/TST/TEQ/ADD/SUB/RSB`, non-flag `ADC/SBC/RSC`, `AND/EOR/ORR/BIC`, ARM9 `CLZ` (pre-T4), PC/SP-relative adds, Thumb `BL` setup. Flags: exact NZCV in `cpsr[31:28]` — N from bit31, Z via `cntlzw`, C from XER CA post `addc/subfc`, V from signed-overflow exprs. **Block compiler** (`compileBlock`/`tryExecuteBlock`): ≤24 instrs (`MAX_BLOCK_INSTRS`), one 4KB page, optional leading single-reg load, terminates on `B/BL/BX/BLX/Bcc`. Optimizations: backward CPSR-liveness pass, tail-call epilogue skipping dead PC/pipeline updates with same-page fast path. **Safety:** never cross `core->events[0].cycles`; ARM9 CP15 exits to interpreter (`cp15Exit9`, = Gate G6). **Invalidation:** `alignas(32) BlockEntry` (1 cache line), 1KB `pageHasBlockBits` bitmap, `tailHash` check on epoch change (stops false SMC recompiles, e.g. IWRAM `0x03000000` sharing). Code pool: MEM2, 1MiB (`DEFAULT_POOL_BYTES`), wraps via `wrapPool()` bumping 30-bit `currentEpoch`; stale entries rejected by epoch mismatch.

**Key files** (relative to `SRC`):

| File | Role |
|---|---|
| `arm_jit.h` | `JitPpc::` encoder (52 forms); `ArmPpcJit`: `stubTable[2][8192]`, `blockTable[2][8192]`, `pageEpoch[2][4096]`, `pageHasBlockBits[2][128]`, `setPoolLimitWords`, invalidation API, `dumpStats`/`Stats`. Constants: `DEFAULT_POOL_BYTES=1MiB`, `MAX_BLOCK_INSTRS=24`, `NOODS_JIT_POLL_SKIP` default 0. |
| `arm_jit.cpp` | Classification/emitters/block compiler/dispatch/SMC invalidation. MEM2 alloc via `Noods_MEM2_Alloc`; `flushCodeRange` runs `dcbst;sync;icbi;sync;isync`. Check free MEM2 before resizing pools. |
| `jit_bridge.S` | `jit_enter`/`jit_exit`; 32-byte frame, LR at `36(r1)`, `r28..r31` at `16..28(r1)`, `Interpreter*` in `r31`, dispatch via `mtctr r3;bctr`. |
| `interpreter.h/.cpp` | Hooks: `init`, `loadState`, `directBoot`, `runOpcode()`. **Authoritative semantics reference** — for ambiguous cases, copy the real handler (in `interpreter_alu/transfer/branch.cpp`) verbatim rather than reasoning from spec memory. |
| `memory.h/.cpp`, `dldi.cpp` | `invalidateSharedAddr` (Memory write paths incl. VRAM), `invalidateCpu` (`updateMap9/7`), `invalidateAll` (`Dldi::patchRom`). `memory.cpp` ~line 630: ARM9 halfword-read fast path for DISPSTAT (`0x04000004`) only — VCOUNT/IPCSYNC use normal dispatch. Separately `arm_jit.cpp::armLoadAddrOk` whitelists DISPSTAT/VCOUNT/IPCSYNC for load classification (different mechanism). |
| `main.cpp` | `NoodsTestHarness`: reads `sd:/autoboot.txt` (`MAX_FRAMES`,`CHECKPOINTS`,`POOL_LIMIT_WORDS`); writes timebase profiling and dumps (`sd:/g2_state.log`,`mem_dump.bin`,`fb_dump.bin`); flushes FAT (`fatUnmount`/`fatInitDefault`) before sentinel. |
| `defines.h` | `U8TO32`/`U8TO16` LE guest reads. |

**JIT register/ABI conventions (do not break):** Target devkitPPC `-mcpu=750`/Gekko/Broadway — no 64-bit int ops, no `isel`, no AltiVec. Host ABI PPC32 SysV (volatile r0-r12, non-volatile r13-r31). Internal convention: `R31`=`Interpreter*` base; `R3-R5`=operands/result; `R6`=scratch CPSR; `R7-R9`=scratch; `R11`=banked-reg pointer scratch; `R12`=helper addr for `mtctr/bctrl`; `R28-30,R1`=save/restore block owned by `jit_bridge.S`. **Broadway has incoherent split I/D caches** — any new compile path must call `ArmPpcJit::flushCodeRange()` (as `compileBlock`/`compileStub` already do) or risk intermittent real-hardware-only garbage execution. Build flags: `NOODS_JIT`, `NOODS_JIT_BLOCKS`, `NOODS_JIT_POLL_SKIP` (default off), `NOODS_JIT_PROFILE`.

**Runtime flags:**

| Flag | Default | Status |
|---|---|---|
| `NOODS_JIT`/`NOODS_JIT_BLOCKS` | per Makefile | Build config; `JIT=0`=oracle. |
| `NOODS_JIT_IDLE_SKIP` | N/A | **Absent from current source.** Earlier: net loss. Not a real toggle — don't document as available. |
| `NOODS_JIT_POLL_SKIP` | 0 | Joint ARM7/9 poll-loop skip w/ global `pollEpoch`. Not enabled last audit; never fired on rockwrestler (`skips=0`), ~1.7× slower historically. **Leave off** until invalidation is redesigned address-aware. Experimental: `CXXFLAGS=-DNOODS_JIT_POLL_SKIP=1 make JIT=1 BLOCKS=1` (untested this audit). |
| `NOODS_JIT_PROFILE` | off | Dispatch profiling buckets via `dumpStats`; use for T7 (§9). |
| DISPSTAT halfword fast path | ON | Active during G2/G3 parity; effect not isolated. |

---

## 8. Hard invariants — do not change

Design **fails toward correctness**: unclassifiable → interpreter. **Never weaken a classifier to widen coverage — only widen via a correct emitter.**

1. `tryExecuteBlock()` must refuse blocks whose `prefixCycles` would cross `core->events[0].cycles` (else blocks skip timer/DMA/IRQ boundaries).
2. Keep `bit27==1` (LDM/STM, SWI, B/BL, CP15) and `Rd==15` instrs out of block compilation unless fully/correctly implemented.
3. Keep ARM9 CP15 ops gated out of blocks (Gate G6).
4. SMC invalidation (`invalidateAddr/Range/SharedAddr`) must fire on every guest write path the JIT doesn't own.
5. Memory access only via the interpreter's guarded bus helpers — **never** inline raw pointer R/W (breaks MMIO/VRAM mirroring).
6. ARM-mode non-initial block instructions must be unconditional (`cond==0xE`) unless full condition eval is implemented for them.

---

## 9. Improvement backlog (ordered; T1–T2 correctness, T3–T7 perf)

Each = own commit + own regression pass (§10). **Verify names/line numbers/masks below against current `SRC/arm_jit.{h,cpp}` first** — descriptions/invariants/conventions are durable, code excerpts are starting points.

**T1 — `BLX Rm` (register) never writes LR · ARM9 · medium impact/low risk.** In `execBranchArm()`'s final "BX Rn" tail (shared by `BX 0x012FFF1x` and `BLX 0x012FFF3x`, bit5), LR is silently dropped. Fix: detect bit5, write LR first, gate on `!cpu->arm7` (mirror the already-correct Thumb `BLX Rs` a few lines above):
```cpp
const bool isBlxReg = !cpu->arm7 && (opcode & (1u << 5)) != 0;
cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
uint32_t target = *cpu->registers[opcode & 0xFu];
if (isBlxReg) *cpu->registers[14] = cpu->registersUsr[15] - 4u;
cpu->cpsr = (cpu->cpsr & ~(1u << 5)) | ((target & 1u) << 5);
cpu->registersUsr[15] = target;
cpu->flushPipeline();
return prefixCycles + 3;
```
Also confirm `isTerminatorBranchArm()`/`isUncondBranchArm()` treat `BLX Rm` as a terminator (can't build a block through it).

**T2 — `BLX <label>` (`cond==0xF`) wrongly condition-tested · ARM9 · medium/medium.** `cond=0b1111` is ARMv5's unconditional-extension space, not a real condition; current `cond!=0xE` guard routes it through the 16-entry LUT (accidental), and the `0x0A000000` mask doesn't even match BLX. Fix — early-out before any condition lookup:
```cpp
if (cond == 0xFu) {
    if (cpu->arm7) {  // ARMv4T: UNDEFINED, defer to interpreter
        cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
        cpu->flushPipeline();
        return prefixCycles + cpu->handleReserved(opcode);
    }
    uint32_t r15 = oldR15 + (uint32_t)remDelta;
    int32_t off = ((int32_t)(opcode << 8) >> 6) | (int32_t)((opcode >> 23) & 2u); // H -> +2
    *cpu->registers[14] = r15 - 4u;
    cpu->cpsr |= (1u << 5);  // force Thumb
    cpu->registersUsr[15] = r15 + (uint32_t)off;
    cpu->flushPipeline();
    return prefixCycles + 3;
}
```
Verify the `H`-bit offset decode against the real `interpreter_branch.cpp` handler; copy verbatim if it differs.

**T3 — Native `STR`/`STRB` fast path · both CPUs · HIGH impact/low risk (biggest win).** Only `STRH` (pre-index immediate, no writeback) is fast-pathed (`isNativeStoreArm`); ordinary word/byte stores — the most common ARM9 compiler-generated store — always fall back. Add classification beside `isNativeStoreArm()`, emitter beside `emitNativeStoreArm()`, wire into `canTranslateNativeArm()`+`compileBlock()`. Mirror `isNativeLoadArm()`: pre-indexed, `W=0`, `Rn≠PC`, `Rd≠PC`, reg-offset restricted to unshifted/`LSL #imm`, no `Rm==PC`, no bit4 reg-shift-by-reg:
```cpp
static inline bool isNativeStoreWordByteArm(uint32_t op) {
    if ((op >> 28) != 0xEu) return false;                // AL only
    if ((op & 0x0C000000u) != 0x04000000u) return false; // single data transfer
    if (op & (1u << 20)) return false;                   // L=0
    if (!(op & (1u << 24))) return false;                // P=1
    if (op & (1u << 21)) return false;                   // W=0
    if (((op >> 16) & 0xFu) == 15u) return false;        // Rn!=PC
    if (((op >> 12) & 0xFu) == 15u) return false;        // Rd!=PC
    if (op & (1u << 25)) {                                // register offset
        if (op & (1u << 4)) return false;
        if ((op & 0xFu) == 15u) return false;
        if (((op >> 5) & 3u) != 0u) return false;         // LSL only
    }
    return true;
}
```
Emitter must reuse the same base+offset calc and guarded bus-write helper `emitNativeLoadArm()`'s slow path uses — don't hand-roll (invariant §8.5).

**T4 — Native `CLZ` · ARM9 · low impact/lowest risk (good smoke test, do first).** `armWrittenFlagsMask()` already special-cases `0x016F0F10` as "no flags written" — only the emitter is missing; `cntlzw` is 1:1:
```cpp
static inline bool isClzArm(uint32_t op) { return (op & 0x0FFF0FF0u) == 0x016F0F10u; }
// in emitNativeArm(), gated !cpu->arm7:
emitLoadGuestReg(p, JitPpc::R4, opcode & 0xFu);
*p++ = JitPpc::cntlzw(JitPpc::R3, JitPpc::R4);
emitStoreGuestReg(p, JitPpc::R3, (uint8_t)((opcode >> 12) & 0xFu));
outCycles = 1;  // CLZ never updates NZCV
```

**T5 — Native `MUL`/`MLA` · both CPUs · medium/medium.** `armPurity()` already recognizes the encoding but nothing emits it. `mullw` 1:1; MLA adds one accumulator `add`. `S=1` sets only N,Z (C is UNPREDICTABLE, interpreter leaves it) — reuse `emitUpdateNZ()`:
```cpp
static inline bool isMulArm(uint32_t op) {
    if ((op & 0x0FC000F0u) != 0x00000090u) return false;  // bits27:22=0, 7:4=1001
    uint8_t rd = (uint8_t)((op >> 16) & 0xFu), rm = (uint8_t)(op & 0xFu);
    return rd != 15u && rm != 15u && rd != rm;  // rd==rm UNPREDICTABLE
}
// mullw R3,R4(Rm),R5(Rs) -> optional add R3,R3,R6(Rn) for MLA -> emitStoreGuestReg -> if(emitFlags) emitUpdateNZ(p,R3)
```
**Caution:** multiply aliases the data-processing space — check `isMulArm()` *before* the generic ALU switch.

**T6 — Native reg-specified shifts (`LSL Rd,Rm,Rs`) · both · medium/medium.** Currently rejected (bit4=1 bail in `canTranslateNativeArm()`). PPC `slw/srw/sraw` already give correct ≥32-shift results, but ARM reads count from Rs bits[7:0] — mask to byte, clamp to 32 via `cmplwi`+conditional select. Carry-out needs an extra shift by `(amount-1)` guarded for `amount==0` — the trickiest part; get it wrong and every `ADC/SBC/ROR`-through-shift breaks.

**T7 — Re-measure native halfword loads · both · low/low.** `kNativeHalfLoads=false` per an old "no speedup" measurement that predates Phase-2 blocks. Flip behind a build flag, re-run `NOODS_JIT_PROFILE` (§7) — chained-in-block dispatch overhead it used to compete against is gone; old conclusion may not hold.

**Deferred (don't do unless asked):** `UMULL/UMLAL/SMULL/SMLAL` (`mulhw[u]`+`mullw` pairs), `QADD/QSUB/QDADD/QDSUB` (branchless clamp + sticky Q, CPSR bit27), `SMLAxy/SMULxy/SMLAWy` DSP family, native `LDM`/`STM`.

---

## 10. Validation checklist (after every §9 task)

1. **Compile**: full Wii build (§5), zero warnings.
2. **Stats direction** (`dumpStats`/`NOODS_JIT_PROFILE`): `fbInsn9/7` drop for newly-native classes; `natInsn9/7` rise correspondingly; `blkComp*/blkHit*` up, `blkFail*` flat. T3 specifically: `fbClassArm[cpu][2]` (LDR/STR/halfword bucket) and store-opcode `fbOpc7` entries fall sharply. *(Re-verify exact field names against current `Stats` struct.)*
3. **Correctness** beyond standard G2/G3 ROMs: several titles/CPU incl. one ARM9 `BLX`-trampoline (mixed ARM/Thumb) title, one GBA-mode title (`gbaMode`/`dsiMode` guards in `pollExecArm`), one with known SMC/DMA-timed code.
4. **Hardware**: final pass on real Wii or cycle-accurate Dolphin — `dcbst`/`icbi` ordering/alignment bugs hide on non-cycle-accurate hosts.
5. **SMC regression** (esp. T3): verify DMA code upload (ARM7→ARM9, decompression stubs) still invalidates (`pageEpoch`, invariant §8.4).
6. **Re-hash** §6's digest table — any JIT change invalidates prior hashes.
7. **Re-run G2/G3** (§4/§13) against new build; update status table honestly with evidence paths.

---

## 11. Gate definitions (reference)

- **G-1**: vbagx pinned @ `4d5b9984e9b7457c9146e37dace5b95fa04b73ef` (pin verified, content not re-audited):
  ```bash
  mkdir -p /home/user/reference
  REF=/home/user/reference/vbagx
  [ ! -e "$REF" ] || { echo "Refusing to overwrite: $REF" >&2; exit 1; }
  git clone --depth 1 https://github.com/dborth/vbagx.git "$REF"
  git -C "$REF" fetch --depth 1 origin 4d5b9984e9b7457c9146e37dace5b95fa04b73ef
  git -C "$REF" checkout FETCH_HEAD
  ```
- **G0**: 3 configs build (§6). **G1**: 52/52 encoder forms bit-exact vs GNU `as`/`objdump` (in-situ MEM2 disasm CARRIED). **G2**: checkpoint parity @ frames 15/30/45 (`rockwrestler.nds`) and 50/60/120 (`suite.gba`), both JIT variants, exact cycles/registers/CPSR/memory(413,696B)/framebuffer(393,216B), incl. `POOL_LIMIT_WORDS=1024` wrap runs. **G3**: mGBA suite reaches `ALL DONE` all 3 variants, identical `gbaout.log` SHA-256 (not full mGBA compat). **G4**: Phase1 vs interpreter (carried: slower). **G5**: Phase2 ARM7 vs Phase1/interpreter (carried: faster than P1, slower than interpreter). **G6**: ARM9 blocks+CP15 exits (= invariant §8.3; counters §4, carried).

---

## 12. Tooling (`PROJ/tools/`, all mode `100644` — use `bash`/`python3`)

1. `bootstrap_toolchain.sh` — pinned devkitPro/libogc install + link smoke test; `sudo bash tools/bootstrap_toolchain.sh`.
2. `build_sd_image.sh <out.img> [--autoboot sd:/...]` — 128MiB FAT16 SD image w/ ROMs + optional autoboot.
3. `run_dolphin.sh DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL]` — `flock`-serialized headless runner; writes `result.txt` + cleaned log.
4. `check_g1_encoder.sh` + `test_encoder.cpp` — G1 oracle; path-safe from `/tmp`; overwrites fixed `evidence/g1_encoder_oracle.log`.
5. `run_g2_case.sh DOL LABEL ROM CHECKPOINTS MAX_FRAMES TIMEOUT [POOL_LIMIT]` — one G2 case → `evidence/g2_<LABEL>/` (**deletes/recreates dir each run**). Check `result.txt` for `STATUS=PASS`+`SENTINEL_HIT=1`; exit code alone insufficient.
6. `run_suite_gba.sh DOL LABEL OUT_DIR [TIMEOUT]` — G3 runner. **Bug:** execs `build_sd_image.sh`/`run_dolphin.sh` directly; since both are `100644`, bare `bash tools/run_suite_gba.sh` fails exit 126 at line 36. Fix: invoke children via `bash` (as `run_g2_case.sh` does). Workaround: `chmod +x` both children, run, `chmod 644` to restore.
7. `compare_runs.py --mode checkpoints|suites --ref ... --cand ... --out ...` — comparator; reports `status`/`mismatch_count`/`mismatches`; missing data ≠ pass.

`Makefile`, `bootstrap_toolchain.sh`, `check_g1_encoder.sh`, `run_g2_case.sh` honor `DEVKITPRO` (default `/opt/devkitpro`).

---

## 13. Reproduction sequence

```bash
set -euo pipefail
# A. Checkout
REPO=/home/user/NooDS-Wii
if [ ! -d "$REPO/.git" ]; then
  [ ! -e "$REPO" ] || { echo "Refusing to clone over: $REPO" >&2; exit 1; }
  git clone https://github.com/radicalten/Actions-Minutes2.git "$REPO"
fi
git -C "$REPO" status --short --branch
PROJ="$REPO/NooDS-Wii"; cd "$PROJ"; git -C "$REPO" rev-parse HEAD

# B. Packages + toolchain (§3)
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools
sudo bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH

# C. ROM hashes
sha256sum nds/rockwrestler.nds gba/suite.gba

# D. G0 builds + digests (§5/§6)
for cfg in '0 0' '1 0' '1 1'; do
  read -r JIT BLOCKS <<< "$cfg"
  make clean JIT="$JIT" BLOCKS="$BLOCKS"; make -j2 JIT="$JIT" BLOCKS="$BLOCKS"
done
sha256sum NooDS-Wii_jit*.dol

# E. G1
mkdir -p evidence/reverify_2026-10-10
bash tools/check_g1_encoder.sh > evidence/reverify_2026-10-10/g1.log 2>&1
tail -4 evidence/reverify_2026-10-10/g1.log  # expect 52/52, STATUS=PASS

# F. G2 (fresh TAG each run; run_g2_case.sh deletes its label's dir)
TAG="recheck_$(date +%Y%m%d_%H%M%S)_$$"
run_g2() { # cfg label rom points frames secs pool
  bash tools/run_g2_case.sh "NooDS-Wii_${1}.dol" "$2" "$3" "$4" "$5" "$6" "$7"
  grep -q '^STATUS=PASS$' "evidence/g2_${2}/result.txt"
  grep -q '^SENTINEL_HIT=1$' "evidence/g2_${2}/result.txt"
}
compare_g2() { python3 tools/compare_runs.py --mode checkpoints --ref "evidence/g2_${1}" --cand "evidence/g2_${2}" --out "evidence/${3}.json"; }

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

# G. G3 (3 variants; needs temp +x on two children)
OUT="$PWD/evidence/g3_${TAG}"; mkdir -p "$OUT"
trap 'chmod 644 tools/build_sd_image.sh tools/run_dolphin.sh' EXIT
chmod +x tools/build_sd_image.sh tools/run_dolphin.sh
for cfg in jit0_blocks0 jit1_blocks0 jit1_blocks1; do
  bash tools/run_suite_gba.sh "NooDS-Wii_${cfg}.dol" "$cfg" "$OUT" 300
  grep -q '^STATUS=PASS$' "$OUT/dolphin_${cfg}/result.txt"
done
python3 tools/compare_runs.py --mode suites --ref "$OUT/mgba_summary_jit0_blocks0.json" --cand "$OUT/mgba_summary_jit1_blocks0.json" --out "evidence/g3_compare_${TAG}_jit1_blocks0.json"
python3 tools/compare_runs.py --mode suites --ref "$OUT/mgba_summary_jit0_blocks0.json" --cand "$OUT/mgba_summary_jit1_blocks1.json" --out "evidence/g3_compare_${TAG}_jit1_blocks1.json"
sha256sum "$OUT"/gbaout_*.log  # must all match
chmod 644 tools/build_sd_image.sh tools/run_dolphin.sh; trap - EXIT

git status --short --branch  # review untracked evidence; don't commit/push unless asked
```

Upstream base-commit check (separate, one-time): clone `radicalten/NooDS-Wii@main` (currently `1c995b48…`), `make -j2` from its root, verify DOL size/hash against §4. Don't confuse with the `Actions-Minutes2` checkout.

---

## 14. Open issues (next agent)

1. Use §6 digests, not stale `deliverables/*.sha256`; re-hash after any JIT change.
2. Committed evidence is sparse — last audit's outputs are local/untracked; G4–G6 + in-situ G1 still absent/carried.
3. Rebuild G4/G5/G6 before quoting any performance number, including T3/T5/T6/T7 before/after — never claim speedup from one-shot wall time.
4. Fix `run_suite_gba.sh`'s child-exec bug in source (invoke via `bash`); not yet patched.
5. Decide/clarify ROM tracking policy (currently tracked; older docs wrongly said ignored).
6. `feature/arm-ppc-jit` branch doesn't exist remotely — create only if asked, else stop referencing.
7. `AGENTS.md`/`Handoff.md` deleted at current HEAD; this merged doc is local-only — decide separately whether to restore to repo.
8. Poll-skip stays off until invalidation is redesigned (address-aware, not global epoch); `NOODS_JIT_IDLE_SKIP` doesn't exist — don't document as available.
9. **Re-verify all §7/§9/§10 identifiers** (function names, line positions, bucket indices, encoding masks) against current source before implementing T1–T7 — last confirmed independently of the §4/§13 audit. Descriptions, invariants (§8), and ABI/register conventions (§7) are durable; code excerpts are drafts to verify, not paste-and-ship.

---

## 15. Session log — 2026-10-10

Cloned/fetched `Actions-Minutes2@main` → HEAD `c79fd20`; confirmed `1c995b4` absent from history, `AGENTS.md`/`Handoff.md` deleted at/near HEAD. Toolchain bootstrap `PASS` (versions match §3). Built upstream base (hash matches §4) and all 3 current JIT variants (hashes match §6; clean `JIT=0 BLOCKS=0` rebuild byte-identical). G1 from `/tmp`: 52/52 PASS. ROM hashes + vbagx pin verified. G2: all 8 candidate comparisons (2 ROMs × 2 JIT variants × normal/wrap) PASS, 0 mismatches. G3: confirmed clean-checkout exit-126 bug, then via temporary `chmod +x` workaround (restored after) ran all 3 variants to `ALL DONE` with identical logs and 0 comparison mismatches. G4/G5/G6, in-situ MEM2 disasm, and detailed architecture claims **not** re-measured (CARRIED). All generated evidence is untracked (`evidence/reverify_2026-10-10/`, `evidence/g2_audit26_*`, `evidence/g3_audit26/`); no tracked changes, commits, or pushes. **No T1–T7 JIT work was attempted this session** — that backlog (§9) is the primary queued work, subject to the re-verification caveat in §14.9.
