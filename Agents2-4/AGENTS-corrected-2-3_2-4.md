# AGENTS.md — NooDS-Wii native JIT, continuation from a frozen Stage 1

**Read this first.** You are picking up a continuation of a prior session on the NooDS-Wii Wii emulator.
This file is self-contained. It preserves the newer Stage-1 handoff (`AGENTS-1.md`) and restores
usable devkitPPC/Dolphin setup instructions from `AGENTS-2.md`, checked on 2026-10-14. The newer
source-state guidance takes precedence; its claim that Dolphin cannot run does **not**.

**One-line status:** The newer handoff reports Stage 1 (PC-destination load terminators, ARM7 IPCSYNC
fast path, generalized STRH stores) as implemented, interpreter-parity verified, and frozen. Continue
from that state when the actual package is supplied; do not replace it with the older source baseline.

**Verification scope:** Only the two Markdown files were attached for this correction. `repo/`,
`harness/`, and `handoff/` were not present. A separate pristine checkout at the specified base commit
`92e634aa696e1ea7b4cf6159110c75b01bfd6b4d` was used to verify tool installation, Wii builds, and
Dolphin execution. Those fresh checks prove the workflow works, **not** that the missing Stage-1
patch or binaries pass it. Stage-1 results below are carried from `AGENTS-1.md`, not re-measured here.
See §§13–15 for the verification records and their separate scopes.

---

## 1. The task in the user's terms

Improve the NooDS-Wii dynamic recompiler for the Wii's PowerPC 750CL (Broadway), for **both** guest
CPUs — ARM7 (ARMv4T) and ARM9 (ARMv5TE) — in **both** ARM and Thumb modes. `.nds` performance is the
priority; `.gba` support and the ROM browser must keep working.

Standing rules the user set (do not violate):

- **Never commit or push.** Leave changes in the working tree; the user uploads them.
- **Guest correctness first.** Never trade correctness for speculative speed. Every JIT change must
  pass interpreter-parity before it is considered real.
- Do not break `.gba` support or the ROM browser (see §6 carried results and §12 validation matrix).
- Use one sequential worker. Serialize Dolphin runs; `tools/run_dolphin.sh` uses `flock`.
- Do not dump full Dolphin logs to the terminal; use bounded `grep`/`tail`/`sed` on cleaned logs.
- **Never present harness timing or ms/frame as real Wii FPS.** The user runs the emulator on real
  hardware (SD card + `sd:/apps/<app>/boot.dol`) and has repeatedly said harness improvements did not
  translate into a perceptible FPS change. The old handoff mentions a confusing "Emu 4 / Display 11"
  report — the wording is ambiguous and **must not** be used to claim a speedup or slowdown.
- Keep progress updates short while working. Report skipped/unrun tests explicitly, never silently.
- Keep generated SD images and any proprietary BIOS/firmware out of uploads.

## 2. Expected Stage-1 package layout

This describes the original Stage-1 package, not files guaranteed to exist in a fresh workspace.
Inspect the workspace before using these paths; stop and request missing Stage-1 artifacts rather
than pretending a pristine clone contains the frozen edits.

| Path | What it is |
|---|---|
| `handoff/` | **The upload deliverables.** `handoff/NooDS-Wii/NooDS-Wii/{arm_jit.cpp,arm_jit.h,ipc.h}` = the modified sources at repo-relative paths; `handoff/NooDS-Wii/jit.dol` = rebuilt browser JIT; `handoff/noods_jit_stage1.patch` = the same change as a git patch; `handoff/README.md` = sha256s + apply instructions. |
| `repo/` | Full checkout of `radicalten/Actions-Minutes2` (HEAD `92e634a`) with the Stage-1 edits already applied. Project root `repo/NooDS-Wii/`, sources `repo/NooDS-Wii/NooDS-Wii/`. ROMs: `nds/rockwrestler.nds`, `gba/suite.gba`. Tools: `repo/NooDS-Wii/tools/`. |
| `harness/` | A **PPC-Linux + qemu harness** that runs the actual emulation core (real `arm_jit.cpp`) natively on big-endian PowerPC. Sources of truth for reproducing the carried harness results in §6: `build.sh`, `src/harness_main.cpp`, `shims/`, prebuilt stripped `bin_interp/`, `bin_jit/`, `bin_jit_hot/`, and all logs in `runs/`. |
| `uploads/` | Attached/historical handoff docs. In this correction: `AGENTS-1.md` and `AGENTS-2.md`. Older source/backlog details need rechecking; devkitPPC/Dolphin setup remains usable. |

Notes:
- Preserve existing work. Record `git status --short` and HEAD before doing anything; do not clone
  over an existing tree, reset it, or force it to match an old status listing.
- If the package lacks usable Git history, reconstruct in a **separate** directory at the exact
  base commit. A shallow clone of the moving default branch is not a reliable way to obtain it:

  ```bash
  set -euo pipefail
  cd /home/user  # repo_base and handoff/ below are siblings here
  test ! -e repo_base
  test -f handoff/noods_jit_stage1.patch
  git clone https://github.com/radicalten/Actions-Minutes2.git repo_base
  git -C repo_base checkout --detach 92e634aa696e1ea7b4cf6159110c75b01bfd6b4d
  git -C repo_base rev-parse HEAD
  # Only with the actual Stage-1 patch present, and only on this pristine base:
  git -C repo_base apply --check ../handoff/noods_jit_stage1.patch
  git -C repo_base apply ../handoff/noods_jit_stage1.patch
  ```

  Confirm the patch paths and resulting sources before using `repo_base` as the working tree.
  Copying the handoff sources is another option; verify hashes against `handoff/README.md`.
- The original Stage-1 status reported modified `NooDS-Wii/NooDS-Wii/{arm_jit.cpp,arm_jit.h,ipc.h}`,
  modified `NooDS-Wii/jit.dol`, and untracked `NooDS-Wii/evidence/toolchain_bootstrap_result.txt`.
  Treat that as a historical baseline, not a command to discard other work. `interpreter.cpp` was
  pristine; keep temporary tracing out of final deliverables.

## 3. Environment

**devkitPPC and Dolphin are usable in this sandbox.** Not preinstalled or not included in an
upload does not mean unavailable. On 2026-10-14, Debian 13.7 x86_64 accepted the host packages,
the pinned devkitPPC bootstrap passed, and the headless Dolphin runner completed NDS/GBA tests.

The original package did not bundle the toolchain. Install it outside `/home/user`, preferably
`/opt/devkitpro`; files there and apt-installed dependencies do not persist in workspace snapshots.
Recheck/install them in each fresh environment. Keep downloads outside the upload package too.

