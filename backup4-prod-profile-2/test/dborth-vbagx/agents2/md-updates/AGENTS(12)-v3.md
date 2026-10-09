# AGENTS.md — NooDS-Wii: ARMv4T/ARMv5TE → Wii PowerPC JIT

**Refreshed 2026-10-09 (second verification pass, LM Arena sandbox).** This guide is sufficient for a fresh agent starting from the pinned NooDS-Wii base plus this file. It is a plan and a verification record — not a claim that the JIT exists. Treat the PASS/NOT_RUN labels in §1 as authoritative over any older wording. Everything marked PASS below was executed in this sandbox and its evidence is committed on branch `feature/arm-ppc-jit` (branch `feature/arm-ppc-jit`, on top of base `1c995b4`).

## 0. Agent directives

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs with `flock` (the provided `tools/run_dolphin.sh` does this).
- **Conserve context.** Inspect targeted line ranges; redirect logs to disk and inspect with `grep`/`tail`/`sed`. Never `cat` a Dolphin log (it contains NUL bytes and ALSA noise; `run_dolphin.sh` strips both).
- **Report honestly.** Use `PASS`, `FAIL`, `HANG`, or `NOT_RUN` with exact evidence (commit, hash, counts, exit code, `END: X/Y`). Missing files or captures are `NOT_RUN`, never zero mismatches.
- **Keep evidence in the repository** under `evidence/`; built DOLs and hashes under `deliverables/`. Do not fabricate output.
- **Do not destroy work.** Inspect `git status` before switching/resetting. No `rm -rf` on a checkout or reference tree unless the user asks.
- **Target Wii only** (`-mrvl`). The vbagx clone is read-only study material; never build, link, or include it.
- Work on branch `feature/arm-ppc-jit` only. Preserve the interpreter as the correctness oracle and fallback.
- **Sandbox facts (this environment):** Debian 13 x86_64, user `user` with passwordless `sudo`, 2 cores, ~2 GB RAM, ~20 GB free disk. `apt-get` works. Files outside `/home/user` (notably `/opt/devkitpro` and apt-installed packages) **do not persist** between sessions; see §13 for the ~2-minute re-provisioning sequence.

## 1. Verified state (2026-10-09, this sandbox)

