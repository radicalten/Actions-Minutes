# AGENTS.md — NooDS-Wii native JIT, continuation from a frozen Stage 1

**Read this first.** You are picking up a continuation of a prior session on the NooDS-Wii Wii emulator.
You receive **only this workspace package** — no chat history. This file is self-contained and supersedes
the older `uploads/AGENTS.md` wherever they disagree (`uploads/` docs describe an earlier, different
checkout and Dolphin-based workflow; this package is a later state with a different test setup).

**One-line status:** Stage 1 of the ARM7/ARM9 native-JIT work (PC-destination load terminators, ARM7
IPCSYNC fast path, generalized STRH stores) is **implemented, verified bit-exact against the
interpreter, and frozen** in this package. Your job is to continue extending native coverage from this
state without regressing guest correctness.

---

## 1. The task in the user's terms

Improve the NooDS-Wii dynamic recompiler for the Wii's PowerPC 750CL (Broadway), for **both** guest
CPUs — ARM7 (ARMv4T) and ARM9 (ARMv5TE) — in **both** ARM and Thumb modes. `.nds` performance is the
priority; `.gba` support and the ROM browser must keep working.

Standing rules the user set (do not violate):

- **Never commit or push.** Leave changes in the working tree; the user uploads them.
- **Guest correctness first.** Never trade correctness for speculative speed. Every JIT change must
  pass interpreter-parity before it is considered real.
- Do not break `.gba` support or the ROM browser (see §6 validation matrix).
- **Never present harness timing or ms/frame as real Wii FPS.** The user runs the emulator on real
  hardware (SD card + `sd:/apps/<app>/boot.dol`) and has repeatedly said harness improvements did not
  translate into a perceptible FPS change. The old handoff mentions a confusing "Emu 4 / Display 11"
  report — the wording is ambiguous and **must not** be used to claim a speedup or slowdown.
- Keep progress updates short while working. Report skipped/unrun tests explicitly, never silently.
- Keep generated SD images and any proprietary BIOS/firmware out of uploads.

## 2. What is in this package

| Path | What it is |
|---|---|
| `handoff/` | **The upload deliverables.** `handoff/NooDS-Wii/NooDS-Wii/{arm_jit.cpp,arm_jit.h,ipc.h}` = the modified sources at repo-relative paths; `handoff/NooDS-Wii/jit.dol` = rebuilt browser JIT; `handoff/noods_jit_stage1.patch` = the same change as a git patch; `handoff/README.md` = sha256s + apply instructions. |
| `repo/` | Full checkout of `radicalten/Actions-Minutes2` (HEAD `92e634a`) with the Stage-1 edits already applied. Project root `repo/NooDS-Wii/`, sources `repo/NooDS-Wii/NooDS-Wii/`. ROMs: `nds/rockwrestler.nds`, `gba/suite.gba`. Tools: `repo/NooDS-Wii/tools/`. |
| `harness/` | A **PPC-Linux + qemu harness** that runs the actual emulation core (real `arm_jit.cpp`) natively on big-endian PowerPC. Sources of truth for reproducing every result in §5: `build.sh`, `src/harness_main.cpp`, `shims/`, prebuilt stripped `bin_interp/`, `bin_jit/`, `bin_jit_hot/`, and all logs in `runs/`. |
| `uploads/` | The previous session's handoff docs (`AGENTS.md`, `Speedup.md`). Historical context only — many paths/tools they mention do not exist in this checkout. |

Notes:
- `repo/.git` may be missing or partial in the package. If you need history, re-clone:
  `git clone --depth 1 https://github.com/radicalten/Actions-Minutes2.git repo` (verify HEAD is
  `92e634a`), then `git apply handoff/noods_jit_stage1.patch` (verified to apply cleanly to pristine
  `92e634a`). Or just copy `handoff/NooDS-Wii/**` over the checkout — byte-identical.
- `git status` in `repo/` must show **only**: `M NooDS-Wii/NooDS-Wii/{arm_jit.cpp,arm_jit.h,ipc.h}`,
  `M NooDS-Wii/jit.dol`, `?? NooDS-Wii/evidence/toolchain_bootstrap_result.txt`. `interpreter.cpp`
  and every other source are pristine — keep it that way (a pristine copy was once needed to undo
  temporary tracing edits).

