# AGENTS.md — NooDS-Wii native JIT: current handoff

Read this file first. It describes the current working tree (JIT Stage 2, final for this handoff), what was
verified and how, what is still open, and the exact commands to reproduce each check. The older
Stage-1 document and its patch-based instructions were removed; there are no patch files in this handoff.

## 0. One-line status

The working tree contains the ARM9/ARM7 JIT changes with interpreter parity verified in the harness
(qemu), Dolphin gates G0–G3, and browser boot in Dolphin. The user has **not** tested on a Wii yet.

## 1. Task and standing rules (from the user)

- Goal: improve the NooDS-Wii dynamic recompiler (Wii PowerPC 750CL/Broadway) for ARM7 (ARMv4T) and
  ARM9 (ARMv5TE), ARM and Thumb. `.nds` performance is the priority. `.gba` support and the ROM browser
  must keep working.
- **Never commit or push.** Leave changes in the working tree; the user uploads them.
- **Guest correctness first.** Every JIT change must pass interpreter parity before it counts.
- Use one sequential worker. Serialize Dolphin runs (`tools/run_dolphin.sh` uses `flock`).
- Do not dump full Dolphin logs; use bounded `grep`/`tail`/`sed` on cleaned logs.
- **Never present harness timing or ms/frame as Wii FPS.** The user's real hardware results have not
  matched harness numbers. Do not claim a speedup from harness or Dolphin numbers.
- Report skipped or unrun tests explicitly.
- Keep generated SD images and proprietary BIOS/firmware out of uploads (`/tmp` is fine for SD images).
- Keep `handoff/` in sync with any source change (see §10).

## 2. Repository layout

| Path | Contents |
|---|---|
| `NooDS-Wii/NooDS-Wii/` | Emulator sources. JIT: `arm_jit.cpp`, `arm_jit.h`. Also `ipc.h`, `interpreter*.cpp`, `main.cpp`, etc. |
| `NooDS-Wii/Makefile` | Wii build. Variables: `JIT`, `BLOCKS`, `CONFIG_TAG`, `CXXFLAGS`. |
| `NooDS-Wii/tools/` | `bootstrap_toolchain.sh`, `build_browser_jit.sh`, `build_sd_image.sh`, `check_g1_encoder.sh`, `compare_runs.py`, `run_dolphin.sh`, `run_g2_case.sh`, `run_suite_gba.sh`, `test_branch_arm.py`, `test_encoder.cpp`. |
| `NooDS-Wii/nds/rockwrestler.nds`, `NooDS-Wii/gba/suite.gba` | Test ROMs. SHA-256 `f905e165…069a` and `63f8c6b1…0495`. |
| `NooDS-Wii/interp.dol` | **Shipped DOL 1:** browser build, interpreter only (`NOODS_FORCE_BROWSER=1`). SHA-256 `913c647c…9771`. |
| `NooDS-Wii/jit.dol` | **Shipped DOL 2:** browser build, JIT on (`JIT=1 BLOCKS=1`). SHA-256 `2e505a16…6da`. |
| `NooDS-Wii/evidence/` | Gate logs and comparison JSON from this session (untracked, plus tracked older files). |
| `harness/` | PPC-Linux + qemu harness that runs the real emulation core natively. `build.sh`, `compare.py`, `src/`, `shims/`, `bin_interp/`, `bin_jit/` (rebuilt for this tree), `runs/` (older Stage-1 logs; not regenerated). |
| `handoff/` | The upload: `NooDS-Wii/NooDS-Wii/{arm_jit.cpp,arm_jit.h,ipc.h}` (identical to the repo) and `README.md` (hashes and verification). No patch files. |
| `AGENTS.md` | This file. |

The shipped DOLs are the only DOLs in the project. Autoboot DOLs (`NooDS-Wii_jit*.dol`) are not
shipped; they can be rebuilt with `make` (§9) but are not needed for the browser.

## 3. Current source state

Changes relative to base `92e634a` (Stage-1 changes are in `HEAD c55f68a`; the `arm_jit.cpp` diff below is
against the working tree at the start of this session):

**Stage 1 (carried, present in the sources):**
1. PC-destination load block terminator (`isPcDestLoadTerminatorArm`, `emitPcDestLoadTerminatorArm`).
2. ARM7 IPCSYNC fast path: `LDRH @0x04000180` reads the live `ipc.ipcSync[1]` (`friend class ArmPpcJit`
   in `ipc.h`).
3. Generalized STRH stores (`isNativeStoreArm`/`emitNativeStoreArm`), including `P=0,W=0` post-update.
   The `stwbrx` encoder is `0x7D003D2C`; `sthbrx` is `0x7D003F2C`.
