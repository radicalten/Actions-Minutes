# v3 working notes

State of the tree, what has been *measured*, and the traps that cost time. The
design itself is in `BUILDING.md`; the comparison against the two earlier
recompilers is in `../COMPARISON.md`.

## How to reproduce everything in this file

```bash
export DEVKITPRO=/home/user/dkpopt/opt/devkitpro        # tools/agent_env.sh dkp
# 1. differential self-test, in emulator, on boot
make -j4 JIT=1 EXTRA_CXXFLAGS=-DJIT3_EMBED_ROM=1 DEVKITPRO=$DEVKITPRO
bash tools/run_emulator_test.sh                          # headless Dolphin, ~75 s
# 2. A/B against the interpreter-only build
make -j4 JIT=0 EXTRA_CXXFLAGS=-DJIT3_EMBED_ROM=1 DEVKITPRO=$DEVKITPRO
#    ...run again, then:
tools/compare_reports.py report-jit.txt.clean report-interp.txt.clean
# 3. shadow verification (re-runs every block through the interpreter and logs
#    the first disagreement to sd:/jit3-verify.txt)
make -j4 JIT=1 EXTRA_CXXFLAGS="-DJIT3_EMBED_ROM=1 -DJIT3_VERIFY=1 -DJIT3_VERIFY_MAX=900000"
```

`-DJIT3_EMBED_ROM=1` only affects diagnostics: it writes `embed_rom.inc` to
`sd:/rockwrestler.nds`, turns on auto-boot and turns on the frame report.

## The A/B run is what found the real bugs

The differential self-test passed (`fail=0`) while the JIT was still corrupting
guest state on a real ROM. Three separate bugs came out of comparing a JIT build
against an interpreter-only build of the same source:

1. **The self-test's own teardown corrupted the guest PC.** `flushPipeline()`
   *adds* the pipeline step to `registersUsr[15]`, and the test restored the
   already-converted PC and then called it - so the ROM resumed one instruction
   past where it should. Only the JIT build runs the self-test, so only the JIT
   build diverged. Fix: hand `flushPipeline()` the raw target
   (`savedRegs[15] - 4`).
2. **Halfword/signed load-store encodings were compiled as data processing.**
   `STRH`/`LDRH`/`LDRSB`/`LDRSH`/`LDRD`/`SWP` are bits 27-25 = `000` *with bits
   7 and 4 set* - the same top-level class as data processing, which is why the
   classifier's `cls != 0 && cls != 1` test let them through. `STRH r3, [r2, r0]`
   (0xE1C230B0) therefore ran as `MOV r3, r2, <shift by r11>`: a guest register
   corrupted *and* the store never performed. `armjit.cpp:456` now refuses
   `cls == 0 && (op & 0x90) == 0x90`. This bug was invisible to the host
   differential suite (it only generates encodings it has a model for) and to
   the self-test (whose programs contain no memory instructions).
3. **`EXTRA_CXXFLAGS` was dropped from every `JIT=0` build.** The Makefile only
   appended it in the `JIT=1` branch, so the "diagnostic" interpreter build
   never autobooted and never wrote a report - which looks exactly like a hang.
   The stamp that decides whether to rebuild now hashes the whole of `CXXFLAGS`,
   and `make` echoes the effective flags on every run.

## Shadow verification (`-DJIT3_VERIFY=1`)

A slice-1 block performs no memory access at all (anything containing a load or
store is refused), so a block only changes the register file, the CPSR and its
cycle count - which makes it exactly replayable. The verification mode snapshots
the CPU, runs the block, restores the snapshot, runs the same instructions
through the interpreter and compares. On a mismatch it writes the block PC, the
source words, both cycle counts, both PCs and every differing register to
`sd:/jit3-verify.txt`.

Before the fix that log named the bug in eight lines; after it, **900,000
dispatches produced no log file at all**.

## The emulator is deterministic, so a JIT/interpreter difference is a bug

Two runs of the *same* interpreter build produced identical rows for all 80
sampled frames (cycles, both PCs, RAM hash, framebuffer hash). That is what
makes a per-frame A/B meaningful: any difference between the two builds is the
JIT's, not run-to-run noise.

## What a "pass" means

`ok` in the self-test is **semantic**: same cycles charged, same 16 registers,
same CPSR. It is deliberately *not* "the same number of dispatches" - the JIT
retires a whole block per dispatch, so `flags` is 1 dispatch for the JIT against
7 for the interpreter, at identical cycle cost. Instruction *figures* are
informational only; see the next point.

## `Stats::instrs` is an upper bound, not a retired-instruction count

`noteDispatch()` credits `block->sourceWords` - the block's whole static word
count - on every dispatch, even when the block exits early or hands the PC back
to the interpreter. For the loop program it over-credits 10 instructions (43 vs
33, exactly one per iteration) and for the call program it under-credits 1 (the
`bx lr` handoff dispatches the interpreter, which is not credited at all). Do
not gate any verdict on this number. If an accurate count is ever wanted, the
block has to report the instructions it actually retired, e.g. a per-exit `stw`
of a compile-time constant into a slot the dispatcher reads.

## Known gap: cycle-deadline fidelity (r29)

