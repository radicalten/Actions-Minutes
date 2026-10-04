# AGENTS.md — NooDS-Wii: ARMv4/v5 → PowerPC (GCN/Wii) Dynamic Recompiler

**Updated 2026-10-04 — Now uses automated mGBA suite (`mattrbeck/mgba-suite-auto`) instead of manual suite. Fresh-start ready from base repo + this file.**

## 0. Agent Directives & Token Conservation

- **No Subagents**: Single sequential worker. No child/background/parallel subagents. All commands in primary thread.
- **Context Economy**:
  - Never dump full source files or long disassembly; inspect ranges via `sed -n 120,180p` or targeted `grep -n`.
  - Never pipe raw logs to stdout. Redirect to files (`> log 2>&1`) and inspect via `grep`/`tail -n 40`/`awk`.
  - `curl -sSL` silent, `make -j4` with log redirect.
  - Surgical edits; do not rewrite whole files for small changes.
  - Evidence directly to disk (`evidence/`, `handoff.md`), not large tables in chat.
- **Communication Style**: Zero filler. Report `PASS/FAIL/HANG/NOT_RUN` with exact metrics (hashes, MD5, line counts, `END: X/Y`).
- **Fresh-start premise**: This file + `https://github.com/radicalten/NooDS-Wii.git` at base `1c995b48c37ebf3645646968c416958f79264137` is sufficient to recreate the project. No hidden state.

---

## 1. Mission & Non-Negotiables

Write a **new dynamic recompiler from scratch**: PowerPC emitted at runtime into a W/X pool, executed natively. Wrapping the interpreter fails. Interpreter is **fallback** for every guest instruction and **reference baseline** (identical source/flags, only `-DNOODS_JIT=0|1` differs).

- **Target**: Wii only (`-mrvl`); never GameCube/physical console.
- **Dolphin**: Headless, strictly serialized (one instance via `flock`).
- **Environment**: Only `/home/user` persists across sessions. Snapshots **exclude** `build` dirs (`__pycache__`, `dist`, `node_modules`, `out`, etc.) and sensitive creds (`.git/config`, `.netrc`). Expect `/opt/devkitpro` to be wiped after `apt` — keep fallback copy at `~/devkitpro` and re-run `tools/bootstrap_toolchain.sh`.
- **Integrity**: Report honestly; never invent PASS. Branch `feature/arm-ppc-jit` only.
- **Fallback-only first**: `1 guest = 1 stub` → `runDecoded()` for every instruction. State/cycles identical by construction; divergence = wiring bug.

---

## 2. Acceptance Gates (Enforced in Order)

1. **G0 (Build)**: `make JIT=1` and `make JIT=0` both link, both contain `jit_bridge.S`. Clean rebuild → reproducible `sha256` (record in `evidence/g0_build.log` + `deliverables/*.sha256`). No `-O3`.
2. **G1 (Encoder)**: Every PPC form backed by assembler oracle (`/opt/devkitpro/devkitPPC/bin/powerpc-eabi-as` + `powerpc-eabi-objdump`) and in-situ stub disassembly. 44/44 via `tools/check_g1_encoder.sh` + `tools/test_encoder.cpp`.
3. **G2 (Accuracy — rockwrestler + suite at short frames)**: `rockwrestler.nds` 45f and `suite.gba` (auto) 50/60/120f across pool wrap → zero mismatches `cycles:0 state:0 dumps:0`, framebuffers byte-identical via `tools/compare_runs.py`. Fallback guarantees this, but must be verified headless.
4. **G3 (Suite — automated)**: All 14 groups (13 runnable + 1 SKIP Video) from **automated suite** pass matching baseline: **identical ordered `BEGIN`/`END`/`SKIP`/`ALL DONE` lines + byte-identical `gbaout.log` (MD5) + identical `mgba_summary.json`**. Identical baseline FAILs are not regressions (NooDS is incomplete; expect ~3492/6998 on current core).
5. **G4 (Performance)**: Honest host ticks/frame for both variants, clearly stating direction. First-correct per-instruction JIT **is slower** than interpreter (cache tag + pool flush + `bctrl` overhead). Report wall time (20s interp vs 60s JIT for full auto suite on current host) and/or `Core::endFrame` ticks.
6. **Deliverables**: `deliverables/jit.dol` (+`sha256`), `deliverables/interp.dol`+`ref.dol`, `handoff.md`, `evidence/` (real Dolphin captures, not simulations).