| Check | Result |
|---|---|
| NooDS-Wii base | **PASS** — `https://github.com/radicalten/NooDS-Wii.git`; pinned commit `1c995b48c37ebf3645646968c416958f79264137` is the current `main` HEAD. |
| Work branch | **PASS** — `feature/arm-ppc-jit` created from the pinned commit; branch `feature/arm-ppc-jit` adds `tools/`, `evidence/`, `deliverables/`, `.gitignore`. No source file under `NooDS-Wii/` has been modified. |
| Planned JIT files in base | **ABSENT** (re-confirmed) — no `arm_jit.*`, `jit_bridge.S`, `runDecoded()`, `NOODS_JIT`, autoboot, or mGBA debug-register code. Base source dir is `NooDS-Wii/` (34 `.cpp` incl. `wii_video.cpp`). |
| Toolchain bootstrap | **PASS** — `tools/bootstrap_toolchain.sh` installs devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib 4.6.0.20260123-4, rules 1.2.1, libogc 3.1.0, libfat-ogc 2.1.0-4, gamecube-tools 1.0.7 (`elf2dol`), general-tools 1.4.4 from the instructed mirror, plus **devkitppc-crtls v2.1.0-1** (present on mirror at `devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst`, and GitHub `devkitPro/devkitppc-crtls` tag `v2.1.0`). All artifacts SHA-256 pinned; ends with a link smoke test. Runtime ≈ 5 s on this network. |
| Baseline build (G0 baseline) | **PASS** — unmodified base `make -j2` ≈ 17 s wall. `NooDS-Wii.dol` 1,317,888 bytes, SHA-256 `cd48c6a4971085b920ef692c79a6698e94e47b1723f90441614fbf6345d44f9c`; `.elf` 3,400,068 bytes `d9476188…`. **Reproducible**: two `make clean && make` runs and a rebuild after a from-scratch toolchain reinstall all produced identical hashes. Log: `evidence/g0_baseline_build.log`; copy: `deliverables/base_1c995b4_unmodified.dol(.sha256)`. |
| Host dependencies | **PASS (installed and used)** — `dolphin-emu 2503+dfsg-1+deb13u1` (binary `/usr/games/dolphin-emu-nogui`, "Dolphin [master] 2503"), `mtools 4.0.48`, `dosfstools 4.2`, `zstd 1.5.7`, `binutils-arm-none-eabi 2.44`, `python3-pyelftools 0.32`, `xvfb`. There is no apt package named `dolphin-emu-nogui`. `mkfs.vfat` is in `/usr/sbin` (not on the user PATH). |
| ROM downloads | **PASS** — both URLs HTTP 200; sizes/SHA-256 match §9.1 exactly. Stored in-repo at `nds/`, `gba/` (git-ignored). |
| SD image recipe | **PASS** — full §9.2 manifest executed as one script (`tools/build_sd_image.sh`): 128 MiB whole-file FAT16, 6 dirs, 9 ROM copies, autoboot override, `mdir`/`mcopy` round-trip hash-identical. 8 MiB FAT16 is rejected by `mkfs.vfat` itself ("too small or too large"). |
| Dolphin headless + SD mount | **PASS** — baseline DOL boots in Wii mode under `dolphin-emu-nogui -p headless -v Null`; IOS log shows `/dev/sdio/slot0` opened, "SD card is inserted and initialized", and two `DMA Read 64 Block(s)` from the image (libfat reading the FAT16 volume). Evidence: `evidence/dolphin_smoke_baseline.log`, `evidence/dolphin_smoke_result.txt`. The base app then idles at its file menu (no autoboot), SD image unchanged. |
| Dolphin shutdown | **Verified quirk** — headless Dolphin 2503 ignores a single `SIGTERM` ("A signal was received. A second signal will force Dolphin to stop.") and keeps running. Always use `timeout -k <grace> <secs>` or kill the Dolphin PID directly; `tools/run_dolphin.sh` handles this and was tested for both sentinel-stop and timeout paths with no orphan left behind. |
| vbagx reference | **PASS** — HEAD `4d5b9984e9b7457c9146e37dace5b95fa04b73ef` (same SHA as the previous refresh; **its latest commit is dated 2026-10-09**, so the repo is active and a future clone will likely differ — pin the SHA). All §4.3 file:line citations re-checked at that SHA. `evidence/vbagx_ref_commit.txt`, `vbagx_jit_files.txt` (18 files), `vbagx_jit_grep.txt` (29 lines) written. §4.4 notes and §4.5 map **still required** before Phase 2. |
| mgba-suite-auto source | **PASS** — `src/main.c` has a 14-entry `suites[]`; Video has no `run` → `SKIP`; 13 `BEGIN`/`END: i/n` pairs then `ALL DONE`. Debug registers in `src/mgba.c`: `0x4FFF780` enable, `0x4FFF700` flags, `0x4FFF600` string. Results are also `savprintf`'d into SRAM (a second capture channel via NooDS's `.sav`). Release `latest` = "r101", published 2026-09-21. |
| Compiler macro check | **PASS** — `powerpc-eabi-g++ -mrvl -mcpu=750 -dM -E` predefines `PPC`, `__PPC__`, `_ARCH_PPC`. Use namespace `JitPpc`, never `PPC`. |
| G0 (JIT=0/1 builds), G1–G6 | **NOT_RUN** — no JIT code exists yet. |

Older draft figures (DOL hashes `2aacbc…`/`35481b…`, suite counts, `20s/60s`, `~5×`) remain unverified; do not use them as thresholds.

## 2. Mission and implementation order

Build a Wii PowerPC dynamic recompiler inside NooDS-Wii for **both ARM7 (ARMv4T) and ARM9 (ARMv5TE)**. Do not wrap the interpreter in a way that changes guest scheduling or pretends to be native translation. Keep a true interpreter-only build as the reference baseline; variants must use the same source and flags except `-DNOODS_JIT=0|1`.

### Phase 1 — 1:1 fallback JIT first

- One eligible guest dispatch maps to one cached native stub. The stub calls a NooDS-Wii helper with the **already fetched opcode**; the helper invokes the existing interpreter handler and returns its real cycle cost.
- The JIT path must not refetch the opcode, advance the guest PC twice, refill the pipeline twice, skip condition handling, desynchronize `pcData` (§7), or alter scheduler timing.
- Condition-false instructions stay on the interpreter fast path. Reserved-condition opcodes must preserve `handleReserved()` behavior (§7); do not blanket-ignore condition nibble `0xF`.
- This phase is a correctness/wiring baseline and may be slower than interpretation.

### Phase 2 — traces/blocks only after G-1 and Phase 1 gates

Adapt architecture from vbagx, not its source. vbagx's compiler is **Thumb-only** and single-CPU. Add NooDS-native block/trace translation incrementally with interpreter exits for anything unproven. Preserve dynamic bus/wait-state/prefetch timing.

## 3. Fresh checkout and workspace setup

Repository root is `/home/user/NooDS-Wii`. If it already exists, inspect `git branch --show-current` and `git status` first; expect branch `feature/arm-ppc-jit` at or after branch `feature/arm-ppc-jit`.

```bash
cd /home/user
# Only if NooDS-Wii does not already exist:
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd /home/user/NooDS-Wii
git cat-file -e 1c995b48c37ebf3645646968c416958f79264137^{commit}
git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
mkdir -p evidence deliverables tools nds gba
```

