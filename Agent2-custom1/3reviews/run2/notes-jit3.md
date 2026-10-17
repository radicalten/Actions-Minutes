# notes-jit3.md — the v3 recompiler, its predecessors, and this workspace

This file is the hand-off document for `v3-NooDS-Wii/NooDS-Wii/jit3/`, the PowerPC
(Broadway / 750CL) dynamic recompiler for the ARM7 (ARMv4T) and ARM9 (ARMv5TE) cores
of NooDS-Wii.  It has three jobs:

1. explain **how jit3 works** — mechanism, not marketing;
2. compare it, concretely, with the two recompilers it was written to replace
   (`b4-pp2-NooDS-Wii` and `Agents2-5-NooDS-Wii`);
3. say, in copy-pasteable commands, **how to rebuild this whole environment** —
   devkitPPC, headless Dolphin, the host test toolchain, the ROM, and the run/test
   recipes — because none of that survives on its own.

State it documents (2026-10-16, the frozen build):

| | |
|---|---|
| shipped binary | `/home/user/v3-jit.dol`, md5 `ba410c11644ba39502ee0374e65a3ee0` |
| source snapshot | `/home/user/v3src-backup/v3-src-1940-final.tar.gz` |
| headline | **1.64×–1.67× faster than the interpreter build, state-identical** |
| correctness | shadow verifier `bad=0` over 11,601,919 blocks; host sweep `247,412 checked, 0 failed`; on-device self-test 4/4 |
| companion docs | `RESULTS.md` (results + how measured), `NOTES.md` (dated log, every bug), `BUILDING.md` (build detail), `/home/user/README.md` (workspace index) |

---

## Part 1 — How jit3 works

### 1.1 Where it plugs in

NooDS executes guest instructions one at a time:

```
Interpreter::runOpcode()                      NooDS-Wii/interpreter.cpp:236
    int jitCycles = jit3Run();                jit3/jit3.cpp:440
    if (jitCycles >= 0) return jitCycles;     // a compiled block ran
    ...interpret exactly one instruction as before
```