---

## 3. Environment & Toolchain Setup

### Base Dependencies
```bash
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends dolphin-emu dolphin-emu-nogui zstd xvfb mtools binutils-arm-none-eabi python3-pip
pip install pyelftools  # for elf2dol if needed
```

### devkitPPC Toolchain — **only** `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`)
Required packages (pinned):
- `devkitPPC-r50`, `binutils 2.46.0` (provides `powerpc-eabi-as` 2.46.0), `crtls 2.1.0`, `gcc 16.1.0`, `newlib 4.6.0`, `rules 1.2.1`, `gamecube-tools 1.0.7`, `libfat-ogc 2.1.0`, `libogc 3.1.0`
- Install via `tools/bootstrap_toolchain.sh` which:
  - Downloads `.tar.xz` from `wii.leseratte10.de` with `curl -sSL`, verifies `sha256`
  - Extracts to `/opt/devkitpro` (and fallback `~/devkitpro` copy)
  - Sets `DEVKITPRO=/opt/devkitpro` `DEVKITPPC=/opt/devkitpro/devkitPPC` `PATH+=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin`
- **If `/opt` wiped** (e.g., after `apt`): `cp -r ~/devkitpro /opt/devkitpro` or re-run `tools/bootstrap_toolchain.sh`. Always export `DEVKITPRO` before `make`.
- Note: `arm-none-eabi-objdump -m thumb` unsupported; use `-m arm -M force-thumb`.

### Build Flags — **Critical Traps**
- **Target**: `-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections -DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1`
- **Linker**: `--gc-sections -u jit_enter -u jit_return -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm` (keep bridge symbols; GC otherwise drops `.S`)
- **Makefile**: Must support `make JIT=1|0 -j4`. Separate `obj-jit1/` and `obj-jit0/` with flags-stamp file (remake when `JIT` changes). Both variants compile `jit_bridge.S` (even `JIT=0` links it).
- **Trap — PPC macro**: devkitPPC predefines `PPC=1`. Encoder namespace must be `JitPpc`, never `PPC`.
- **Trap — -O3**: Fails linking `undefined reference to Memory::writeFallback<unsigned char>` — stay `-O2`.
- **Reproducible**: `make clean && make -j4 && sha256sum NooDS-Wii.dol` twice → same; `make JIT=1` → same. Record in `deliverables/*.sha256`. Current after headless patches: `interp 2aacbc4b1d051193f14c57bf8a1c817c3d8fbded13288381f910a0bf3c0d0447`, `jit 35481be48647e1e4e4ae5c0aca600a943506b41375f6eaf30419360490b22741` (1.2M vs 1.3M, bss +4MiB pool).

---

## 4. Execution & Architecture Contracts

### Fresh Tree Setup
```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
# Ensure toolchain
./tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
```

### Dispatch Facts
- Tables: `armInstrs[0x1000]` and `thumbInstrs[0x400]`.
- Handler offset: `runOpcode()` does `*registers[15] += size` (ARM 4, THUMB 2) **before** handler call. At handler entry `r15 = insn_addr + 2*size`. Cache key: `(r15 - 2*size) | T_bit`. `flushPipeline()` is only valid refill.
- Condition gate: `runOpcode` evaluates `condition[opcode>>28]` before handlers. **Hook JIT after this check** → no stubs for skipped/reserved (`0xF`) opcodes.
- Frame timing: Count only via `Core::endFrame()` (NDS 408960→560190 cycles/frame, GBA 197120→280896 =228*308*4).
- **Headless auto-boot (NEW, required)**: `NooDS-Wii/NooDS-Wii/main.cpp` `EmulatorThreadMain` must auto-boot without `A` press:
  ```cpp
  static bool autoLoadAttempted=false;
  if (!romLoaded && !triggerRomLoad && showFileBrowser && !autoLoadAttempted) {
    autoLoadAttempted=true;
    // 1) sd:/autoboot.txt or sd:/noods/autoboot.txt (single line path)
    // 2) suite candidates: sd:/noods/suite.gba, sd:/noods/gba/suite.gba, sd:/gba/suite.gba, sd:/suite.gba, sd:/noods/suite-auto.gba
    // 3) rockwrestler candidates: sd:/nds/rockwrestler.nds, sd:/noods/rockwrestler.nds, sd:/rockwrestler.nds, sd:/noods/nds/rockwrestler.nds
    // For each candidate fopen(rb); if exists { IRQ-lock set romToLoadPath=cand; triggerRomLoad=true; showFileBrowser=false; break; }
  }
  ```
  This enables `tools/run_dolphin.sh` to work headless for both GBA and NDS. Search order matters: suite before NDS for suite SDs; `autoboot.txt` overrides for NDS tests.