## 3. Environment

The package does **not** contain devkitPPC or any toolchain (they exceed the package size cap; see §9).
You have two independent ways to work:

### 3a. Harness only (no toolchain needed) — this is how all results in §5 were produced

Host packages (Debian 13 x86_64 in the prior session): `build-essential`, `qemu-user-static`
(provides `qemu-ppc-static`), `g++-powerpc-linux-gnu`, `binutils-powerpc-linux-gnu`, `python3`,
`binutils-arm-linux-gnueabi` (only for guest disassembly), `zstd`. Install what is missing.

The prebuilt binaries run as-is:

```bash
cd harness
qemu-ppc-static ./bin_jit/noods_host --rom ../repo/NooDS-Wii/nds/rockwrestler.nds \
    --frames 120 --checkpoints 30,60,120 --out runs/x.log --stats
python3 compare.py runs/nds_interp120.log runs/x.log    # prints MISMATCHES=<n>, STATUS=PASS/FAIL
```

`bin_jit` = current frozen sources, `JIT=1 BLOCKS=1` (+ `NOODS_JIT_IPCSYNC_FAST=1`, the default).
`bin_interp` = `JIT=0`. `bin_jit_hot` = same sources with `-DNOODS_JIT_HOTPC=1` (opt-in per-PC
fallback profiler). All three are stripped but fully runnable.

To rebuild the harness from source (≈1 min, needs `powerpc-linux-gnu-g++`):

```bash
cd harness
./build.sh interp            # -> bin_interp/noods_host
./build.sh jit               # -> bin_jit/noods_host
EXTRA_DEFS=-DNOODS_JIT_HOTPC=1 ./build.sh jit bin_jit_hot
SRC=/absolute/path/to/NooDS-Wii/NooDS-Wii ./build.sh jit   # if the tree is not at ../repo/NooDS-Wii/NooDS-Wii
```

`build.sh` mirrors the Wii Makefile flags (`-O2 -DENDIAN_BIG -mcpu=750 -mhard-float …`); `ENDIAN_BIG`
is load-bearing — a build without it silently miscompiles guest memory accesses. Do not "fix" the
harness unless something in `shims/` is actually broken.

CLI: `--rom <nds> [--gba <gba>] --frames N [--checkpoints a,b,c] --out FILE [--stats] [--pool-words N]
[--mem-dump FILE] [--fb-dump FILE]`. For GBA runs use `--rom "" --gba ../repo/NooDS-Wii/gba/suite.gba …`.

### 3b. Real Wii builds (devkitPPC) — needed to hand the user a DOL

Toolchain is not packaged; bootstrap it into a path **outside** the workspace (so it never bloats the
package) and keep only the built DOL:

```bash
export DEVKITPRO=/opt/devkitpro            # or $HOME/devkitpro; must be writable
bash repo/NooDS-Wii/tools/bootstrap_toolchain.sh    # downloads pinned devkitPPC r50 (GCC 16.1.0) + libogc
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
cd repo/NooDS-Wii
make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1     # autoboot-capable regression DOL
bash tools/build_browser_jit.sh                     # -> jit.dol (browser build, NOODS_FORCE_BROWSER=1)
sha256sum jit.dol   # frozen value: cf53857c4d8fe12e6b7c6fc07ce57d89e16f342247ea7062c429f836c7f83515
```

Never use `make clean-all` casually (deletes named regression DOLs). The browser DOL ignores autoboot
files by design — use the `jit1_blocks1` DOL for scripted/autoboot runs.

### 3c. Dolphin: not available

`dolphin-emu` was not installable on the host distro, so `tools/run_dolphin.sh`, `run_g2_case.sh`
and `run_suite_gba.sh` **cannot run here**. Mark any Dolphin-based G2/G3 check as NOT_RUN and use the
harness instead. Do not claim on-Wii or Dolphin numbers you did not measure.

## 4. Fast start: reproduce the frozen state (≈2 minutes)