Set paths to the **actual Stage-1 tree**, after verifying that it exists:

```bash
set -euo pipefail
export WORKSPACE=/home/user
export REPO="$WORKSPACE/repo"  # adjust if the patched tree is elsewhere
export PROJ="$REPO/NooDS-Wii"
export SRC="$PROJ/NooDS-Wii"
test -f "$PROJ/tools/bootstrap_toolchain.sh"

sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential git curl dolphin-emu zstd xvfb xauth mtools dosfstools \
  binutils-arm-none-eabi python3-pyelftools
export PATH="/usr/sbin:/sbin:/usr/games:$PATH"
dolphin-emu-nogui --version
```

Dolphin's executables are under `/usr/games`; `mkfs.vfat` is under `/usr/sbin`. A PATH omission is
not an installation failure. Debian's `python3-pyelftools` is importable with `/usr/bin/python3`;
`/usr/local/bin/python3` may not see it. Use `/usr/bin/python3` when a step needs `elftools`.

There are complementary workflows: PPC-Linux/qemu for rapid core parity, devkitPPC for real Wii
DOL builds, and Dolphin for DOL/SD/autoboot integration. The harness does not replace DOL validation.

### 3a. PPC-Linux/qemu harness (no devkitPPC needed) — source of the carried §6 parity results

Install harness dependencies separately from the Wii/Dolphin dependencies:

```bash
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  build-essential qemu-user-static g++-powerpc-linux-gnu binutils-powerpc-linux-gnu \
  python3 binutils-arm-linux-gnueabi zstd
command -v qemu-ppc-static powerpc-linux-gnu-g++ arm-linux-gnueabi-objdump
```

Fresh follow-up check: Debian's `qemu-user-static` compatibility package installs `qemu-user` and
provides `qemu-ppc-static` (QEMU 10.0.13). The actual packaged harness is still absent and was not
executed. If another host has only `qemu-ppc`, inspect its installation before adapting commands.

If supplied, the prebuilt binaries are intended to run directly under qemu:

```bash
cd "$WORKSPACE/harness"
qemu-ppc-static ./bin_jit/noods_host --rom "$PROJ/nds/rockwrestler.nds" \
    --frames 120 --checkpoints 30,60,120 --out runs/x.log --stats
python3 compare.py runs/nds_interp120.log runs/x.log    # prints MISMATCHES=<n>, STATUS=PASS/FAIL
```

`bin_jit` = current frozen sources, `JIT=1 BLOCKS=1` (+ `NOODS_JIT_IPCSYNC_FAST=1`, the default).
`bin_interp` = `JIT=0`. `bin_jit_hot` = same sources with `-DNOODS_JIT_HOTPC=1` (opt-in per-PC
fallback profiler). The original handoff describes all three as stripped and runnable; verify their presence first.

To rebuild the harness from source (≈1 min, needs `powerpc-linux-gnu-g++`):

```bash
cd "$WORKSPACE/harness"
bash build.sh interp            # -> bin_interp/noods_host
bash build.sh jit               # -> bin_jit/noods_host
EXTRA_DEFS=-DNOODS_JIT_HOTPC=1 bash build.sh jit bin_jit_hot
SRC=/absolute/path/to/NooDS-Wii/NooDS-Wii bash build.sh jit   # if the tree is not at ../repo/NooDS-Wii/NooDS-Wii
```

`build.sh` mirrors the Wii Makefile flags (`-O2 -DENDIAN_BIG -mcpu=750 -mhard-float …`); `ENDIAN_BIG`
is load-bearing — a build without it silently miscompiles guest memory accesses. Do not "fix" the
harness unless something in `shims/` is actually broken.

CLI: `--rom <nds> [--gba <gba>] --frames N [--checkpoints a,b,c] --out FILE [--stats] [--pool-words N]
[--mem-dump FILE] [--fb-dump FILE]`. For GBA runs use `--rom "" --gba "$PROJ/gba/suite.gba" …`.

### 3b. Real Wii builds (devkitPPC) — needed to hand the user a DOL

Bootstrap the pinned toolchain with sufficient permissions. Invoke tracked shell scripts via `bash`
(they need not have executable bits):

```bash
cd "$PROJ"
sudo env DEVKITPRO=/opt/devkitpro bash tools/bootstrap_toolchain.sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH"
grep '^STATUS=PASS$' evidence/toolchain_bootstrap_result.txt
powerpc-eabi-g++ --version

make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1  # autoboot-capable regression DOL
make clean JIT=1 BLOCKS=1 CONFIG_TAG=jit_browser
bash tools/build_browser_jit.sh                # -> jit.dol, NOODS_FORCE_BROWSER=1
sha256sum jit.dol
```

The bootstrap SHA-256-checks its downloads: devkitPPC r50 (GCC 16.1.0, binutils 2.46.0), newlib,
devkitppc rules/crtls, libogc 3.1.0, libfat-ogc, gamecube-tools (`elf2dol`), and general-tools.
It runs a Wii compile/link/DOL-conversion smoke test and records
`evidence/toolchain_bootstrap_result.txt`. Fresh check: `STATUS=PASS`, GCC 16.1.0,
AS 2.46.0.20260210, `elf2dol` present. Inspect the script in the working checkout before running it.

The carried Stage-1 browser hash is
`cf53857c4d8fe12e6b7c6fc07ce57d89e16f342247ea7062c429f836c7f83515`.
It belongs to the frozen patched tree, not the pristine verification clone. Re-hash after changes;
do not expect older/base-commit DOL hashes to match it.

The Makefile does not track changes to compiler/linker flags or toolchain versions as dependencies.
The browser helper reuses `build_jit_browser/`; changing flags alone can report "Nothing to be done"
and copy a stale DOL. Clean that **specific** configuration before the browser build, as above.
Likewise clean/re-tag each harness/Wii configuration after flag or toolchain changes; inspect real
compiler commands for `-DENDIAN_BIG`, the requested JIT switches, and browser/autoboot behavior.
Do not pass `CXXFLAGS=...` as a make command-line assignment that drops Makefile-added flags;
use the helper's environment convention and verify the effective command. Record custom flags.

Never use `make clean-all` casually (deletes named regression DOLs). The browser DOL ignores autoboot
files by design — use `NooDS-Wii_jit1_blocks1.dol` for scripted/autoboot runs, not browser `jit.dol`.

### 3c. Dolphin — available, headless DOL/SD validation

The installed Debian package is `dolphin-emu 2503+dfsg-1+deb13u1`. It includes
`dolphin-emu-nogui`; the verified runner uses `-p headless -v Null -a HLE`. This workflow does not
require a physical display, GPU rendering, Xvfb, or proprietary BIOS/firmware.