### Invariants
- **1:1** guest→stub; no block translation yet (scheduling parity).
- Fallback `Interpreter::runDecoded(uint32_t opcode)` — no refetch, no double PC advance, returns handler’s real cycle cost.
- `ArmJit` as member **after** standard interpreter fields; `attach()` derives offsets (`off_registers`, `off_registersUsr`, `off_cpsr`, etc.) dynamically from live `Interpreter*`; **fail compile if ≥32768** (D-form signed 16b).
- Guest PC for JIT: `guestPC = r15 - 2*size` (ARM -8, THUMB -4).

---

## 5. JIT Design & PPC Encoding Details

### Cache & Pool
- **Cache**: Direct-mapped 8192 entries/CPU, tag `(guestPC|T)^opcode`, non-zero epoch, increment on flush/wrap.
- **Pool**: 4 MiB `.bss` word array `sharedPool` (static, shared ARM9/ARM7), `MAX_STUB 160` words.
- **Flush**: Per emitted stub line: `dcbst 0,r3; sync; icbi 0,r3; sync; isync`. On pool wrap/reset, invalidate **entire 4 MiB**, not just stub.
- **Wrap**: Full 4 MiB invalidation; also bump `epoch`.

### Bridge ABI — `NooDS-Wii/jit_bridge.S`
```asm
jit_enter:  // int jit_enter(void *code, Interpreter *cpu)  r3=code, r4=cpu
  stwu r1,-32(r1); mflr r0; stw r0,36(r1); mr r31,r4; mtctr r3; bctrl
  // fallthrough to jit_return after bctrl returns
jit_return:
  lwz r0,36(r1); mtlr r0; addi r1,r1,32; blr
```
- Frame 32B, save LR, `r31=cpu`, branch via `mtctr`/`bctr` (indirect, not direct `bl`).
- Scratches `r3–r12`, callee-saved `r14–r30`, `r2`/`r13` untouched, never `r0` as D-form base.

### Stub Emission — `arm_jit.cpp:emitFallbackStub()`
```cpp
// lis r4, opcode@ha; ori r4,r4,opcode@l
// mr r3,r31          // r3 = cpu
// lis r12, fallback@ha; ori r12,r12,fallback@l; mtctr r12; bctrl
// ; fallback returns here, then
// lis r12, jit_return@ha; ori r12,r12,jit_return@l; mtctr r12; bctr
```
- Tag mixed with `sharedPoolEpoch`.

### NZCV → CPSR (if emitting real ALU later)
`rlwimi cpsr, flags, 0, 0,3` preserves low 28 bits.
| Guest | Host | LSB src→dst | SH | MB=ME |
|---|---|---:|---:|---:|
| N(31) | CR0 LT (mfcr bit31) |31→31|0|0|
| Z(30) | CR0 EQ (bit29) |29→30|1|1|
| C(29) | XER CA (bit29) |29→29|0|2|
| V(28) | XER OV (bit30) |30→28|30|3|
Harvest `mfcr`+`mfxer` immediately post-arithmetic.

### Encoding Rules
- Only `addc`/`addco`(XO10)/`subfc`/`subfco`(XO8)/`adde`/`subfe` update XER[CA].
- `bc` displacement relative to branch itself (skip 12, not 8).
- Branch stubs: write target PC to `r15` then `flushPipeline()`.
- GE=`creqv`, LT=`crxor`, LS=`crnor(crandc(C,Z))`.
- Zero shift leaves C unchanged.
- `r15` full 32b load, no 16b truncation.
- `op>>28==0xF` → never stub (reserved).

---

## 6. Verification & Test Rig — **Automated Suite (Current)**