If the clone is fresh (no `tools/` directory), the three tested scripts described in §10 must be recreated from this guide's recipes — they live only on the work branch in the sandbox workspace, not upstream. The repo had no `.gitignore`; the branch adds one for `*.o *.elf *.dol *.map /nds/ /gba/` (build is in-tree).

## 4. Mandatory vbagx reference study (before Phase 2)

### 4.1 Fetch and record the actual checkout

```bash
mkdir -p /home/user/reference /home/user/NooDS-Wii/evidence
# Pin the SHA: the repo is actively developed (last commit 2026-10-09).
git clone --depth 1 https://github.com/dborth/vbagx.git /home/user/reference/vbagx
git -C /home/user/reference/vbagx rev-parse HEAD   # expect 4d5b9984e9b7457c9146e37dace5b95fa04b73ef
# If HEAD differs: git -C /home/user/reference/vbagx fetch --depth 1 origin 4d5b9984e9b7457c9146e37dace5b95fa04b73ef && git -C /home/user/reference/vbagx checkout FETCH_HEAD
git -C /home/user/reference/vbagx rev-parse HEAD > /home/user/NooDS-Wii/evidence/vbagx_ref_commit.txt
```

The clone is ~22 MB and is kept under `/home/user/reference/vbagx` (persists). Line citations below are valid only at `4d5b9984`.

### 4.2 Locate JIT files

```bash
cd /home/user/reference/vbagx
grep -rlZ -iE 'jit|dynarec|recompil|codegen|emit(ppc|_ppc)?|trampoline' source/vba/gba | xargs -0 -n1 echo \
  > /home/user/NooDS-Wii/evidence/vbagx_jit_files.txt
grep -rn -iE 'class .*Jit|struct .*Jit|JIT_ARENA_SIZE|HASH_TABLE_SIZE|SMC_MAP_SIZE|JIT_TRACE_MAX' source/vba/gba \
  > /home/user/NooDS-Wii/evidence/vbagx_jit_grep.txt
```

JIT sources at `source/vba/gba/`: `JIT.h`, `JITCache.{h,cpp}`, `JITCompiler.cpp`, `JITPPCEmitter.h`, `JITTrampoline.S`, `JITDifferential.{h,cpp}`, `JITDebugStateLog.{h,cpp}`. Write `evidence/vbagx_jit_notes.md` and `evidence/vbagx_arch_map.md` before Phase 2 (still **absent**).

### 4.3 Verified vbagx facts (re-checked at `4d5b9984`)

- **Scope:** `JIT.h:11-18` — micro-JIT for GBA **THUMB** runs, explicitly not ARM state.
- **Dispatch/length:** `JIT.h:69` `JIT_TRACE_MAX_INSTRUCTIONS 42`; `JIT.h:110` `JITCompileThumbTrace`; `JITCompiler.cpp:2255-2271` unsupported opcode → `JIT_LOG_BAILOUT(... BAILOUT_UNSUPPORTED_OPCODE)` and trace ends.
- **Lookup:** `JITCache.h:113-123` `getBlock()` hashes `((pc >> 1) ^ (pc >> 13)) & (HASH_TABLE_SIZE - 1)`, compares `startPC`; direct-mapped, no chaining.
- **Wii arena:** `Makefile.wii:53-59` `JIT_ARENA_MB ?= 16` (also sizes a linker-reserved MEM1 block via `wii_mem.ld`); `JITCache.h:55-61` `JIT_ARENA_SIZE`, `HASH_TABLE_SIZE 65536`, `SMC_MAP_SIZE 65536` (1 KiB pages over 64 MiB). `JITCache.cpp:89-101` bump allocator calls `flushCache()` on exhaustion (its comment still says "512KB" — stale; trust the Makefile).
- **SMC:** `JITCache.h:15-19,61,128-143`, `JITCache.cpp:154-164,261-320` — page flags + per-page block lists; guard aimed at GBA EWRAM/IWRAM; invalidation patches the block entry and unlinks it.
- **Registers/flags:** `JITCompiler.cpp:249-305` lazy LRU allocation of guest R0-R14 into PPC R15-R28, R29 = PC; `JITPPCEmitter.h:48-90` full map: R14 = GBA register array base, R30 = read table, R6 = packed N/Z/C/V. Flags computed when the instruction updates them (not fully lazy).
- **Linking:** `JITCache.cpp:192-249` lookup/linker stub patches returning branches to cached targets.
- **Fallback:** unsupported code returns to the C++ Thumb interpreter loop (`GBA-thumb.cpp:1377-1495`, `#if VBA_JIT`).
- **I-cache:** `JITCache.cpp:253-257,288-296` `DCStoreRange` / `ICInvalidateRange`.
- **Trampoline:** `JITTrampoline.S:73-131` `mflr 0; stwu 1,-128(1); stmw 14,8(1)` … `lmw 14,8(1)` — saves LR and r14-r31 in a 128-byte frame.
- **Licensing:** no root license file in the checkout; README calls it GPL. Study architecture only; do not copy source.
- **Performance claims** (180 fps / 25 MIPS / ~5×): not corroborated; not a G5 target.