Use the checkout's existing scripts (see §12):
- `bash tools/build_sd_image.sh ...` creates the test SD image and checks ROM hashes.
- `bash tools/run_dolphin.sh DOL SD_IMAGE SECONDS OUT_DIR SENTINEL` runs bounded, serialized tests.
- `bash tools/run_g2_case.sh ...` creates autoboot settings and extracts checkpoint/memory/FB dumps.
- `bash tools/run_suite_gba.sh ...` runs the GBA suite through `ALL DONE`.

At verified base commit `92e634a`, `run_g2_case.sh` **and** `run_suite_gba.sh` already invoke both
child shell scripts with `bash`, although their tracked modes are `100644`. The older file's G3
exit-126 workaround is checkout-specific and is **not needed here**. If another checkout directly
executes non-executable children, inspect it and prefer invoking them via `bash`; do not blindly
change file modes or carry the obsolete bug forward.

Do not mark Dolphin G2/G3 `NOT_RUN` merely because dependencies are initially absent. Attempt the
setup and checks; report actual installation/runtime failures with logs. Tests not executed or
missing required evidence are `NOT_RUN`, never passes.

**Result interpretation:** For sentinel-based runs require runner `STATUS=PASS` and `SENTINEL_HIT=1`,
then comparator `status=PASS` and `mismatch_count=0`. Dolphin may be deliberately terminated after
its sentinel (e.g. `RC=143`); process exit code alone is not the oracle. `run_g2_case.sh` can exit 0
when dumps exist despite a failed runner; inspect `result.txt` separately. Missing checkpoints or
dumps are not success. Dolphin timings are not real-Wii FPS; real-Wii cache/hardware validation
remains separate.

## 4. Fast start: initial harness parity checks (runtime depends on setup)

```bash
set -euo pipefail
cd "$WORKSPACE/harness"
test -f compare.py
mkdir -p runs
# 1. smoke (interp + jit, same 5 frames)
qemu-ppc-static ./bin_interp/noods_host --rom "$PROJ/nds/rockwrestler.nds" --frames 5 \
    --checkpoints 5 --out runs/now_smoke_i.log
qemu-ppc-static ./bin_jit/noods_host    --rom "$PROJ/nds/rockwrestler.nds" --frames 5 \
    --checkpoints 5 --out runs/now_smoke.log --stats
python3 compare.py runs/now_smoke_i.log runs/now_smoke.log      # MISMATCHES=0
# 2. the real checks (expected: MISMATCHES=0 on every one)
qemu-ppc-static ./bin_interp/noods_host --rom "$PROJ/nds/rockwrestler.nds" --frames 120 \
    --checkpoints 30,60,120 --out runs/now_interp120.log
qemu-ppc-static ./bin_jit/noods_host    --rom "$PROJ/nds/rockwrestler.nds" --frames 120 \
    --checkpoints 30,60,120 --out runs/now_jit120.log --stats
python3 compare.py runs/now_interp120.log runs/now_jit120.log   # expect MISMATCHES=0, STATUS=PASS
```

Before accepting **any** change: rebuild `bin_jit` (`bash build.sh jit`), re-run the harness checks
recorded in §6 and the DOL validation matrix in §12, then update `handoff/` (§11).

## 5. The frozen change (Stage 1) — what and where

The following implementation details are **CARRIED** from `AGENTS-1.md`; verify them against the
actual patched sources before editing. They are not present merely because the base checkout exists.
The changes are reported in `SRC/arm_jit.cpp` (plus `arm_jit.h` / `ipc.h`):

1. **PC-destination load block terminator** — `isPcDestLoadTerminatorArm(op)` +
   `ArmPpcJit::emitPcDestLoadTerminatorArm(...)`. Native ARM `LDR/LDRB/LDRH PC,[...]` (pre-index,
   no writeback) compiled as a **block terminator** (computed branch / jump table). Fast path mirrors
   the interpreter's `ldrOf/ldrbOf/ldrhOf` (instruction-specific misalignment and T-bit behavior,
   inline pipeline refill when the target page is mapped and word-clean; otherwise that whole
   instruction falls back to the interpreter handler). Wired through `compileBlock`
   (count/nativeCount/prefixCycles/blockPure/hasTerm), `tryExecuteBlock`, and the `executeArmCore`
   Rd15 fallback gate.
2. **ARM7 IPCSYNC fast path** (`NOODS_JIT_IPCSYNC_FAST`, default 1) — `LDRH @ 0x04000180` reads the
   live `ipc.ipcSync[1]` via a new `friend class ArmPpcJit` in `ipc.h`; plus the ARM7 generic
   halfword-load gate in `isNativeLoadArm`.
3. **Generalized STRH stores** — `isNativeStoreArm` / `emitNativeStoreArm`: STRH pre/post-index,
   ±writeback, ±unshifted register offset, including `P=0,W=0` (NooDS's `strhPt*` post-updates Rn
   **even when W=0**; verified against the interpreter by parity). New `stwbrx` encoder `0x7D003D2C`
   (assembler rechecked: `stwbrx r8,0,r7` = `7d 00 3d 2c`). This is a **word** byte-reverse store;
   do not confuse it with `sthbrx r8,0,r7` = `7d 00 3f 2c`. Its use in the missing patched emitter
   remains carried; instruction encoding alone does not verify store width/addressing correctness.
4. Removed a duplicated HOTPC `fbSiteRecord` double-count. HOTPC profiling stays opt-in
   (`NOODS_JIT_HOTPC` default 0).

**Plain STR/STRB native acceptance is deliberately disabled — read §8 before touching stores.**

Other switches in `arm_jit.cpp` (status in the frozen tree): `NOODS_JIT_DISPSTAT_FAST=1`,
`NOODS_JIT_KEYINPUT_FAST=1` (inherited ARM9 MMIO fast paths), `NOODS_JIT_NATIVE_HALF_LOADS=0`,
`NOODS_JIT_POLL_SKIP` (off; regressed when tried).

## 6. Frozen Stage-1 results (carried from AGENTS-1.md; not re-run in this correction)

Paths `runs/...` in this section are relative to `harness/`; these logs were not attached.

Carried parity (interpreter vs JIT, `compare.py` → `MISMATCHES=0`, `STATUS=PASS` on all):

| Run | Frames / checkpoints | Log |
|---|---|---|
| rockwrestler.nds | 120 f, cps 30/60/120 | `runs/nds_jit_p0.log` |
| rockwrestler.nds | 300 f, cps 100/200/300 | `runs/nds_jit300_p0.log` |
| rockwrestler.nds dense sweep | 35 f, cps 1,2,3,5,8,11,14,17,20,23,26,29,32,35 | `runs/exp_p0.log` vs `runs/bis_interp.log` |
| suite.gba | 60 f, cps 20/60 | `runs/gba_jit_p0.log` |

