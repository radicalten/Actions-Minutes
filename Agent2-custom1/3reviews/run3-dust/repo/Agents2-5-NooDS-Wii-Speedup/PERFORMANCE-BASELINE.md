# Dolphin performance baseline — correctness-fixed Agents2-5

**Target selected:** Dolphin headless. **Workload:** supplied `suite.nds`. **Date:** 2026-10-18.

## Provenance

- Source: frozen correctness-fixed reference at `/home/user/repo/Agents2-5-NooDS-Wii` (O2 Makefile defaults; no speedup-tree stats gates).
- Clean test DOL: `baseline-artifacts/NooDS-Wii-Agents2-5-JIT-test-browser0-fullram-scriptinput-O2.dol`
- Test DOL SHA-256: `b13d49b23668d07e4a7bed2a6d0e9c8f1c9eeb021f31212d475019943c256ed6` (reproduced byte-for-byte from the frozen source on 2026-10-18).
- Build flags: `JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 G2_SCRIPTED_INPUT=1`.
- ROM SHA-256: `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`.
- Dolphin: `Dolphin [master] 2503`; headless backend `Null`, dummy SDL video/audio.
- Historical baseline SD setup: each of the three clean O2 runs used a fresh clone of the then-current user-authorized 64 MiB FAT image (SHA-256 `9569b9537aa1a11ce4c9f591cf5fd1a644a39c7548431fbd3a5924a51ef6f32c`), with the verified ROM and page-3 schedule recopied for each run. That image was absent from a later workspace snapshot. The replacement image currently retained for reproduction is a 64 MiB FAT16 image at `/home/user/repo/dolphin-test/cpu-correctness-iter1/sd.raw`, SHA-256 `b970e17e11421f0a743c92f54cca2f3facd7302ed67c94977e0b2dfc03207df0`; it was used for the fresh page-2/page-3 validation runs documented in `test-evidence/`.
- Schedule in each fresh run image: `MAX_FRAMES=650`, `CHECKPOINTS=650`, Right at `[600,605)` and `[620,625)`; frame 650 displays page 3.

## Results

Three independent fresh-profile/fresh-SD runs gave the same measured `runCore` time:

| Run | `accum_us` | `avg_frame_us` | Rendered frames | `core_fps` |
|---|---:|---:|---:|---:|
| 1 | 61,878,535 | **95,197** | 217 / 650 | 11 |
| 2 | 61,878,541 | **95,197** | 217 / 650 | 11 |
| 3 | 61,878,544 | **95,197** | 217 / 650 | 11 |

Frame-650 correctness remained at **25/27 groups**, 0 unrun; CPU9-family trace was 13,252 pass / 128 fail, and CPU7-family trace was 12,056 pass / 0 fail. Page 3 showed groups 14–26 passing. The 128 combined ARM9/ARM9 Thumb failures remain within the accepted allowance.

`avg_frame_us` measures the emulated core’s accumulated run time in this Dolphin setup; it is the main before/after metric. `core_fps` is affected by video/vsync and should not be treated as a pure CPU throughput measure. This is a Dolphin-only baseline, not a prediction of real-Wii performance.

## Deterministic JIT counters (frame 650)

```text
JIT_STATS enabled=1 blocks=1 disp9=91949880 disp7=25717888 stubComp9=14156 stubComp7=27445 stubHit9=945614 stubHit7=757495 blkComp9=35275 blkComp7=36279 blkHit9=88809607 blkHit7=23568959 blkInsn9=192022483 blkInsn7=80124136 natInsn9=292079 natInsn7=709789 fbInsn9=2190223 fbInsn7=1369940 cp15Exit9=11715 smcInv9=3043306 smcInv7=3043306 mapInv9=3524 mapInv7=117 poolUsed=113754 poolLimit=262144 poolWraps=21 poolAddr=0x93296520 blkFail7=0 blkFail9=0 fbArm7=232864,2744,957612,36188,0,9184,0,0 fbThumb7=0,0,2296,268,141,26,1002,792
```

The three per-run SD clones and duplicate raw dumps were pruned during workspace cleanup after the metrics and trace totals were recorded. Retained page-2/page-3 frame-650 logs and screenshots for the current candidate are in `test-evidence/`; the clean O2 test DOL is retained in `baseline-artifacts/`. Dolphin’s wrapper returned 137 after valid frame-650 captures were written; the checkpoint, schedule, capture sizes, and counters were checked before pruning.

## Instrumented JIT profile (diagnostic only)

A separate `JIT_PROFILE=1` frame-650 run reported 115,827 µs/frame. This is substantially instrumented and is **not comparable** to the clean baseline or candidate timings. The largest reported profile buckets were:

| CPU | Bucket | Count | Reported time |
|---|---|---:|---:|
| ARM9 | translated block execution | 80,155,286 | 28,741,553 µs |
| ARM9 | first-load translated block | 8,654,321 | 3,621,392 µs |
| ARM7 | first-load translated block | 13,706,499 | 8,216,503 µs |
| ARM9 | interpreter memory fallback | 1,907,691 | 951,008 µs |
| ARM9 | CP15 fallback | 11,715 | 550,978 µs |

These profiler buckets guided focus toward JIT block execution/dispatch, but they must not be summed as independent shares or used as throughput results. The raw profiler run/DOL was pruned; the key bucket totals above and the former profiler DOL SHA-256 (`0cbb5a8880f8a71d282529283b10022e1bf61366abfd53076435aa8dd5aff633`) are retained here for history.

## Comparison status

The clean `-O2`, stats-on test DOL remains the timing reference; do not substitute the earlier instrumented 99,428 µs/frame result. The current best experiment, `-O3` with JIT counters compiled out, measured **86,052 µs/frame** on three runs (9.61% lower) while preserving the same frame-650 CPU counts. An O3/stats-on build of the macro-gated counter code was also run once: it measured 94,284 µs/frame and emitted the same counter totals as this baseline. Experiment outcomes, retained DOL hashes, correctness checks, and build commands are in `OPTIMIZATION-RESULTS.md`.

The clean baseline browser and diagnostic DOLs remain preserved under `baseline-artifacts/`. Keep future changes in the speedup worktree, validate page 2 and page 3, retain the accepted 128 combined ARM9/ARM9 Thumb failures, and do not regress any passing vector. Timing remains Dolphin-specific; test on real Wii hardware before claiming hardware speedup.
