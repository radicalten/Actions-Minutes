# Building v3-NooDS-Wii

> **Status note (2026-10-16):** the build plumbing in this file is accurate, but the
> JIT description further down is from slice 1 (ALU only).  Since then the emitter
> compiles loads/stores, chains blocks, delegates unmodelled instructions inline and
> verifies itself in-emulator.  For the current design use **`notes-jit3.md`**;
> `RESULTS.md` has the measured outcome.

## Quick start (no root, self-contained toolchain)

```bash
tools/mk_toolchain.sh            # fetches devkitPPC + libogc into ./toolchain, then builds
```

Result: `NooDS-Wii.dol` (and `NooDS-Wii.elf` for Dolphin/gdb).  The script
resolves the current package filenames from devkitPro's live package databases,
so it keeps working as their versions move.  `PREFIX=... SKIP_BUILD=1` are
honoured if you only want the toolchain.

## With a normal devkitPro installation

```bash
make                             # uses /opt/devkitpro
make DEVKITPRO=/path/to/devkitpro   # or point it somewhere else
make -j$(nproc) DEVKITPRO=...
```

`Makefile` used to hardcode `/opt/devkitpro` in the includes, the `-L` paths and
`elf2dol`; it now takes `DEVKITPRO` from the environment/command line and finds
`elf2dol` under `$(DEVKITPRO)/tools/bin`.  Everything else is unchanged.

Packages the build needs (all installed by `tools/mk_toolchain.sh`):
`devkitppc-gcc`, `devkitppc-binutils`, `devkitppc-newlib`, `devkitppc-crtls`,
`devkitppc-rules` (provides `wii_rules`), `libogc`, `libfat-ogc`,
`general-tools` and `gamecube-tools` (that is where `elf2dol` lives).  The
script resolves the `.pkg.tar.zst` filenames out of devkitPro's live pacman
databases (`dkp-libs` for `any`, `dkp-linux` for `x86_64`) instead of hardcoding
them, and it reuses archives already in `toolchain/pkgs`, so a populated tree
rebuilds with no network.  It also unpacks a `zstd` from Debian if the machine
has none, since `.pkg.tar.zst` needs one.

## What is in the .dol right now

**The JIT is wired in** (`JIT=1` by default; `make JIT=0` builds a pure
interpreter, which is the fastest way to tell a JIT bug from an emulation bug).

The pieces:

| file | what it does |
|---|---|
| `NooDS-Wii/jit3/armjit.cpp` | the emitter: guest ARM -> PowerPC words. No NooDS/GEKKO dependency at all |
| `NooDS-Wii/jit3/jit3.h` | the bridge's public interface (invalidate, stats) |
| `NooDS-Wii/jit3/jit3.cpp` | block cache, real `Interpreter` offsets, cache flushing, `Interpreter::jit3Run()` |
| `NooDS-Wii/interpreter.cpp` | `runOpcode()` asks the JIT first; the fetch helpers |
| `NooDS-Wii/interpreter.h` | `jit3Run()` + `jitFetch16/32()`/`jitReadMap()` declarations |
| `NooDS-Wii/memory.cpp` | `Jit3::invalidateAll()` on `updateMap9/7`, `updateVram`, `loadState` |
| `Makefile` | `JIT ?= 1`, adds the jit3 sources and `-DJIT3_ENABLED` |

How it behaves:

* `runOpcode()` calls `jit3Run()`.  On a hit the whole block runs and its cycle
  total is returned, exactly like a set of single instructions would have been.
  A miss (-1) means the interpreter executes that one instruction as before.
* A block is entered at word offset 0 (prologue: loads the guest registers,
  takes `Interpreter*` in r3 and the deadline in r4); `bodyOffset`/`warmOffset`
  is the same code without the prologue, which is what chaining will use.  The
  block returns cycles in r3 and the exit PC in r4 as one `uint64_t`, or
  0xFFFF0000 in r4 when it handed the instruction to the interpreter and the PC
  must be read back from the CPU.
* Thumb, halted CPUs and anything the emitter refuses stay on the interpreter.
* The generated code leaves `registersUsr[15] == exitPc + step`; the dispatcher
  then calls the interpreter's own `flushPipeline()` so `pipeline[0..1]` and
  `pcData` point at `exitPc` again (this includes a BX that switched T).
* **Self-modifying code is safe without hooking the store path**: every block
  stores a checksum of the exact words it was compiled from plus the read-map
  pointer and a mapping generation, and re-checks all three on every dispatch.
  Remaps/state loads additionally flush the whole cache.
* Blocks are at most 8 instructions (`kMaxBlockInstrs` in `jit3.cpp`), so the
  scheduler is still consulted at least every 8 guest instructions.  A cycle
  deadline check inside the emitted code (r29 is reserved for it) is the next
  refinement - today an event can be serviced up to one short block late.

Telling whether it is running, from Dolphin's log or a USB Gecko (the code uses
the project's `LOG_INFO`, i.e. `printf`):