JIT counters, 120 f rockwrestler, pre-change (`runs/nds_jit120.log`) → frozen (`runs/nds_jit_p0.log`):

| counter | before | after |
|---|---|---|
| `fbInsn7` (ARM7 fallback instr dispatches) | 2,050,058 | **558** |
| `fbInsn9` (ARM9 fallback instr dispatches) | 152,906 | **59,028** |
| `disp7` (ARM7 dispatch entries) | 6,455,359 | **4,443,987** |
| `blkInsn7` (instrs executed inside blocks) | 12,600,837 | **14,616,021** |
| `blkHit7` / `stubHit7` | 4,318,741 / 86,577 | 4,322,553 / 120,898 |
| `blkInsn9` / `disp9` | 40,080,444 / 13,596,142 | 40,180,401 / 13,501,823 |
| `blkFail7` / `blkFail9` | 0 | 0 |

`PERF` in `runs/nds_jit_p0.log`: ~24.6–25.0 ms/f harness (interpreter ~30.7–31.2 ms/f).
**Harness-only — never cite as Wii FPS.**

Provenance check: `runs/rebuild_check_120f.log` is a 120 f run from a **fresh rebuild of the packaged
sources** (`bash build.sh jit <tmp>`): parity `MISMATCHES=0` and `JIT_STATS` byte-identical to
`runs/nds_jit_p0.log`, so the shipped `bin_jit` and the shipped sources are the same build.

Real Wii targets rebuilt: `make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1` OK; browser DOL
`jit.dol` sha256 `cf53857c…f83515` (matches `handoff/`). On-Wii behavior of this build has **not**
been confirmed by the user yet — do not claim on-Wii numbers.

ROM hashes (for `tools/build_sd_image.sh`): `rockwrestler.nds`
`f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`; `suite.gba`
`63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495`.
Carried encoder oracle: `bash tools/check_g1_encoder.sh` → 44/44 PASS. The fresh pristine-base
check matched **52/52** forms, with required minimum 44/44, `STATUS=PASS` (see §13). Inspect the
actual output rather than treating the historical count as the exact current total. The tool needs
devkitPPC's `powerpc-eabi-as` and host `g++`; it writes `evidence/g1_encoder_oracle.log`.

## 7. Invariants you must preserve

The interpreter is the oracle. Preserve instruction-specific behavior, not a generalized ARM rule
that differs from the implementation. The base source confirms the interpreter/cache/scheduler
facts below; Stage-1-specific terminator integration still needs the missing patched source.

- A JIT block **enters with `R15 = instrPC + 8`** (ARM) and the block epilogue recomputes `pcData` /
  pipeline state; the interpreter handler slow path expects `r15Instr = instrPC + totalDelta + 8`
  before the load.
- `flushPipeline()`: ARM `r15 = (r15 & ~3) + 4`, Thumb `(r15 & ~1) + 2`, refills `pipeline[0]/[1]`.
  In the checked interpreter, `ldrOf` and `ldrbOf` with `Rd=15` apply
  `cpsr |= (val & !arm7) << 5`; `ldrhOf` with `Rd=15` **does not** apply that T-bit update.
  All three taken PC-load handlers return 5 cycles. Check other addressing forms individually.
- `Interpreter::condition` is a **lookup table**, not a function: 0 = condition false,
  1 = execute normally, 2 = reserved-condition handling (`cond=0xF`). Reserved does **not** mean
  always skip: `handleReserved()` handles BLX-immediate and HLE/DLDI cases. Preserve ARM7/ARM9
  distinctions and reserved dispatch; do not conflate it with an ordinary failed condition.
- PC-dest load terminator costs: **not-taken = linear path, +1 cycle; taken = +5 cycles.**
- MMIO fast paths are per-address guarded and only replace the load when the address matches; all
  other addresses keep the interpreter path. Keep ARM7/ARM9 differences intact.
- Bus/MMIO semantics, unaligned access handling, SMC invalidation and pipeline/flag exactness are
  non-negotiable; event boundaries (GPU/SPU/DMA scheduling) are where guest-visible timing comes from.
- Preserve the `tryExecuteBlock()` scheduler guard: the checked base refuses block entry when
  `cpu->cycles + scaledPrefix >= core->events[0].cycles`; ARM7 in NDS mode scales prefix cycles by 2.
  Recheck the patched implementation and preserve boundary refusal, not just approximate cycle counts.
- Broadway has incoherent split I/D caches. Every new emitted-code path must flush its written range
  through `flushCodeRange()` before execution (D-cache writeback, barriers, I-cache invalidation).
  Dolphin parity cannot prove real-Wii cache coherency.
- Follow the checked `jit_bridge.S` contract: reserve `r1` (stack), `r2` and `r13` (system/SDA
  anchors); `r31` holds `Interpreter*`; the bridge saves/restores `r28-r31`. Do not assume volatile
  scratch or CR/LR/CTR survives C-helper calls. Follow the exact emitter/bridge save discipline.
- Runtime code must target 32-bit Broadway/750CL: no AltiVec, `isel`, or 64-bit integer instructions.
  Do not widen a classifier without a matching correct emitter. Memory fast paths must retain
  address/mapping/width/alignment/MMIO/SMC guards and interpreter-compatible helper fallback;
  do not add unguarded raw guest-memory pointer access.
- Keep poll skipping off. `NOODS_JIT_IDLE_SKIP` is absent in the checked base, not an available toggle.
  Do not import older blanket bans on all branches/`Rd=15`: Stage 1 explicitly adds guarded
  terminators. Unsupported cases still fall back to the interpreter.

## 8. Parked: STR/STRB (do not re-enable without solving)

`isNativeStoreArm` also *recognizes* the plain STR/STRB single-data-transfer encodings, and the
emitter handles them — but accepting them is **disabled**, with an explanatory comment at the
function. Evidence:

- `jit_nb` (per-instruction JIT, BLOCKS=0) build with STR/STRB enabled is **bit-exact vs the
  interpreter** (257,789-instruction trace, zero diffs).
- Under **block compilation** on rockwrestler it diverges: first a **+1 ARM9 cycle at frame 1**, then
  pc/pipeline off by one word. Tier bisect (35 f dense sweep): FAIL = imm-offset STR/STRB, all
  STR/STRB tiers; PASS = reg-offset only, STRB only, STR only. Symptom points at a block containing a
  native STR/STRB ending with a **stale `pipeline[0]`/`R15`** pair (hypothesis, not proven).