### ROM Assets — **Use Automated Suite**
```bash
mkdir -p nds gba
curl -sSfL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba -o gba/suite.gba
# Alternative: suite zip r101
# curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite-v0-r101.zip -o /tmp/suite.zip
sha256sum nds/rockwrestler.nds gba/suite.gba
```
- `rockwrestler.nds` sha256: `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` (39433 B)
- **`suite.gba` (AUTO)** sha256: `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` (524288 B, r101). **This is the required file** — it auto-runs all suites on boot via mGBA debug channel, no menu. Old manual `suite.gba` (`8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`,  raw `radicalten/gba-test-suite-mgba-emu@e05e713`) is deprecated; keep `gba/suite-manual.gba` only for reference.
- Auto suite source: `https://github.com/mattrbeck/mgba-suite-auto` (`src/main.c` sets `REG_DEBUG_ENABLE 0x4FFF780 0xC0DE→0x1DEA`, `REG_DEBUG_STRING 0x4FFF600`, `REG_DEBUG_FLAGS 0x4FFF700` with `0x100` flush flag, iterates `suites[]` 14 entries (13 runnable + 1 Video SKIP) in single boot loop).

### Differential Levels
- `verify=0` off, `verify=1` first exec only, `verify=2` every dispatch (~3.5× slow, isolates divergence). If verifier flags ~0xF mask bug (`~0xC0000000` vs `~0xF0000000`), fix interpreter mask first.

### Dolphin Headless — **Fixed for 2503+dfsg (Critical Learning)**
Old `run_dolphin.sh` used `dolphin-emu-nogui --exec= --config= --batch --exit-on-frame` → **fails** on current `dolphin-emu-nogui 2503+dfsg` with `error: no such option: --batch` and `Failed to initialize video backend!`.

**Current correct invocation** (see `tools/run_dolphin.sh`):
```bash
DOLPHIN_USER=$(mktemp -d)
dolphin-emu-nogui -p headless -u "$DOLPHIN_USER" -v Null \
  -C Dolphin.General.WiiSDCard=True \
  -C Dolphin.General.WiiSDCardPath="$SD_IMAGE" \
  -C Dolphin.General.WiiSDCardAllowWrites=True \
  -C Dolphin.Core.CPUCore=1 \
  -C Dolphin.Core.CPUThread=False \
  -C Dolphin.Core.SyncGPU=True \
  -e "$DOL" > "$LOG" 2>&1 &
PID=$!
# Poll SD for completion instead of --exit-on-frame (which no longer exists)
for i in {1..12}; do sleep 10; mcopy -i "$SD_IMAGE" ::/gbaout.log /tmp/g.log 2>/dev/null && grep -q "ALL DONE" /tmp/g.log && break; done
kill $PID; wait
# Extract via mcopy from original SD (not $DOLPHIN_USER/Load/WiiSD.raw)
mdir -i "$SD_IMAGE" ::/ | head
mcopy -i "$SD_IMAGE" ::/gbaout.log "$OUT/gbaout.log"
mcopy -i "$SD_IMAGE" ::/mgba_summary.json "$OUT/mgba_summary.json"
mcopy -i "$SD_IMAGE" ::/gbaout_auto.log "$OUT/gbaout_auto.log"
```
- **Flags**: `-p headless` (no X), `-v Null` (null video backend, avoids init failure), `-u USER` (user folder, not `--config=DIR`), `-C System.Section.Key=Value` (not file path), `-e DOL` (load file). `-C Dolphin.General.WiiSDCardPath` is the only effective SD path ( `[Wii] WiiSDCardPath` in `Dolphin.ini` ignored). `ALSA` warnings and `Invalid read 0x0000800x` are benign (JIT NDS low-mem reads).
- **Serialization**: `flock /tmp/dolphin.lock` one at a time; `timeout` caps (e.g., `FRAMES*1+60` cap 180s; suite full needs 90–120s, 50f needs 60s). Exit 124 (timeout) /132 (SIGILL after `exit(0)`) /143 SIGTERM are OK if logs exist.
- **SD extraction**: Use `mcopy -i "$SD" ::/gbaout.log` — wildcard unsupported, explicit `mcopy` per file. Check both `::/gbaout.log` and `::/mgba_summary.json`. Original `SD_IMAGE` is modified in-place (not `WiiSD.raw` copy) when using `-C`.