So the JIT is **additive**: it answers a question ("can you run the code at this PC
for a while?") and returns the guest cycles it consumed, or `-1` meaning "not mine,
interpret this word".  Every code path that the emitter refuses, every Thumb
instruction (not compiled yet), and every halted or unmapped PC falls through the
same door and behaves exactly as the interpreter always did.  That is what makes
the A/B comparison honest: `JIT=0` builds the identical emulator without the door.

The dispatcher's own state convention: `*registers[15] - 4` is the logical PC at
entry (NooDS keeps `PC + step` in r15), and on exit a block leaves
`registersUsr[15] = exitPc + step` with `pipelineStale = true`, so the interpreter
refills `pipeline[0..1]` lazily.  Get this wrong by one step and the guest runs an
instruction twice or skips one — the self-test exists to catch exactly that.

### 1.2 Compilation: what becomes a block

`compileBlock()` (`armjit.cpp:1175`) scans forward from the entry PC and stops on
any of these conditions:

* **one 4KB page per block** — a fetch must stay in the page whose host pointer the
  block was compiled against, because that pointer is the block's identity;
* an instruction the classifier translates but that **moves the PC** (branch, BX,
  `ldr pc`, any writeback of r15, LDM/STM containing r15, SVC/coprocessor) ends the
  block — both its taken and not-taken exits are emitted, which is why blocks no
  longer decode dead code after a jump;
* an instruction the classifier **does not model** is either compiled as an
  *inline delegate* (the interpreter runs that one instruction from inside the
  block, then the block continues) or ends the block, depending on whether it can
  move the PC;
* `cfg.maxBlockInstrs` = 24, or 32 scan slots, or the arena cap of 1200 host words;
* a **conditional branch may continue into its fall-through** if there is room and
  the fall-through stays in the same page — the taken path is emitted as an exit and
  the block carries on with the not-taken path.

Then a **flag liveness pass** runs backwards over the block: each instruction gets
an emit mask saying which of N,Z,C,V it must actually materialise, given which
flags anything downstream still reads.  A conditional instruction cannot make an
earlier writer's flags dead (it may not execute), so only always-executed writes
clear liveness.  That pass is why an ALU op with `S=1` is a few host instructions
rather than fifteen.

The scheduler is consulted at every exit, so a block can never run past the
driver's event check for longer than one block (≤24 guest instructions).

### 1.3 The block ABI

Host registers inside generated code (PowerPC EABI preserved):

| PPC | holds | |
|---|---|---|
| `r1`, `r2`, `r13` | host stack / EABI-reserved / EABI-reserved | untouched |
| `r3..r10` | scratch (`T0..T7`) | |
| `r11`, `r12` | helper address / `mtctr` staging (`TCALL`) | |
| `r14..r27` | **guest `r0..r13`, resident** | `RR(g) = 14 + g` |
| `r28` | guest CPSR (flags, T, mode — whole word) | |
| `r29` | cycle deadline supplied by the glue | |
| `r30` | guest cycles accumulated by this block | returned in `r3` |
| `r31` | `Interpreter*` anchor | |

Block entry `r3 = Interpreter*`, `r4 = deadline`.  Return is a two-word struct in
`r3:r4` (PowerPC SVR4: exactly where `epilogue()` leaves `RCYC` and the exit PC):
`r3` = guest cycles, `r4` = next guest PC, **or `0xFFFF0000` (`kPcFromCpu`) meaning
"the PC is in the CPU, read it back"** — that is the convention for a delegated
instruction that moved the PC behind the block's back.

```
prologue:  stwu r1,-96(r1); mflr; stw lr; stmw r14,r1+8
           r31 = r3 (cpu); r29 = r4 (deadline); r30 = 0
           load guest r0..r7 from cpu->registersUsr[]
           load guest r8..r13 through cpu->registers[g] (banked SP etc.)
           load r28 = cpu->cpsr
           <-- info.warmOffset: the chain entry, "everything already resident"
body:      the emitted guest instructions
epilogue:  r3 = r30; lmw; lwz lr; mtlr; addi r1,+96; blr
```

`storeModified()` writes back only registers the block touched; `storeAll()`
writes back everything and is used whenever **chaining is enabled** — the successor
is entered at its warm entry, so the predecessor's dirty registers live in the host
registers, and one full write-back per *chain* (instead of one per block) is the
point of the design.  Get this mixed up and you silently lose guest registers:
that is the "chained exit must use `storeAll()`" invariant (`armjit.cpp:160`).

### 1.4 Exits and chaining

Four exit shapes:

| exit | used for | leaves |
|---|---|---|
| `exitPcConst(pc)` | fall-through, static branch targets | guest r15 = pc+step, `r4 = pc` |
| `exitPcReg(reg)` | `bx`, `blx`, computed targets (step known) | guest r15 = target+step, `r4 = target` |
| `exitPcRegDyn(reg)` | `bx`/`blx` that may change the T bit | re-derives step from the new T bit |
| `exitDelegateNow()` | a delegated instruction that moved the PC | `r4 = 0xFFFF0000` |

Every exit first tries the **chain probe** (`Em::chainEmit`, `armjit.cpp:195`).
Instead of returning to C++, the emitted code:

1. mixes the target PC the same way the driver's `slotFor()` does
   (`(pc>>2) ^ (pc>>14)`, masked to 8191) and adds it to the driver's block table;
2. compares `entry.pc`, then `entry.gen` against the generation the block was
   compiled in, then checks `RCYC < chainBudget` (default **3000 guest cycles**);
3. on a hit, loads `entry.warmPtr`, sets `r3 = Interpreter*`, `mtctr`, `bctr` —
   jumping straight into the successor **past its prologue**;
4. on any miss, falls through into the ordinary exit above, so the driver remains
   the safety net (nothing compiled yet, stale generation, budget exhausted).

This is where the measured win lives: on this ROM a dispatch carries **3,641,796
chain hops against 425,452 driver entries at frame 10** — about 90 % of block
transitions never touch C++.  Note the ledger caveat (it has bitten twice):
chained hops *do not* increment `dispatches`/`instrs`, so those columns describe
the driver's slice of the work, not the guest's total.  `chainhits` (HITS=1) is the
only counter that sees the fast path.

### 1.5 Prove-or-delegate: what is compiled, what is not

The rule is: **an instruction is either emitted completely, or it is handed to the
interpreter — never partially translated.**  The classifier is `armCanTranslate()`
(`armjit.cpp:815`) with `armIsDelegate()` beside it for what can safely run inline.

Compiled natively today:

* ARM data processing, immediate and register shifts, S-bit flags with the exact
  NooDS semantics (including `shifterWritesC` cases), TST/TEQ/CMP/CMN;
* MUL/MLA **on ARM9 only** (ARM7's variable cycle count is not modelled);
* `MRS`/`MSR` for the flag/status bytes of CPSR (the mode byte is refused:
  `setCpsr()` has side effects beyond the register);
* B/BL (both exits), BX Rm, BLX(register) on ARM9, `ldr pc` style computed targets;
* **single data transfers** — LDR/STR/LDRB/STRB through one helper call each;
* **misc transfers** — LDRH/STRH/LDRSB/LDRSH, including the ARM7 misaligned-read
  rotation.

Handed to the interpreter *inside* the block (inline delegate, `H_InterpOne`):
LDM/STM, SWP, SVC, coprocessor/CP15, `mul r15`, `muls` with a register-shifted
operand, and the other shapes listed as refusals below.

Refused outright (block never starts, or the word is delegated): Thumb (whole ISA,
not compiled yet), RRX and register-shifted S-forms whose carry/XER ordering the
flag pass does not model, register-shift forms with PC operands, `MSR` of the mode
byte, SPSR forms, QADD, ARM7 multiplies, BLX-immediate.

Two traps this classifier was built around, both of which produced silent corruption
and are worth keeping in mind when extending it:

* bits 27–25 = `000` is **not** "data processing" — the extra load/store forms
  (STRH/LDRH/LDRSB/LDRSH/LDRD/SWP) live there too and are told apart by **bit 7**.
  Without that test `strh r14,[r1,#0x84]` was emitted as `add r14, r1, r0`.
* `isPsrOp()`'s pattern also matches the register-operand forms of CMP/CMN/TST/TEQ.
  Treating those as PSR ops emitted an MRS (CPSR into the destination) and no flags.
  Hence `isPlainRegDp()` and its explicit dispatch ahead of the PSR path.

### 1.6 Memory: helper calls

Every compiled load/store is a call into a NooDS-side helper — `jit3Load32/8/16/`
`jit3LoadS8/S16/jit3Store32/16/8` (`jit3.cpp:348+`) — which performs the access
through the emulator's own handlers, so cycle charges, page maps, misalignment
rotation, IO side effects and the ARM7 HALTCNT check are *the interpreter's*, not a
re-implementation.  Values come back in **r3** (`emitMiscTransfer` reads it from
there; a hand-reversion once left those two sites reading a scratch register and
corrupted guest registers into a heap crash inside newlib `_free_r`).

An **inlined page-map fast path was built, verified and then reverted**: look up
`readMap9A/readMap7[addr>>12]`, null-check, fold the 12-bit offset with `rlwinm`,
use `lwbrx/lhbrx/stwbrx/sthbrx`, and make the helper the cold path.  It was
byte-identical to the helper form in the host probe and `VERIFY`-clean — and
**1 % slower** (`ab-D7`: 115,514 ms vs 114,338 ms at frame 10).  Conclusion: the
helper `bctrl` is not the bottleneck.  Source kept at
`/home/user/archive/fastmem-keep-armjit.cpp`.  Two lessons are recorded in
`NOTES.md`: the offset mask must keep **MB = 20** for every access size (only ME
moves), and every fast path must be tested with bit-12-set addresses.

### 1.7 Invalidation — and the gap

A block is identified by `(pc, page pointer, generation)` and the block table entry
is *also* the chain probe's data structure (`ChainEntry` is the first four fields —
layout shared between emitter and driver via `armjit.hpp` so they cannot drift).

`invalidateAll()` (bump the generation, clear both tables, reset the arena) is
called from exactly three places in the emulator:

| site | trigger |
|---|---|
| `memory.cpp:256` | `Memory::updateMap9` — ARM9 memory map change |
| `memory.cpp:345` | `Memory::updateMap7` — ARM7 memory map change |
| `memory.cpp:623` | `Memory::updateVram` — VRAM remap |

plus the driver itself when the 1 MB arena runs out (`Stats::flushes`).

**Known gap, stated plainly:** there is *no* store-path hook and *no* per-dispatch
checksum in this build, so a guest that writes to memory it is currently executing
(self-modifying code) keeps running the stale block until something else invalidates
— the same class of defect `COMPARISON.md` §2.1 charges against `b4-pp2`.
`rockwrestler.nds` does not exercise it (11.6 M verified blocks, `bad=0`), and
nothing in this ROM's hot path rewrites itself, but a general NDS title that does
(e.g. copy-protection that patches its own code) is *not* safe on this build.  The
fixes, in increasing cost: a per-guest-page "has blocks" bitmap checked on stores
(what `b4-pp2`'s cover mask was meant to be), a generation bump from the write
path, or the per-dispatch word checksum the earlier revisions used.  Any of them
must be followed by a fresh `VERIFY=1` run and a re-timing, because the shipped
md5 will change.

### 1.8 Instruments

All three are opt-in by dropping a marker file on the SD card, so ordinary runs pay
nothing (see §4.4–4.5, and `tools/run_ab.sh` sets them from env vars):

| marker | env | effect |
|---|---|---|
| `sd:/jit3-verify` | `VERIFY=1` | shadow verifier: run the block, snapshot, re-run the same instruction count on the interpreter, compare cycles/PC/CPSR/halt/all 16 registers.  The **interpreter's** answer survives; the first divergence is logged.  Forces one instruction per dispatch and disables chaining, so it is a correctness instrument, not a measurement |
| `sd:/jit3-trace` | `TRACE=1` | append every compiled block's disassembly + metadata to `sd:/jit3-stage.txt` |
| `sd:/jit3-chainhits` | `HITS=1` | count chain hops (costs five words per hop) |

Always-on and cheap: the **refusal census** (`dumpRefusals()`), written to the
stage file every 50 frames — both a per-compile classification of delegated words
and an entry-weighted one (`# deleg:` vs `# deleg-entry:`), plus the eight hottest
refused PCs.  That census is how the transfer slice was prioritised, and it is the
first thing to read before writing new emitter code.

The **on-device self-test** runs four guest programs twice at boot (JIT live, then
`g_interpretOnly`) and compares cycles, all registers, CPSR and exit PC; its verdict
is the first lines of the stage file, and it is the cheapest end-to-end sanity check
after any emitter change.

### 1.9 Files

| path | role |
|---|---|
| `NooDS-Wii/jit3/ppc.hpp` | pure 750CL encoders, no emulator headers (host-buildable) |
| `NooDS-Wii/jit3/armjit.hpp` | the emitter's contract: `Cfg`, `HelperId`, `ChainEntry`, `BlockInfo` |
| `NooDS-Wii/jit3/armjit.cpp` | block scan, classifier, emitter, exits, chain probe (~1300 lines) |
| `NooDS-Wii/jit3/jit3.h` | driver's public surface: stats, switches, invalidate |
| `NooDS-Wii/jit3/jit3.cpp` | arena, block table, negative cache, `jit3Run()`, helpers, self-test |
| `NooDS-Wii/interpreter.cpp` | the one hook (`jit3Run()` first) + fetch helpers |
| `NooDS-Wii/memory.cpp` | the three `invalidateAll()` call sites |
| `NooDS-Wii/main.cpp` | per-frame report, marker-file startup, selftest/report plumbing |
| `tools/host/*` | `ppc_sim.hpp` (host PPC interpreter), `arm_ref.hpp` (reference ARM model), `jit3_test.cpp` (differential sweep), `xfer_probe.cpp` (single-instruction tool), `run_tests_ppc.sh` |

The **host-buildability split is the single most valuable design decision here**:
`armjit.cpp` decides *what host code to emit* and has no NooDS or GEKKO dependency,
so it compiles on a PC and is differentially tested against a model of the
interpreter's handlers (`arm_ref.hpp`) — emit → run under qemu-ppc → compare
registers, flags, cycles, next PC.  Without that loop, every emitter bug costs a
40-second Dolphin round trip instead of a 2-second host run.

---

## Part 2 — How it compares with the two predecessors

Both predecessors are complete, serious recompilers (~2000–2800 lines each).  The
comparison below is the one that drove v3's requirements (`COMPARISON.md` §3), with
the measured outcome added.

### 2.1 Side by side

| | `b4-pp2` | `Agents2-5` | **v3 / jit3** |
|---|---|---|---|
| engine | `jit_ppc.cpp` 2049 + `jit_trampoline.S` 24 | `arm_jit.cpp` 2798 + `jit_bridge.S` 73 | `armjit.cpp` 1304 + `jit3.cpp` 868 (+`ppc.hpp`) |
| unit of compilation | block ≤48 instrs, per-page table | per-instruction stubs + blocks ≤24 | block ≤24 instrs, one page, ±conditional fall-through |
| guest registers | resident (r14–r28) | **in memory**, via `r31` | resident (r14–r27), CPSR in r28 |
| flags | always all of N,Z,C,V when S=1 | all-NZCV + liveness analysis | **liveness masks per instruction** + exact NooDS per-op semantics |
| condition test | `mtcrf` + `bc` | load cpsr → `condition[]` → `lbz` → `cmpwi` → `bc` | `mtcrf` + `bc` (CR-logic ops for HI/LS/GE/LT/GT/LE) |
| memory ops | **always** a C helper call | inline page-map fast path + helper on miss | helper call (fast path measured 1 % slower, reverted) |
| unhandled instruction | exit block → 1 interpreter opcode, or a permanent "sentinel" PC | per-instruction native stub, else the interpreter's handler | **inline delegate** (`H_InterpOne`) or refusal; never a partial translation |
| invalidation | **none wired** (dead cover mask) | page epochs + tail hash, hooked into every mapped store | generation + page identity; **no store hook (gap, §1.7)** |
| halt respected | checked after every store | **not checked in the instruction path** | at dispatch + ARM7 IO-store check |
| block linking | none | none | **chain probe into the driver's own table, budgeted** |
| event scheduling | ignored inside 48-instr blocks | block rejected if it crosses an event | every exit returns to the driver; ≤24 instrs per block |
| code budget | 16 MB MEM2 | 1 MB (worst case ~64 blocks before a full wipe) | 1 MB arena, 1200-word cap reserved before each compile |
| diagnostics | printf + `dumpFallbacks()` | `Stats`, hot-PC profile | 18-field per-frame report, refusal census, selftest, verifier, trace |

### 2.2 Good ideas that were kept

From **b4-pp2**: the resident register file with a block prologue/epilogue that
loads and stores them (its fastest trait), `mtcrf`+`bc` condition tests, careful
CARRY/XER construction, and halting respected on stores.
From **Agents2-5**: "the interpreter is the fallback authority" for anything not
translated, block formation that respects the scheduler, flag liveness, and a
profile that says which classes are still worth translating.

### 2.3 Defects that are structurally absent in v3

* **b4-pp2: no invalidation at all** (`invalidateJitPage` had zero callers): the
  dead cover-mask machinery existed but nothing consulted it, so SMC, overlays and
  TCM/VRAM remaps ran stale host code.  v3 keys blocks on the page pointer and a
  generation, and flushes on both map changes and VRAM remaps — but see §1.7 for the
  SMC hole that remains.
* **b4-pp2: PC/pipeline coupling** — `JitHelp_commit` rewrote `registers[15]` and
  set a global `g_pipeDirty`, leaving `pcData`/`pipeline[]` stale for anyone else
  who looked.  v3 has one convention (see §1.1) and republishes the pipeline state
  at every exit.
* **b4-pp2: cycle accounting inflated ~2×** (`runCpu(...) << 1` for ARM9,
  `<< 2` for ARM7 against the interpreter's 1:1 / 2:1) and a per-instruction table
  that disagreed with the handlers.  v3 charges the handler's own formula at compile
  time and returns a cycle total the driver adds verbatim — which is why the cycle
  column matches the interpreter to the digit on every checkpoint.
* **b4-pp2: the "sentinel PC"** — one uncompilable instruction at a hot PC
  permanently routed that PC to the interpreter until a full flush.  v3's negative
  cache is per `(pc, page, generation)` and is dropped by the same invalidation that
  drops blocks.
* **b4-pp2: ARM7 `LDR pc` latched the Thumb bit unconditionally** where the
  interpreter only does it `!arm7`.  v3's exits re-derive step and alignment from
  the new T bit (`exitPcRegDyn`).
* **Agents2-5: `halted` never checked in the instruction path**, so a block kept
  running up to 23 instructions after the store that halted the CPU.  v3 checks at
  dispatch and checks the HALTCNT page on ARM7 IO stores.
* **Agents2-5: memory-bound codegen** (two dependent loads to read one guest
  register, ~10–14 host instructions for a flag-setting ALU op).  v3 keeps guest
  r0–r13 in host registers across the whole block.
* **Both: no block linking** — every block returned to C++, recomputed the PC,
  re-hashed the table and re-entered.  v3 chains, and it is worth ~9× fewer driver
  dispatches on this ROM.

### 2.4 Deliberate divergences

* **The inline page-map memory path was not adopted** even though Agents2-5 has one
  and it is the "right" idea in general.  On *this* design it measured 1 % slower
  (§1.6), because the helpers were already off the critical path once registers are
  resident and blocks chain.  Measurement beat expectation; the code is archived.
* **Flags are not always materialised.**  b4-pp2 pays 15 host instructions per
  S-bit op; v3 materialises only what a downstream reader can observe.
* **Unmodelled instructions are delegated *inside* the block** rather than ending
  it (Agents2-5 ends the block; b4-pp2 exits to the interpreter one instruction at a
  time).  This is what killed the "blocks average 2 instructions" pathology: the
  block boundary is now paid per 24 instructions, not per refusal.

### 2.5 Measured results (this ROM, headless Dolphin, `ab-N4int` reference)

| frame | interpreter ms | v3 JIT ms | speed-up |
|---|---|---|---|
| 10 | 191,411 | 114,338 | 1.67× |
| 30 | 619,653 | 378,361 | 1.64× |
| 60 | 1,274,376 | 774,294 | 1.65× |
| 90 | 1,942,061 | 1,170,242 | 1.66× |
| 120 | 2,609,744 | 1,566,267 | 1.67× |
| 140 | 3,054,834 | 1,826,305 | 1.67× |

14/14 common checkpoints match on guest cycles **and** main-RAM hash **and**
framebuffer hash; a 670-frame run (150 s wall) shows `flushes=0`.
For scale, honestly: 670 frames is 11.2 s of guest time, so this is ~13× slower than
realtime — the result is a 1.65× win over *our own interpreter*, not a playable
Nintendo DS.

For contrast, the same harness measured the earlier v3 slices at **2.5× slower than
the interpreter** (slice 1, ALU only) and then **1.43× slower** (delegation without
compiled transfers).  The 1.65× came from compiling the transfers plus chaining;
nothing about the machine changed in between.

### 2.6 What v3 still does not do

* **Thumb is not compiled** (`compileBlock()` returns false for Thumb) — a large
  share of real NDS ARM7 code, and plenty of ARM9 code, still runs one instruction
  per dispatch.
* **LDM/STM and SMLAL-style multiplies are delegated**, and they are exactly the
  next slice: function prologues/epilogues, including `ldmfd sp!,{...,pc}` returns,
  show up in the refusal census as the remaining hot class.
* **SMC/self-modifying code is unprotected** (§1.7).
* Blocks still average ~2 instructions *on this ROM*, so the boundary is a real
  cost even after chaining.
* The dispatcher's counters under-report chained work (§1.4) — read timing from
  `ms`/`cycles`, and use the counters only to see *where* the work is.

---

## Part 3 — Recreating the workspace environment

### 3.1 What survives, what does not

The sandbox keeps small files (`/home/user` minus a snapshot's exclusions) and
**deletes the big generated trees between turns**: the devkitPPC install, Dolphin's
~400 MB extracted root, and the host toolchain under `~/.local/tc`.  Source files
have also silently reverted to an older revision several times (three times so far).
Therefore:

* **`tarballs are the authority`** — `/home/user/v3src-backup/v3-src-*.tar.gz`.
  `tools/restore_v3.sh` restores the newest by dropping it over the tree, fetches
  missing toolchains, and with `--verify` rebuilds and compares the md5.
* **refresh the tarball after every meaningful change** (`tar czf … NooDS-Wii/jit3
  NooDS-Wii/main.cpp … Makefile tools NOTES.md BUILDING.md RESULTS.md notes-jit3.md`).
* the fetch scripts are all idempotent; when in doubt, re-run them.

### 3.2 One command

```bash
cd /home/user/repo/v3-NooDS-Wii
bash tools/agent_env.sh all        # dkp + host + dolphin + sdcard, in order
```

Note it takes **one** target argument (`all`, `dkp`, `host`, `dolphin`, `sdcard`) —
`agent_env.sh dkp dolphin` silently runs only `dkp`.

### 3.3 devkitPPC (the Wii compiler) — two routes

The build wants a directory to pass as `DEVKITPRO` containing
`devkitPPC/bin/powerpc-eabi-g++`, `libogc/lib/wii/libogc.a`, `devkitPPC/wii_rules`
and `tools/bin/elf2dol` (only `devkitPPC`, `libogc`, `portlibs`, `tools` are needed).

```bash
# route A - official packages (offline if $PREFIX/pkgs is populated)
PREFIX=/home/user/dkpopt SKIP_BUILD=1 bash tools/mk_toolchain.sh
#   downloads devkitppc-{gcc,binutils,newlib,crtls,rules}, libogc, libfat-ogc,
#   general-tools, gamecube-tools as .pkg.tar.zst from the live dkp-libs /
#   dkp-linux pacman databases; unpacks a zstd from Debian if the host has none.

# route B - Docker Hub, no docker daemon (used when the CDN answers 403)
bash tools/fetch_dkp_docker.sh "" "" /home/user/dkpopt
#   talks to auth.docker.io + registry-1.docker.io with python/urllib,
#   caches the image layers in /tmp/dkp-layers (~650 MB, re-downloadable),
#   untars them into /home/user/dkpopt/opt/devkitpro and prints that path.
```

`tools/agent_env.sh dkp` tries A, then falls back to B automatically.  Both print
the path to use:

```bash
export DEVKITPRO=/home/user/dkpopt/opt/devkitpro     # docker route (this workspace)
```

### 3.4 Headless Dolphin

Dolphin is installed **without root** by extracting its Debian package and its full
dependency closure; there is no `dpkg -i` and no sudo anywhere.

```bash
bash tools/agent_env.sh dolphin
#   1. apt-get install --print-uris -y --no-install-recommends dolphin-emu \
#        dolphin-emu-data      -> resolves the closure as URLs into dolphin/urls.txt
#   2. curl each .deb into /home/user/dolphin/debs (180 packages, cached)
#   3. bash /home/user/dolphin/setup.sh
#        dpkg-deb -x every .deb into /home/user/dolphin/root, fixes Debian's
#        SDL2 shim symlink, then runs ldd and prints any missing library
```

Run it through the wrapper, which needs a virtual X server and the extracted
libraries:

```bash
/home/user/dolphin/run_x.sh -p headless -v Null -u /home/user/dolphin/userdir -e /abs/path/game.dol
```

`run_x.sh` starts `Xvfb :99` if needed, then sets `LD_LIBRARY_PATH`,
`LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`, `DOLPHIN_SYS` (the extracted
`sys` directory, required — the emulator refuses to start without it) and execs
`root/usr/games/dolphin-emu-nogui`.  `-v Null` disables video output; audio is off
in the config, and ALSA warnings on stderr are normal.

The emulated **SD card** is a FAT image inside `dolphin/userdir/Load/WiiSD.raw`,
mirrored from the folder `dolphin/userdir/Load/WiiSDSync/` at every start
(Dolphin's folder sync is on).  It is reached with `mtools` out of the extracted
tree: `dolphin/root/usr/bin/mcopy` and `mformat`.

### 3.5 Host test toolchain (differential suite)

```bash
bash tools/agent_env.sh host       # ~10 s
#   powerpc-linux-musl-cross (GCC 11.2.1) -> /home/user/.local/tc/powerpc-linux-musl-cross
#   qemu-ppc 10.x (qemu-user + qemu-user-static debs) -> /home/user/.local/tc/quser
```

Both are static binaries run directly; the suite compiles `armjit.cpp` for PowerPC
with musl-g++ and executes the emitted code under `qemu-ppc`, so real big-endian
XER/CR/memory behaviour is exercised.  Override with `PPC_TC=` / `PPC_QEMU=`.
Note `~/.local` is a snapshot-excluded path, so this always needs rebuilding — it
is 10 seconds, not a problem.

### 3.6 The ROM

`/home/user/roms/rockwrestler.nds` (39,433 B) is a small homebrew ROM used for every
measurement:

```
https://raw.githubusercontent.com/RockPolish/rockwrestler/master/rockwrestler.nds
```

It boots via `directBoot` + the HLE BIOS (no real BIOS/firmware needed): ARM9 entry
`0x02000100` size `0x8EE0`, ARM7 entry `0x03800100` size `0x660`.  Its workload is a
VBlank spin plus drawing — memory-heavy, which is precisely why the JIT needed
compiled transfers before it could win.

The harness puts it on the SD card as `rom.nds`; the emulator autoloads it (see the
`directBoot`/`autoBootRom` keys `agent_env.sh sdcard` writes in `noods.ini` for the
manual route, and `sd:/rom.nds` for `run_ab.sh`).

### 3.7 Environment variables and build switching

```bash
export DEVKITPRO=/home/user/dkpopt/opt/devkitpro      # or .../toolchain/opt/devkitpro
export TMPDIR=/home/user/.cache/tmp                   # /tmp is a small tmpfs
mkdir -p "$TMPDIR"
rm -f .jit-flag                                       # force a clean rebuild
```

`Makefile`: `JIT ?= 1` adds `jit3/armjit.cpp` + `jit3/jit3.cpp` and `-DJIT3_ENABLED=1`;
`JIT=0` builds the plain interpreter (the reference for every speed claim).
`EXTRA_CXXFLAGS=-DJIT3_EMBED_ROM=1` bakes a ROM into the `.dol` for autoboot.
`.jit-flag` stamps the effective flags so a flag change drops the objects (make
cannot see flags by itself) — delete it when in doubt.

### 3.8 Recovery order

When something is missing or behaving oddly, do this in order:

```bash
# 1. sources: restore from the newest snapshot and check the DOL reproduces
cd /home/user/repo/v3-NooDS-Wii
bash tools/restore_v3.sh --verify       # restores tarball, fetches toolchains, rebuilds, checks md5

# 2. toolchains by hand, if the above could not
bash tools/agent_env.sh dkp
bash tools/agent_env.sh dolphin
bash tools/agent_env.sh host

# 3. rebuild and confirm the md5
export DEVKITPRO=/home/user/dkpopt/opt/devkitpro TMPDIR=/home/user/.cache/tmp
rm -f .jit-flag && make -j4 JIT=1 && md5sum NooDS-Wii.dol
#   expect ba410c11644ba39502ee0374e65a3ee0 for the frozen build

# 4. cheap sanity: host suite (~13 s)
sh tools/host/run_tests_ppc.sh -q
# 5. end-to-end sanity: 25 s in Dolphin with the verifier
pkill -9 -f 'dolphin[-]emu'; sleep 1
VERIFY=1 bash tools/run_ab.sh /abs/path/file.dol 25 /home/user/runs/check
grep -c 'bad=0' /home/user/runs/check-verify.txt      # thousands of lines, all bad=0
```

If a rebuild produces a *different* md5 with no source change, suspect a
half-reverted tree: `diff -r` the tree against a fresh extraction of the newest
tarball before debugging the code.

---

## Part 4 — Building and testing ROMs

### 4.1 Build

```bash
cd /home/user/repo/v3-NooDS-Wii
export DEVKITPRO=/home/user/dkpopt/opt/devkitpro TMPDIR=/home/user/.cache/tmp
rm -f .jit-flag
make -j4 JIT=1            # -> NooDS-Wii.dol (+ .elf for Dolphin/gdb)
make -j4 JIT=0            # -> interpreter-only reference build
```

### 4.2 Host differential suite

```bash
sh tools/host/run_tests_ppc.sh            # full sweep: ~247k checks, 13 s
sh tools/host/run_tests_ppc.sh -q         # quick (skips random sequences)
sh tools/host/run_tests_ppc.sh -q -one E1A00060    # trace a single encoding
JIT3_LIST_FAILS=1 sh tools/host/run_tests_ppc.sh   # list failures by class
```

Current verdict: `OK: 247412 checked, 0 failed, 1658 not modelled`.  The "not
modelled" set is deliberate (reserved encodings, SPSR forms, MSR mode byte, ARM7
multiply timings, RRX and the register-shift S-forms).  Single-instruction memory
work has its own tool: `tools/host/xfer_probe.cpp` compiles one load/store, prints
the emitted PowerPC, and runs it against stub memory (`fast` vs `nofast` diffing is
how the reverted fast path was checked).

### 4.3 Running a ROM headless

```bash
pkill -9 -f 'dolphin[-]emu'; sleep 1
bash tools/run_ab.sh /abs/path/build.dol 60 /home/user/runs/my-run
#   60 s of wall clock; writes my-run-report.txt, my-run-stage.txt,
#   my-run-verify.txt (with VERIFY=1), and my-run-run.log
```

`run_ab.sh` **must get an absolute DOL path** (it `cd`s into `/home/user/dolphin`;
a relative path fails with "The specified file does not exist" and the guest never
boots).  It recreates the SD image, drops `rom.nds` (and any marker files) into the
sync folder, runs Dolphin for N seconds, SIGKILLs it, then reads the outputs back
out of `WiiSD.raw` with `mcopy`, stripping NULs (a SIGKILLed run always leaves the
last cluster half-written — including, sometimes, a mangled final report line that
looks like a row with a nonsense frame number; walk the rows monotonically and drop
the tail).  `pkill -9` before every run: a leftover `dolphin-emu` holds the SD image.

Useful knobs: `ROM=/path/other.nds`, `TRACE=1`, `VERIFY=1`, `HITS=1`.

### 4.4 Verifying correctness in-emulator (the gate)

```bash
VERIFY=1 bash tools/run_ab.sh /abs/path/build.dol 25 /home/user/runs/verify
tail -1 /home/user/runs/verify-verify.txt     # "# verify: blocks=NNNNNNN bad=0"
grep -c 'bad=[1-9]' /home/user/runs/verify-verify.txt    # must be 0
```

The verifier runs every compiled block twice — once as emitted, once through the
interpreter — and keeps the interpreter's answer, so the emulation stays on the
reference track while it reports.  It forces one instruction per dispatch (blocks
cannot be compared instruction-by-instruction otherwise) and disables chaining.
A mismatch logs the PC, the source words, both cycle counts and every register that
differs, with the pre-state of all 16 registers for the first few.

**Any change to the emitter — and any change to `Cfg`, since `Cfg()`'s zero-init
size shifts the binary — invalidates prior evidence.  Re-run `VERIFY=1` and re-time;
never let a new binary inherit an old run's numbers.**

### 4.5 Tracing and chain counting

```bash
TRACE=1 bash tools/run_ab.sh … # stage file gets "# block pc=… words=… cyc=…" + disassembly
HITS=1 bash tools/run_ab.sh …  # report's chainhits column becomes meaningful
```

### 4.6 The report format

`sd:/jit3-report.txt`, one row per 10 rendered frames:

```
# frame pc9 pc7 cycles ram fb ms | jit blocks compiles refusals flushes dispatches instrs chainhits deleginstrs runs nowork
10 020001B8 038002AC 15534090 E3B2A964 5BBF5D8D 114338 | 30 35 55 0 425452 862485 0 116 425507 55
```

| field | meaning |
|---|---|
| `frame` | rendered guest frames |
| `pc9`/`pc7` | sampled ARM9 / ARM7 guest PC |
| `cycles` | `core->globalCycles` — the accuracy column |
| `ram`/`fb` | FNV hashes of 256 KB of main RAM / of the rendered frame — the correctness columns |
| `ms` | wall-clock ms since the ROM came up — the speed column |
| `blocks` / `compiles` | resident blocks / blocks ever compiled |
| `refusals` | dispatches the driver answered with "-1" |
| `flushes` | arena exhaustions (should stay 0; each one wipes the cache) |
| `dispatches` | compiled-block **entries via the driver** (chained hops are not counted) |
| `instrs` | guest instructions retired in those entries |
| `chainhits` | chain hops (only with `HITS=1`) |
| `deleginstrs` | instructions inside entered blocks handed to the interpreter inline |
| `runs` | `jit3Run()` calls ≈ one per guest instruction executed |
| `nowork` | of `runs`, those the interpreter executed itself (Thumb/halted/unmapped/refused) |

Parsing traps: rows have 18 whitespace fields; the header line starts with `#`;
`nowork` is a suffix of nothing but `runs` grows monotonically and `chainhits` is 0
unless `HITS=1`; the final line of a killed run can be garbage.  A report with only
its header means the guest hung — that was the signature of the `rlwinm` mask bug.

### 4.7 Traps that cost real time (do not rediscover these)

1. **`run_ab.sh` needs an absolute DOL path.** Relative → "Could not boot".
2. **`pkill -9 -f 'dolphin[-]emu'` before every run** (the pattern must not match
   the pkill's own command line).
3. **The SD image is created fresh each run**, and the guest's writes land in the
   raw image (`WiiSD.raw`) — read *those* back with `mcopy`.  The folder sync that
   would copy the image back into `WiiSDSync/` only runs at startup, so a killed
   run never updates the folder; do not go looking for outputs there.
4. **libfat commits on `fclose`, not `fflush`** — anything that must survive the
   kill has to be opened, written and closed (this is why the report path does exactly
   that every sample).
5. **A report with only its header = the guest hung**, not "no data".
6. **Any hand-revert must be diffed *and* re-verified.** A revert that left the
   transfer emitters reading the loaded value from a scratch register instead of
   `r3` produced a heap-corruption crash in newlib `_free_r` (`PC = 0x800f73a8`) —
   a crash inside malloc/free after an emitter change means the emitted code is
   corrupting guest state, not that the allocator broke.
7. **`rlwinm` for a 12-bit page fold: MB stays 20 for every size** (only ME moves,
   31/30/29).  Per-size MB keeps bit 12 → the access reaches one page past the one
   just looked up → silent hang.
8. **Guest memory in host RAM is little-endian**: any inlined fast path must use
   `lwbrx/lhbrx/stwbrx/sthbrx`; plain `lwz/stw` swap bytes silently.
9. **`lbz/stb` have no indexed form** — fold the page offset into the base with
   `add` first.
10. **The host probe's stubs must reproduce NooDS's masking and rotation**, and
    every fake page-map slot must alias the same `ram[]` they mask into, or the
    fast-vs-helper diff shows phantom mismatches.
11. **When in doubt about the harness, read `tools/run_ab.sh`** — it is 37 lines
    (paths, marker files, SD image, read-back) and is the authority over any prose
    description of it, including this one.

---

## Part 4b — Thumb, and the external oracle (added after Part 5 below)

Thumb is compiled for both CPUs now (see NOTES.md for the full changelog).  Two things
in this document are stale because of it: the "expect ba410c11..." md5 in Part 3 (the
current artifact is `34bccfac282f5072caedfcf2782cd405`, pinned in tools/restore_v3.sh)
and the coverage statement - the emitter handles the ALU row including the
register-shift forms, format 1/2/3, loads/stores (all nine forms), PUSH/POP, LDM/STM,
B, Bcc (incl. as a block continuation), fused BL, and BX with a mode switch.

The accuracy oracle is worth knowing about in its own right:

    # one-time: the suite's CPU-core tables (the ROM itself cannot run here: NooDS-Wii
    # has no GBA-mode CPU, so only the tables are used)
    curl -sL -o /home/user/gba-suite/shifter.c https://raw.githubusercontent.com/mgba-emu/suite/master/src/shifter.c
    curl -sL -o /home/user/gba-suite/carry.c   https://raw.githubusercontent.com/mgba-emu/suite/master/src/carry.c

    python3 tools/host/gen_suite_vectors.py /home/user/gba-suite   # -> suite_vectors.txt
    tools/host/run_thumb_probe.sh -suite tools/host/suite_vectors.txt

`gen_suite_vectors.py` maps each suite case onto the Thumb encoding that performs the
same operation and carries the suite's expected result *and CPSR* into the vector file.
The probe reports three verdicts per vector (jit vs suite, NooDS model vs suite,
jit vs model) - the middle one is how a NooDS-quirk-vs-hardware difference is told
apart from a jit3 bug.  Cases with no Thumb form are listed with a reason, never
dropped silently.

Other instruments added with Thumb (all in tools/host, all usable for ARM work too):
`-seq <halfwords>[-csr]` (compile and run one literal block, print the emitted words),
`-max <n>` (block length), `-v` (name the opcode before a crash), and the SD-card
marker `BLOCKS=<n>` (how many instructions per dispatch the in-emulator self-test uses;
`tools/run_ab.sh` has `BLOCKS=` for it).  Self-test programs end `...; b +4; b .` so the
terminating sentinel is reachable only as a block *start* - a multi-instruction block
that swallows it otherwise looks like a mispriced branch.

## Part 4c — The ARM oracle, and two NooDS bugs it found

Part 4b built the Thumb half of the external oracle.  This is the ARM half, plus
what it changed.  The rule for the whole board: the mGBA suite is the authority,
"matches the interpreter" is not evidence, and where the interpreter is wrong the
fix goes into the interpreter too.

* `tools/host/suite_vectors_arm.txt` - 207 vectors, `core|name|op|r0..r3|r8|r9|
  cpsr_in|checks|note`.  Run it with `sh tools/host/run_tests_ppc.sh -suite
  tools/host/suite_vectors_arm.txt` (add `-v` for every row).  Regenerate with
  `python3 tools/host/gen_suite_vectors.py`, which needs the suite sources in
  `/home/user/gba-suite/`.
* `any` rows are architectural: checked on the ARM9 path in full, because that is
  the path jit3 compiles.  `arm7` rows are ARM7TDMI-specific and are checked on
  the ARM7 path in full - where jit3 refuses long multiplies, so the interpreter
  runs them - plus an ARM9 pass with C masked, which still covers the result
  registers and N/Z of the emitted code.
* Three verdicts, and only two of them fail the run: `jit vs suite` and
  `shipped-on-own-core` (the interpreter where jit3 refuses).  `interpreter vs
  suite` is reported so a NooDS finding is named rather than hidden.

What it found, in order:

1. **The oracle itself.** `arm_imm()` never set the data-processing opcode, so
   "movs r0, #imm" rows ran as "and r0, r0, #imm", and those four rows had a
   9-field line shape that the old runner skipped on `nf < 10`.  Two bugs holding
   each other up; both fixed.
2. **The ARM7TDMI multiply C flag** - the multiplier's final-adder carry, recorded
   per operand pair in the suite.  26/72 values were wrong because NooDS wrote no
   C there.  `NooDS-Wii/arm7tdmi_mul.hpp` is a port of the published Booth/CSA
   simulation (zaydlang, zlib) that reproduces all 72; `interpreter_alu.cpp` uses
   it for the four S forms on ARM7 only.  ARM9 keeps C unchanged on purpose: it
   is an ARM946E-S, the suite cannot speak for it, and melonDS's ARM9 does
   something else again (C := 0).
3. **RSCS could not clear C or V.** Its handler masked with `~0xC0000000` and
   OR-ed the computed bits in, so a computed 0 left the incoming flag set - six
   carry-table rows.  jit3's emitter had copied that (`orCFromXer`/`orVSub`).
   Both sides now replace all four flags; the OR helpers are deleted.

Lessons worth keeping:

* A faithful emitter that mirrors a wrong interpreter is two bugs, not one -
  and it looks *clean* by every differential test you have.  Only an external
  oracle sees it.
* When a suite expectation looks like an artefact (a "meaningless" flag), check
  whether it is a known silicon quirk before writing it off: this one had a
  published algorithm, and matching it exactly was cheaper than arguing about it.
* Scope quirks by core.  The suite is a GBA suite; applying its ARM7TDMI
  artefacts to the DS's ARM946E-S would be inventing behaviour, not matching it.

## Part 5 — Where the next slice goes

In priority order, with the instrument that will judge it:

1. **STM/LDM** (`E92D4010`-shaped pushes, `E8BD8010`-shaped returns) — the refusal
   census says this is the remaining *hot* delegated class; same shape of work as
   the transfer slice that produced the 1.65×.  Judge with `ms` at frames 10/30/60
   and confirm with `VERIFY=1`.
2. **SMLAL/long multiplies** (`E0C320B2`-shaped) — smaller win, but they are
   prologue-adjacent.
3. **Thumb decoding** — the largest remaining structural gap; today every Thumb
   instruction is a dispatch.
4. **The SMC hole (§1.7)** — not a speed item; a correctness item for any title
   that patches its own code.  Cheapest fix is a per-page has-blocks bitmap checked
   from the ARM7/ARM9 store helpers.
5. Only then micro-optimise the block boundary (single-entry table, skip
   `flushPipeline()` when the exit PC is the block's own fall-through) — chaining
   already removed ~90 % of it.