### 4.4 Required 10-point extraction checklist

Complete each in `evidence/vbagx_jit_notes.md` with file:line citations and paraphrase: (1) dispatch unit and all trace terminators; (2) hash input and collision policy vs arena exhaustion; (3) arena allocation/exhaustion; (4) SMC flags/registry/page size/write hooks/invalidation patching; (5) PPC GPR map, allocation, spill/reload, fixed registers; (6) NZCV computation and laziness; (7) direct linking and post-patch coherency; (8) trampoline frame, LR, nonvolatile saves; (9) unsupported-instruction handoff and cycle/resume-PC reporting; (10) code flush / icache maintenance.

### 4.5 NooDS mapping and non-goals

In `evidence/vbagx_arch_map.md`: NooDS-native code and real interpreter ABI only; ARM+Thumb on ARM7+ARM9; cache identity must include guest PC, ISA mode, and CPU; partition per CPU or key by CPU; shared NDS memory written by either CPU/DMA must invalidate all affected caches; audit every write path (NooDS has per-CPU `writeMap7/9A/9B` tables, see §7, but DMA/HLE/DLDI paths must be checked separately); do not port GameCube ARAM paging, GBA PPU/APU, or vbagx build conventions.

## 5. Acceptance gates (in order)

All gates are targets. Record logs and exact inputs under `evidence/`.

0. **G-1 — Reference study.** `vbagx_jit_notes.md` and `vbagx_arch_map.md` complete and citing `4d5b9984`. **Pending.** Required before Phase 2.
1. **G0 — Build.** `make JIT=0` and `make JIT=1` both link; `BLOCKS=1` → `-DNOODS_JIT_BLOCKS=1`. Use separate object dirs keyed by `JIT`+`BLOCKS` (the base builds `.o` files in-tree next to sources — JIT=0/1 builds **will** clobber each other until the Makefile is changed). Add `.S` sources explicitly (`C_SOURCES` wildcard is `*.cpp` only). Two clean reproducible builds; record compiler versions, sizes, hashes, map evidence. Baseline reference hash for the *unmodified* base is `cd48c6a4…` (§1); a `JIT=0` build with a modified Makefile/sources need not match it, but must match itself across clean rebuilds.
2. **G1 — PPC encoder.** Check every emitter form against the installed `powerpc-eabi-as`/`objdump` (both present after bootstrap), then disassemble an in-situ emitted stub. Target 44/44 via `tools/check_g1_encoder.sh` + `tools/test_encoder.cpp` (to implement).
3. **G2 — Differential parity.** Interpreter vs JIT on `rockwrestler.nds` (45 frames) and the auto suite (50/60/120 frames incl. pool wraps). Zero real mismatches for cycles, CPU state, memory dumps, framebuffers. Missing captures = `NOT_RUN`.
4. **G3 — Auto suite parity.** 13 `END: X/Y`, one `SKIP` (Video), `ALL DONE`, parsed from the current run. Matching baseline failures OK; mismatches not.
5. **G4 — Phase 1 performance baseline.** Same host/Dolphin/ROM/settings/run length; report direction honestly. `Core::endFrame()` does not expose guest cycles/frame.
6. **G5 — ARM7 blocks** behind `-DNOODS_JIT_BLOCKS=1`; ARM and Thumb; G2/G3 parity; measured improvement vs local Phase 1.
7. **G6 — ARM9 parity** with ARMv5TE; CP15 exits to interpreter; G2/G3 plus an NDS9-heavy ROM with source/hash recorded.
8. **Deliverables.** `deliverables/jit.dol(.sha256)`, `deliverables/interp.dol(.sha256)`, optional documented `ref.dol`, `handoff.md`, real evidence per gate. `deliverables/base_1c995b4_unmodified.dol` already exists as the unmodified-base artifact (not a JIT=0 build).

## 6. Environment and toolchain

### Host dependencies (verified install)

```bash
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="$PATH:/usr/sbin:/sbin:/usr/games"   # mkfs.vfat and dolphin-emu-nogui live there
```

`dolphin-emu` (≈290 packages with deps) took a few minutes; the light packages ≈ 10 s. Verify with `dolphin-emu-nogui --version` (→ `Dolphin [master] 2503`), `mcopy -V`, `mkfs.vfat --help`. Dolphin's headless run of the base DOL used ≈ 270 MB RSS and ~1 core.

### devkitPPC / libogc (verified, scripted)

Run `sudo tools/bootstrap_toolchain.sh` (default prefix `/opt/devkitpro`; cache `DKP_CACHE=/tmp/dkp_pkgs`). It writes `bootstrap_result.txt` with `PASS`/`FAIL`. Facts it encodes:

- Mirror: `https://wii.leseratte10.de/devkitPro/` only (never `pkg.devkitpro.org`). Directory names contain spaces and are URL-encoded, e.g. `devkitPPC/r50%20%282026-05-03%29/`.
- Packages are `.pkg.tar.zst` rooted at `opt/devkitpro/…` (extract with `--strip-components=2` into the prefix): `devkitppc-binutils-2.46.0-1-linux_x86_64`, `devkitppc-gcc-16.1.0-1-linux_x86_64` (r50 dir); `devkitppc-newlib-4.6.0.20260123-4-any` (**`devkitPPC/r49 (2026-01)/` dir** on the mirror, URL-encoded as `r49%20%282026-01%29/`); `devkitppc-rules-1.2.1-1-any` (`devkitPPC/devkitppc-rules/`; provides `wii_rules`, `base_rules`); `libogc-3.1.0-1-any` (`libogc/libogc_3.1 (2026-05-03)/`); `libfat-ogc-2.1.0-4-any` (`libfat/libfat_2.1.0/`); `gamecube-tools-1.0.7-1-linux_x86_64` (`other-stuff/gamecube-tools/`; provides `tools/bin/elf2dol`); `general-tools-1.4.4-1-linux_x86_64`. The `devkitPPC-r50-1-any` meta-package is empty (dependency list only). SHA-256s are pinned in the script.
- **The Wii linker scripts (`rvl.ld` + `libogc_common.ld`):** GCC's `-mrvl` plus libogc's `rvl.specs` (`-dT rvl.ld`) require `rvl.ld` and `libogc_common.ld`. They are packaged in `devkitppc-crtls-2.1.0-1-any.pkg.tar.zst`, available directly on the mirror at `https://wii.leseratte10.de/devkitPro/devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst` (SHA-256 `5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd`) as well as from GitHub `https://github.com/devkitPro/devkitppc-crtls` (tag `v2.1.0` = commit `11ee160d81704ddf695ed58012f1376768ccb8fc`). Tag `v1.0.0` (the version named in the r50 buildscripts) lacks `libogc_common.ld` and the link fails with `undefined reference to __ppc_excpt_buf` and `cannot find entry symbol _start`. The crtls package installs `rvl.ld`, `libogc_common.ld`, `gcn.ld`, and `ogc.ld` to `devkitPPC/powerpc-eabi/lib/`; `newlib` supplies `crt0.o`.
- `devkitppc-mn10200-binutils` (listed as a meta dependency) is not needed to build NooDS-Wii.
- The base `Makefile` hard-codes `export DEVKITPRO := /opt/devkitpro`, includes `$(DEVKITPPC)/wii_rules`, and invokes bare `elf2dol` from `PATH`. Build with: `export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:$PATH; make -j2`.
- Base flags (verbatim): `-O2 -DENDIAN_BIG -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions`; libs `-lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm` from `libogc/lib/wii`. Stay at `-O2`. Build emits one harmless assembler warning (`setting incorrect section attributes for .rodata`).
- `PPC` is predefined by this compiler → encoder namespace `JitPpc`.

## 7. Interpreter and guest-CPU contracts (re-verified in the pinned base)

- Dispatch tables `armInstrs[0x1000]`, `thumbInstrs[0x400]` (`NooDS-Wii/interpreter.h:95-96`). `runOpcode()` (`interpreter.cpp:224-248`) shifts `pipeline[0]=pipeline[1]`, advances `*registers[15]` by 2/4, refills `pipeline[1]`, then dispatches.
- **`pcData` fast fetch:** the refill is `(((*registers[15] += N) & 0xFFE/0xFFC) && pcData) ? U8TO16/32(pcData += N, 0) : getOpcode16/32()`. `pcData` (`interpreter.h:74`) is a cached host pointer into `core->memory.readMap7` / `readMap9A` (4 KiB pages, `getOpcode*` at `interpreter.cpp:252-263`). Any JIT branch/exit must keep `pcData` consistent (simplest: go through `flushPipeline()` or reset `pcData = nullptr`).
- `flushPipeline()` (`interpreter.cpp:320-331`) sets PC to instruction address +2/+4 before refill; at handler entry visible `r15` = instruction address +4 (Thumb) / +8 (ARM).
- ARM condition switch (`interpreter.cpp:242-245`): case 0 → return 1 cycle; case 2 → `handleReserved(opcode)`; default → handler. `handleReserved()` (`interpreter.cpp:424+`) handles ARM9 BLX-immediate (`(opcode & 0xE000000) == 0xA000000`), HLE IRQ return (`opcode == 0xFF000000` when `bios`), and DLDI HLE opcodes (`DLDI_START/INSERT/READ/WRITE/CLEAR/STOP` → `bx(14)`). Keep all of these on the interpreter path.
- ARM9 handlers for CLZ, QADD-family (`clampQ()`, `interpreter_alu.cpp:65`), enhanced multiplies, BLX, CP15 MRC/MCR (`interpreter_transfer.cpp:1174,1185` → `core->cp15.read/write`) already exist. Do not duplicate them.
- `Interpreter` mixes `public:` (`.h:30`) and `private:` (`.h:70`) members → not standard-layout; `registers[32]` (`.h:77`) is a banked **pointer** table. Derive offsets from a live object via a friend/accessor and assert 16-bit D-form range.
- Memory maps: `memory.h:40-45` `readMap9A/9B/7[0x100000]`, `writeMap9A/9B/7[0x100000]` — per-CPU 4 KiB page pointer tables (ARM9 has two read/write map variants). Natural SMC/write-hook points, but confirm DMA/HLE/DLDI stores also go through them before relying on that.
- `Core::endFrame()` (`core.cpp:349`) sets `running = 0`, bumps `fpsCount`; no guest cycle count.
- `main.cpp`: libogc 3.x `KThread` API (`KThreadPrepare(&emulatorThread, EmulatorThreadMain, …)` at `:568`), ROM handoff via `static std::string romToLoadPath` + `static volatile bool triggerRomLoad` (`:153-154`), `fatInitDefault()` at `:538`, MEM2 arena via `SYS_GetArena2Lo/Hi` (`:64-65`, relevant when placing a code pool). No autoboot logic.