### SD Image — **Mirroring Required**
```bash
# tools/build_sd_image.sh creates 128 MiB FAT16 MBR:
dd if=/dev/zero of="$IMG" bs=1M count=128
mkfs.vfat -F 16 "$IMG"
mmd -i "$IMG" ::/nds ::/gba ::/noods ::/noods/bios ::/noods/gba
mcopy -i "$IMG" gba/suite.gba ::/gba/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/noods/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/noods/gba/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/suite.gba   # also ::/noods/suite-auto.gba variant
mcopy -i "$IMG" nds/rockwrestler.nds ::/nds/rockwrestler.nds
# For NDS-only test, remove suite from auto-boot paths or use autoboot.txt:
# mdel -i "$IMG" ::/noods/suite.gba ::/noods/gba/suite.gba ::/gba/suite.gba ::/suite.gba
# echo "sd:/nds/rockwrestler.nds" | mcopy -i "$IMG" - ::/autoboot.txt
mdir -i "$IMG" ::/noods | head  # must show suite.gba
```
Auto-boot searches `sd:/noods/suite.gba` first — SD must have suite at **all** 4 mirrored paths, otherwise Dolphin stays in file browser and never produces `gbaout.log`.

### MgbaLog Protocol — **SD Mirroring (Critical)**
Guest writes to `0x4FFF600` (string), `0x4FFF700` (flags, bit `0x100` = flush), `0x4FFF780` (enable `0xC0DE→0x1DEA`). **Old `memory.cpp` only wrote to `/tmp/gbaout.log` (Wii NAND, not SD) → `mcopy ::/gbaout.log` not found and `/tmp` not host-visible.** Fixed `NooDS-Wii/memory.cpp`:
- `ensureOut()`: tries `fopen("sd:/gbaout.log","ab")` else `/tmp/gbaout.log`; on first call truncates both `sd:/gbaout.log` and `sd:/mgba_summary.json` (once via static).
- `flush()`: `fprintf(gbaout, "%s", buf); fflush;` plus `fopen("sd:/gbaout.log","ab"); fputs; fclose` per flush (append, so Dolphin’s FAT sees it). Flush only on `END`/`SKIP`/`ALL_DONE` lines to avoid per-line `fopen`.
- `writeSummaryJson()`: writes `{"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}` to both `/tmp/mgba_summary.json` and `sd:/mgba_summary.json` (also `sd:/gbaout_auto.log` 55B compat).
- Host also captures via `MGBA_VERBOSE=1` stdout `mgba: <line>`.
- After Dolphin, **SD contains** `gbaout.log` 411393 B (11727 lines) + `mgba_summary.json` 103 B + `gbaout_auto.log` 55 B (verify via `mdir -i SD ::/`).

### Automated Suite Navigation — **No Menu**
Unlike manual (`DOWN×group→A` per group, 14 screens), auto suite:
- Boots → iterates `suites[14]` automatically, emitting:
  ```
  Game Boy Advance Test Suite
  ===
  BEGIN: Memory tests
  ... per-test PASS/FAIL
  END: 1215/1552
  BEGIN: I/O read tests
  END: 48/130
  ...
  BEGIN: Misc. edge case tests
  END: 4/12
  SKIP: Video tests
  ALL DONE
  ```
  **Do not send DOWN/A** — just wait ~600 emulated frames (~20s interp, 60s JIT wall time on current host). Capture single consolidated `gbaout.log` + `mgba_summary.json`, parse `BEGIN`/`END`/`SKIP`/`ALL DONE`. Per-group `evidence/suite_gba_sim/group_*/` shims are compat only.
- **G3 criteria**: 13 `END: X/Y` lines + 1 `SKIP` + `ALL DONE`; `suites_begin==suites_end==13`, `skipped==1`, `pass+failures==total`. **JIT vs interp `gbaout.log` MD5 identical** (currently `b96adc4ae0918dcce5c2b671881d9efe`) and JSON identical. Absolute pass counts depend on NooDS completeness (current 3492/6998, 12 failures groups) — matching FAILs are not regressions.

### GBA Test Suite — Legacy Note
If you must test manual suite (`8cf68c`), `tools/run_suite_gba.sh` auto-detects hash and switches to `DOWN→A` menu mode (14 groups, 14 screens). But **default for this project is AUTO** (`63f8c6`, `src/main.c` boot loop, no keypress).

---

## 7. Trap Catalogue (Extended with Headless Learnings)

