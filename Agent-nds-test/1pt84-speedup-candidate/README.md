# NooDS-Wii v1.83 — 3D benchmark + renderer load hoist

## What changed in v1.83

Two things, both measured on a workload that actually exercises the 3D renderer.

**1. A wolveslayer sequence that reaches real 3D gameplay.** The previous benchmark
never left the menus (`calls poly=0` at frame 2000), so 3D cost was invisible. The new
script (see `WOLF3D.md`) walks through the splash, the "Entering Area" load and into
the first 3D room with the character moving. The game consumes input *during* loading,
so movement presses must start after ~frame 1900.

**2. Renderer load hoist.** In the fallback span loop of `gpu_3d_renderer.cpp`, 29
`polygon->field` loads per pixel are now hoisted into locals before the loop; the
compiler cannot prove the framebuffer/depth/attrib stores don't alias the polygon
object. Bit-exact by construction, and verified bit-exact on every gate.

| workload (frameskip 0) | v1.82 | v1.83 | delta |
| --- | --- | --- | --- |
| wolveslayer **3D gameplay**, 3200 frames | 47,362,790 | 47,170,584 | **-0.41 %** |
| suite-poly, 6000 frames | 180,851,660 | 180,774,117 | -0.04 % |
| nds-2d-suite, 6000 frames | 23,939,837 | 23,937,291 | -0.02 % |

Measured cost profile of the 3D workload (PROFILE build): **3D renderer 42 %**, 2D 12 %,
CPU **1.7 %**. Inside the renderer, `calls poly=1,682,097` with `fallback=1,676,540` —
99.7 % of polygons miss the fast linear path, because it requires near-orthographic
spans. The per-pixel integer division in that fallback loop is the next big lever; see
`WOLF3D.md` for the size estimate and the caveat about the earlier failed DDA attempt.

**Harness (test build only):** framebuffer snapshots at every `--checkpoint`
(`fb_cp<frame>.bin`) and scriptable touch points
(`INPUT_TOUCH<n>_FROM/TO/X/Y`, n = 0..3) — the fixed centre tap was not enough to drive
the game's touch menus.

## v1.82 — predication correctness fix

v1.82 was v1.81 with the launch regression fixed. The speedups of v1.81 are intact:
the same 3.5 % on the CPU-bound workloads, bit-identical output on every accuracy
gate, frameskip 0 throughout.

## What went wrong in v1.81

v1.81 introduced *predication*: a conditional ARM instruction is no longer a block
terminator, it is translated inline and guarded at run time by a PowerPC condition
register test (`emitCondTest` / `groupSlot`). The guard is built from a cached copy
of the guest CPSR flags in CR0, and the block scanner happily put conditional
instructions into blocks that also contained other code.

Two things about that are unsafe, and both are fixed here.

### 1. CR0/CR7 are caller-saved

A compiled block calls C++ helpers in the middle of its body — a load that misses the
inline path (`emitNativeLoadArm`), a store, a register-shift helper. The PowerPC ABI
makes CR0..CR7 volatile across a call, so every such call silently destroys the
`CPSR -> CR0` tag that the guard depends on. The next guard then tested a register
that the C++ runtime had overwritten, i.e. it took the skip branch on an unrelated
bit. Whether it fires depends on the address being accessed and on what the helper
left behind — which is exactly why the accuracy suites (no game loader, no odd
addresses) never saw it, and why a game's file loader dies at boot.

v1.82 re-tags CR0 from CPSR at **every** guard group instead of caching it. Two
instructions per group; the tag can no longer be stale by construction.

### 2. A predicated instruction that reads PC corrupts the block exit

When a translated instruction reads R15, the emitter stores `instrPC + (m+2)*step`
into the guest R15 and records that in the compile-time `appliedPcDelta` bookkeeping.
The store is emitted *inside* the guard, but the bookkeeping is updated
unconditionally. If the guard skipped the instruction, the block still believed R15
had been advanced, and the exit fix-up (`remDelta = totalDelta - appliedPcDelta`)
added the wrong delta to a register that had not been written — a wild jump.

PC reads are now excluded from predication entirely (`isCondSafeArm` rejects
`Rn == 15` and `Rm == 15`). Such instructions terminate the block and go through the
interpreter, exactly as in v1.80.

## Regression test: `tests/cond-test.nds`

A self-checking DS ROM (`tests/make_cond_test.py`, 19 KB, no dependencies) that is an
*oracle-free* A/B test: it digests registers and CPSR after executing predicated
instruction runs and stores 720 digests plus a checksum in main RAM. Run it with a
build that routes conditional instructions through the interpreter
(`-DNOODS_V181_COND=0`) and with the build under test; the two 720-word images must be
identical.