## 8. JIT design, PPC ABI, and encoding

(Design proposals; nothing here exists in code.)

- **Phase 1 cache/pool:** direct-mapped 8192 entries per CPU tagged by guest PC/mode/opcode + nonzero epoch; 4 MiB `.bss` (or MEM2) word pool, max stub 160 words; on wrap flush all stubs and bump the epoch. Make code executable with `dcbst; sync; icbi; sync; isync` per written range (compare libogc `DCStoreRange`/`ICInvalidateRange`).
- **Phase 2:** hash-indexed block table + bounded arena, per-CPU or CPU-keyed; SMC map only over writable+executable memory; cap trace length/compile latency/emitted size; end traces on branches/exceptions/unsupported/unmodeled state changes; CP15 always bails. Track hit rate, collisions, flushes, SMC invalidations in `evidence/`.
- **Bridge ABI (`jit_bridge.S`, to write):** document `jit_enter(void *code, Interpreter *cpu)` argument registers, frame, LR save, callee-saved set. If `r31` holds `Interpreter*`, save/restore the caller's `r31`; keep `r2`/`r13` reserved; never `r0` as a D-form base. Explicit exit thunk; `mtctr/bctrl/bctr`. vbagx's 128-byte `stmw 14` frame is a reference, not the NooDS ABI.
- **Fallback stub:** pass opcode + `Interpreter*` to the C++ helper, return its real cycles, exit through the bridge.
- **NZCV mapping (to verify with the G1 oracle):** N←CR0.LT (mfcr bit 31→CPSR 31), Z←CR0.EQ (bit 29→30), C←XER.CA (29→29), V←XER.OV (30→28). Harvest immediately; preserve untouched flags; Q (bit 27) is separate and ARM9-only.
- **Encoder traps:** verify every form against `powerpc-eabi-as`/`objdump`; carry/overflow semantics of `addc/adde/subfc/subfe`; branch displacement origin; zero-shift carry; never truncate guest `r15`; never suppress all `opcode >> 28 == 0xF`.


### 8.1 Architectural decisions (evaluated & adopted)

1. **JIT Code Buffer Placement (MEM2 Arena)**:
   - MEM1 (24 MiB total, ~16 MiB usable) hosts the base `.dol`, `.text`, `.data`, `.bss`, stack, and OS structures. Putting a 4–8 MiB JIT cache in MEM1 risks heap starvation or collision.
   - MEM2 (64 MiB total, ~50+ MiB free) is managed in NooDS-Wii via `Noods_MEM2_Alloc()` (`main.cpp:72-80`).
   - The JIT code cache and stub pools will be allocated from MEM2 at initialization via `Noods_MEM2_Alloc()`. Broadway PowerPC branch instructions (`b`, `ba`, `bl`) have a ±32 MiB displacement (`LI` field 24 bits << 2); relative branches between code in MEM1 and MEM2 or within MEM2 can use direct branches if within ±32 MiB, or register indirect / CTR branches (`mtctr` / `bctr`) across the 0x80000000 <-> 0x90000000 boundary.

2. **Dual-Channel Autoboot & Test Output (SD + OSReport)**:
   - **Autoboot**: Checked right after `fatInitDefault()` in `main.cpp` before UI loop; inspects `sd:/autoboot.txt` or falls back to suite paths, setting `romToLoadPath` and `triggerRomLoad = true`.
   - **Logging**: Dual reporting — both write/flush to `sd:/gbaout.log` (persistent on the SD image for `mcopy` extraction) AND mirror directly to console via libogc's `OSReport()` (captured in Dolphin's stdout log via `-C Logger.Logs.OSREPORT=True`). This enables real-time sentinel termination by `run_dolphin.sh` without waiting for process exit.

