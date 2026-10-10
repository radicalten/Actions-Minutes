# AGENTS.md — NooDS-Wii: ARM → Wii PowerPC JIT

> Operating guide for the next agent. Last updated 2026-10-10 at the end of the JIT performance session.
> Companion: `Handoff.md` (status and resume point).

## 0. Standing directives (from the user; do not drop)

- **One sequential worker.** No subagents or detached implementation jobs. Serialize Dolphin runs with `flock` (`tools/run_dolphin.sh` does this).
- **Do not commit.** The user commits when finished. Keep all changes in the workspace.
- **Keep G1–G3 passing** after every change. A performance change that breaks parity is not acceptable.
- **Report honestly** with `PASS`, `FAIL`, `HANG` or `NOT_RUN` and exact evidence (counts, hashes, exit codes). A missing file or capture is `NOT_RUN`, never zero mismatches. Do not fabricate output.
- **Never `cat` a Dolphin log.** It contains NUL bytes and ALSA noise. Read `result.txt`, `g2_state.log` or summaries with `grep`/`tail`.
- **Do not `rm -rf` a checkout or reference tree** unless the user asks. Inspect `git status` before resetting anything.
- **Target Wii only** (`-mrvl`). Never build, link or include the `vbagx` reference clone.
- **The interpreter is the correctness oracle and the fallback.** Keep `JIT=0 BLOCKS=0` working.
- **Performance is measured by `accum_us`** (PERF line, frame 45, rockwrestler). Compare numbers only within one session, since they are wall-clock.

## 1. Repository layout

Git root: `/home/user/NooDS-Wii` (branch `main`, no remote configured in this clone).

```text
/home/user/NooDS-Wii/                     git root
  AGENTS.md, Handoff.md                    this guide and the handoff report
  NooDS-Wii/                               project directory (build here)
    Makefile                               JIT=0|1 BLOCKS=0|1 (defaults 0 0)
    README.md, LICENSE
    NooDS-Wii/                             emulator + JIT source
      arm_jit.h, arm_jit.cpp               ARM/Thumb → PowerPC JIT (JitPpc encoder, ArmPpcJit)
      jit_bridge.S                         jit_enter / jit_exit (PPC EABI bridge)
      interpreter*.cpp/.h                  interpreter, dispatch (runOpcode, runCoreNds)
      memory.cpp/.h                        memory map, I/O; DISPSTAT/VCOUNT/IPCSYNC read fast path
      main.cpp                             NoodsTestHarness (sd:/autoboot.txt, checkpoints, dumps)
      ...                                  other emulator modules
    tools/                                 bootstrap_toolchain.sh, build_sd_image.sh, run_dolphin.sh,
                                           run_g2_case.sh, run_suite_gba.sh, compare_runs.py,
                                           check_g1_encoder.sh, test_encoder.cpp
    nds/rockwrestler.nds, gba/suite.gba    test ROMs (tracked)
    evidence/                              run records, compare JSONs, summaries (see §6)
```

**Permissions:** git records `tools/*.sh` and `tools/*.py` as mode `100644`. On disk they are executable (`chmod +x` was applied). Run them via `bash tools/…` if in doubt. Decide with the user whether to commit the mode change (`git update-index --chmod=+x`).

## 2. Current state (2026-10-10)

**Verified build:** `make -j2 JIT=1 BLOCKS=1` from the current working tree produces a DOL with SHA-256 `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e` (the same hash as the `/tmp/br1.dol` that was gated). No DOL is kept in the tree; the cleanup removed the build outputs. Rebuild, then check the hash. This build contains all the uncommitted JIT work listed above, including the inline same-page branch exit (`emitInlineBranchArm`).

**Uncommitted changes (what the user will commit).** The diff against HEAD `fdcb968` is 2 files, +343 / −10, and it is the whole JIT work of the session, not just the last change:

| File | Change |
|---|---|
| `NooDS-Wii/NooDS-Wii/arm_jit.cpp` | Native loads (LDR/LDRH/LDRB, fast path plus helper slow path) and native STRH; exact R15 sync around helper calls (`emitHelperR15Sync`); inline same-page B/BL exit (`emitInlineBranchArm`, the latest change) and its call in `compileBlock`. |
| `NooDS-Wii/NooDS-Wii/arm_jit.h` | Declarations for the above, `sthbrx`, `MAX_BLOCK_WORDS = 1024`. `emitInlineBranchArm` must stay `static` (a non-static member fails to build from `compileBlock`). |
| `NooDS-Wii/tools/*.sh`, `*.py` (7 files) | Mode change `100644 → 100755` only. No content change. |

Snapshots: `/tmp/arm_jit_br1.cpp` and `/tmp/arm_jit_br1.h` match the working tree (may not survive the sandbox).