4. HOTPC double-count removed; HOTPC profiling is opt-in (`NOODS_JIT_HOTPC=0`).

**Stage 2 (this session):**
5. **ARM B/BL with any non-reserved condition may start a block** (`isBranchHeadArm`). The branch-only
   block evaluates the condition natively on both edges (`emitInlineBranchArm`), or calls `execBranchArm`
   when the target leaves the page. Harness ARM9 fallbacks: 59,028 → 31,602 per 120 frames (rockwrestler).
6. **Time published on helper calls** (`emitHelperR15Sync`): the block prefix is added to
   `Core::globalCycles` before a native-to-helper call and removed after it. Without this, timer reads
   (TMxCNT_L, `0x04000100`) inside a block saw a stale clock. This was the root cause of the earlier
   ARM7 immediate-offset LDRH divergence and the frame-167/frame-900 mismatches.
   **Important:** `Core` is larger than 32 KB, so `offsetof(Core, …)` does not fit a signed 16-bit
   displacement. Load such offsets with `emitLoadImm32` and an add, as `emitTime` does. Do not cast
   them to `int16_t`.
7. **IPCSYNC fast path on by default** (`NOODS_JIT_IPCSYNC_FAST=1`). It is skipped when `Core::gbaMode` is set.
8. `s_emitTimeShift` is 1 for ARM7 in NDS mode and 0 otherwise.

Other switches: `NOODS_JIT_DISPSTAT_FAST=1`, `NOODS_JIT_KEYINPUT_FAST=1`, `NOODS_JIT_NATIVE_HALF_LOADS=0`,
`NOODS_JIT_POLL_SKIP` (off; regressed when tried), `NOODS_JIT_BLOCKS` per build.

Plain STR/STRB native acceptance is **disabled** (§7). Do not enable it without solving §7.

## 4. Verification record (this handoff)

All results below are for the working tree at `2e505a16…`/`913c647c…` unless noted.

| Check | Result | Evidence |
|---|---|---|
| Harness, rockwrestler, every frame 1–60 (interp vs JIT, 60 checkpoints) | `MISMATCHES=0`, `STATUS=PASS` | run this session; logs deleted in cleanup; repeat with §8 |
| Harness, suite.gba, every frame 1–60 | `MISMATCHES=0`, `STATUS=PASS` | §8 |
| Harness, rockwrestler 120 f, cps 30/60/90/120 | `MISMATCHES=0`, `STATUS=PASS` | §8 |
| Harness, rockwrestler 900 f, cps 150–900 (IPCSYNC on and off) | `MISMATCHES=0`, `STATUS=PASS` | run earlier this session; re-run with §8 |
| Harness, suite.gba 900 f, cps 150–900 | `MISMATCHES=0`, `STATUS=PASS` | run earlier this session |
| Harness, `bin_interp`/`bin_jit` rebuilt from this tree, smoke | `MISMATCHES=0` for rockwrestler 120 f and suite.gba 60 f | §8 |
| G0 builds `JIT/BLOCKS` 0/0, 1/0, 1/1 | All built | `evidence/verify_bccipc/build_*.log` |
| G1 encoder oracle | `FORMS_MATCHED=52/52`, required 44/44, `STATUS=PASS` | `evidence/verify_bccipc/g1.log` |
| G2 NDS rockwrestler 15/30/45, GBA suite 50/60/120; pool 0 and 8192; 8 comparisons | All `PASS`, 0 mismatches | `evidence/g2_compare_verify_20261015_153558_14711_*.json` |
| G2 `jit1_blocks0` pool 8192 wrap | **NOT_RUN_WRAP** (stub JIT never wraps; documented in §12) | `evidence/g2_verify_..._nds_p8192_jit1_blocks0/` |
| G3 suite.gba, all three variants | All `ALL DONE`; `gbaout` SHA-256 `97b08848…ff` identical; both JIT comparisons PASS | `evidence/g3_compare_verify_20261015_153558_14711_jit1_blocks{0,1}.json` |
| G3 suite counts | 3506 passed / 3492 failed in every variant (same as base) | `evidence/g3_verify_20261015_153558_14711/` |
| Dolphin, `interp.dol` boot | `[NOODS] FILE_BROWSER active dir=sd:/`, sentinel hit, no fatal lines; 60 s run clean | session-only log (not kept) |
| Dolphin, `jit.dol` boot | Same as above | session-only log (not kept) |

Corrections and caveats:
- **G3 `jit1_blocks1` now passes** in this tree. An earlier Stage-1 G3 failure on that variant was caused by
  the stale-clock bug in §3.6, not by Bcc heads.