## 9. Verification rig and test assets

### 9.1 ROM assets — hashes verified 2026-10-09

```bash
cd /home/user/NooDS-Wii && mkdir -p nds gba
curl -sSfL 'https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds' -o nds/rockwrestler.nds
curl -sSfL 'https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba' -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
```

- `nds/rockwrestler.nds`: 39,433 bytes; `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`.
- `gba/suite.gba` (r101, 2026-09-21): 524,288 bytes; `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495`.
- Both links are mutable; re-verify on every download (`tools/build_sd_image.sh` refuses mismatches). The suite auto-runs at boot (no menu input); `suite.gba` tests GBA/ARM7 only.

### 9.2 SD image (verified)

`tools/build_sd_image.sh IMG [--autoboot 'sd:/nds/rockwrestler.nds'] [--no-suite] [--no-nds]` — creates a 128 MiB whole-file FAT16 volume (no MBR), directories `nds gba noods noods/bios noods/gba noods/nds`, copies the suite to `gba/suite.gba noods/suite.gba noods/gba/suite.gba suite.gba noods/suite-auto.gba` and rockwrestler to `nds/… noods/… noods/nds/… /…`, optionally writes `::/autoboot.txt`, and prints `mdir`. Fails closed if `IMG` exists. Manual equivalent is the explicit `dd`/`mkfs.vfat -F 16`/`mmd`/`mcopy` sequence; use `mcopy -i IMG ::/path out` per file for extraction (no wildcards). Stop Dolphin before extracting; record byte counts and hashes.

### 9.3 Autoboot and mGBA logging (must implement; absent in base)

1. One-line `sd:/autoboot.txt` or `sd:/noods/autoboot.txt` overrides defaults.
2. Else search suite paths (`sd:/noods/suite.gba`, `sd:/noods/gba/suite.gba`, `sd:/gba/suite.gba`, `sd:/suite.gba`, `sd:/noods/suite-auto.gba`) then NDS paths (`sd:/nds/rockwrestler.nds`, `sd:/noods/rockwrestler.nds`, `sd:/rockwrestler.nds`, `sd:/noods/nds/rockwrestler.nds`).
3. Set `romToLoadPath`/`triggerRomLoad` the same way the menu does (`main.cpp:928-931`), from the emulator thread's perspective; test GBA and NDS selection.

Implement the mGBA debug channel (`0x4FFF780` enable, `0x4FFF700` flags/level, `0x4FFF600` string buffer) in NooDS memory I/O, append records to `sd:/gbaout.log`, flush on complete records, and compute `mgba_summary.json` from observed counters only. Optionally also capture the SRAM `.sav` text. A missing `ALL DONE`/log/JSON is a failed run.

### 9.4 Dolphin headless runner (verified)

`tools/run_dolphin.sh DOL SD_IMAGE SECONDS [OUT_DIR] [SENTINEL_REGEX]` → `OUT_DIR/dolphin.log` (NUL/ALSA stripped) and `OUT_DIR/result.txt` (`STATUS=PASS|FAIL|HANG|NOT_RUN`, rc, elapsed, DOL/SD hashes before/after, boot/SD-init/DMA counters). Verified behaviours:

- Flags accepted by Dolphin 2503 `--help`: `-u USER`, `-e FILE`, `-C System.Section.Key=Value`, `-v BACKEND`, `-a AUDIO`, `-p PLATFORM`, `-m`, `-s`, `-n`. **No** `--batch`/`--exit-on-frame`.
- Working invocation core: `dolphin-emu-nogui -p headless -u "$USERDIR" -v Null -a HLE -C Dolphin.General.WiiSDCard=True -C "Dolphin.General.WiiSDCardPath=$SD" -C Dolphin.General.WiiSDCardAllowWrites=True -C Dolphin.Core.CPUCore=1 -C Dolphin.Core.CPUThread=False -C Dolphin.Core.SyncGPU=True -e "$DOL"`.
- Console logging needs `-C Logger.Options.WriteToConsole=True -C Logger.Options.Verbosity=4` (1=notice … 4=info, 5=debug) plus per-channel `-C Logger.Logs.IOS_SD=True`, `BOOT`, `OSREPORT`, `OSREPORT_HLE`, `CORE`. `Logger.Options.WriteToFile=True` did **not** produce a `Logs/` file in the temp user dir; rely on stdout capture.
- Proof the image was mounted: log lines `Opening /dev/sdio/slot0`, `IOCTL_GETSTATUS. Replying that SD card is inserted and initialized`, and `IOS_SD ... DMA Read N Block(s)`. Boot proof: `Booting from executable: <dol>`. The `keys.bin could not be found` and ALSA warnings are harmless.
- Shutdown: a lone `SIGTERM` does not stop headless Dolphin; use `timeout -k 10 SECS` and kill the Dolphin PID (child of `timeout`) — never only the `timeout` PID, or an orphan survives holding the `flock`. Exit code is normally 137 after forced kill; treat it as `HANG` unless a sentinel/artifacts prove completion.
- For the suite, run with sentinel `ALL DONE` once OSREPORT/SD logging exists, then extract `gbaout.log` with `mcopy`.