```bash
cd harness
# 1. smoke (interp + jit, same 5 frames)
qemu-ppc-static ./bin_interp/noods_host --rom ../repo/NooDS-Wii/nds/rockwrestler.nds --frames 5 \
    --checkpoints 5 --out runs/now_smoke_i.log
qemu-ppc-static ./bin_jit/noods_host    --rom ../repo/NooDS-Wii/nds/rockwrestler.nds --frames 5 \
    --checkpoints 5 --out runs/now_smoke.log --stats
python3 compare.py runs/now_smoke_i.log runs/now_smoke.log      # MISMATCHES=0
# 2. the real checks (expected: MISMATCHES=0 on every one)
qemu-ppc-static ./bin_interp/noods_host --rom ../repo/NooDS-Wii/nds/rockwrestler.nds --frames 120 \
    --checkpoints 30,60,120 --out runs/now_interp120.log
qemu-ppc-static ./bin_jit/noods_host    --rom ../repo/NooDS-Wii/nds/rockwrestler.nds --frames 120 \
    --checkpoints 30,60,120 --out runs/now_jit120.log --stats
python3 compare.py runs/now_interp120.log runs/now_jit120.log   # expect MISMATCHES=0, STATUS=PASS
```

Then, before **any** change you intend to keep: rebuild `bin_jit` (`./build.sh jit`), re-run the full
matrix in §6, and only then update `handoff/` (§9).

## 5. The frozen change (Stage 1) — what and where

All in `repo/NooDS-Wii/NooDS-Wii/arm_jit.cpp` (plus 2 lines each in `arm_jit.h` / `ipc.h`):

1. **PC-destination load block terminator** — `isPcDestLoadTerminatorArm(op)` +
   `ArmPpcJit::emitPcDestLoadTerminatorArm(...)`. Native ARM `LDR/LDRB/LDRH PC,[...]` (pre-index,
   no writeback) compiled as a **block terminator** (computed branch / jump table). Fast path mirrors
   the interpreter's `ldrOf/ldrbOf/ldrhOf` (misaligned-load rotation, ARM9 T-bit from value bit 0,
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
   (verified vs `powerpc-eabi-as -mgekko` output `7d 00 3d 2c`).
4. Removed a duplicated HOTPC `fbSiteRecord` double-count. HOTPC profiling stays opt-in
   (`NOODS_JIT_HOTPC` default 0).

**Deliberately NOT enabled — read §8 before touching stores.**

Other switches in `arm_jit.cpp` (status in the frozen tree): `NOODS_JIT_DISPSTAT_FAST=1`,
`NOODS_JIT_KEYINPUT_FAST=1` (inherited ARM9 MMIO fast paths), `NOODS_JIT_NATIVE_HALF_LOADS=0`,
`NOODS_JIT_POLL_SKIP` (off; regressed when tried).

## 6. Frozen results (all reproduced on this exact tree)

Parity (interpreter vs JIT, `compare.py` → `MISMATCHES=0`, `STATUS=PASS` on all):

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
sources** (`./build.sh jit <tmp>`): parity `MISMATCHES=0` and `JIT_STATS` byte-identical to
`runs/nds_jit_p0.log`, so the shipped `bin_jit` and the shipped sources are the same build.

Real Wii targets rebuilt: `make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1` OK; browser DOL
`jit.dol` sha256 `cf53857c…f83515` (matches `handoff/`). On-Wii behavior of this build has **not**
been confirmed by the user yet — do not claim on-Wii numbers.

ROM hashes (for `tools/build_sd_image.sh`): `rockwrestler.nds`
`f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`; `suite.gba`
`63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495`.
Encoder oracle: `bash tools/check_g1_encoder.sh` → 44/44 PASS (needs devkitPPC's `powerpc-eabi-as`
and host `g++`; writes `evidence/g1_encoder_oracle.log`).

## 7. Invariants you must preserve

- A JIT block **enters with `R15 = instrPC + 8`** (ARM) and the block epilogue recomputes `pcData` /
  pipeline state; the interpreter handler slow path expects `r15Instr = instrPC + totalDelta + 8`
  before the load.
- `flushPipeline()`: ARM `r15 = (r15 & ~3) + 4`, Thumb `(r15 & ~1) + 2`, refills `pipeline[0]/[1]`.
  An `Rd=15` load additionally sets `cpsr |= (val & !arm7) << 5` (T-bit) and costs 5 cycles.
- `Interpreter::condition()` returns 0/1/2; only condition `15` yields 2 (never-true fast path).
- PC-dest load terminator costs: **not-taken = linear path, +1 cycle; taken = +5 cycles.**
- MMIO fast paths are per-address guarded and only replace the load when the address matches; all
  other addresses keep the interpreter path. Keep ARM7/ARM9 differences intact.
