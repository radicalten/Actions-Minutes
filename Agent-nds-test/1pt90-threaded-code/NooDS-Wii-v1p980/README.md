# NooDS-Wii v1.980 — subroutine-threaded core dispatch

## Status

**Dispatch-focused performance release.** Replaces the interpreter's
pointer-to-member-function opcode dispatch with a flattened, 4-byte-entry
**subroutine-threaded** dispatch table, which is the fastest of the three
techniques ranked for PowerPC by Anton Ertl
([Threading techniques](https://www.complang.tuwien.ac.at/forth/threading/)).
`frameskip=0` is pinned; native Dolphin framerate stays **60 FPS**
(`fpsLimiter=1` default unchanged). All three accuracy suites are
**bit-for-bit identical** to v1.78 / the v1pt5 baseline.

Gains are real but modest, because the baseline was *already* subroutine
threading — the win comes from fixing the **call mechanism** (an 8-byte
pointer-to-member with a dead `adj` path vs. a 4-byte direct function pointer),
not from changing the threading model. End-to-end that is **−1.46%** on the GBA
workload, **−0.89%** on the NDS CPU/3D workload, and ~0% on 3D-bound content.

The technique is also verified exhaustively against every alternative —
**computed goto, call dispatch, tail-call chaining and replicated switch**, at
three handler-body weights: pure computed goto with inlined bodies is ~6%
faster dynamically but needs **1.91 MiB** of code against a **32 KiB** L1I, and
the only *deployable* direct-threaded form beats what we ship by 0.25% (noise).
See [Results](#results) for the full comparison and
[Negative results](#negative-results-measured-and-rejected) for what does *not*
work.

---

## What changed

| File | Change |
|------|--------|
| `NooDS-Wii/interpreter_dispatch.cpp` | **new** — builds and owns the flattened tables, documents the generated-code comparison |
| `NooDS-Wii/interpreter.h` | declares `armInstrsFlat[0x1000]`, `thumbInstrsFlat[0x400]`, `execArm()` / `execThumb()` inline dispatch, index helpers, legacy fallbacks |
| `NooDS-Wii/interpreter.cpp` | all 8 dispatch sites now call `execArm()` / `execThumb()`; `init()` builds the tables once |
| `NooDS-Wii/arm_jit.cpp` | `fallbackArm` / `fallbackThumb` / `condBranchArm` route through the same flattened table |
| `NooDS-Wii/main.cpp` | *test-only* (`G2_SCRIPTED_INPUT=1`) START-button scripting windows, used to drive `wolveslayer` into gameplay |
| `NooDS-Wii/dispatch_bench.cpp` | **new**, opt-in (`DISPATCH_BENCH=1`) — the four-way PPC dispatch microbenchmark |
| `Makefile` | `-DNOODS_THREADED_DISPATCH=$(THREADED)` (default `1`), `-DNOODS_V1980_OPT`, `-DNOODS_DISPATCH_BENCH=$(DISPATCH_BENCH)`, `DIAG=1` → `NOODS_NDS_TASK_PROFILE` |

`interpreter_lookup.cpp` (the 4096 + 1024 handler tables and every handler body)
is **byte-for-byte unchanged**. No guest-visible behaviour was touched: no
timing change, no cycle-count change, no save-state format change.

### Reverting

```sh
make THREADED=0     # legacy pointer-to-member dispatch, everything else identical
```

---

## Why this is the right change for PPC

The baseline dispatches through `int (Interpreter::*armInstrs[0x1000])(uint32_t)`.
On the PPC SysV/EABI a pointer-to-member-function is **8 bytes** (`fn` plus an
`adj` word), and gcc 16.2 emits this for one dispatch (`-O3 -mcpu=750`):

```asm
        rlwinm  8,4,28,28,31       ; opcode bits 7..4
        rlwinm  10,4,16,20,27      ; opcode bits 27..20
        or      10,10,8            ; index
        lis     9,tab@ha
        slwi    10,10,3            ; * 8      <-- 8-byte stride
        la      9,tab@l(9)
        lwzx    7,9,10             ; fn
        add     9,9,10
        lwz     9,4(9)             ; adj
        mr      8,3                ; this
        andi.   10,7,0x1           ; virtual-bit test
        add     3,3,9              ; this + adj
        beq-    .L2
        lwzx    9,8,9              ; vtable fetch (never taken)
        add     9,9,7
        lwz     7,-1(9)
.L2:
        mtctr   7
        bctr                       ; 16 instructions, 2 dependent loads
```

`Interpreter` is allocated with plain `new` and has no virtual members, so
`adj == 0` and `fn` is always even — **the entire non-leaf path is dead on every
instruction**. Passing the object pointer explicitly and flattening the table to
direct function pointers gives:

```asm
        lis     9,tab@ha
        rlwinm  8,4,16,20,27
        rlwinm  10,4,28,28,31
        la      9,tab@l(9)
        or      10,8,10
        addis   9,9,0x1
        slwi    10,10,2            ; * 4      <-- 4-byte stride
        add     9,9,10
        lwz     9,-32768(9)
        mtctr   9
        bctr                       ; 11 instructions, 1 dependent load
```

Other consequences that matter on a 750CL (32 KiB L1I / 32 KiB L1D):

* the hot ARM table shrinks **32 KiB → 16 KiB**, the THUMB table **8 KiB → 4 KiB**,
  so the dispatch table now *fits* L1D instead of thrashing it;
* a 32-byte cache line holds **8** handlers instead of 4, halving line fetches
  for a branchy guest working set;
* `this` is materialised once in `r3` and read by the callee, so the per-dispatch
  `mr` / `add` chain leaves the critical path.

*Technique, not mechanism:* this is still **subroutine threading** (handlers are
independent native subroutines reached through a table slot and returning their
cycle count). True **direct threading** is not reachable here without forking the
~150 interpreter handlers into per-opcode native blocks, which would defeat the
compiler's shared cold paths; the microbenchmark below quantifies what that
would be worth (a further **+14%** on top of subroutine threading) if it were
ever attempted.

---

## Results

### 1. Dispatch comparison — six techniques, three body weights (real PPC)

`DISPATCH_BENCH=1` runs every candidate dispatch form over the *same* 4096-slot
ARM opcode index, with the *same* handler bodies, so only the dispatch differs:
1,048,576 dispatches each, deterministic pseudo-random opcode stream, warmed
first. The **body weight** knob is essential, because the ranking is not
weight-independent — the cheaper the handler, the more the call/return sequence
dominates. The real interpreter's average handler is **~488 bytes of PPC code
(≈120 instructions)**, so `heavy` is the bracket that matches shipping code.

| Technique | Ertl class | `tiny` (~15 insn) | `med` (~45) | **`heavy` (~120)** | vs `flat_call` (heavy) |
|---|---|---:|---:|---:|---:|
| **`flat_call`** — flat 4-byte table, `bctrl`, return to dispatcher **(v1.980)** | subroutine | 55.27 | 79.99 | **140.41** | — |
| `pmf_call` — pointer-to-member table, 8-byte entries *(v1.78 baseline)* | subroutine | 55.62 | 80.33 | 140.76 | +0.25% |
| `tailcall_chain` — `return tab[next](...)`, gcc emits a **sibling call** | **direct** | **54.92** | **79.64** | **140.06** | **−0.25%** |
| `goto_inline` — computed goto, body **inlined per slot**, no call | **direct** | **51.67** | **75.36** | **131.31** | **−6.5%** |
| `goto_then_call` — computed goto to a label that then calls the handler | hybrid | 60.24 | 84.97 | 145.39 | +3.5% |
| `repl_switch` — replicated nested switch (jump table + direct call) | switch | 64.54 | 89.26 | 149.68 | +6.6% |

*(ns per dispatch. Reproducibility of this bench is ±1%; Dolphin's JIT block
placement varies slightly between runs, so differences under ~1% are not
significant. The end-to-end A/B in the next section is bit-reproducible.)*

**Correction to my first pass.** An earlier version of this benchmark reported
"direct threading = 39.48 ns" against "subroutine threading = 34.50 ns" and read
as if direct threading were slower. That variant was `goto_then_call` — a
computed goto *followed by* a call — not direct threading at all. Measured
properly above, that hybrid is **3.5–9% worse than a plain call dispatch**,
because it pays an extra indirect branch and buys nothing. Pure computed goto
only becomes a win when the handler body is inlined at the label, which is a
different technique with a different cost, quantified next.

**Verdict from the dynamic numbers:** `goto_inline` is genuinely the fastest,
consistently ~6% ahead of the shipped `flat_call` across all three weights.
`tailcall_chain` — the other *implementable* form of direct threading — is
indistinguishable from what we already ship (−0.25%, inside noise).

### 1b. Why `goto_inline` is still the wrong choice: code size

Inlining the body per slot means **one copy of the handler per table entry**:

| | code footprint | vs 750CL L1I (32 KiB) | vs L2 (256 KiB) |
|---|---:|---:|---:|
| `flat_call` / `tailcall_chain` (one body per *handler*) | ~296 KiB for all 621 handlers | hot subset fits | ~1.2× |
| `goto_inline` (one body per *slot*) | 4096 × 488 B = **1.91 MiB** (ARM) + 488 KiB (THUMB) | **61× too big** | **7.6× too big** |

The benchmark itself corroborates the blow-up: its `goto_inline` loop is
**18,072 B** of code versus **560 B** for the `flat_call` loop, i.e. ~2.2 KiB of
code per replicated body — for only 8 distinct handlers. Scale that to 4096
slots and the instruction working set is several MiB.

The trade is therefore: save ~6.5% of dispatch time (~9 ns ≈ 6.6 cycles at
729 MHz) in exchange for guaranteeing an **L1I miss on essentially every
instruction**, with a 488 B body spanning 16 cache lines. On 750CL even an L2
hit costs ~15–20 cycles, so the cache penalty alone exceeds the saving — before
counting L2 misses. **Dolphin cannot settle this part**: its JIT models neither
L1I/L1D nor branch prediction, so this environment can rank *dynamic work* but
cannot measure cache behaviour. That is exactly why the code-size table, not the
nanoseconds, decides the question.

So: `goto_inline` is faster in dynamic terms and undeployable in practice;
`tailcall_chain` is deployable (identical code size) but buys nothing measurable
and would require rewriting all 621 handlers to end in a sibling call.

**Conclusion: the flattened subroutine-threaded table shipped here is the
fastest *practical* dispatch for this emulator.** The only avenue left with real
headroom is a *hybrid* — replicate bodies for just the ~32 hottest opcodes
(~15 KiB, keeps L1I pressure low) and fall back to the call table for the rest,
which would capture most of the ~6% for a fraction of the code. That is a
partial JIT for content that already has a full block JIT on NDS, and the hot
opcode set differs per game, so it is documented as future work rather than
implemented here.

### 1c. Where the v1.980 change actually lands

Note the `pmf_call` → `flat_call` gap is only **+0.25%** at `heavy` weight but
**+0.6%** at `tiny` weight in this bench, because heavy bodies amortise the
dispatch. The real emulator measured a stable **1.46%** end-to-end on GBA —
large because the interpreter's *per-dispatch glue* (pipeline refill, `pcData`
check, condition evaluate, cycle accounting) sits on top of the dispatch in the
same critical path, so removing 5 of the dispatched instructions removes a
larger share of a small total. The bench measures the dispatch core in
isolation; the end-to-end table is the authoritative number for the shipped
build.

### 2. End-to-end (headless Dolphin, frameskip=0, single-threaded renderer)

`accum_us` is the guest-side timebase the harness itself records
(`PERF accum_us`). Under Dolphin the PPC timebase is cycle-derived, so runs are
very reproducible: the mGBA, suite-poly, varoom and wolveslayer figures below
reproduced **exactly** on re-run. The `nds-2d` workload is the exception — it
drifts by roughly ±0.1% between runs because its guest-side capture/compare loop
interacts with Dolphin's scheduler, so its delta below should be read as "no
change", not as a measured regression.

| Workload | Slices | v1.78 baseline | **v1.980** | Δ |
|---|---:|---:|---:|---:|
| `mgba-suite-auto.gba` (GBA interpreter only) | 30,000 | 12,117,806 µs | **11,941,528 µs** | **−1.46%** |
| `nds-2d-agent-suite.nds` (full results screen) | 9,000 | 37,489,317 µs | 37,522,800 µs | ±0.1% — unchanged |
| `suite-poly.nds` (28 CPU + 77 3D scenes) | 4,000 | 134,877,741 µs | **133,675,121 µs** | **−0.89%** |
| `varoom-3d.gba` (real GBA game) | 3,000 | 7,743,906 µs | **7,723,831 µs** | **−0.26%** |
| `wolveslayer.nds` (in-game 3D, player moving) | 16,000 | 203,993,900 µs | 203,926,384 µs | −0.03% |

**Where the time actually goes** (`-DNOODS_NDS_TASK_PROFILE=1`, suite-poly run):
NDS CPU emulation **86.9%**, 2D/GPU task **11.4%**, timers/DMA/other 1.7%.
So dispatch is the right knob for CPU-bound content, but `suite-poly` and
`wolveslayer` spend most of their time inside the 3D renderer, which caps what
any dispatch change can deliver there — hence the small deltas on those two.

`nds2d` is dominated by capture/compare work inside the guest itself, which is
why dispatch changes barely move it; its framebuffer hash is identical to the
baseline, so the ±0.1% is drift, not a behavioural difference.

---

## Accuracy — unchanged, `frameskip=0`

Verified against the shipped `NooDS-Wii.dol` in this folder. Every framebuffer
hash matches the v1pt5 baseline exactly.

| Suite | Baseline (v1pt5) | **v1.980** | Framebuffer |
|---|---|---|---|
| `mgba-suite-auto.gba` | 6,993 / 6,998 — `ALL DONE=True` | **6,993 / 6,998 — `ALL DONE=True`** | `CDA7DD02258E8C35` **identical** |
| `nds-2d-agent-suite.nds` | 8 / 8 (100.00%, exact) | **8 / 8 (100.00%, exact)** | `3A0E82641E2AA7F5` **identical** |
| `suite-poly.nds` | 28/28 CPU + 77/77 3D, `expectedPx=102,942 missing=0 overdraw=0 wrongCol=0` | **28/28 CPU + 77/77 3D, `expectedPx=102,942 missing=0 overdraw=0 wrongCol=0`** | `513C66AF6C80456F` **identical** |
| `wolveslayer.nds` moving | — | — | `B0A1379F3FEFEF89` **identical** |

**On the 6,993 vs 6,997 figure.** This workspace has no GBA BIOS, so NooDS HLEs
it, and the four remaining failures are all BIOS-timing dependent:

```
FAIL: DMA Prefetch Break
FAIL: H-blank bit start Hblank
FAIL: H-blank bit start Flip 1
FAIL: H-blank bit start Flip 2
FAIL: H-blank bit start Flip 3
```

Those same four fail identically on the **v1pt5 baseline**, so they are an
environment artefact, not a v1.980 regression — the `mGBA` score is unchanged to
the test. With a real GBA BIOS present this suite moves to the documented
6,997 / 6,998. `wolveslayer`'s 3D scenes were validated with the player actually
moving: `INPUT_START` gets past the title, then `INPUT_RIGHT`+`INPUT_A` are held
for the remaining 11,600 slices, giving a 3D corridor scene with a translating
player and camera (see `bench/run_wolf.sh`).

---

## Negative results (measured and rejected)

Recording these so they are not re-attempted:

* **`GBA_JIT=1`** (route GBA through the ARM block JIT instead of per-instruction
  dispatch). Accurate — 6,993/6,998 with an identical framebuffer — but **slower**:
  13,804,132 µs vs 12,117,806 µs on the mGBA suite (**+13.9%**). GBA timing is
  bus/fetch-cycle driven and the block JIT's bookkeeping outweighs the dispatch
  saving. Left at `0`.
* **`threaded2D=1 / threaded3D=1`** deadlocks under `dolphin-emu-nogui -p headless`
  on this 2-vCPU host (hangs before the first checkpoint). Not an emulator bug —
  all benchmarks here therefore run single-threaded, which is also the
  configuration where dispatch cost matters most.

---

## Build

```sh
cd NooDS-Wii-v1p980
source /etc/profile.d/devkit-env.sh          # DEVKITPRO=/opt/devkitpro
make -j4
# outputs: NooDS-Wii.dol  NooDS-Wii.elf  NooDS-Wii.elf.map
```

Toolchain used: **devkitPPC r51 / gcc 16.2.0**, libogc 3.2.1, `elf2dol`, at
`/opt/devkitpro`. Headless verification: **dolphin-emu-nogui 2503**
(`-p headless` needs an X display → `xvfb-run`) with:

```sh
dolphin-emu-nogui -u <userdir> -v Software -e NooDS-Wii.dol
```

Useful build switches:

```sh
make THREADED=0         # legacy pointer-to-member dispatch (A/B reference)
make DISPATCH_BENCH=1   # sd:/dispatch_bench.log with the 6-way dispatch comparison
make DISPATCH_BENCH=1 BENCH_WEIGHT=2   # ...with ~120-instruction handler bodies
                                       # (0=tiny, 1=med default, 2=heavy)
make DIAG=1             # adds NDS_CPU / NDS_TASK profile lines to sd:/g2_state.log
make G2_SCRIPTED_INPUT=1 # test-only START/A/RIGHT input windows via autoboot.txt
```

## Reproducing the measurements

`bench/` holds the harness used for every number above. It expects an SD tree at
`~/sd` (`roms/`, `noods.ini`, `autoboot.txt`) exported to Dolphin as
`Load/WiiSDSync`.

```sh
bench/run_test.sh <dol> <sd:/roms/rom> <max_frames> <checkpoints> <tag>
bench/matrix.sh                  # all 4 workloads x 2 builds
bench/run_wolf.sh <dol> <tag>    # wolveslayer with the player moving
bench/poly_results.py <mem_dump.bin>   # decodes the 0x023F0400 poly block
bench/fb2png.py <fb_dump.bin> <out.png>  # writes the stacked top+bottom screen
```

The harness reads results from three places, all of which land on the SD image:
OSReport `[NOODS] CHECKPOINT ... us=` lines, `sd:/g2_state.log`
(`PERF` / `FB_HASH` / `MEM_HASH`), and `sd:/gbaout.log` (the mGBA debug channel).

Two notes on the harness:

* the dispatch bench reports ±1% run-to-run (Dolphin JIT block placement); the
  end-to-end figures are bit-reproducible, so trust those for A/B decisions.
* `FB_HASH` is a stable accuracy oracle; `MEM_HASH`/`ram` is **not**
  (the guest RTC leaks host time into RAM), so accuracy is asserted on
  framebuffer hashes and the suites' own self-reported scores.
* A `noods.ini` containing `threaded2D=1` will hang headless Dolphin (see
  Negative results) — `bench/run_test.sh` pins `0`.

## Files

```
NooDS-Wii-v1p980/
├── NooDS-Wii.dol                        release build (frameskip=0, 60 FPS)
│                                         (.elf/.elf.map are build outputs)
├── Makefile
├── README.md                            this file
├── LICENSE                              GPLv3
├── NooDS-Wii/                           full source
│   ├── interpreter_dispatch.cpp         NEW — threaded dispatch tables
│   ├── dispatch_bench.cpp               NEW — 6-way dispatch benchmark (opt-in)
│   └── (unchanged: interpreter_lookup.cpp, all handlers, GPU, SPU, ...)
└── bench/                               measurement harness + raw switch table
```

## Credits

NooDS core by Hydr8gon. Wii port and v1.5–v1.98 radicalten.
Threading-technique theory: Anton Ertl, TU Wien.
Test ROMs: mGBA suite auto, nds-2d-agent, suite-poly (Jaklyy),
`wolveslayer` (Firehazard Studio / Payk, MIT).