**Gate results on this build:**

| Gate | Result | Evidence |
|---|---|---|
| G1 PPC encoder oracle | **PASS**, 52/52 (REQUIRED_MINIMUM 44/44) | `evidence/g1_encoder_oracle_br1_2026-10-10.log` |
| G2 rockwrestler checkpoints 15/30/45 vs interpreter | **PASS**, `mismatch_count` 0, memory and framebuffer hashes match | `evidence/g2_compare_br1.json` (run `evidence/g2_g2_br1/`) |
| G3 mGBA auto-suite vs interpreter | **PASS**, 13 suites, 6,998 tests, 3,506 passed, 0 mismatches, gbaout SHA `97b08848…` | `evidence/g3_compare_br1.json` (run `evidence/g3_br1/`) |

**Performance (frame 45, rockwrestler, same session):**

| Build | accum_us |
|---|---|
| Interpreter (`JIT=0 BLOCKS=0`) | 4,568,589 |
| Previous baseline, exact R15 sync (`g2_g2_final4`) | 4,913,380 |
| **Current (inline branch exit)** | **4,586,792** (≈6.6% faster than baseline; ≈0.4% slower than the interpreter) |

Do not claim a JIT speedup over the interpreter. The JIT is still roughly even with it on this measurement.

## 3. Build and run

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
cd /home/user/NooDS-Wii/NooDS-Wii

make clean JIT=1 BLOCKS=1 && make -j2 JIT=1 BLOCKS=1     # → NooDS-Wii_jit1_blocks1.dol
make clean JIT=0 BLOCKS=0 && make -j2 JIT=0 BLOCKS=0     # → NooDS-Wii_jit0_blocks0.dol (oracle)
```

Toolchain (if `/opt/devkitpro` is missing, which happens between sessions):

```bash
sudo tools/bootstrap_toolchain.sh        # devkitPPC r50, libogc 3.1.0, pinned by SHA-256, ~5 s
```

Host packages: `build-essential git curl dolphin-emu zstd xvfb mtools dosfstools binutils-arm-none-eabi python3-pyelftools`.

**Gate commands:**

```bash
# G1
bash tools/check_g1_encoder.sh                            # expects STATUS=PASS, 52/52

# G2 (run each DOL, then compare against the interpreter reference)
bash tools/run_g2_case.sh NooDS-Wii_jit1_blocks1.dol <label> "sd:/nds/rockwrestler.nds" "15,30,45" 45 150 0
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_rw_interp \
        --cand evidence/g2_g2_<label> --out evidence/g2_compare_<label>.json

# G3
bash tools/run_suite_gba.sh <dol> <label> evidence/g3_<label> 600
python3 tools/compare_runs.py --mode suites --ref evidence/mgba_summary_interp.json \
        --cand evidence/g3_<label>/mgba_summary_<label>.json --out evidence/g3_compare_<label>.json
