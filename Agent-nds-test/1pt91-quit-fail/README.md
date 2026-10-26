# NooDS-Wii v1.982 — subroutine-threaded dispatch (hybrid tried, rejected)

## Status

**v1.982 is a negative result plus a harness fix.** The planned optimisation —
*hybrid* dispatch, i.e. inlining the hottest opcode handlers so the most
frequent instructions skip the dispatch load entirely — was **implemented,
built and measured, and it is slower**. On the most CPU-bound GBA workload it
costs **+25.8%**, reproducibly to the microsecond. It is therefore **not shipped**;
the shipped dispatch is still the plain subroutine-threaded table from v1.981.
The evidence and the mechanism are in
[Hybrid dispatch (v1.982)](#hybrid-dispatch-v1982--implemented-measured-rejected).

What *did* ship in v1.982 is a teardown fix: a checkpointed test run used to
die with `SIGILL` (`rc=132`) at process exit because the checkpoint path
unmounted and re-initialised the SD filesystem from inside the emulator thread.
Numbers are unchanged (`11,944,085 µs` vs `11,944,086 µs` on the GBA suite —
noise), but `CHECKPOINTS=` runs now exit `rc=0` instead of `rc=132`.

> **Speed baseline is unchanged from v1.981.** The `.dol` in this folder is
> `md5 25229dcb0412e8cb8e305f6314968db8` (1,762,304 B); the only source
> difference against v1.981 is two lines removed from `main.cpp`. Building the
> v1.981 source reproduces `md5 6f62082e…` exactly, and building the hybrid
> experiment with `HYBRID=0` reproduces that same v1.981 binary bit-for-bit.


**Dispatch-focused performance release + clean-shutdown fix.** Replaces the interpreter's
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

## Clean shutdown (v1.981)

**Problem.** The only ways out of the process were the HOME button and the
settings-menu item `Quit to Wii Menu`. Everything else — a finished autoboot
test run, a console power-off, or **the host closing the emulator** — left the
main loop spinning in `while (true)` forever, so Dolphin had to be force-quit
every time. Measured on the pre-fix build: after the guest requested exit,
Dolphin was **still alive 20–21 s later and needed SIGKILL, 3 runs out of 3**.

**Fix.** A single idempotent teardown, plus the triggers that were missing:

| Trigger | Before | Now |
|---|---|---|
| Test run reaches `MAX_FRAMES` | main loop spun forever | **exits by itself** |
| Host closes Dolphin | wedged, needed SIGKILL | **exits in ~1 s** |
| Console power button | ignored, wedged | saves + exits |
| Console reset button | ignored, wedged | saves + exits |
| HOME button / `Quit to Wii Menu` | exited (duplicate code) | same path, now shared |

`NoodsCleanShutdown()` in `main.cpp` does exactly what the old HOME handler did
— `runEmulatorThread = false`, `KThreadJoin`, `ShutdownAudio`,
`StopAndDeleteCore` — and then adds the part that was missing:

```cpp
if (g_settingsLoaded) Settings::save();
fatUnmount("sd");                       // commit g2_state.log / gbaout.log / saves
WPAD_Shutdown();
SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0); // hand control back to the loader
```

Shutdown is **two-stage** because the trigger can fire on any thread.
`requestCleanShutdown()` only raises a flag — calling it from the emulator
thread (where `MAX_FRAMES_DONE` is raised) or from a libogc power/reset callback
would otherwise `KThreadJoin` our own thread and deadlock. The main loop sees
the flag and performs the join/teardown:

```cpp
while (true) {
    if (g_shutdownRequested) NoodsCleanShutdown();
    ScanWiiInputs();
    ...
```

`SYS_SetPowerCallback()` / `SYS_SetResetCallback()` are what make a host-side
close work: closing Dolphin triggers the console power/reset path, which now
saves, flushes the SD card and terminates instead of wedging.

**Narrowed, not guessed.** Registering *no-op* callbacks reproduces the hang
exactly (wedged 20 s, 2/2 runs), while the real callbacks exit in 1 s (3/3) —
so the fix is provably the callback → clean-teardown path, not a side effect.

**`HOLD_OPEN=1`** in `autoboot.txt` keeps the process alive after
`MAX_FRAMES_DONE`, so the final on-screen result of a test run can still be
inspected. Default is `0` (exit when finished).

**No cost.** The GBA workload is bit-identical and marginally faster
(11,948,862 → 11,947,576 µs). Framebuffer hashes unchanged on all suites.
`bench/run_test.sh` no longer polls or kills anything: it just runs Dolphin and
asserts `rc=0`, which also means a wedged build is now *detected* rather than
worked around.


---

## Hybrid dispatch (v1.982) — implemented, measured, rejected

**The idea.** v1.981 dispatches one instruction in two loads and one indirect
call: read the 4-byte entry out of `armInstrsFlat[slot]`, then `bctrl`. The
remaining overhead is that *every* opcode pays that cost, even the handful of
opcodes that dominate. Hybrid dispatch splits the 4 096 ARM slots into a small
"hot class" and everything else:

* `armHotClass[slot]` (one byte) names one of the K hottest handlers, or 0xFF.
* `switch (armHotClass[slot])` gives K **explicit, statically known call sites**
  — so the hottest handlers are reachable by *direct* `bl` instead of an
  indirect `bctrl`.
* `default:` falls back to the existing `armInstrsFlat[slot](this, opcode)`.

The hot set was chosen from a **real opcode oracle**, not from a static guess:
`NOODS_DISPATCH_HIST=1` counts every dispatch by handler class while
`varooom-3d.gba` runs. It measured `arm_total=6,751,469` over **69 ARM
classes** and `thumb_total=1,504,565` over **70 THUMB classes**:

| ARM class | share | | ARM class | share |
|---|---:|---|---|---:|
| `cmpImm` | 10.77% | | `_andImm` | 4.3% |
| `movImm` | 10.44% | | `orrLli` | 4.2% |
| `mvnImm` | 10.39% | | `subLli` | 4.2% |
| `movsAri` | 10.39% | | `strhPtip` | 4.0% |
| `cmnImm` | 10.39% | | `rsbsLri` | 3.9% |
| `movLli` | 7.4% | | `b` | 3.7% |
| `ldrPtip` | 6.2% | | `subsImm` | 3.2% |

Top-15 ARM classes cover **92.6%** of all dispatches; THUMB's top-10 cover ~58%
(`bneT` 8.11%, `cmpImm8T` 8.08%, `lslImmT` 6.88%, `ldrPcT`, `popT`, `stmiaT`,
`subImm8T`, `movHT`, `bxRegT`, `beqT`). K was set to **16 ARM / 10 THUMB**.

**Why a class byte and not per-slot inlining.** There are 4 096 ARM slots and
the average handler body is 488 B, so inlining per slot is 4 096 × 488 B =
**1.91 MiB** — the exact reason `goto_inline` was rejected in v1.980. A class
table costs only K × handler size ≈ **5.2 KiB** by the per-handler sizes
(`movImm` 88 B, `mvnImm` 92 B, `movLli` 96 B, `_andImm` 148 B, `orrLli`/`subLli`
156 B, `cmnImm` 200 B, `movsAri` 228 B, `cmpImm` 236 B, `subsImm` 308 B,
`rsbsLri` 324 B, `ldrOfip` 516 B, `ldrPtip` 520 B, `strhPtip` 628 B, `b` 60 B,
`stmiaW` 1,724 B), which would fit L1I.

**It is slower. Measurably, reproducibly, and by a lot.**

| Workload (frameskip=0) | frames | `HYBRID=0` (v1.981) | `HYBRID=1` | Δ |
|---|---:|---:|---:|---:|
| `mgba-suite-auto.gba` | 8 000 | 8,144,828 µs | 10,246,623 µs | **+25.8%** |
| `varooom-3d.gba` | 3 000 | 7,723,954 µs | 8,222,366 µs | **+6.5%** |
| `nds-2d-agent-suite.nds` | 6 000 | 24,072,610 µs | 24,067,684 µs | −0.02% (gated off) |
| `suite-poly.nds` | 2 000 | 79,191,218 µs | 80,357,353 µs | +1.5% (gated off) |

Both GBA rows repeated to the **exact microsecond** on re-run, so this is not
noise. Framebuffer hashes were identical in every configuration
(`CDA7DD02258E8C35`, `3A0E82641E2AA7F5`, `513C66AF6C80456F`) — the experiment
was accuracy-neutral and purely a speed loss.

**Why it lost.** The disassembly explains it. `dispatchHotArm` *was* fully
inlined into `runGbaOpcode` (it has no symbol of its own), but the K handler
**bodies were not**: `runGbaOpcode` contains 16 new direct `bl` instructions —
one per hot class — and zero inlined handler bodies. So the change bought
"indirect call → direct call" and paid for it with a **second unpredictable
indirect branch**: a 17-way jump table that the branch unit cannot predict,
because real code spreads 92.6% of traffic across 15 classes whose address
order is arbitrary (the Gekko/PowerPC 750 predicts indirect branches as
not-taken and only caches *direct* branches in the BTIC).

That is exactly what the v1.980 microbenchmark predicted, one order of magnitude
milder:

| Technique | heavy workload (ns/dispatch) | predicted |
|---|---:|---|
| `flat_call` (shipped) | 140.41 | — |
| `goto_then_call` (switch → call) | 145.39 | **+3.5%** |

The microbench used 3 handler classes; the emulator has 69. The switch is
unpredictable here in a way it simply was not in the microbench, so the 3.5%
tax becomes 6.5%–25.8%.

The bodies cannot be inlined as things stand: they live in `interpreter.cpp`,
and `__attribute__((always_inline))` across translation units is not possible —
forcing it fails to build with *"inlining failed in call to 'always_inline'
'cmpImm8T': function body not available"*. Genuine inlining means relocating
~24 handler bodies (and their private helpers) into a header.

**This result is consistent with Ertl's ranking, not a contradiction of it.**
The hybrid is a *replicated switch for the hot opcodes and subroutine threading
for the rest*. Ertl ranks replicated switch **worst** of the three techniques
on PowerPC, and that is precisely what the measurement shows: the mixed scheme
is worse than the pure subroutine threading v1.981 already ships. Subroutine
threading through a per-slot table is the right answer for this machine — the
hybrid experiment is the evidence for that, not an exception to it.

**Status:** the code (`interpreter_hot.h`, `NOODS_HYBRID_DISPATCH`, `HYBRID`,
`K_ARM`, `K_THUMB`, the `NOODS_DISPATCH_HIST` opcode oracle) is **not** in this
release — it lives in the `work/hyb` scratch tree. `HYBRID=0` rebuilt the
shipped `.dol` bit-for-bit, which is how the compile-out was verified.

---

## What changed

| File | Change |
|------|--------|
| `NooDS-Wii/interpreter_dispatch.cpp` | **new** — builds and owns the flattened tables, documents the generated-code comparison |
| `NooDS-Wii/interpreter.h` | declares `armInstrsFlat[0x1000]`, `thumbInstrsFlat[0x400]`, `execArm()` / `execThumb()` inline dispatch, index helpers, legacy fallbacks |
| `NooDS-Wii/interpreter.cpp` | all 8 dispatch sites now call `execArm()` / `execThumb()`; `init()` builds the tables once |
| `NooDS-Wii/arm_jit.cpp` | `fallbackArm` / `fallbackThumb` / `condBranchArm` route through the same flattened table |
| `NooDS-Wii/main.cpp` | **clean shutdown**: `NoodsCleanShutdown()` / `requestCleanShutdown()`, power+reset callbacks, `MAX_FRAMES_DONE` self-exit, `HOLD_OPEN`; plus *test-only* (`G2_SCRIPTED_INPUT=1`) START-button scripting windows used to drive `wolveslayer` into gameplay |
| `NooDS-Wii/main.cpp` | **v1.982**: removed `fatUnmount("sd")` / `fatInitDefault()` from the checkpoint path — see [Checkpoint teardown bug](#checkpoint-teardown-bug-fixed-in-v1982) |
| `Makefile` | `-DNOODS_THREADED_DISPATCH=$(THREADED)`, `-DNOODS_V1980_OPT`, `-DNOODS_DISPATCH_BENCH`, `-DBENCH_WEIGHT`, `DIAG=1` |
| `NooDS-Wii/dispatch_bench.cpp` | **new**, opt-in (`DISPATCH_BENCH=1`) — the 6-way PPC dispatch microbenchmark |

`interpreter_lookup.cpp` (the 4096 + 1024 handler tables and every handler body)
is **byte-for-byte unchanged**. No guest-visible behaviour was touched: no
timing change, no cycle-count change, no save-state format change. The v1.981
shutdown work only affects process lifetime, never emulation.

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
* **The pre-v1.981 hang was *not* fixable from the main loop alone.** Adding the
  `g_shutdownRequested` check to the main loop without registering the power and
  reset callbacks changed nothing (wedged 20 s, 2/2) — the guest is blocked in
  the emulator thread and never returns to the main loop when the host closes.
  Only the callback path reaches it. Verified by A/B, not assumed.
* **Hybrid dispatch** (inline the K hottest opcode handlers behind a small
  `armHotClass[slot]` switch). **+25.8%** on the mGBA suite, **+6.5%** on
  `varooom-3d.gba`, identical accuracy. The K handler bodies did not actually
  inline (cross-translation-unit), so the change paid for an extra 17-way
  unpredictable jump table and gained only indirect→direct calls. Full numbers
  and disassembly in [Hybrid dispatch](#hybrid-dispatch-v1982--implemented-measured-rejected).
* **`threaded2D=1 / threaded3D=1`** deadlocks under `dolphin-emu-nogui -p headless`
  on this 2-vCPU host (hangs before the first checkpoint). Not an emulator bug —
  all benchmarks here therefore run single-threaded, which is also the
  configuration where dispatch cost matters most.

---

## Checkpoint teardown bug (fixed in v1.982)

**Symptom.** Any run passing a `CHECKPOINTS=` list exited with `SIGILL`
(`rc=132`) *after* completing normally. All output was correct and already on
disk — both `CHECKPOINT` lines, `MAX_FRAMES_DONE`, the `.png` and the state log —
so the only visible damage was a non-zero exit code and a `!! … exited rc=132`
line from `bench/run_test.sh`.

```
[NOODS] CHECKPOINT frame=15000 … fb=CDA7DD02258E8C35 us=8802192
[NOODS] CHECKPOINT frame=30000 … fb=CDA7DD02258E8C35 us=11943317
[NOODS] MAX_FRAMES_DONE frame=30000 hold=0
!! canon_cps: dolphin exited rc=132        <- SIGILL after everything succeeded
```

**Cause.** The checkpoint path unmounted and re-initialised the SD card
filesystem from *inside the emulator thread*:

```cpp
if (checkpoint && fbDumpRequested) {
    ...
    fatUnmount("sd");        // <- removed in v1.982
    fatInitDefault();        // <- removed in v1.982
}
```

Re-initialising the EXI/FAT device under the running emulation corrupts the
device state that Dolphin is emulating. It is also unnecessary: every writer in
that path already does `fflush` + `fclose`, which is what actually commits the
data.

**Fix and verification.** Delete the two lines. With checkpoints
(`15000,30000`) the run now exits `rc=0` and reports `11943317 µs`; without
checkpoints, `rc=0` and `11944085 µs` — both matching the pre-fix numbers
exactly, so the fix changes timing not at all, and the checkpoint files are
still written.

**Known remaining issue.** Some **NDS** runs still exit `rc=132` at teardown
even with no checkpoints (`nds-2d-agent-suite.nds` reproduced 3/3 in one
session, while `suite-poly.nds` and all GBA runs exited `rc=0`). This is
pre-existing v1.981 behaviour, happens after all measurement output is flushed,
and does not affect any number in this document — but it means `rc` is not a
reliable pass/fail signal for NDS runs; assert on the `CHECKPOINT` /
`MAX_FRAMES_DONE` lines instead.

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

### autoboot.txt keys understood by the test harness

```
sd:/roms/game.nds          # first non key=value line: ROM to load
MAX_FRAMES=4000            # exit cleanly once this many slices have run
CHECKPOINTS=1000,2000,4000 # emit state/framebuffer hashes at these frames
HOLD_OPEN=1                # keep the process alive after MAX_FRAMES (inspect
                           #   the final on-screen result); default 0 = exit
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
│   ├── main.cpp                         clean shutdown + test harness
│   └── (unchanged: interpreter_lookup.cpp, all handlers, GPU, SPU, ...)
└── bench/                               measurement harness + raw switch table
```

## Credits

NooDS core by Hydr8gon. Wii port and v1.5–v1.98 radicalten.
Threading-technique theory: Anton Ertl, TU Wien.
Test ROMs: mGBA suite auto, nds-2d-agent, suite-poly (Jaklyy),
`wolveslayer` (Firehazard Studio / Payk, MIT).