- Bus/MMIO semantics, unaligned access handling, SMC invalidation and pipeline/flag exactness are
  non-negotiable; event boundaries (GPU/SPU/DMA scheduling) are where guest-visible timing comes from.

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
  (35 f, 14 checkpoints) vs interpreter. A single +1 cycle at frame 1 is the signature of a
  block-level cycle miscount, not memory corruption. Remove all temporary instrumentation before
  declaring anything frozen (keep `interpreter.cpp` pristine).

## 9. Next steps, in priority order

1. **Fix the parked STR/STRB block divergence** (§8). Highest value; tooling and evidence already exist.
2. **Let an ARM `Bcc` start a block.** Block heads are currently only a native instruction, an
   **unconditional** B/BL (`isUncondBranchArm` requires cond=AL), or a PC-destination load — a
   **conditional** branch can terminate a block but cannot begin one (`compileBlock` returns false
   when the first instruction is a `Bcc`). The residual ARM9 fallbacks (59,028) match the
   directly-dispatched `Bcc` sites from the pre-change profile: `02000284 1AFFFFF9` (40,376),
   `020001BC 0AFFFFFC` (18,843), `020001AC` (6,685). Likely direction: allow a `Bcc` to head a
   branch-only block evaluating the condition natively on both edges. Re-profile first with
   `bin_jit_hot` (the shipped `runs/nds_hot_final.log` profile predates the P=0 STRH change, so its
   per-site distribution is stale but the method is right).
3. ARM7 residual is nearly trivial: `0380072C 1AFFFFFD` (492, the INTWAIT loop's `bne`) +
   `03800304` (2). Anything done there should be cheap and provably safe.
4. If asked for game-wide speed: profile a reproducible gameplay scene instead of more poll
   shortcuts — see the warning in §1 and `uploads/Speedup.md`'s backlog (frameskip/presentation,
   GPU 3D/2D) for non-JIT directions.

## 10. Dead ends — do not retry

From this session:

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
  emitter. **Grep symbol counts after every patch** (`grep -c 'isPcDestLoadTerminatorArm' …`).
- Hand-rolled ARM opcode decoding is unreliable — use
  `arm-linux-gnueabi-objdump -D -b binary -m arm --adjust-vma=<base>`. The hot ARM9 code lives in
  **RAM**, not ROM/ITCM: slice `runs/nds_jit120.mem` at offset 163840, length 0x400000, base
  `0x02000000`; ARM7 WRAM slice at offset 32768, base `0x03800000`.
- Diagnostic traces: record the guest PC **before** execution (`*registers[15] - step` at entry), or
  branch targets get mislabeled as skipped/inserted instructions.
- `sed -n "$(grep …)"` fails when `grep` matches multiple lines — use explicit line ranges or
  `grep -n` + a small python edit script. `rg` is unavailable; use `grep`.

From the inherited handoff (still valid): the old Dolphin-era DISPSTAT result (~101.9 → ~88.0 ms/frame)
produced **no perceptible change** for the user; the KEYINPUT build likewise; do not re-litigate those
as wins. Do not attempt Dolphin-based G2/G3 runs here (§3c).

## 11. Housekeeping for this workspace

- **Snapshot size cap:** the platform packages the workspace at ~128 MB and *silently omits whole
  directories* over that. Keep the tree small: at most the three stripped `harness/bin_*` binaries;
  no toolchain inside the workspace; `rm -rf` any `bin_<experiment>/`, `obj/` before finishing.
- **Keep `handoff/` in sync.** It is what the user uploads. After any change to `arm_jit.*` / `ipc.h`:
  copy the files into `handoff/NooDS-Wii/NooDS-Wii/`, refresh `noods_jit_stage1.patch` (or a new
  patch name) with `git diff`, update hashes in `handoff/README.md`, and rebuild + copy `jit.dol`
  only if you also rebuilt the browser DOL and verified it.
- **Do not commit.** `git status` should show exactly the files listed in §2, nothing else.
- Report in short statements; end with what changed, what was verified (with log paths), and what is
  parked. If the user asks "how do I test it", point them at `handoff/README.md`.