Coverage, for all 15 ARM condition codes x all 16 NZCV combinations, eight times
around so the block cache actually compiles and runs the code natively:

| part | shape |
| --- | --- |
| 1 | five predicated ALU ops (`MOVcc`, `ADDcc`, `SUBcc`, `ANDScc`, `ADCcc`) |
| 2 | predicate, a load that takes the shared helper path, predicate with the opposite condition |
| 3 | predicated instructions that read PC (`ADDcc rX, pc, #imm`) |

v1.82 result: 720/720 identical, and `CYCLES global=22256370` identical to the
interpreter build, so the guard is also cycle-exact.

Reproducing the run (devkitPPC + a headless Dolphin + an SD image with the ROM):

```
python3 tests/make_cond_test.py cond-test.nds
mcopy -i sd.raw -o cond-test.nds ::cond-test.nds
python3 run_noods_test.py --dol NooDS-Wii_test.dol --rom cond-test.nds --frames 40 --tag cond
python3 run_noods_test.py --dol baseline_cond0.dol --rom cond-test.nds --frames 40 --tag cond_base
# digests live at mem_dump.bin offset 0x125000 (720 words)
```

## Accuracy — all bit-identical to v1.78 / v1.80 / v1.81

| gate | result |
| --- | --- |
| suite-poly.nds, 6000 frames | CYCLES 1213505123, MEM total `1CF4E24E52C1C0F0`, FB `E5981203DB30C8FD` / `513C66AF6C80456F`, 77/77 3D scenes |
| nds-2d-agent-suite.nds, 6000 frames | CYCLES 249135062, FB top `06A9537E55B0068D`, 8/8 exact framebuffer |
| wolveslayer.nds, 1200 / 1800 / 2000 frames | `107965442` / `EB78CBC3BF6C2325`, `171504398` / `68370B7E156A7FBC`, `189940393` / `E8C7BE5C9C448DD8` |
| mgba-suite-auto.gba, 30000 frames | `gbaout.log` byte-identical, sha256 `55a623bc…`, 6993 PASS, ALL DONE |
| tests/cond-test.nds, 720 cases | identical to the interpreter, cycle-exact |

## Performance — frameskip 0, clean builds, `PERF accum_us`

| workload | v1.82 | `-DNOODS_V181_COND=0` | delta | v1.81 |
| --- | --- | --- | --- | --- |
| poly 6000 frames | 180,851,660 | 187,438,679 | **-3.52 %** | 180,899,644 |
| wolveslayer 2000 frames | 25,411,149 | 26,335,999 | **-3.51 %** | 25,413,595 |
| 2d-suite 6000 frames | 23,939,837 | 23,944,496 | -0.02 % | 23,941,377 |

The always-retag costs nothing measurable: poly and wolf land within 0.03 % of the
v1.81 binaries.

## Building

```
export DEVKITPRO=/opt/devkitpro
make -j$(nproc)                                 # release  -> NooDS-Wii.dol
make -j$(nproc) G2_SCRIPTED_INPUT=1 TEST_FAST=1 # harness  -> NooDS-Wii_test.dol
```

Release DOL: `NooDS-Wii.dol`, 1,772,160 bytes,
sha256 `9a2fa97a27bad3d94b21c987201f8ce1ce09e074e920ad52abe0368864e7ffe0`.
Harness DOL: `NooDS-Wii_test.dol`,
sha256 `370e0ace78119f53cbf0d9fc48038ed8c50e2de9c6d971a9344724b15f633846`.

Build knobs (unchanged from v1.80): `JIT=1 BLOCKS=1 GBA_JIT=0 POLL_SKIP=0
JIT_STATS=0 PROFILE=0`, `-O3 -flto=auto`. `-DNOODS_V181_COND=0` restores the v1.80
JIT for A/B work; `JIT_STATS=1` adds the `guards9=` counter introduced here.

`make clean` does not remove the `build_*` trees — `rm -rf build_*` between variant
builds, and keep the source directory named `NooDS-Wii`.

## Files

```
NooDS-Wii/            emulator source (single source of truth)
Makefile              devkitPPC build, variant tags in the build directory name
NooDS-Wii.dol         release build
NooDS-Wii_test.dol    test harness build (MAX_FRAMES / CHECKPOINTS / scripted input)
tests/                cond-test.nds + generator
validation/           gate logs: v181_*, v182_*, v183_* (+ a 3D gameplay screenshot)
WOLF3D.md             how to reach WolveSlayer's 3D gameplay and what it costs
*.nds, *.gba          accuracy suites and the WolveSlayer benchmark
```