- **`jit.dol` hash discrepancy.** The gate run recorded `d2048cf…` for the browser DOL. Two clean
  rebuilds from the same source gave `2e505a…`, and that is what ships. The likely cause is the toolchain
  re-bootstrap between runs. This was not isolated. The autoboot DOL hashes from the same gate run did
  reproduce.
- **Not run:** real Wii hardware; the browser UI and ROM browser on a Wii; any performance measurement on
  hardware; G2 pool-8192 wrap for the NDS stub JIT (NOT_RUN_WRAP); a full G2 matrix on `interp.dol` is not
  applicable.
- The `NooDS-Wii/evidence/` directory holds untracked logs from this session. The handoff README cites them.
- Harness numbers (~25 ms/f JIT, ~31 ms/f interpreter from older runs) are harness-only.

## 5. Invariants (preserve these)

The interpreter is the oracle. Preserve instruction-specific behavior.

- A JIT block **enters with `R15 = instrPC + 8`** (ARM). The block epilogue recomputes `pcData` and pipeline
  state. The interpreter handler slow path expects `r15Instr = instrPC + totalDelta + 8` before a load.
- `flushPipeline()`: ARM `r15 = (r15 & ~3) + 4`, Thumb `(r15 & ~1) + 2`, refills `pipeline[0]/[1]`.
- `ldrOf`/`ldrbOf` with `Rd=15` apply `cpsr |= (val & !arm7) << 5`; `ldrhOf` with `Rd=15` does **not**.
  All three taken PC-load handlers return 5 cycles. Check other addressing forms individually.
- `Interpreter::condition` is a lookup table: 0 = false, 1 = execute, 2 = reserved (`cond=0xF`).
  Reserved does not mean "always skip"; `handleReserved()` handles BLX-immediate and HLE/DLDI.
- PC-destination load terminator costs: not taken = linear path +1 cycle; taken = +5 cycles.
- MMIO fast paths are guarded per address and fall back to the interpreter for all other addresses.
- Bus/MMIO semantics, unaligned access, SMC invalidation, and pipeline/flag exactness are required.
- `tryExecuteBlock()` refuses block entry when `cpu->cycles + scaledPrefix >= core->events[0].cycles`
  (ARM7 in NDS mode scales prefix by 2). Keep the boundary refusal.
- Broadway has split I/D caches. Every emitted-code path must flush its range through `flushCodeRange()`.
  Dolphin cannot prove real-Wii cache coherence.
- Follow the `jit_bridge.S` contract: `r1` stack, `r2` and `r13` system/SDA anchors, `r31` = `Interpreter*`,
  the bridge saves and restores `r28–r31`. Do not assume volatile scratch, CR, LR, or CTR survives C helper calls.
- Target 32-bit Broadway: no AltiVec, `isel`, or 64-bit integer instructions. Memory fast paths keep
  address, mapping, width, alignment, MMIO, and SMC guards. Do not add unguarded raw guest-memory access.
- Keep poll skipping off (`NOODS_JIT_POLL_SKIP`).
- `Core` is larger than 32 KB (§3.6).

## 6. Environment and setup (sandbox resets remove installed packages)

Packages and toolchain are **not** kept between sessions. Reinstall at the start of each session.

```bash
sudo apt-get install -y zstd qemu-user-static g++-powerpc-linux-gnu binutils-arm-linux-gnueabi \
  binutils-powerpc-linux-gnu dolphin-emu xvfb xauth dosfstools mtools
sudo env DEVKITPRO=/opt/devkitpro bash NooDS-Wii/tools/bootstrap_toolchain.sh   # devkitPPC + libogc
```

- `zstd` is required by the bootstrap's `tar`. The bootstrap installs to `/opt/devkitpro`; that path is
  outside the workspace and is not persisted.
- Dolphin headless runner: `NooDS-Wii/tools/run_dolphin.sh` (uses `dolphin-emu-nogui` and `flock`).

Check after install: `which qemu-ppc-static powerpc-linux-gnu-g++ dolphin-emu-nogui mkfs.vfat mcopy`.

## 7. Parked: STR/STRB (do not re-enable without solving)

`isNativeStoreArm` recognizes plain STR/STRB and the emitter handles them, but acceptance is disabled.

- Build with STR/STRB enabled and `BLOCKS=0`: bit-exact vs the interpreter (257,789-instruction trace).
- Under block compilation on rockwrestler: a +1 ARM9 cycle at frame 1, then PC/pipeline off by one word.
  Tier bisect: imm-offset STR/STRB fails; register-offset, STRB-only, and STR-only pass.
  Hypothesis (unproven): a block containing a native STR/STRB ending with a stale `pipeline[0]`/`R15`.