- Reclaimable if solved: `020001E4 str r1,[r3],#4` (24,576 dispatches) and the
  `02000388 / 02000348 / 0200038C` sites.
- Method that worked for bisecting: temporary compile-time tier macro + dense checkpoint sweep
  (35 f, 14 checkpoints) vs interpreter. A single +1 cycle at frame 1 suggests a possible
  cycle-accounting/pipeline error; it is not proof that memory/SMC behavior is correct. Remove all
  temporary instrumentation before declaring anything frozen (keep `interpreter.cpp` pristine).

## 9. Next steps, in priority order

1. **Fix the parked STR/STRB block divergence** (§8). Highest value; obtain and verify the carried
   tooling/evidence first, then reproduce the failure before changing the emitter.
2. **Let an ARM `Bcc` start a block.** Block heads are currently only a native instruction, an
   **unconditional** B/BL (`isUncondBranchArm` requires cond=AL), or a PC-destination load — a
   **conditional** branch can terminate a block but cannot begin one (`compileBlock` returns false
   when the first instruction is a `Bcc`). Candidate sites from the older profile include: `02000284 1AFFFFF9` (40,376),
   `020001BC 0AFFFFFC` (18,843), `020001AC` (6,685). Likely direction: allow a `Bcc` to head a
   branch-only block evaluating the condition natively on both edges. Re-profile first with
   `bin_jit_hot` (the shipped `runs/nds_hot_final.log` profile predates the P=0 STRH change, so its
   per-site distribution is stale but the method is right). The three listed ARM9 site counts sum
   to 65,904, not the frozen 59,028 fallback total; do not treat this old profile as an exact
   decomposition of the frozen counter.
3. ARM7 residual is nearly trivial: `0380072C 1AFFFFFD` (492, the INTWAIT loop's `bne`) +
   `03800304` (2), from the older profile; those counts do not account for all frozen 558 fallbacks.
   Re-profile before claiming complete coverage. Any optimization must be cheap and provably safe.
4. If asked for game-wide speed: profile a reproducible gameplay scene instead of more poll
   shortcuts — see the warning in §1 and `uploads/Speedup.md`'s backlog (frameskip/presentation,
   GPU 3D/2D) for non-JIT directions **if that document is supplied**; it was not attached here.

## 10. Dead ends — do not retry

Carried experimental observations from the Stage-1 session, not this documentation review:

- **STR/STRB native acceptance under blocks** — parked, see §8.
- **`NOODS_JIT_POLL_SKIP=1`** — big regression (~153 vs ~88 ms/f in the older Dolphin setup), skipped
  0 iterations. ARM7 wait loops are not "pure": they write IPCSYNC `STRH` and do a conditional
  `LDR PC`. Do not force skip.
- Global `-O3` — slightly slower than `-O2`. (If testing flags, inspect the real compile command; the
  Makefile appends flags.)
- Generic native halfword loads alone — passed parity but slightly slower (hot MMIO is unmapped).
- Removed MMIO/SPU shortcuts that gave nothing: ARM7 IPCSYNC shortcut variants and a no-channel SPU
  shortcut were reverted before; only the *measured* ARM7 `LDRH @0x04000180` live-field read stayed.
- Python patch-script hazard: re-running an already-applied patch silently duplicated a predicate +
  emitter. **Inspect definitions and the diff after every patch** (`grep -n 'isPcDestLoadTerminatorArm' …`); raw match counts include legitimate call sites.
- Hand-rolled ARM opcode decoding is unreliable — use
  `arm-linux-gnueabi-objdump -D -b binary -m arm --adjust-vma=<base>`. The hot ARM9 code lives in
  **RAM**, not ROM/ITCM in the carried harness profile: slice `runs/nds_jit120.mem` at offset 163840, length 0x400000, base
  `0x02000000`; ARM7 WRAM slice at offset 32768, base `0x03800000`. These are **carried harness
  dump-layout offsets**, not Dolphin G2 dump offsets. Inspect the writer/layout and validate file
  size before slicing; the verified G2 `mem_dump.bin` is only 413,696 bytes.
- Diagnostic traces: record the guest PC **before** execution (`*registers[15] - step` at entry), or
  branch targets get mislabeled as skipped/inserted instructions.
- `sed -n "$(grep …)"` fails when `grep` matches multiple lines — use explicit line ranges or
  `grep -n` + a small python edit script. `rg` may not be installed; check `command -v rg`, or use `grep`.

From the inherited handoff (still valid): the old Dolphin-era DISPSTAT result (~101.9 → ~88.0 ms/frame)
produced **no perceptible change** for the user; the KEYINPUT build likewise; do not re-litigate those
as wins. Dolphin-based G2/G3 runs are usable (§3c) and should complement harness validation.

## 11. Housekeeping for this workspace

- **Snapshot limits:** persistence is best-effort, capped around 128 MB / 10,000 files. Keep the
  deliverable tree small; put the toolchain and download cache outside `/home/user`. Remove only
  known generated experiment objects/binaries and SD images, not user work or required evidence.
  Named `build_jit*` directories are not automatically excluded merely because they are build output.
- **Keep `handoff/` in sync.** It is what the user uploads. After any change to `arm_jit.*` / `ipc.h`:
  copy every changed/new required source into its repo-relative path under `handoff/NooDS-Wii/`,
  not just the original three files. Refresh the source patch against the **intended base commit**,
  selecting source/build/tool paths explicitly and excluding DOLs, ROMs, dumps and generated evidence.
  Plain `git diff` omits staged changes; even `git diff BASE` omits untracked files. Inspect both
  staged/untracked status and account for new files explicitly in the patch or source bundle.
  Example for the original three tracked Stage-1 files (adjust the list if the change grows):

  ```bash
  set -euo pipefail
  test -d "$WORKSPACE/handoff"
  git -C "$REPO" diff --binary --full-index \
    92e634aa696e1ea7b4cf6159110c75b01bfd6b4d -- \
    NooDS-Wii/NooDS-Wii/arm_jit.cpp NooDS-Wii/NooDS-Wii/arm_jit.h \
    NooDS-Wii/NooDS-Wii/ipc.h > "$WORKSPACE/handoff/noods_jit_stage1.patch.new"
  ```

  First ensure the output directory exists; do not overwrite the accepted patch until the new one
  is nonempty when changes are expected, inspected, and successfully applied to a disposable clean
  checkout of the intended base. Compare reconstructed source hashes with the working tree.
  Include untracked source files deliberately (a source bundle or explicit new-file patch); do not
  stage, reset, or discard user work just to generate a patch. Publish DOLs separately with hashes.
  Update `handoff/README.md` and copy `jit.dol` only after the clean browser build and validation.
  If a source update cannot yet be rebuilt/validated, do not leave an old DOL beside it labeled as
  matching or verified: mark it stale/not rebuilt and withhold it from the new verified release.