The ABI reserves PPC r29 for the cycle deadline the glue wants the block to stop
at, and the block's prologue loads it - but no code in the block ever tests it,
and `jit3Run()` passes `0xFFFFFFFF`. Blocks therefore run to their natural end
and can overshoot the deadline the scheduler gave the CPU. Consequence, measured
on the A/B run: the **sampled PC** (`registersUsr[15] - 4` at a frame boundary)
sits up to one instruction away from the interpreter's on 12 of 39 sampled
frames, while cycles, main-RAM hash and framebuffer hash are identical on **all**
39. Closing it means giving the block its remaining budget and checking it at
instruction boundaries - the next refinement, and the reason the register is
already reserved.

## Measured speed, and why it is currently a loss

Same ROM, same run length, from the report's `ms` column:

| build | frames measured | wall clock | per frame |
|---|---|---|---|
| `JIT=1` | 2 -> 78 | 68.1 s | 896 ms |
| `JIT=0` | 2 -> 160 | 56.2 s | 356 ms |

The JIT build is **2.5x slower than the plain interpreter** on this ROM, and the
reason is architectural, not a bug: slice 1 compiles ARM ALU ops only, so every
block containing a load, a store, a coprocessor access or Thumb code is refused,
and a refusal costs a block-table probe plus an FNV checksum over the block's
source words *before* the instruction is interpreted anyway. On a ROM whose time
is spent on memory accesses, that is pure overhead. Native loads/stores (and,
after that, trimming the per-dispatch validation) is what turns it into a win.

## libfat commits on `fclose`, not on `fflush`

The frame report was 0 bytes on the card for several rounds because
`WriteJit3Sample()` kept its `FILE*` open for the whole run. `fflush()` pushed
the bytes into libfat's write-back cache and nothing ever reached the FAT. The
sample path now does open -> write -> **close** on every sample. The same trap
applies to anything else that must survive the emulator being killed.

## Dolphin recipe that works

`-v Null` (headless, no X11), `userdir/Config/Dolphin.ini` as the only working
config path, `userdir/Load/WiiSD.raw` as the only SD card the emulator will read,
and mtools (the copies under `dolphin/root/usr/bin/`) for reading files out of
the card image. `-v Software`/`-v OGL` cannot work in this sandbox, the GDB stub
never listens, and `-C` overrides are ignored.

Three consequences worth remembering:

- never write to `sd:/` before `fatInitDefault()` - the resulting fault is
  `mkdir+0x40` and looks like a JIT bug;
- the file's last record can be a run of NUL bytes if the emulator is killed
  mid-write; strip them (`tr -d '\000'`) rather than trusting the size;
- run the emulator in the foreground of a bounded `timeout`: the same command
  started as a background job was restarted repeatedly by the sandbox and never
  got far enough to write anything.

## The test ROM idles

`rockwrestler.nds` is one of RockPolish's CPU stress ROMs. Both CPUs sit on base
`0x04000100`, and from frame 8 the ARM9 PC is `02000228` and the ARM7's is
`03800234` in the interpreter build - a tight I/O-polling loop that does keep
writing main RAM (the RAM hash moves every frame), but never changes what is on
screen. The A/B run therefore proves agreement on the ROM's boot, its idle loop,
BIOS HLE, the frame loop, timers, DMA and both IPC handshakes - not on a full
game. The self-test plus shadow verification is what covers opcode semantics.

---

## Session log: arena/exit bugs, instrumentation, and where v3 stands

Everything below is from the simulator harness in `tools/` (headless Dolphin +
a diagnostic `.dol` that carries the test ROM inside itself).  Reference builds:
`/home/user/v3-jit.dol` (JIT=1) and `/home/user/v3-interp.dol` (JIT=0).

### Real bugs found and fixed this session

1. **Arena overflow (memory corruption).** `compileInto()` handed the emitter
   `arena + arenaUsed` with a 768-word budget and only *afterwards* checked
   whether the block had fitted - so a nearly-full arena wrote up to 3 KB past
   the end of the array, over whatever the linker put next (that region ends up
   holding other globals).  Space is now reserved before emitting: if a
   worst-case block does not fit, the arena wraps first and the generation is
   bumped so every existing entry is invalidated.
2. **Missing fall-through exit.** `compileBlock()` ended the block with an exit
   "if no exit was emitted anywhere" (`if (!e.stop)`).  `emitDP` turns
   `mov pc, lr` into a full exit, and a *conditional* one still falls through -
   so a block could reach its end with no exit on its sequential path and run off
   into arena memory that was never written.  The guest then executes `00000000`
   and the emulator dies with "unknown instruction".  The rule is now: a block
   gets a fall-through exit unless its last instruction is a branch/BX, whose
   emitter always produces both paths (duplicate exits are dead code).
3. **Blocks decoded past a terminator.** `armCanTranslate()` allowed `b`/`bl`
   without setting `*terminates`, so the decoder kept going: it emitted dead code
   and, in the self-test, decoded whatever the *previous* program had left in the
   borrowed words - the same pc compiled to different code on different walks.
   Branches now terminate the block (this also makes blocks much smaller: the
   first self-test block went from 154 to 83 words, which matters because an
   83-word block only lets ~200 blocks live in the 136 KB arena).