- Reclaimable if solved: `020001E4 str r1,[r3],#4` and the `02000388 / 02000348 / 0200038C` sites.
- Method: temporary compile-time tier macro plus a dense checkpoint sweep against the interpreter.
  Remove instrumentation afterwards.
- Not re-tested after the §3.6 time fix. Re-run the bisect before assuming it still fails or passes.

## 8. Reproduce the harness checks

```bash
export WORKSPACE=/home/user PROJ=/home/user/Actions-Minutes2/NooDS-Wii
export SRC=$PROJ/NooDS-Wii
cd /home/user/Actions-Minutes2/harness
# rebuild if sources change (needs powerpc-linux-gnu-g++):
SRC=$SRC bash build.sh interp bin_interp && SRC=$SRC bash build.sh jit bin_jit
rm -rf bin_interp/obj bin_jit/obj
mkdir -p /home/user/parity && cd /home/user/parity
N=$PROJ/nds/rockwrestler.nds; G=$PROJ/gba/suite.gba; H=/home/user/Actions-Minutes2/harness
qemu-ppc-static $H/bin_interp/noods_host --rom $N --frames 120 --checkpoints 30,60,120 --out ni.log
qemu-ppc-static $H/bin_jit/noods_host    --rom $N --frames 120 --checkpoints 30,60,120 --out nj.log
python3 $H/compare.py ni.log nj.log          # expect MISMATCHES=0, STATUS=PASS
```

Use `--checkpoints` lists (the dense sweep is `seq -s, 1 60`). Compare runs with `compare.py`. Outputs
go outside the repo.

