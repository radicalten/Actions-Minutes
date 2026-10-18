# Agents2-5 Dolphin speedup results

**Date:** 2026-10-18. **Target:** headless Dolphin `[master] 2503`, Null video backend. **Workload:** verified `suite.nds`, frame-650 checkpoint, scripted page input. This report covers only the separate speedup worktree; the correctness-fixed reference tree and original DOL remain frozen.

## Current best candidate in the speedup worktree

Two conservative changes are active in this worktree:

1. Build user-facing JIT DOLs at `-O3` by default (`OPT_LEVEL ?= 3`; unrolling remains on, LTO remains off).
2. Compile out diagnostic JIT counter updates in normal browser builds (`BROWSER=1` defaults `JIT_STATS=0`). Autoboot diagnostic builds still default to counters on; explicitly use `JIT_STATS=0` for a performance-comparable test build. `JIT_STATS=1` retains the detailed counters.

The hot-path stats updates are diagnostic only: the counters do not affect emulation decisions. When disabled, `dumpStats()` reports `JIT_STATS enabled=0 counters=disabled`. All changes are in the speedup worktree; no correctness handlers or CPU semantics were edited.

## Timing comparison

All clean samples used fresh Dolphin profiles and fresh SD copies of the authorized replacement FAT image, the same verified ROM, page-3 input at `[600,605)` and `[620,625)`, and frame 650. `avg_frame_us` is the emulated core-time metric; `core_fps` is affected by display/vsync.

| Build | `avg_frame_us` samples | Mean | Change vs. O2/stats baseline | DOL size |
|---|---|---:|---:|---:|
| O2, counters on (clean baseline) | 95,197; 95,197; 95,197 | **95,197** | — | 1,376,032 B |
| O3, counters on | 94,284; 94,284; 94,284 | 94,284 | −0.96% | 1,481,536 B (+7.67%) |
| O2, counters off | 90,733; 90,733; 90,733 | 90,733 | −4.69% | 1,371,296 B |
| **O3, counters off (current candidate)** | **86,052; 86,052; 86,052** | **86,052** | **−9.61%** | **1,475,968 B (+7.26%)** |

The O3/no-stats candidate saves **9,145 µs per frame** on this Dolphin workload. The three independent frame-650 memory traces for each of the baseline, O2/no-stats, and O3/no-stats builds all match at ARM9-family **13,252 pass / 128 fail** and ARM7-family **12,056 pass / 0 fail**; no extra failures or unrun vectors appeared. After introducing the `JIT_STATS` macro gates, a fresh O3/stats-on diagnostic build also completed frame 650 at 94,284 µs/frame, emitted the same detailed counters as the clean baseline, and retained the same trace totals. Its experimental DOL was pruned; the retained DOLs are the best candidate and clean O2 baseline listed below.

- Clean O2 test baseline: `baseline-artifacts/NooDS-Wii-Agents2-5-JIT-test-browser0-fullram-scriptinput-O2.dol`, SHA-256 `b13d49b23668d07e4a7bed2a6d0e9c8f1c9eeb021f31212d475019943c256ed6`.
- Best test candidate: `NooDS-Wii-Agents2-5-Speedup-Test.dol`, SHA-256 `53e41b481deada0bd102ca63fa1988933e8b6c1db3dd9dcf54f870328ebe4dda` (1,475,968 bytes).
- User-facing O3/no-stats browser DOL: `NooDS-Wii-Agents2-5-Speedup.dol`, SHA-256 `80a6bc9b54d80cd80c8f70c2bb1ab113cff7b4d20360ba90f95a256ac6361d6c` (1,468,960 bytes). It was built with `BROWSER=1`; its browser marker string is present. Runtime startup is still **unverified**: Dolphin’s first-run telemetry dialog blocked the GUI smoke, and a 10-second headless smoke produced no guest browser log marker.

Page 2 was tested with the current O3/no-stats candidate using only the Right pulse at frames 600–604; page 2 displayed groups 0–13, with groups 2/3 the only nonpassing groups. Page 3 was tested with Right pulses at frames 600–604 and 620–624; groups 14–26 all passed. Current validation logs and screenshots are in `test-evidence/page2-frame650.*` and `test-evidence/page3-frame650.*`.

To keep the workspace focused, rejected-experiment DOLs and duplicate run dumps were pruned. Their measured outcomes and known pitfalls remain in the table below; only the O2 baseline and current candidate DOLs are retained.

## Other experiments

| Experiment | Result | Decision |
|---|---|---|
| O2 + `-flto` | 95,756 µs (one run), +0.59%; 1,402,528 B | Reject: slower. LTO also surfaces the existing GBA/high-resolution `Gpu::getFrame` potential buffer overflow warning at `gpu.cpp:125` (786,432-byte write into a 393,216-byte `tempBuffer` declared in `main.cpp:462`). This path is not covered by the suite; no fix was made here. |
| O3 + `-flto` | 93,754 µs on three runs, −1.52%; 1,519,584 B | Do not adopt: the same pre-existing potential overflow warning appears with LTO. |
| O3 with loop unrolling disabled | 97,071 µs (one run), +1.97% | Reject; unrolling was faster on this workload. |
| `NOODS_JIT_POLL_SKIP=1` | 151,796 µs (one run), +59.45%; 0 poll skips | Reject for this suite; no idle loop was skipped. Correctness counts remained unchanged. |
| 4 MiB JIT code-pool request | 96,413 µs; allocator fell back to the 64 KiB BSS pool (`poolLimit=16,384`, `poolWraps=936`) | Reject; the requested MEM2 allocation did not fit. The default remains 1 MiB. |
| 2 MiB JIT code-pool request | Dolphin exited before frame 1 with invalid accesses and an empty checkpoint | Invalid run; do not use. The pool-size tuning parameter was removed from the worktree. |
| JIT timing profiler | Instrumented run reported 115,827 µs/frame; CPU9 block dispatch dominated its measured profile | Diagnostic only, not a timing sample. Instrumentation adds overhead. |

The `-O3 -flto` warning is not part of the candidate: the current candidate uses O3 without LTO and builds cleanly. No changes were made to the original source to address the separate rendering-buffer warning.

## Reproduction

From `/home/user/repo/Agents2-5-NooDS-Wii-Speedup`:

```sh
# Normal, user-facing JIT DOL: BROWSER=1 and JIT_STATS=0 by default.
make -j"$(nproc)" JIT=1 BLOCKS=1

# Diagnostic frame-650 DOL with stats enabled (default for BROWSER=0).
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 G2_SCRIPTED_INPUT=1

# Performance candidate: O3/no-stats test DOL.
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 \
  G2_SCRIPTED_INPUT=1 OPT_LEVEL=3 JIT_STATS=0
```

For devkitPPC/Dolphin setup, fresh SD/profile procedure, build commands, and capture checks, follow `HANDOFF.md`. The retained DOL checksums are in `SHA256SUMS.txt`, and baseline/profiling details are in `PERFORMANCE-BASELINE.md`. Do not compare an instrumented `JIT_PROFILE=1` run to these timings.

## Caveat

The 9.61% result is specific to this suite under Dolphin; `suite.nds` is a correctness workload, not a representative game. O3 increases the DOL file by about 7.3% for the diagnostic build. Do not claim a real-Wii speedup without testing on hardware; the original O2 reference DOL and source remain preserved.