- **Do not commit or push.** Review `git status` against the recorded starting state (§2).
  Keep useful test evidence separate and account for generated files; do not erase unrelated changes.
- Report in short statements; end with what changed, what was verified (with log paths), and what is
  parked. For testing, use `handoff/README.md` **if present** and §§4/12. Never point to an absent
  file as the only instructions.


## 12. DOL validation matrix and reproduction commands

Run on the **actual patched Stage-1 source tree** after each retained JIT change. These are regression
checks, not performance claims. Use unique G2 labels: the runner deletes an existing matching output
directory. Keep harness checkpoints/dense sweeps in §6 too, especially for the parked store issue.

| Check | Required coverage |
|---|---|
| G0 | Build `JIT/BLOCKS = 0/0, 1/0, 1/1` from the same source and toolchain. |
| G1 | Encoder oracle `STATUS=PASS`; inspect matched/required counts. |
| G2 NDS | rockwrestler frames 15/30/45, each JIT vs interpreter, normal and a supported reduced pool with observed wraps. |
| G2 GBA | suite frames 50/60/120, each JIT vs interpreter, normal and a supported reduced pool with observed wraps. |
| G3 | All three variants reach `ALL DONE`; suite summaries/logs match interpreter. |
| Browser | Rebuild browser `jit.dol`; user checks ROM browser and NDS/GBA loading on Wii. |
| Hardware | User tests the final DOL on real Wii; do not infer hardware FPS/cache correctness from Dolphin. |

G3 parity does not mean all suite tests pass: the fresh base run had 6,998 tests, 3,506 passed and
3,492 failed in **every** variant, 13 suites and 1 skip. Matching failures are differential parity,
not full mGBA compatibility. Recheck counts for the actual patched tree.