## 9. Build the shipped DOLs

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH"
cd $PROJ
# JIT browser (clean first; the browser helper reuses build_jit_browser/ and can report "Nothing to be done")
make clean JIT=1 BLOCKS=1 CONFIG_TAG=jit_browser
bash tools/build_browser_jit.sh                    # -> jit.dol
# Interpreter browser
make clean JIT=0 BLOCKS=0 CONFIG_TAG=interp_browser
CXXFLAGS="-DNOODS_FORCE_BROWSER=1" make -j2 JIT=0 BLOCKS=0 CONFIG_TAG=interp_browser
cp -f NooDS-Wii_interp_browser.dol interp.dol
# Remove the other build outputs from the project root afterwards (only the two DOLs are shipped):
rm -f NooDS-Wii*.dol NooDS-Wii*.elf NooDS-Wii*.elf.map && rm -rf build_jit* build_interp*
sha256sum interp.dol jit.dol
```

Autoboot DOLs (for the gate scripts, which expect `NooDS-Wii_jit{0_blocks0,1_blocks0,1_blocks1}.dol`):
`make -j2 JIT=<0|1> BLOCKS=<0|1> CONFIG_TAG=jit<J>_blocks<B>`. Keep them out of `handoff/`.

## 10. Keep handoff in sync

- After any change to `NooDS-Wii/NooDS-Wii/arm_jit.*` or `ipc.h`: copy the changed files to
  `handoff/NooDS-Wii/NooDS-Wii/`, verify with `cmp`, rebuild both DOLs, and update the hashes in `handoff/README.md`.
- Do not ship patch files. The sources are the deliverable.
- Ship `interp.dol` and `jit.dol` only after a clean build and validation. Do not leave an old DOL labeled
  as matching a newer source. The DOLs stay in the repo at `NooDS-Wii/`, not in `handoff/`.

## 11. Dolphin browser check

```bash
cd $PROJ && bash tools/build_sd_image.sh /tmp/noods_sd.img
bash tools/run_dolphin.sh interp.dol /tmp/noods_sd.img 90 /tmp/dol_interp 'FILE_BROWSER active'
bash tools/run_dolphin.sh jit.dol    /tmp/noods_sd.img 90 /tmp/dol_jit    'FILE_BROWSER active'
```

Expect `STATUS=PASS`, `SENTINEL_HIT=1`. `RC=143` is the expected kill after the sentinel. Check the result
file, then `grep -a -i 'panic\|fatal\|exception\|assert' <out>/dolphin.log` (should be empty). The browser
listing itself is not verified by this check.

## 12. Gate matrix

Run from `$PROJ` after §9 and the sandbox setup. Each Dolphin run is serialized by `flock`.

- **G0:** build `jit{0,1}_blocks{0,1}` autoboot DOLs (0/0, 1/0, 1/1). Hash them.
- **G1:** `bash tools/check_g1_encoder.sh` → `FORMS_MATCHED=52/52` (required 44), `STATUS=PASS`.
- **G2:** for `pool in 0 8192` and `kind in nds gba`:
  - NDS: `rom=sd:/nds/rockwrestler.nds`, checkpoints `15,30,45`, frames 45, timeout 90 s.
  - GBA: `rom=sd:/gba/suite.gba`, checkpoints `50,60,120`, frames 120, timeout 180 s.
  - Run `bash tools/run_g2_case.sh NooDS-Wii_<cfg>.dol <label> <rom> <points> <frames> <secs> <pool>` for
    `jit0_blocks0` (reference) and `jit1_blocks0`, `jit1_blocks1`. Require `STATUS=PASS` and
    `SENTINEL_HIT=1` in `evidence/g2_<label>/result.txt`.
  - Compare with `python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_<ref> --cand evidence/g2_<cand> --out evidence/g2_compare_<cand>.json`.
  - Pool 8192 = 2 × `MAX_BLOCK_WORDS` (4096). `jit1_blocks0` (stub) never wraps: record `NOT_RUN_WRAP`
    rather than failing. A block-JIT pool-8192 case must wrap; require `poolWraps > 0` there.
- **G3:** `bash tools/run_suite_gba.sh NooDS-Wii_<cfg>.dol <cfg> <outdir> 300` for the three configs. Require
  `ALL DONE`, identical `gbaout_<cfg>.log` hashes, and
  `python3 tools/compare_runs.py --mode suites --ref <outdir>/mgba_summary_jit0_blocks0.json --cand <outdir>/mgba_summary_<cfg>.json`.
- **Browser:** §9 and §11.
- **Hardware:** the user runs the final `jit.dol` and `interp.dol` on a Wii. Do not infer hardware FPS or
  cache correctness from Dolphin.

The gate runner script used for the last full run was a temporary file and was deleted. Repeat the
steps above. Use a new evidence label for each run.

The G3 suite counts (3506 passed / 3492 failed) are differential parity, not mGBA compatibility.

## 13. Next steps, in priority order

1. **Real Wii test** of `jit.dol` and `interp.dol` (ROM browser, NDS and GBA loading). Ask the user for the
   results; this is the only check that matters for the user's FPS question.
2. **STR/STRB** (§7): re-run the tier bisect with the §3.6 time fix in place. Fix only if it reproduces.
3. **Re-profile ARM9 and ARM7 fallbacks** with the current tree. The old profile in the earlier document predates
   Bcc heads and the time fix. Do not claim full coverage without a new profile.
4. **Pool-wrap workload for NDS stub JIT** (G2 NOT_RUN_WRAP) if the wrap gate is to be complete.
5. **Game-wide speed:** profile a reproducible gameplay scene before doing more JIT work. Dolphin and harness
   numbers have not matched user-visible FPS.

## 14. Dead ends (do not retry)

- **STR/STRB native acceptance under blocks** (§7) until solved.
- **`NOODS_JIT_POLL_SKIP=1`**: large regression, skipped 0 iterations. ARM7 wait loops write IPCSYNC via STRH
  and do conditional `LDR PC`. Do not force skip.
- **Global `-O3`**: slightly slower than `-O2`.
- **Generic native halfword loads without the time fix**: diverged in GBA mode (§3.6). The IPCSYNC-only
  path (`LDRH @0x04000180`) is the verified form.
- **Patch scripts that re-run on already-patched source**: duplicate predicates and emitters. This is why
  no patch files are shipped.
- **`int16_t` casts of `offsetof(Core, …)`**: silently truncated (§3.6).
- **Hand-rolled ARM opcode decoding.** Use `arm-linux-gnueabi-objdump -D -b binary -m arm`.
- **Diagnostic traces**: record the guest PC before execution (`*registers[15] - step`), or branch targets
  get mislabeled.
- **`sed -n "$(grep …)"`** fails when grep matches multiple lines. Use explicit line numbers.
- **Old Dolphin-era DISPSTAT and KEYINPUT results** (~101.9 → ~88.0 ms/frame) produced no perceptible
  change for the user. Do not re-litigate them as wins.

## 15. Housekeeping

- Snapshots are capped (~128 MB / 10,000 files). Keep `/home/user` small. Put the toolchain, SD images,
  and Dolphin user dirs outside the workspace or delete them after use.
- Remove only generated experiment outputs. Keep sources, both shipped DOLs, `handoff/`, ROMs, and evidence
  that the handoff README cites.
- Do not commit. `git status` will show changed tracked files (the sources, both DOLs, `harness/bin_*`,
  and the handoff files) and untracked new files (`interp.dol`, `evidence/`).
- Report in short statements. End with what changed, what was verified (with paths), and what remains
  unverified.