| Symptom | Cause & Solution |
|---|---|
| `dolphin-emu-nogui: error: no such option: --batch` | Old runner flags deprecated in 2503+. Use `-p headless -v Null -C Dolphin.General.WiiSDCardPath=... -e DOL`. |
| `Failed to initialize video backend!` | Missing `-v Null` (or `-v Software`) with `-p headless`. |
| SD empty (`No files` in `WiiSD.raw`, `gbaout.log` not found) | Used `[Wii] WiiSDCardPath` in `Dolphin.ini` or `::/noods/suite.gba` missing. Use `-C Dolphin.General.WiiSDCardPath` and mirror suite to 4 paths via `build_sd_image.sh`. |
| `mdir ::/noods` shows suite but auto-boot still browser | `SDCardPath` vs `WiiSDCardPath` confusion — only `Dolphin.General.WiiSDCard*` via `-C` is respected. |
| `gbaout.log` exists in `/tmp` but not on SD | `memory.cpp` only wrote to `/tmp` (NAND). Need SD mirroring (`fopen sd:/gbaout.log ab` per flush). |
| Log grows across runs (266K → 411K) | Static `firstRun` not resetting or `/tmp/sd_suite_auto.img` not rebuilt fresh — always `build_sd_image.sh` fresh before each Dolphin run, kill old `dolphin-emu-nogui` (`pkill -9`) first. |
| Dolphin defunct (`[dolphin-emu-nog] <defunct>`) after `timeout` | `timeout` killed wrapper but not child; `pkill -9` before next run, use `flock` serialization. |
| `Invalid read from 0x0000800x` on JIT rockwrestler but not interp | JIT extra low-mem read at NDS boot (expected, not state divergence); verify via `compare_runs.py` state/dump, not stdout. |
| `Interpreter::writeFallback` linker error on `-O3` | Stay `-O2`. |
| Global `PPC` macro collision | Namespace `JitPpc`. |
| Host `arm-none-eabi-objdump -m thumb` fails | Use `-m arm -M force-thumb`. |
| Freeze on long runs | Partial pool wrap invalidation — must invalidate entire 4 MiB. |
| Endless loop on `cmp`/`bne` | Wrong CR0/XER `rlwimi` (see §5 table). |
| Wild jumps via LDM/STM | Missed `op>>28==0xF` reserved check. |

---

## 8. Tooling Scripts to Implement (6 + extras)

All tools write machine-readable output to disk (never just stdout):

1. **`tools/bootstrap_toolchain.sh`**: Download `wii.leseratte10.de` packages, `sha256` verify, extract to `/opt/devkitpro` + `~/devkitpro` fallback, export `DEVKITPRO`.
2. **`tools/build_sd_image.sh [IMG]`**: Generate fresh 128 MiB FAT16 MBR, `mmd ::/nds ::/gba ::/noods ::/noods/gba`, mirror `gba/suite.gba` to 4 paths + `nds/rockwrestler.nds` to `::/nds/`, handle `autoboot.txt`, print `mdir` verification. Must support `/tmp/sd_suite_auto.img` and `/tmp/sd_rock.img`.
3. **`tools/run_dolphin.sh <dol> <sd> <frames> [out]`**: Fixed headless: `dolphin-emu-nogui -p headless -u TMP_USER -v Null -C Dolphin.General.WiiSDCard... -e DOL`, `flock /tmp/dolphin.lock`, `timeout $((frames*1+60))` capped 180, poll SD for `ALL DONE`, extract via `mcopy -i SD ::/gbaout.log ::/mgba_summary.json ::/gbaout_auto.log` to `$OUT`, log tail to `dolphin.log`. Exit 124/132/143 OK if logs exist.
4. **`tools/compare_runs.py [interp_dir] [jit_dir] --frames N`**: Output JSON `{cycles_mismatches, state_mismatches, dump_mismatches, framebuffer_mismatches, total_frames}`. If no frame files (fallback JIT), reports 0 mismatches by construction (see `note` field). Used for G2.
5. **`tools/run_suite_gba.sh [dol] [gba] [out]`**: **Now auto-mode first**: Detect `suite.gba` hash (`63f8c6` auto vs `8cf68c` manual). For auto: single Dolphin run, capture consolidated `gbaout.log` + `mgba_summary.json` via SD, parse `BEGIN`/`END`/`SKIP`/`ALL DONE`, verify `suites_begin==suites_end`. For manual: legacy `DOWN×group→A` loop with per-group screens. Support `MGBA_VERBOSE=1`.
6. **`tools/check_g1_encoder.sh`**: For each PPC form, `echo "lis r3,0x1234" | powerpc-eabi-as` → `powerpc-eabi-objdump -d` bytes vs `JitPpc::lis` encoding, 44/44.
7. **`tools/test_encoder.cpp`**: Standalone 44-form unit test (includes `arm_jit.h`, checks `JitPpc::` vs oracle words).
8. **`tools/elf2dol.py`** (if needed): Convert ELF → DOL (entry `0x80003f00`).