```bash
set -euo pipefail
cd "$PROJ"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH"

git status --short  # record the starting state, including untracked files
sha256sum -c <<'HASHES'
f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  nds/rockwrestler.nds
63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495  gba/suite.gba
HASHES
for pair in '0 0' '1 0' '1 1'; do
  read -r jit blocks <<< "$pair"
  make clean JIT="$jit" BLOCKS="$blocks"
  make -j2 JIT="$jit" BLOCKS="$blocks"
done
bash tools/check_g1_encoder.sh
sha256sum NooDS-Wii_jit{0_blocks0,1_blocks0,1_blocks1}.dol

TAG="verify_$(date +%Y%m%d_%H%M%S)_$$"
run_g2() {
  local cfg="$1" label="$2" rom="$3" points="$4" frames="$5" secs="$6" pool="$7"
  bash tools/run_g2_case.sh "NooDS-Wii_${cfg}.dol" "$label" "$rom" "$points" "$frames" "$secs" "$pool"
  grep -q '^STATUS=PASS$' "evidence/g2_${label}/result.txt"
  grep -q '^SENTINEL_HIT=1$' "evidence/g2_${label}/result.txt"
  # The stock comparator does not enforce requested checkpoints or complete state sections.
  python3 - "evidence/g2_${label}/g2_state.log" "$points" "$cfg" "$pool" "$rom" \
    "evidence/g2_${label}/result.txt" "NooDS-Wii_${cfg}.dol" <<'PY'
import hashlib, pathlib, re, sys
sys.path.insert(0, 'tools')
from compare_runs import parse_checkpoint_log
path, requested, cfg, pool, rom, result_path, dol = sys.argv[1:]
match = re.fullmatch(r'jit([01])_blocks([01])', cfg)
if not match:
    raise SystemExit(f'FAIL: unsupported config {cfg}')
want_jit, want_blocks = map(int, match.groups())
want_gba = int(rom.lower().endswith('.gba'))
result = dict(line.strip().split('=', 1) for line in open(result_path) if '=' in line)
digest = hashlib.sha256(pathlib.Path(dol).read_bytes()).hexdigest()
if (result.get('DOL_SHA256_BEFORE') != digest or result.get('DOL_SHA256_AFTER') != digest
        or pathlib.Path(result.get('DOL', '')).resolve() != pathlib.Path(dol).resolve()):
    raise SystemExit('FAIL: runner evidence does not match the requested DOL')
expected = sorted(int(n) for n in requested.split(','))
headers = [int(n) for n in re.findall(r'^=== CHECKPOINT FRAME (\d+) ===$',
                                    open(path).read(), re.M)]
if sorted(headers) != expected:
    raise SystemExit(f'FAIL: expected checkpoints {expected}, got {headers}')
frames = parse_checkpoint_log(path)
required = {
    'CYCLES': {'global', 'arm9', 'arm7', 'gbaMode'},
    'ARM9': {'cpsr', 'spsr', 'pc', 'pipe0', 'pipe1', 'cycles', 'halted', 'ime', 'ie', 'irf',
             'postFlg', 'svc13', 'svc14', 'irq13', 'irq14', 'fiq13', 'fiq14'}
            | {f'r{i}' for i in range(16)} | {f'usr{i}' for i in range(16)},
    'ARM7': {'cpsr', 'spsr', 'pc', 'pipe0', 'pipe1', 'cycles', 'halted', 'ime', 'ie', 'irf',
             'postFlg', 'svc13', 'svc14', 'irq13', 'irq14', 'fiq13', 'fiq14'}
            | {f'r{i}' for i in range(16)} | {f'usr{i}' for i in range(16)},
    'MEM_HASH': {'ram', 'wram', 'itcm', 'dtcm', 'wram7', 'pal', 'vram', 'oam', 'total'},
    'FB_HASH': {'top', 'bottom', 'combined'},
}
for frame in expected:
    for section, keys in required.items():
        state = frames[frame].get(section, {})
        if not keys <= state.keys() or any(not state[k] for k in keys):
            raise SystemExit(f'FAIL: incomplete frame {frame} section {section}')
    if frames[frame]['CYCLES']['gbaMode'] != str(want_gba):
        raise SystemExit(f'FAIL: wrong guest mode at frame {frame}')
    stats = frames[frame].get('JIT_STATS', {})
    if stats.get('enabled') != str(want_jit):
        raise SystemExit(f'FAIL: wrong JIT setting at frame {frame}')
    if want_jit:
        if stats.get('blocks') != str(want_blocks):
            raise SystemExit(f'FAIL: wrong block setting at frame {frame}')
        if int(pool) > 0 and stats.get('poolLimit') != pool:
            raise SystemExit(f'FAIL: requested pool limit not applied at frame {frame}')
if want_jit and int(pool) > 0:
    wraps = frames[expected[-1]]['JIT_STATS'].get('poolWraps', '0')
    if not wraps.isdigit() or int(wraps) == 0:
        raise SystemExit('NOT_RUN: requested wrap case did not exercise a pool wrap')
PY
}
compare_g2() {
  local ref="$1" cand="$2" points="$3"
  python3 tools/compare_runs.py --mode checkpoints \
    --ref "evidence/g2_${ref}" --cand "evidence/g2_${cand}" \
    --out "evidence/g2_compare_${cand}.json"
  python3 - "evidence/g2_compare_${cand}.json" "$points" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
expected = sorted(int(n) for n in sys.argv[2].split(','))
if not (r.get('status') == 'PASS' and r.get('mismatch_count') == 0
        and r.get('frames_compared') == expected):
    raise SystemExit(f'FAIL: incomplete/mismatching comparison: {r}')
PY
}
# Select the minimum accepted reduced pool from the actual header; never assume 1,024 works.
WRAP_WORDS="$(python3 - "$SRC/arm_jit.h" <<'PY'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
m = re.search(r'MAX_BLOCK_WORDS\s*=\s*(\d+)\s*;', text)
if not m:
    raise SystemExit('Inspect MAX_BLOCK_WORDS and setPoolLimitWords manually; do not guess')
print(2 * int(m.group(1)))
PY
)"
# The current base has MAX_BLOCK_WORDS=4096, so the minimum accepted pool is 8192.
for pool in 0 "$WRAP_WORDS"; do
  for kind in nds gba; do
    if [ "$kind" = nds ]; then
      rom='sd:/nds/rockwrestler.nds'; points='15,30,45'; frames=45; secs=90
    else
      rom='sd:/gba/suite.gba'; points='50,60,120'; frames=120; secs=180
    fi
    ref="${TAG}_${kind}_p${pool}_ref"
    run_g2 jit0_blocks0 "$ref" "$rom" "$points" "$frames" "$secs" "$pool"
    for cfg in jit1_blocks0 jit1_blocks1; do
      cand="${TAG}_${kind}_p${pool}_${cfg}"
      run_g2 "$cfg" "$cand" "$rom" "$points" "$frames" "$secs" "$pool"
      compare_g2 "$ref" "$cand" "$points"
    done
  done
done

OUT="$PWD/evidence/g3_${TAG}"
for cfg in jit0_blocks0 jit1_blocks0 jit1_blocks1; do
  bash tools/run_suite_gba.sh "NooDS-Wii_${cfg}.dol" "$cfg" "$OUT" 300
  grep -q '^STATUS=PASS$' "$OUT/dolphin_${cfg}/result.txt"
  grep -q '^SENTINEL_HIT=1$' "$OUT/dolphin_${cfg}/result.txt"
  python3 - "$OUT/dolphin_${cfg}/result.txt" "NooDS-Wii_${cfg}.dol" <<'PY'
import hashlib, pathlib, sys
r = dict(line.strip().split('=', 1) for line in open(sys.argv[1]) if '=' in line)
p = pathlib.Path(sys.argv[2]).resolve()
digest = hashlib.sha256(p.read_bytes()).hexdigest()
if (r.get('DOL_SHA256_BEFORE') != digest or r.get('DOL_SHA256_AFTER') != digest
        or pathlib.Path(r.get('DOL', '')).resolve() != p):
    raise SystemExit('FAIL: G3 runner evidence does not match the requested DOL')
PY
done
for cfg in jit1_blocks0 jit1_blocks1; do
  python3 tools/compare_runs.py --mode suites \
    --ref "$OUT/mgba_summary_jit0_blocks0.json" \
    --cand "$OUT/mgba_summary_${cfg}.json" \
    --out "evidence/g3_compare_${TAG}_${cfg}.json"
  python3 - "evidence/g3_compare_${TAG}_${cfg}.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
if r.get('status') != 'PASS' or r.get('mismatch_count') != 0:
    raise SystemExit(f'FAIL: {r}')
PY
done
sha256sum "$OUT"/gbaout_*.log
python3 - "$OUT" <<'PY'
import hashlib, pathlib, sys
out = pathlib.Path(sys.argv[1])
logs = [out / f'gbaout_{cfg}.log' for cfg in ('jit0_blocks0', 'jit1_blocks0', 'jit1_blocks1')]
if any(not p.is_file() or p.stat().st_size == 0 for p in logs):
    raise SystemExit('FAIL: missing/empty G3 log')
if len({hashlib.sha256(p.read_bytes()).hexdigest() for p in logs}) != 1:
    raise SystemExit('FAIL: G3 logs are not byte-identical')
PY
make clean JIT=1 BLOCKS=1 CONFIG_TAG=jit_browser
bash tools/build_browser_jit.sh
sha256sum jit.dol
git status --short  # review only; never commit/push
```

Report every gate with evidence and tested revision. A five-frame smoke test alone is not the full
G2 matrix. The stock comparator compares frames/sections it finds, not the requested frame list;
identical omissions can produce a false PASS. The wrapper above rejects missing/duplicate frames
and missing required state fields from the checked base schema, then checks the comparator's frame
list. It also checks guest mode, JIT/block enablement, requested nonzero pool limit, actual wrap
activity, and runner DOL identity/hashes; otherwise an interpreter DOL mislabeled as JIT can pass
parity without testing the JIT. For wrap cases, zero actual wraps means coverage was not exercised.
At the checked base `setPoolLimitWords()` accepts only `[2 * MAX_BLOCK_WORDS, DEFAULT_POOL_WORDS]`;
`MAX_BLOCK_WORDS=4096` makes 1,024 invalid. It silently leaves the default limit unchanged.
The old guide's passing "1,024-word wrap" labels therefore do not establish wrap coverage here.
Use the actual patched setter/header to choose an accepted reduced limit. The selected minimum
is not a guarantee of a wrap: if counters remain zero, report the wrap gate as NOT_RUN/not exercised
and add a focused workload or a scene that compiles enough code. Do not shrink below the supported
minimum or weaken the guard just to obtain a PASS. If the patched checkpoint schema changes, audit it and update the required keys explicitly. It uses explicit failure
checks rather than Python `assert`, which `PYTHONOPTIMIZE` can disable.

G2 coverage is sampled, not exhaustive. At the checked base, `mem_dump.bin` contains selected
WRAM/TCM/palette/OAM regions plus only the first 256 KiB of RAM (413,696 bytes total). Checkpoint
hashes cover 4 MiB NDS RAM (256 KiB GBA RAM) and VRAM banks; those hashes are not full byte dumps.
Do not describe the DOL tests as byte-for-byte comparison of every memory region.