```
jit3: JIT active (ARM9), first block at 02000A1C (118 words, 3 instrs)
jit3: 46 blocks, 46 compiles, 12 refusals, 1 invalidations, 1048576 instrs
```

## Scope: what is actually accelerated

Slice 1 only - ARM data processing (with all the flag quirks), B/BL/BX/BLX(reg),
MRS/MSR (CPSR), and MUL/MLA on the ARM9.  Loads/stores, LDM/STM, SWP and the
whole Thumb ISA are still interpreted, so speed-ups today are limited to
code-dense guest loops.  Slice 2 builds those on the same machinery.

## Host verification

```bash
tools/host/run_tests_ppc.sh            # full sweep
tools/host/run_tests_ppc.sh -q         # quick sweep (skips the 3k random blocks)
tools/host/run_tests_ppc.sh -q -one E1A00060    # trace one encoding
```

The harness compiles jit3 for PowerPC, drops the emitted words in an RWX page,
runs them under qemu-ppc (real XER/CR/big-endian behaviour) and diffs registers,
CPSR, cycle counts and the next PC against an executable transcription of the
NooDS handlers (`tools/host/arm_ref.hpp`).  It also runs random *sequences* and
a **chained-dispatch test** (`runChainSweep`) that mirrors the Wii loop above:
random 1-20 instruction programs ending in an unconditional `b LR`, walked once
by the reference model and once the way `jit3Run()` does it - compile a block,
run it, take its exit PC as the next block's start, single-step whatever the
emitter refuses - with the same stopping rule for both walks.  It checks the
instruction and cycle totals, every register, the CPSR and the
`registersUsr[15] == nextPc + step` invariant.  `CHAINDBG=1` traces each trial.

Current state: **247513 checked, 0 failed, 1557 not modelled**, and the chain
sweep reports `chains=500 chainFail=0` (the unmodelled set is deliberate:
reserved encodings, S+PC writes, MSR control-byte, SPSR forms, ARM7 multiply
timings, register-shift S-forms that need the shifter carry).

## Next steps

1. Slice 2 in the host harness: loads/stores, LDM/STM, SWP, then the Thumb ISA.
2. A cycle deadline in the emitted code (r29 already holds it) so long blocks
   cannot delay a scheduled event, and longer blocks once that exists.
3. Native long multiplies (UMULL/SMULL/CLZ) - the emitter has no helper for them
   yet; the interpreter handles those instructions today.

## Invariants that were learned the hard way

These are the rules the classifier and the diagnostics now follow; each one was
paid for by a bug that passed both the host suite and the self-test.

**The classifier must refuse by *encoding space*, not by a list of opcodes.**
ARM's top-level classes are not enough: bits 27-25 = `000` covers data
processing *and* the multiply / PSR / halfword-and-doubleword load-store / swap
space, which is told apart only by bits 7 and 4 being set. Anything accepted as
"data processing" that is not provably data processing must be refused - in this
tree, `armCanTranslate()` ends with `cls == 0 && (op & 0x90) == 0x90 -> false`.
The host differential suite cannot catch a mistake here, because it only
generates encodings it has a model for; shadow verification and the A/B run can.

**`registersUsr[15]` always carries "next PC + step" between instructions, and
`flushPipeline()` *adds* step to whatever it finds.** Never call it on a value
that is already converted; hand it a raw target.

**`flushPipeline()` is called by the dispatcher, and the JIT block leaves
`registersUsr[15]` one step short of the convention** (see `jit3Run()`), so the
handoff path and the normal path agree about where execution resumes.

**Diagnostics must not perturb the machine.** The self-test saves and restores
the register file, the CPSR and the words it borrows, and it must leave the PC
exactly where it found it - the A/B run against the interpreter build is what
proves that, because only the JIT build runs it.

## Shadow verification (`-DJIT3_VERIFY=1`)

A slice-1 block performs no memory access (every block containing one is
refused), so a block's effect is confined to the register file, the CPSR and its
cycle count - and is therefore exactly replayable. With `JIT3_VERIFY` defined,
`jit3Run()` snapshots the CPU, runs the block, restores the snapshot, runs the
same instructions through the interpreter and compares the two. The first
divergence is written to `sd:/jit3-verify.txt` with the block PC, the source
words, both cycle counts and every differing register. Bound the work with
`-DJIT3_VERIFY_MAX=<dispatches>` (default 1000000) and cap the log with
`Jit3::kVerifyMaxLog`.

## Known gap: the cycle deadline (r29)

`jit3Run()` passes `0xFFFFFFFF` as the block deadline and no emitted code tests
r29 at all, so a block can run past the deadline the scheduler handed the CPU.
Semantics are unaffected (cycles, RAM and video output match the interpreter
frame for frame), but the *sampled PC* can be one instruction further along.
Giving the block its remaining budget and testing it at instruction boundaries
is the next refinement; the register is already reserved for it.