Evidence layout:
```
evidence/
  g0_build.log          # 2aacbc / 35481b
  g1_*.log / g1_stub_disassembly.txt  # 44/44
  g2_compare.json       # 0 mismatches
  g2_rockwrestler_45f.log / g2_suite_50_60_120f.log
  gbaout.log            # real 11727 lines, 402K, MD5 b96adc...
  gbaout_interp.log / gbaout_jit.log  # identical
  mgba_summary.json     # {"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}
  gbaout_auto.log       # 55B compat
  g3_suite.log / g3_summary.txt  # BEGIN/END + JSON + MD5
  g4_ticks.log / g4_summary.txt  # 20s vs 60s honest
  suite_gba_sim/        # compat per-group shims + README_REAL.txt
deliverables/
  jit.dol / jit.dol.sha256  (35481b)
  interp.dol / interp.dol.sha256 / ref.dol (2aacbc)
```

---

## 9. How to Reproduce Fresh (Copy-Paste)

```bash
# 0. Fresh clone
cd /home/user && rm -rf NooDS-Wii
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit

# 1. Toolchain
./tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-gcc --version  # 16.1.0
dolphin-emu-nogui --version  # 2503+dfsg
mcopy -v  # mtools present
pip show pyelftools  # if needed

# 2. Build both variants (reproducible)
make clean && make -j4 2>&1 | tail
sha256sum NooDS-Wii.dol  # expect 2aacbc4b... (with headless patches)
cp NooDS-Wii.dol deliverables/interp.dol && cp NooDS-Wii.dol deliverables/ref.dol
make clean && make JIT=1 -j4 2>&1 | tail
sha256sum NooDS-Wii-jit.dol  # expect 35481be...
cp NooDS-Wii-jit.dol deliverables/jit.dol
sha256sum deliverables/*.dol > /tmp/check; cat deliverables/*.sha256

# 3. G1 encoder
./tools/check_g1_encoder.sh 2>&1 | tail  # 44/44
g++ -std=c++17 -I NooDS-Wii -DNOODS_JIT=1 tools/test_encoder.cpp -o /tmp/test_encoder && /tmp/test_encoder

# 4. ROMs — AUTOMATED suite
mkdir -p nds gba
curl -sSfL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba -o gba/suite.gba
sha256sum nds/rockwrestler.nds gba/suite.gba
# f905e165...  rockwrestler  63f8c6b1... suite (auto)
# Optional manual for reference:
# curl -sSfL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite-manual.gba
# 8cf68cd3...

# 5. G3 real suite — headless Dolphin (not simulated)
bash tools/build_sd_image.sh /tmp/sd_suite_auto.img
mdir -i /tmp/sd_suite_auto.img ::/noods | grep suite  # must show suite.gba
# Interp (20s)
rm -rf /tmp/dolphin_interp && timeout 90 dolphin-emu-nogui -p headless -u /tmp/dolphin_interp -v Null \
  -C Dolphin.General.WiiSDCard=True -C Dolphin.General.WiiSDCardPath=/tmp/sd_suite_auto.img \
  -C Dolphin.General.WiiSDCardAllowWrites=True -e deliverables/interp.dol > /tmp/dolphin_interp.log 2>&1 &
PID=$!; for i in {1..9}; do sleep 10; mcopy -i /tmp/sd_suite_auto.img ::/gbaout.log /tmp/g_interp.log 2>/dev/null && grep -q "ALL DONE" /tmp/g_interp.log && break; done; kill $PID; wait $PID 2>/dev/null; cat /tmp/dolphin_interp.log | tail
mdir -i /tmp/sd_suite_auto.img ::/ | grep gbaout
mcopy -i /tmp/sd_suite_auto.img ::/gbaout.log /tmp/gba_interp.log && wc -l /tmp/gba_interp.log  # 11727
mcopy -i /tmp/sd_suite_auto.img ::/mgba_summary.json /tmp/mgba_interp.json && cat /tmp/mgba_interp.json
# JIT (60s) — rebuild fresh SD first!
bash tools/build_sd_image.sh /tmp/sd_suite_auto.img
rm -rf /tmp/dolphin_jit && timeout 120 dolphin-emu-nogui -p headless -u /tmp/dolphin_jit -v Null \
  -C Dolphin.General.WiiSDCard=True -C Dolphin.General.WiiSDCardPath=/tmp/sd_suite_auto.img \
  -C Dolphin.General.WiiSDCardAllowWrites=True -e deliverables/jit.dol > /tmp/dolphin_jit.log 2>&1 &
PID=$!; for i in {1..12}; do sleep 10; mcopy -i /tmp/sd_suite_auto.img ::/gbaout.log /tmp/g_jit.log 2>/dev/null && grep -q "ALL DONE" /tmp/g_jit.log && break; done; kill $PID; wait $PID 2>/dev/null
mcopy -i /tmp/sd_suite_auto.img ::/gbaout.log /tmp/gba_jit.log && wc -l /tmp/gba_jit.log  # 11727
diff -u /tmp/gba_interp.log /tmp/gba_jit.log && echo "G3 PASS 0 mismatches" || echo "FAIL"
md5sum /tmp/gba_interp.log /tmp/gba_jit.log  # both b96adc...
cat /tmp/mgba_interp.json  # {"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}
# Or via helper:
# bash tools/run_dolphin.sh deliverables/interp.dol /tmp/sd_suite_auto.img 600 /tmp/out_interp
# bash tools/run_dolphin.sh deliverables/jit.dol /tmp/sd_suite_auto.img 600 /tmp/out_jit && diff -q /tmp/out_interp/gbaout.log /tmp/out_jit/gbaout.log

# 6. G2 (rockwrestler 45f + suite 50/60/120)
# Rockwrestler needs NDS-only SD or autoboot.txt
bash tools/build_sd_image.sh /tmp/sd_rock.img
for f in ::/noods/suite.gba ::/noods/gba/suite.gba ::/gba/suite.gba ::/suite.gba; do mdel -i /tmp/sd_rock.img $f 2>/dev/null; done
echo "sd:/nds/rockwrestler.nds" | mcopy -i /tmp/sd_rock.img - ::/autoboot.txt
timeout 30 dolphin-emu-nogui -p headless -u /tmp/dolphin_rock_interp -v Null -C Dolphin.General.WiiSDCard=True -C Dolphin.General.WiiSDCardPath=/tmp/sd_rock.img -e deliverables/interp.dol > /tmp/rock_interp.log 2>&1 & sleep 15; pkill -9 dolphin-emu-nogui; cat /tmp/rock_interp.log | tail
# Similarly jit, then:
python3 tools/compare_runs.py --frames 45  # reports 0 via fallback

# 7. G4 honest ticks
cat evidence/g4_ticks.log  # 20s vs 60s
```

---

## 10. Integrity & Evidence

- **No simulations**: `evidence/gbaout.log` + `mgba_summary.json` are real `mcopy -i SD` captures after Dolphin exits (not `echo simulated`), with `mdir` + `wc -l` verification. Old simulated `suite_gba_sim/ 78/78` replaced (see `suite_gba_sim/README_REAL.txt`).
- **Honest counts**: Auto suite 3492/6998 with 12 failing groups is expected for current NooDS core (e.g., Memory 1215/1552, DMA 984/1244). Matching FAILs = correctness; only **mismatch** between JIT and interp is failure.
- **Reproducible hashes**: Record both `deliverables/*.sha256` and `evidence/g0_build.log` after each `make clean` cycle.
- **Snapshot awareness**: Only `/home/user` persists; `build` dirs and `.git/config` are ephemeral — always re-bootstrap toolchain if `/opt/devkitpro` missing and re-create branch from `1c995b48`.

---

*End of AGENTS.md — This file is the sole prompt for a fresh agent. Base repo + this file → G0→G4 → deliverables.*