Save build/G1/runner/comparator logs under a unique evidence directory; record HEAD, the source diff
and source hashes, DOL hashes, tool versions, build flags, and starting/final status. HEAD alone does
not identify an uncommitted Stage-1 tree. Do not reuse stale DOLs after source/flag changes, and do
not deliver a candidate as verified while required source/DOL/browser/hardware checks remain pending.

Do not carry the older `c79fd20` DOL digests, backlog line numbers, or historical G2/G3
passes forward as proof of the newer Stage-1 patch.

## 13. Fresh workspace verification — 2026-10-14

**Scope:** pristine `Actions-Minutes2@92e634aa696e1ea7b4cf6159110c75b01bfd6b4d` in
`/home/user/verification/Actions-Minutes2`, without the missing Stage-1 patch. No source edits,
tracked mode changes, commits, or pushes were made to that checkout.

| Check | Fresh result | Evidence relative to `/home/user/` |
|---|---|---|
| Host install | Debian 13.7; `dolphin-emu 2503+dfsg-1+deb13u1`; headless CLI works | `host-install.log`, `verification/host-check.txt` |
| devkitPPC bootstrap | `STATUS=PASS`; GCC 16.1.0, AS 2.46.0.20260210, Wii link + DOL conversion | `verification/toolchain-bootstrap.log` |
| G0 | All three `JIT/BLOCKS` configurations built successfully | `verification/build_jit*.log` |
| G1 | 52/52 forms matched; required minimum 44/44; `STATUS=PASS` | `verification/g1.log` |
| ROM hashes | Both match §6 | `verification/verification-report.md` |
| G2 smoke only | NDS frame 5, normal pool; all three runners PASS/sentinel, both JIT comparisons PASS with 0 mismatches | `verification/g2_*.log`, `verification/g2_compare_*.json` |
| G3 | All three runners PASS/sentinel, `ALL DONE`; identical suite log hashes; both comparisons PASS with 0 mismatches | `verification/g3_*.log`, `verification/g3_compare_*.json` |
| G3 child execution | Both children already invoked via `bash`; tracked modes 100644 work without chmod | inspected `tools/run_suite_gba.sh` at the pinned commit |

**NOT_RUN in this correction:** actual patched Stage-1 harness/source/DOL tests (artifacts absent);
full G2 NDS/GBA checkpoint and pool-wrap matrix; browser build/UI check; real-Wii tests; controlled
performance measurements. The carried browser SHA and Stage-1 counters were not re-verified.

The recorded installations succeeded during that audit but are not bundled with this document.
Recheck executables/packages and reinstall as needed in a fresh sandbox; do not assume they remain
installed in a later turn. This correction changes documentation, not JIT implementation.


## 14. Follow-up documentation audit — 2026-10-14

Additional corrections made after reviewing the first corrected file:
- Reserved-condition dispatch is a table/handler path, not a never-true function result.
- PC-load T-bit changes are instruction-specific: checked `ldrhOf` does not apply the `ldrOf` /
  `ldrbOf` T-bit update.
- Restored cache-flush, scheduler-boundary, bridge-register, classifier, and guarded-memory rules
  without importing older prohibitions that conflict with Stage-1 terminators.
- Added requested-checkpoint/state-completeness checks to prevent identical omissions being marked
  PASS by the stock G2 comparator; added enforced ROM/G3 hash checks and optimization-safe failures.
- Qualified stale profile counts, memory-dump offsets, session provenance, optional documents/tools,
  absent artifacts, and nonpersistent installations. Made workspace/harness paths explicit.

Follow-up checks: installed QEMU 10.0.13 compatibility package; re-ran pinned devkitPPC bootstrap
(`STATUS=PASS`); assembled `stwbrx`/`sthbrx` and confirmed distinct encodings; inspected the checked
base interpreter/bridge/cache/scheduler source. Synthetic malformed checkpoint evidence reproduced
the stock comparator's false PASS. See `verification/review_checks/` and
`verification/agents-review.md` for the validation tests and caveats.

No new game/harness/real-Wii or full-matrix tests were run in this follow-up. Stage-1 artifacts are
still missing. No repository source edits, commits, or pushes were made.


## 15. Build/package and configuration audit — 2026-10-14

Verified additional risks and corrected instructions:
- Browser objects are not rebuilt automatically after compiler-flag changes. An actual browser
  build followed by a flag-change dry run reported "Nothing to be done". Clean the named browser
  configuration before building; a clean/forced build includes the requested flags.
- Plain `git diff` omits staged edits and gives no protection against omitted new source files;
  scope a patch to the intended base and required source paths, verify reconstruction, and keep
  DOLs/evidence/ROMs separate. Never package a stale binary as matching updated source.
- Parity alone does not establish which configuration ran. Check JIT/block state, guest mode,
  requested pool limit, exercised wraps, and runner DOL identity/hashes against the actual file.
- Reconstruction instructions now explicitly use the workspace root, check required artifacts,
  refuse an existing destination, and stop on failures.

A fresh check also found that the documented 1,024-word pool is rejected at this base:
`MAX_BLOCK_WORDS=4096`, setter minimum 8192, actual 1024-request run `poolLimit=262144`,
`poolWraps=0`. The new guard correctly stopped the matrix rather than falsely passing wrap coverage.
Fresh pristine-base results:
- Normal G2: NDS checkpoints 15/30/45 and GBA 50/60/120, both JIT variants: four comparisons
  PASS, zero mismatches.
- Supported 8,192-word G2: both ROMs and JIT variants matched the interpreter, four comparisons
  PASS, zero mismatches. **Observed wraps:** NDS block JIT 1; GBA stub JIT 1,925; GBA block JIT
  4,501 at the final checkpoints. NDS stub JIT had **zero wraps**, so its wrap coverage remains
  NOT_RUN/not exercised despite matching output. The strict wrapper correctly rejects that case;
  finish unaffected gates separately under unique labels, and add a workload for the missing wrap.
- G3: all three variants completed and both comparisons/log hash checks passed with the new DOL
  identity guard. This remains differential parity, not complete GBA compatibility.
- Clean browser build succeeded with the expected endian/JIT/browser flags; cleaning the named
  browser configuration preserved all three regression DOL hashes. Browser UI/hardware not tested.
- Disposable patch reconstruction and optimization-enabled negative guard tests passed.

See `verification/audit2/report.md` for evidence. These checks still do not verify the missing
patched Stage-1 source or real-Wii behavior; earlier NOT_RUN entries describe their original audits, not a perpetual tool prohibition.