Two structural invariants were added as well, and they are worth keeping:
every compiled block must (a) end with the epilogue's `blr` and (b) contain no
direct branch that leaves the emitted range.  Both are checked at compile time;
a violation refuses the block (interpreting is always correct) and writes a
breadcrumb to `sd:/jit3-stage.txt` instead of letting the guest run into garbage.

### Harness fixes that cost real time

* libfat commits a file on **close**, not on flush - a report left open is a
  0-byte file on the card.  Every diagnostic write therefore opens, writes and
  closes.
* Even then, the **last** write before a crash is not guaranteed to reach the
  image: the directory entry can be updated while the data cluster is still
  zero.  A missing or truncated final line in a trace is an artifact, not a
  fact.
* The self-test's own verdict was wrong twice: it compared *dispatch counts*
  (a block legitimately retires many instructions per dispatch) and it gated the
  verdict on an instruction counter that credits a block's whole static word
  count per dispatch.  Acceptance is now semantic: equal cycles, equal final
  register file, equal CPSR.  The instruction figure is reported, never gated.
* A self-test program written on top of stale RAM made the harness
  nondeterministic; the borrowed 16 words are filled with `mov r0, r0` first.

### Verified

* `selftest: programs=4 fail=0 cycles=88/88 dispatched=18/54` - flags, a
  conditional loop, `bl`/`bx lr`, and a mix of test ops/MRS/MSR/RSC all produce
  the interpreter's exact cycle count, register file and CPSR, in-emulator.
* The interpreter-only build (JIT=0) boots the test ROM headless and writes the
  frame report; that build is the reference for the A/B diff
  (`tools/compare_reports.py`, `tools/run_ab.sh`).

### Open (blocking "the JIT build beats the interpreter")

A compiled block still jumps into arena memory that was never written
(`Unknown instruction 00000000 at PC = 80378xx`, LR = the block-call site inside
`jit3Run`).  The static branch check passes, the block ends with `blr`, the
self-test's semantic checks pass - so the fault is inside the emitted code in a
way the current checks do not see; the crash offset in the arena moves between
runs, which is why the compile-time code dump (`jit3: code pc=... words=...:`,
written **before** the block can run, so it survives the crash) was added.
Next step is to disassemble that dump - see `evidence/jit3-trace-arena-jump.txt`
for a captured pair of blocks - and fix whatever path leaves the block.

The v3 JIT therefore ships as **not yet faster**: the build works, the harness
proves its translations against the interpreter on four programs, and the
remaining fault is localized to a single class (jumps into unwritten arena) with
the tooling to find it.  `JIT=0` remains the safe configuration.

### Measured (same harness, same host)

* Interpreter build (`v3-interp.dol`): frames 10..240 in 90 s -> **~357 ms/frame,
  2.8 fps**, 1,680,570 cycles/frame; report in
  `evidence/report-interp-frames10-240.txt`.  This is the number to beat and it
  matches the pre-rollback baseline (356 ms/frame), so the harness is comparable.
* The two builds agree on the rendered frame: the framebuffer hash at frame 30 is
  `016D096D` in both reports.
* JIT build (`v3-jit.dol`): **no frame report at all** - it dies during its own
  self-test, so there is no honest JIT number yet.  Earlier rounds measured the
  JIT at 896 ms/frame against the interpreter's 356, i.e. *slower*: the per-dispatch
  probe/checksum/refusal work dominates because the translated set is still small
  (THUMB is refused outright, loads/stores are refused, so blocks average ~2
  instructions and 62% of dispatches go to the interpreter anyway).  The road to
  "faster than the interpreter" is therefore: (1) the arena-jump fault above,
  (2) native loads/stores through helpers, (3) a THUMB decoder - in that order.


## 2026-10-16: correct at last, and where the time actually goes

### The bug that made the JIT diverge (fixed)