```

Notes on G2:
- `evidence/g2_rw_interp` is the interpreter reference. Its build hash was not recorded. To refresh it, rebuild `JIT=0 BLOCKS=0` and re-run G2.
- The PERF line and checkpoint lines come from `g2_state.log`. `result.txt` `STATUS` only means the emulator reached its sentinel. It is **not** a parity result.
- `evidence/mgba_summary_interp.json` is the G3 interpreter reference. Its build hash was also not recorded.
- A session-local wrapper at `/home/user/perf/check.sh` ran the G2 steps above. It is outside the repo.

## 4. Gate definitions

| Gate | Meaning |
|---|---|
| G0 | Clean builds of `JIT=0 BLOCKS=0`, `JIT=1 BLOCKS=0`, `JIT=1 BLOCKS=1`. |
| G1 | 52 PowerPC 750CL instruction forms encode bit-exact against `powerpc-eabi-as`/`objdump`. |
| G2 | Differential checkpoint parity vs. the interpreter at frames 15, 30, 45 (rockwrestler): cycles, registers, CPSR, memory dump, framebuffer. |
| G3 | mGBA auto-suite (`suite.gba`) completes with identical gbaout output and the same pass/fail counts as the interpreter. |

**Not verified in this repo:** G0 hash records, G4–G6 summaries and the earlier base-build hashes cited by older documents. Those files are not in the repository. `deliverables/` and `WORKSPACE_MANIFEST.md` do not exist. Re-run the gates instead of trusting the old hashes.

## 5. Implementation contracts (JIT)

- **Entry:** `jit_enter` (`jit_bridge.S`) uses a 32-byte frame, saves LR at `36(r1)` and `r28..r31` at `16(r1)..28(r1)`, and sets `r31 = Interpreter *`. Frame slots `8(r1)` and `12(r1)` are free padding.
- **Stubs:** Phase 1 (`BLOCKS=0`) compiles one stub per guest instruction that calls the resolved `Interpreter` handler (`resolveArmHandler`/`resolveThumbHandler`) with `(cpu, opcode)`.
- **Blocks:** Phase 2 (`BLOCKS=1`) compiles up to 24 instructions within one 4 KB page into native code. It handles ALU, shifts, flags, CLZ (ARM9), single loads as the first instruction, and a terminating branch. Blocks are cached in `blockTable` and validated against `pageEpoch` and a tail hash.
- **Native loads/stores:** the fast path uses the page read/write tables. On a miss, the slow path calls the interpreter handler through `bctrl`. `emitHelperR15Sync` sets R15 to the exact instruction value around the call (open-bus and BIOS reads depend on it).
- **Inline branch exit (current change):** `emitInlineBranchArm` handles a B/BL whose target and fall-through stay on the block's 4 KB page. It writes R15, `pcData` and the pipeline itself, and returns the same cycle counts as the interpreter path (prefix + 3 when taken, prefix + 1 when not taken). Targets on another page go through `execBranchArm`.
- **Helper calls clobber LR.** Every block that calls a helper restores LR from the frame in its epilogue. Check any new helper call site for this.
- **Invalidation:** `invalidateSharedAddr` on guest writes, `invalidateCpu` on map updates, `invalidateAll` after DLDI patching. Blocks on pages without block bits skip this check.
- **Pool:** 1 MiB code pool in MEM2 (`DEFAULT_POOL_BYTES`). `POOL_LIMIT_WORDS` can shrink it for wrap tests.
- **Cache coherency:** every emitted range is flushed with `dcbst; sync; icbi; sync; isync` (`flushCodeRange`).
- **Scheduler guard:** a block is entered only if `cpu->cycles + prefix < core->events[0].cycles`.
- **Runtime flags:** `NOODS_JIT_POLL_SKIP` (default 0, compiled out; ~1.7× slower when enabled, never fired on rockwrestler). There is no `NOODS_JIT_IDLE_SKIP` in this tree.

## 6. Evidence layout (`NooDS-Wii/evidence/`)

Kept: compare JSONs (`g2_compare_*.json`, `g3_compare_*.json`), G1 logs, `experiments_2026-10-10.md` (every run kept or deleted this session, with results), retained run directories with `result.txt`, `g2_state.log`, `autoboot.txt`, memory and framebuffer dumps (`g2_rw_interp`, `g2_rw_jit1_blocks1`, `g2_g2_br1`, `g2_g2_final4`), G3 `gbaout_*.log`, `mgba_summary_*.json`, and the G-1 `vbagx_*` study notes.

Removed in the 2026-10-10 cleanup: Dolphin logs, root DOL/ELF/MAP copies, `build_*` directories, scratch G2 runs (`g2_bis_*`, `g2_fcand`, `g2_fref`, `g2_g2_*` except those listed above). Their results are summarized in `experiments_2026-10-10.md`.

## 7. Dead ends (do not retry without a new idea)

- **Self-loop chaining with a dirty flag** (native loop-back when no helper ran). It was correct once fixed, but never fires. Every self-loop iteration on rockwrestler calls a helper. Result: no speedup, slightly slower. Experiment sources are at `/home/user/perf/chain_experiment/` (outside the repo).
- **Chaining across helper calls** would need IRQ, halt and scheduler re-checks after each helper. Not attempted; large risk.
- **Native halfword loads** (`kNativeHalfLoads=true`): G2 PASS, no speedup. Left off.
- **PC-base native LDR** (Rn = PC): G2 PASS, no speedup. Gated off.
- **Idle skip / poll skip:** poll skip is G2-clean but never fires on rockwrestler and is slower when enabled. Idle skip was a net loss and is not present in this tree.
- **Inline branch exits do not reduce the dispatch count**, they only remove ARM7 C-side branch dispatches.

## 8. Open items (next work)

1. **ARM7 `LDR pc`:** loads into PC are excluded from native compilation (`arm_jit.cpp`, the `rd == 15` checks around lines 86 and 120) and go through the interpreter fallback. The notes put this at about 4% of profiled time. A native version has to reproduce the ARM7 pipeline refill and cycle accounting exactly. Not started.
2. **`bctrl` LR clobber audit:** the source has `bctrl` call sites that rely on block epilogues to restore LR (comments near the helper emitters and `compileBlock`). The old line numbers (847, 1152, 1555) are stale. Re-audit every `bctrl` with `grep -n bctrl arm_jit.cpp`.
3. **Performance:** most remaining cost is per-dispatch overhead and helper calls on I/O loops (rockwrestler self-loops poll I/O). Real gains need a design that keeps interrupts and scheduling correct across helpers. Discuss with the user before starting.
4. **Documentation of the reference DOLs** (see §3 notes).