## 10. Tooling status

Present and tested on the branch (`tools/`):

1. `bootstrap_toolchain.sh` — pinned mirror packages + crtls v2.1.0, SHA-256 verified, link smoke test, `PASS/FAIL` result file.
2. `build_sd_image.sh` — §9.2 image with ROM hash checks and optional autoboot override.
3. `run_dolphin.sh` — serialized, bounded, sentinel-aware headless runner with honest status.

Still to implement: `clone_vbagx_reference.sh` (optional; §4.1 commands suffice), `compare_runs.py`, `run_suite_gba.sh`, `check_g1_encoder.sh` + `test_encoder.cpp`, and (only if needed) `elf2dol.py` — gamecube-tools' `elf2dol` works, so `elf2dol.py` should not be needed. Every script must fail closed and distinguish `PASS`/`FAIL`/`NOT_RUN`.

## 11. Evidence layout

Present now (committed):

```text
evidence/
  vbagx_ref_commit.txt            4d5b9984e9b7457c9146e37dace5b95fa04b73ef
  vbagx_jit_files.txt             18 paths
  vbagx_jit_grep.txt              29 lines
  g0_baseline_build.log           unmodified-base make log (first attempt with crtls v1.0.0 link failure, then success)
  g0_baseline_hashes.txt          cd48c6a4… .dol / d9476188… .elf
  toolchain_bootstrap_result.txt  PASS + versions
  dolphin_smoke_baseline.log      101 lines, IOS_SD mount evidence
  dolphin_smoke_result.txt        STATUS=PASS (sentinel = SD initialized)
deliverables/
  base_1c995b4_unmodified.dol(.sha256)
```

Still required: `vbagx_jit_notes.md`, `vbagx_arch_map.md`, `g0_build.log` (JIT=0/1), `g1_*`, `g2_*`, `gbaout_*.log`, `mgba_summary_*.json`, `g3_*`…`g6_*`, `deliverables/jit.dol`, `deliverables/interp.dol`, `handoff.md`. Only add evidence for runs actually executed.

## 12. Reproducible order of work

1. Re-provision the sandbox (§13), confirm `git -C /home/user/NooDS-Wii log --oneline -1` shows `6ea88fd` or later on `feature/arm-ppc-jit`, and confirm `make` reproduces `cd48c6a4…` before touching source.
2. Complete §4.4 notes and §4.5 map (G-1) — needed before Phase 2, can be interleaved with Phase 1.
3. Makefile: add `JIT`/`BLOCKS` variables → `-DNOODS_JIT`/`-DNOODS_JIT_BLOCKS`, per-config object dirs, explicit `.S` rule, distinct output names. Verify `JIT=0` output is byte-identical across clean builds.
4. Implement headless autoboot + mGBA debug channel + SD log (§9.3); prove with `run_dolphin.sh … 'ALL DONE'` on the interpreter build and extract `gbaout.log`. This is the first real G3 baseline.
5. Implement Phase 1 fallback JIT + bridge; G1 encoder oracle; G2/G3 differential parity; G4 timing.
6. Phase 2 ARM7 blocks (G5), then ARM9 (G6).
7. Final deliverables, hashes, `handoff.md`, per-gate `PASS/FAIL/HANG/NOT_RUN` summary.

## 13. Integrity and persistence rules

- Do not claim PASS until the command ran and artifacts were inspected. Reproducible hashes are not differential correctness.
- Cite vbagx details at the recorded SHA or say "not located".
- **What persists:** `/home/user/**` (repo with branch, `tools/`, `evidence/`, `deliverables/`, `nds/`, `gba/`, `reference/vbagx`, this file). **What does not:** `/opt/devkitpro`, apt packages, `/tmp/*`, build objects (git-ignored). Re-provisioning at session start:

```bash
sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools
cd /home/user/NooDS-Wii && sudo tools/bootstrap_toolchain.sh          # ~5 s; prints PASS
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/usr/games:$PATH
make -j2 && sha256sum NooDS-Wii.dol                                   # expect cd48c6a4… on the unmodified base
```

- If `NooDS-Wii` is missing, follow §3 and recreate the three §10 scripts from this guide; if `reference/vbagx` is missing, re-clone and pin per §4.1; if ROMs are missing, §9.1.
- Keep the workspace small (currently ≈ 28 MB); do not store toolchain packages under `/home/user`.

---

*End of guide. The pinned NooDS-Wii base builds and boots under headless Dolphin with a mounted SD image in this sandbox; everything JIT-related is still to be written.*