`armCanTranslate()` treated **bits 27-25 = 000 with bit 4 set** as data processing
with a register-shifted operand.  That group is *miscellaneous*: the extra
load/store forms (STRH/LDRH/LDRSB/LDRSH/LDRD/STRD/SWP) live there, and they are
distinguished from a real register shift by **bit 7 being set**.  Without that
test the emitter silently mistranslated every halfword access:

    strh r14, [r1, #0x84]   E1C1E8B4   -> read as `add r14, r1, r0`
                                          (writes r1 into the guest link
                                           register, drops the store)

The verifier caught it on the ARM7 at 0x0380034C: `pre_r14=0000C000` but
`jit r14=04000100` - the base register, whose value is exactly `r1`.  One line
in `armjit.cpp` fixes it (`cls == 0 && !imm && bit4 && bit7 -> refuse`).

With that fixed, the JIT build and the interpreter build produce **identical RAM
and framebuffer hashes at every sample** for the whole run.

### The verifier (tools/run_ab.sh VERIFY=1)

`jit3Run()` can run every compiled block a second time through the interpreter,
for the same instruction count, and compare cycles/PC/CPSR/flags/registers.  The
interpreter's answer is the one that survives, so the *first* divergence is what
lands in `sd:/jit3-verify.txt` (64 lines, then only a heartbeat).  Verification
forces one guest instruction per dispatch (`g_blockLimit = 1`), otherwise the two
sides do not execute the same instruction stream: a multi-instruction block folds
its terminating branch in, so it retires one instruction more than the reference.

Result after the classifier fix: **bad=0 over ~34k verified instructions**, and 6
bad before it (5 cycle-only, all `strh`; 1 register, the LR clobber above).

### Self-test fixes

* Programs now run with 1-instruction blocks, so both sides execute the same
  stream and the per-step cycle counts are comparable.  p3(mix) used to "fail"
  only because the block folded in the terminating `b .`: 9 cycles vs 6, arithmetic
  correct on both sides.
* Per-step cycle comparison reports `bad=<step> cyc=<jit>/<ref>`, naming the
  instruction instead of just the total.
* Header `dispatched=` accumulated instead of being overwritten by the last program.
* Result: `programs=4 fail=0 jit_cycles=21 ref_cycles=21`.

### Measured, same harness and host (60 s runs, clean, no diagnostics)

| build | ticks/frame | cycles/frame | ram/fb hashes |
|---|---|---|---|
| `v3-interp.dol` | 21,916 | 1,680,570 | match interpreter |
| `v3-jit.dol` | 31,312 | 1,680,570 | **identical to interpreter** |

So the recompiler is currently **1.43x slower** than the plain interpreter - but
now for a known reason, not a mystery: it refuses nearly everything.

### Refusal census (always on, dumped to the stage file every 50 frames)

    # refuse: dp=162880 ldrstr=5286604 ldmstm=4152 branch=0 coproc=0 other=21 miscls=43622705
    # refuse #1 pc=020001B4 n=12332730     <- the VBlank spin (`ldrh` on 0x04000006)
    # refuse #2 pc=020001A4 n=4364850

* **89% of refusals are the misc load/store class**, 11% plain LDR/STR.  Both are
  refused because `compileBlock()` only translates DP/branch/PSR/MUL.
* Blocks therefore average **2.0 guest instructions** per dispatch
  (8,378,886 instrs / 4,219,790 dispatches), so the dispatch, the negative-cache
  lookup and `flushPipeline()` are paid per instruction: that is the whole
  slowdown.

### Next (in order)

1. **Emit loads/stores** (extra class first: it is 89% of the refusals and the
   hottest PC in the game).  Page-map fast path + helper fallback for the
   unaligned/IO/PC cases, and a per-guest-page "has blocks" bitmap so a store
   into a page holding translated code drops those blocks (self-modifying code).
2. Re-measure; the verifier makes each step safe to take.
3. Only then look at dispatch overhead (single-entry table, no `flushPipeline()`
   when the exit PC is the block's own fall-through).

## 2026-10-16, later: refusals are gone, the dispatch cost is the wall

### In-block delegation (armjit.cpp `Em::delegateInline`, `exitDelegateNow`)

`compileBlock()` used to *end the block* at every instruction it does not model,
and loads/stores were not modelled at all - so a block averaged 2.0 instructions,
the driver was entered 4.2M times per 60 s run, and 60% of those entries were
refusals that handed the instruction straight back to the interpreter.  Measured
consequence: the recompiler was 1.43x *slower* than no recompiler at all.

Now an unmodelled instruction is handed to the interpreter **from inside the
block** (`H_InterpOne`, with the JIT switched off for that one instruction so no
nested dispatch can compile over the arena the running block still occupies),
and the block carries on with the next instruction:

* refusals **44.3M -> 45,428** for the same 60 s of emulation
* instructions per dispatch 2.0 -> **3.1**
* correctness unchanged: identical RAM and framebuffer hashes at every sample

`jit3Config` keeps `H_InterpOne` as the only helper wired up; `H_Load*/H_Store*`
are reserved for the inlined fast path (see below).

### Two bugs this exposed

1. **The emitter was capped with the whole remaining arena.**  `compileBlock()`
   takes a word budget and only checks *its own* cap, but the driver passed
   `kArenaWords - arenaUsed` while reserving only 1200 words.  Long blocks (the
   point of delegation) ran off the end of the arena into the block table.
   Now the cap is `kMaxBlockWords` (1200) and the reserve is the same figure.
2. **`invalidateAll()` can happen inside a block.**  A delegated instruction that
   remaps guest memory (DMA/CP15) drops every block *while the current block is
   still running*.  Harmless for the running block (the table is zeroed, the code
   is not), but the verifier was reading the entry after the block ran and
   reporting `instrs=0`.  It now snapshots the entry first.

### Lazy pipeline refill (interpreter.cpp / jit3.cpp)

`jit3Run()` used to call `flushPipeline()` after every block: two walks through
the memory maps plus two reads, paid per dispatch, for a pipeline the next
dispatch (usually another block) never reads.  The block now leaves the pipeline
PC in the interpreter's convention and sets `pipelineStale`; `runOpcode()`
refills on demand.  Worth ~8% (1.43x -> 1.32x).  The step must follow the T bit -
a block can BX into THUMB - and the deferred refill subtracts the step before
calling `flushPipeline()`, which is what adds it back.

### Measured now (60 s runs, same harness and host, clean, no diagnostics)

| build | ticks/frame | ram/fb hashes |
|---|---|---|
| `v3-interp.dol` (JIT=0, current tree) | 21,830 | reference |
| `v3-jit.dol` (JIT=1, current tree) | 28,724 | **identical to interpreter** |

Still **1.32x slower**, and the reason is now purely host-side: 441,810 dispatches
per frame for 1,376,261 guest instructions, i.e. 3.1 instructions per dispatch,
and each dispatch pays a prologue/epilogue pair (18 saved host registers, 15
loaded guest registers) plus the C++ driver, while each delegated memory access
pays a further 14-register save, a helper call and a 14-register reload.

### Next, in the order the numbers argue for

1. **Block chaining.**  End a block by probing a small next-block cache the
   emitted code can read itself (pc + generation, address baked in via `Cfg`) and
   jump straight to the successor's warm entry - no C++ round trip, no
   prologue/epilogue, no register file round trip.  This is where the 441,810
   dispatches per frame go.
2. **Inline the common loads/stores** (page-map fast path, `H_Load*/H_Store*` as
   the fallback) so a memory access costs ~8 host instructions instead of a
   save/call/reload.
3. Then the rest: fewer guest registers loaded in the prologue (load lazily, the
   emitter already does that for r14/r15).

## 2026-10-16, last: it beats the interpreter, 1.65x (the delegate wall, and the
   two bugs that hid behind it)

The three items at the end of the previous section were all real, and all three are
now done - but the ordering was wrong, and the last one was not "the rest".

### The measurement that changed the picture

Two counters were added to the per-frame row: `deleginstrs` (of the instructions
inside compiled blocks, how many the interpreter executed *inline*) and `runs`
(how many times the driver was asked).  They came back, at frame 10:

    dispatches=425,464   instrs=856,625   deleginstrs=424,983   runs=425,519

i.e. **one delegated instruction per dispatch**, ~50% of everything the blocks
executed.  A delegate was not cheap: write back every guest register, call the
interpreter, reload every guest register - about thirty register moves around one
instruction, plus the C++ dispatch itself.  That was the 8.5% *and* the ceiling.

The words themselves, from the trace (`deleg=` on each compiled block):

    E4813004  str  r3,[r1],#4        (the guest's inner loop)
    E4D23001  ldrb r3,[r2],#1
    E1D230B4  ldrh r3,[r2,#4]
    E59F24E0  ldr  r2,[pc,#0x4E0]
    E92D4010  stmfd sp!,{r4,lr}      E8BD8010  ldmfd sp!,{r4,pc}
    E0C320B2  smlal

### What was built

* `emitTransfer()` + `H_Load8/16/32`, `H_Store8/16/32`, `H_LoadS8/S16`: loads and
  stores compile to address arithmetic in the block, one helper call for the
  access, and *nothing* around it - the write-back, the destination and the base
  live in the block's own registers.  The helper returns the value in r3 only.
  **Never return a struct here**: an eight-byte result travels in r3:r4 under one
  ABI and through a hidden pointer under another, and the failure mode is the
  helper reading the address out of the offset register (observed).
* `emitMiscTransfer()`: LDRH/STRH/LDRSB/LDRSH, which the entry-weighted census
  put at 2.5M of 2.8M delegated instructions.
* **P=0,W=0 is not unpredictable here.** NooDS's handler table routes it to the
  same post-index-with-write-back handler as P=0,W=1 (`strPtip`/`ldrPtip` in
  interpreter_lookup.cpp), and the guest depends on that write-back - refusing
  the encoding, as the ARM ARM invites, left the pointer unbumped and hung the ROM.
* The naive "refuse what I have not verified" rule cost a real bug of its own:
  the narrowed gate let the *plain* register-form compares through and left the
  register-shifted ones refused, which is where the sweep's 62 new failures came
  from before that was tightened.

### Numbers, 60 s and 250 frames, production build, no diagnostics

| frame | interpreter ticks | jit3 ticks | speed-up |
|---|---|---|---|
| 30 | 619,653 | 378,361 | 1.64x |
| 60 | 1,274,376 | 774,293 | 1.65x |
| 120 | 2,609,744 | 1,566,267 | 1.67x |
| 250 | (see ab-Final60-report.txt) | | |

Cycles, RAM hash and framebuffer hash are **identical to the interpreter on every
row**, and the shadow verifier reports `bad=0` over 11.9M blocks.

Chaining is doing its part underneath: `HITS=1` gives 3,643,699 hops against
425,464 dispatches at frame 10 - **8.6 blocks per dispatch**.

### Ledger caveat (cost me an hour, so it is written down)

`entered`/`instrs`/`deleginstrs` are accumulated in the *driver* path, and a
chained hop never returns to the driver - so those counters describe the driver's
slice of the work, not the whole run.  `chainhits` is the only counter that
measures the fast path.  Judge speed by ticks, correctness by the hashes, and use
the two together to see *where* the work is.

### Where the remaining time goes

Blocks still average ~2 instructions, so the boundary (prologue, epilogue, probe)
is paid every two instructions; the remaining delegates are STM/LDM (function
prologues/epilogues), SMLAL, and the PC-relative LDRS that are now compiled.
Filling in those two instruction classes is the next slice, and it is the same
shape of work as this one.

## 2026-10-16, final session: fast memory access tried and rejected; tree frozen

### The inline page-map fast path (built, host-verified, VERIFY-clean, reverted)

What it did: inlined the page-table lookup into the emitted code - `readMap9A/
readMap7[addr>>12]`, null check, `rlwinm` fold of the 12-bit offset, then
`lwbrx/lhbrx/stwbrx/sthbrx`; the helper call became the cold path.  ~61-67 words
of emitted code vs 62-69 for the helper-only form.

Verdict: **1 % slower** - `ab-D7` frame 10 = 115,514 ms vs 114,338 ms baseline
and frame 60 = 775,359 vs 774,294.  The helper `bctrl` is *not* the bottleneck,
so this avenue is closed.  Emitter kept for reference at
`/home/user/archive/fastmem-keep-armjit.cpp`.

Two bugs found on the way, worth remembering:

1. the `rlwinm` offset mask must keep **MB = 20** for every access size (only ME
   moves: 31 / 30 / 29 for 1 / 2 / 4 bytes).  A per-size MB keeps bit 12, so an
   access at `addr & 0xFFF == 0x804` looks one page too far ahead - wrong store,
   silent hang (`ab-D5` = report with only its header line).
2. a hand-reversion that leaves `emitTransfer`/`emitMiscTransfer` reading the
   loaded value out of a scratch register instead of **r3** (where the helpers
   return it) puts garbage in guest registers: heap corruption inside newlib
   `_free_r`, `PC = 0x800f73a8`.  Any manual revert must be diffed *and*
   re-VERIFIED - the first revert passed a build and a host sweep and still
   crashed.

### Frozen state (what is shipped)

* `pad[12]` chain-stride experiment removed from `jit3.cpp` - timing-neutral.
* dead `Cfg::offReadMap` / `offWriteMap` fields removed.  Removing them changes
  `Cfg()`'s zero-init size, so the binary md5 changes: it was **re-verified and
  re-timed** instead of inheriting the old evidence.  `ab-D16-verify.txt` is
  `bad=0` over 11,601,919 blocks; `ab-Final150-clean` matches `ab-Final150` to the
  millisecond at every checkpoint (f60 = 774,294, f150 = 1,958,344; both runs end
  at frame 670, 8,812,904 ms, `flushes=0`).
* shipped md5 `ba410c11644ba39502ee0374e65a3ee0`; a fresh `make -j4 JIT=1` in this
  tree reproduces it byte-for-byte; sources in `v3src-1940-final.tar.gz`.
* `chainhits=0` in ordinary reports is **not** a fault: the counter is only armed
  when `sd:/jit3-chainhits` exists (`HITS=1`).  Armed, frame 10 shows 3,641,796
  hops vs 425,452 dispatches - about 90 % of block transitions skip the driver.

## 2026-10-16, Thumb for both CPUs (jit3 reaches the second instruction set)

jit3 now compiles **Thumb** as well as ARM, for ARM7 and ARM9, modelled on
Agents2-5's `arm_jit.cpp` but against NooDS's own Thumb handlers as the reference.
DOL `ddc221bbf4af27a212b6c56b1cef930d` (`/home/user/dols/v3-thumb-jit.dol`).
Nothing about the ARM path changed: the same evidence holds (see RESULTS.md).

Coverage: immediate shifts and the ADD/SUB/MOV/CMP imm8 forms, the ALU row
(and/eor/orr/bic/adc/sbc/tst/cmp/cmn/neg/mvn, ARM9-only MUL), hi-register ADD/CMP/MOV
plus BX, PC- and SP-relative loads and ADD PC/SP, all nine load/store forms, PUSH/POP,
LDMIA/STMIA, B, Bcc (including as a continuation of the block), fused BL, and a
mode-switching BX. Refused (block ends, interpreter runs the halfword): SWI, the
undefined `0xDExx` encodings, BLX, hi-register forms with an r15 operand, and a stray
BL offset halfword. Delegated (interpreter inline, register file avoided):
POP/LDMIA-with-PC, MUL on ARM7, hi-register r15 writes.

Three real defects were found and fixed, in this order (each one was found by a
different instrument, which is the point of having all three):
* **Operand-2 encoding** (`thumbAlu`): Thumb's `imm3`/`imm8` fields were passed to
  `emitDP` raw, i.e. as if they were ARM operand-2 register fields - `adds r0,r0,r0`
  assembled as `add r0,r0,r0,lsl #0`. Caught by `tools/host/thumb_probe.sh`
  (`FAIL op=1C00`), fixed by encoding the immediate form properly (`bit 25 | rotate<<8
  | value`) and letting the register form be a plain register number.
* **A Bcc that ends a block emitted nothing**: `thumbEmitOne` had no case for it, so it
  fell through to `emitThumbAlu`, whose `thumbAlu()` said "not an ALU op" and returned
  without emitting code or charging cycles. A taken branch cost 0 cycles and the block
  exited on the fall-through. Caught by the in-emulator self-test (`p7 t-br`);
  reproduced in the probe with `-seq D000` (one-instruction block). Fixed by routing it
  through `emitThumbCondBranchExit` and letting the block's tail emit the fall-through.
* **`exitPcRegDyn` left `registersUsr[15] = 2 * target`**: the exit step lived in T1 =
  r4, and `mr(4, T4)` (the block ABI's return value) overwrote it before it was added -
  and the step arithmetic was inverted (`(T+1)<<1` is 4 in Thumb, 2 in ARM). A BX to a
  2-aligned Thumb address therefore resumed 2 bytes early. **VERIFY caught this one**
  (`bad pc=023E001A ... r15=047C0028/023E0016`, i.e. 2x 0x023E0014) - the fast path
  hides it because the driver rewrites registersUsr[15] from the returned exit PC.

Evidence for this build (all with `tools/run_ab.sh` on rockwrestler.nds in headless
Dolphin, or the host probe):
* self-test (10 programs: 4 ARM, 6 Thumb) `fail=0`, `jit_cycles == ref_cycles == 161`,
  all registers and CPSRs equal - at **one** instruction per dispatch and, with
  `BLOCKS=4`, at four per dispatch (conditional branch continuations, fused BL, chained
  dispatch off in the test so blocks align with interpreter steps).
* `VERIFY=1`: `bad=0` across 29.5 million verified blocks.
* `tools/host/thumb_probe.sh`: `OK: 532296 checked, 0 failed, 55296 not modelled`
  (Thumb reference model per format).
* A/B against the interpreter build: guest **cycles identical** at every checkpoint
  (f10 15534090, f30 49145490, f60 99562590, f120 200396790) and 1.63-1.66x on host ms
  at f10-f120, marginal rate 13198 vs 13199 ms/frame - the ARM ratio is unchanged; the
  ~1 s offset on early rows is the larger one-time boot self-test (6 more programs).

New instruments, all reusable:
* `tools/host/thumb_probe.sh -seq <hex4[,hex4...]> [-csr <hex>]` compiles and runs one
  block of literal halfwords with no reference, printing the emitted words and what they
  left behind - the only way to see a block-ending branch or a fused BL at all;
  `-max <n>` sets the block length, `-v` prints progress (a crash names the opcode).
* the same probe skips blocks that delegate or cover more than one instruction: linking
  only the emitter means the interpreter-helper table is not there to call.
* `BLOCKS=<n>` (an SD-card marker read at boot, see main.cpp) asks the in-emulator
  self-test for `n` instructions per dispatch; the verdict is cycles/pc/cpsr/registers
  (the `ins=` figure is block coverage, not instructions executed).
* every self-test program now ends `... ; b +4 ; b .` so that the terminating sentinel
  is reachable only as a block *start*: a multi-instruction block that swallowed it
  executed and charged it while the reference stopped at it, which looked exactly like a
  mispriced branch.

Still open for Thumb: cycle-count timing on a Thumb-heavy program (rockwrestler has no
Thumb at all, so the A/B says nothing about it), the refusal census in a Thumb ROM, and
the doc sections for Thumb in `notes-jit3.md` / `RESULTS.md`.

## 2026-10-16 (later), external oracle + Thumb register shifts

The user pointed at the mGBA test suite (mgba-emu/suite; `mattrbeck/mgba-suite-auto`
builds it).  NooDS-Wii has no GBA-mode CPU, so the ROM itself cannot run under our
build - but the suite's CPU-core *tables* carry explicit hardware expectations, and
that is usable directly:

* `tools/host/gen_suite_vectors.py` parses `src/shifter.c` and `src/carry.c` and emits
  `tools/host/suite_vectors.txt`: one Thumb halfword per vector, with the suite's own
  expected result **and CPSR**.  Cases with no Thumb form (ROR-immediate/RRX,
  three-register forms, PC operands, RSC) are listed with the reason.
* `tools/host/thumb_probe.sh -suite suite_vectors.txt` runs each vector through the
  compiled block and reports *three* verdicts: jit vs the suite (a jit3 bug), the
  NooDS-faithful model vs the suite (a NooDS finding, not ours to fix silently), and
  jit vs model (a jit3 bug even if the suite agrees with one side).

Two model bugs and one emitter bug were found by it (the emitter bug is the
`rlwnm`/`rlwimi` mask convention: **mask fields count from value bit 31 down**, i.e.
mb=0 is value bit 31 and mb=31 is value bit 0 - measured with a PPC program under
qemu-ppc, see /home/user/tmp-ppc/rot2.c).

The reg-shift rows were then compiled instead of delegated.  `lsls/lsrs/asrs/rors Rd,
Rs` map onto the ARM encoding `movs Rd, Rd, <armtype> Rs` (bit 4 set = shift by
register, amount register in bits 11:8), and they are the one ALU-row family whose C
is *runtime-dependent*, so `Em::shiftRegCarry` was added: value via the existing
`shiftReg`, carry via a rotate that isolates the shifted-out bit (LSL: `src[(32-n)&31]`
with n>32 clearing it, LSR: `src[(n-1)&31]` with n>32 clearing it, ASR: `src[(n-1)&31]`
and src[31] for n>32, ROR: `src[(n-1)&31]`), with the live flag substituted when n==0.
`thumbWrittenFlags` also had a latent bug here (reg shifts write C; the table said
N|Z) - invisible while the rows were delegated.

Evidence on `34bccfac282f5072caedfcf2782cd405`:
* oracle: 95 vectors, 0 fails in all three verdicts, 0 skipped.
* probe sweeps: `OK: 538440 checked, 0 failed` (up from 532296 - the added coverage),
  reg-shift rows alone `checks=160 failures=0` each.
* self-test `fail=0` at 1 and at 4 instructions per dispatch, cycles 161/161,
  and the delegation census moved `dp=10` -> `dp=6` (the four rows now compile).
* VERIFY `bad=0` over 29.5M blocks.  A/B: cycles identical to the interpreter at
  every checkpoint, host 1.63-1.66x.

Tooling fix worth remembering: `tools/guard.sh`'s file list named the *directory*
`NooDS-Wii/jit3`, and the check skips directories - so every emitter edit was invisible
to the drift check.  It now lists the individual sources (plus tools/host), which is
how the drift in this turn was seen.

## 2026-10-16 (later again), the ARM board: multiply-long and RSCS as the suite sees them

The Thumb board was already wired to the mGBA suite; this adds the ARM half of the
same oracle and then fixes what it found.  Neither fix came from comparing against
the interpreter - both are places where the interpreter was wrong.

### The ARM oracle

`tools/host/gen_suite_vectors.py` now also emits `suite_vectors_arm.txt`: 207
vectors from the suite's `shifter.c`, `carry.c` and `multiply-long.c` tables, in
the encoding the suite's own asm uses (multiply-long works on `r8`/`r9` because
that is what the suite's `ldr r8, in3 / ldr r9, in2` does, and it skips rows with
no addend exactly as the suite does).  Each row carries a core tag:

    core|name|op|r0|r1|r2|r3|r8|r9|cpsr_in|checks|note

`any` rows are architectural and are checked on the ARM9 path in full - that is
the path jit3 compiles.  `arm7` rows are ARM7TDMI-specific (the multiply-long C
flag) and are run twice: on the ARM9 path for the result registers and N/Z only,
C masked, and on the ARM7 path in full - where jit3 refuses long multiplies
(variable cycle count) so the interpreter's verdict is the shipped one, and a
disagreement there fails the run.

Two oracle bugs had been hiding each other and were found on the first clean run:
`arm_imm()` left the data-processing opcode at 0, so every "movs r0, #imm"
shifter row was really "and r0, r0, #imm"; and the four affected rows had a
9-field line shape, which the old runner skipped.  Fixed both; the rows now run
and pass.

### Finding 1 - the ARM7TDMI multiply C flag (interpreter, ARM7)

The suite's multiply-long table records C for SMULLS/UMULLS, and it is not
"unchanged": it is the carry out of the ARM7TDMI multiplier's final adder.  The
multiplier is a Booth-recoding + carry-save array that walks the multiplier eight
bits per cycle with early termination, and the published simulation of it
(zaydlang, github.com/zaydlang/multiplication-algorithm, zlib licence) reproduces
**72/72** of the suite's C values - verified by compiling that model unmodified
and driving it with the suite's own operand rows, and then again through this
port (`NooDS-Wii/arm7tdmi_mul.hpp`) which matches all 72 C values *and* the exact
64-bit products.

NooDS wrote no C at all there (the S forms wrote N and Z only), so 26 of the 72
recorded values were wrong.  The fix is ARM7-only and deliberate: the DS's ARM9
is an ARM946E-S with a different multiplier, the suite - being a GBA suite -
says nothing about it, and melonDS's ARM9 path does something different again
(it forces C to 0).  Where there is no evidence, leave the behaviour alone.

jit3 does not need the same code: it refuses long multiplies on ARM7 (variable
cycle count) and hands them to the interpreter, which now gets them right, so
the shipped ARM7 path matches the suite without any emitted code.

### Finding 2 - RSCS left the incoming C and V set (interpreter *and* jit3)

Six rows of the suite's carry table failed identically in jit3 and the
interpreter: RSCS with C already set and a computed carry of 0.  NooDS's handler
masks with `~0xC0000000` and ORs the computed C and V in, so RSCS could set those
flags but never clear them, unlike every other arithmetic S form.  The suite says
RSCS replaces all four flags; so does melonDS (`SetNZCV`) and so does the ARM ARM.
jit3's emitter had been faithfully reproducing the oddity (`orCFromXer`/`orVSub`,
with a comment saying it was mirroring `interpreter_alu.cpp`), which is exactly
the failure mode the predecessor comparison warned about.  Both sides now replace
all four flags and the two OR helpers are gone.

### Evidence

* ARM oracle: 207 vectors, `jit vs suite FAIL=0` (C masked on the 72 ARM7 rows),
  `interpreter vs suite FAIL=0`, `shipped-on-own-core FAIL=0`, skipped=0.
* Thumb oracle unchanged: 95 vectors, 0/0/0, skipped=0.
* Host sweeps: ARM/PPC `OK: 247411 checked, 0 failed`; Thumb `OK: 538440 checked,
  0 failed` - both unchanged from the previous slice.
* Console: self-test `fail=0` at 1 and 4 instructions per dispatch; `VERIFY=1`
  `bad=0` over 29,576,191 blocks; 150 s A/B `runs/canonical/ab-mullong-150`,
  cycles identical to the interpreter at every checkpoint (f10 15534090,
  f30 49145490, f60 99562590, f120 200396790), 1.63-1.66x, `flushes=0`,
  delegation census unchanged.
* Build `39b8fe054c1f7d95cf3ce4efaad39116` -> `dols/v3-mullong-arm7.dol`, pinned
  in `tools/restore_v3.sh`.

### Also fixed on the way

`tools/guard.sh` now archives the whole tree instead of a hand-written file list.
The list had drifted: it never covered `tools/host/run_thumb_probe.sh` (gone, and
not in any tarball - recreated) and it named the directory `NooDS-Wii/jit3`,
whose entries the drift check skips, so emitter edits were invisible.
