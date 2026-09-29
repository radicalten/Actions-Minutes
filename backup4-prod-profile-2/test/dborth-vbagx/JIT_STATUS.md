# ARM→PowerPC recompiler: status and continuation notes

This file tracks the state of the JIT work in this directory. It is meant to be
deleted once the recompiler is complete and merged.

## What is in the tree now

| File | State |
| --- | --- |
| `jit_emitter.h` | **done, verified.** Complete PowerPC (Gekko/Broadway, 32-bit big-endian) instruction encoder as macros. 109 encodings checked against GNU as output. |
| `test/jit_emitter_test.cpp`, `test/run_emitter.sh` | **done.** Self-test for the encoder: `cd test && ./run_emitter.sh` prints `jit_emitter: all 109 encodings match GNU as`. |
| `jit.h` | **done.** Native-state layout, block/link descriptors, `Jit` interface, the register contract for generated code. |
| `jit_cache.cpp` | **done, compiles.** Arena, block hash table, branch-link records, invalidation, state transfer (`syncIn`/`syncOut`), the batch dispatcher (`Jit::run`), statistics. `Jit::compile()` is currently a stub that returns `nullptr`. |
| `jit_smc.h` / `jit_smc.cpp` | **done, compiles.** Self-modifying-code write barrier: `jitSmcFlags` page table plus the `jitCodeWrite` hook. |
| `jit_abi.S` | **done, assembles.** Trampoline that loads/stores the native register mapping and returns to C++. |
| `jit_compiler.cpp` | **not in the tree yet.** The ARM/THUMB instruction selector is the remaining piece; the frames that are supposed to be missing are listed below. |
| `interpreter.h` / `interpreter.cpp` | **patched.** `Jit *jit` member, `friend class Jit`, `Interpreter::step()`, and the native fast path at the top of `runOpcode()`. |
| `memory.h` | **patched.** One load from `jitSmcFlags` before every guest store. |
| `core.h` / `core.cpp` | **patched.** Two `Jit` members (`jit9`, `jit7`) constructed after the interpreters. |

With `Jit::compile()` stubbed out, the emulator runs exactly as before on the
interpreter; nothing in the JIT is reachable until a selector exists, and the
write barrier stays cold because no page is ever flagged.

## Native code contract

Generated code and the trampoline share this mapping (`jit.h`, `jit_abi.S`):

```
r3        guest cycle accumulator          r13  JitContext*
r4-r11    scratch                          r14-r26  guest R0-R12
r12       jitSmcFlags base                 r27  guest R13 (SP)
r30       read page table base             r28  guest R14 (LR)
r31       write page table base            r29  guest CPSR
```

* Every guest register lives in the same host register at all times, so a
  branch from one compiled block to another needs no state movement.
* Exits are `blr` back to `jitBlockReturn` (through the trampoline), a patched
  `b` straight into another block's entry, or an inline block-table hash
  lookup for indirect targets.
* A block's shared epilogue stores only the registers it actually wrote,
  plus the cycle accumulator and the flags, then returns.
* `Jit::syncOut()` rebuilds the interpreter pipeline with
  `Interpreter::flushPipeline()` so the next `runOpcode()` executes exactly the
  instruction the exit reported.

## Memory access pattern

The interpreter's fast path is reproduced exactly, so a guard failure is
always equivalent to its slow path:

```
page  = readMap[addr >> 12]        (or writeMap for stores)
if (!page) -> guard exit: the interpreter re-runs this instruction
value = lwbrx/lhbrx/lbzx page + (addr & 0xFFC|0xFFE|0xFFF)
```

Misalignment rotation, the ARM7 halfword swap, the ARM7 store pre-fetch and
the PC-relative read offsets (`PC + 8`, `PC + 12` when the PC is a stored
register) are all emitted inline as constants.

Stores additionally test `jitSmcFlags[(addr >> 10) & 0xFFFF]`; if the page
holds compiled code the store bails to the interpreter, which invalidates the
blocks through `jitCodeWrite()` and then performs the write.

## What `jit_compiler.cpp` still has to contain

1. `JitBlock *Jit::compile(uint32_t pc, bool thumb)` — replace the stub in
   `jit_cache.cpp`, or delete it there and define it here.
2. A staging buffer (`uint32_t code[JIT_CODE_BUFFER_WORDS]`), labels/fixups,
   and a block driver that appends instructions until a terminator, then emits
   the epilogue and copies the block into the arena.
3. Instruction emitters, in this order of value:
   * ARM data processing (immediate and register operands, S variants), with
     the interpreter's exact flag formulas — carry from `addc`/`subfc`/`adde`/
     `subfe` via `XER`, overflow from the interpreter's bit formulas, N/Z from
     `rlwimi`/`cntlzw`.
   * ARM single data transfer (LDR/STR/LDRB/STRB, all four addressing modes,
     immediate and shifted-register offsets) and the halfword/signed forms.
   * ARM branches: B, BL, conditional B, BX, BLX(register) — the last two as
     indirect exits.
   * ARM MUL/MLA, LDM/STM (IA/IB/DA/DB, with and without writeback), and PC
     writes out of any of these.
   * THUMB: shifts, ADD/SUB (all three encodings), MOV/CMP/ADD/SUB #imm8, the
     sixteen-op ALU group, hi-register ops, PC/SP-relative loads and stores,
     PUSH/POP/LDMIA/STMIA, conditional branches, B, BL/BLX.
   Anything else must end the block so the interpreter executes that single
   instruction.
4. Cycle costs exactly as the interpreter returns them (they are inputs to the
   scheduler, not approximations).

## Verification harness

`/home/user/noods-jit-test` builds the real emulator for big-endian PowerPC
(`powerpc-linux-gnu-g++`, run under `qemu-ppc-static`) with the tuxedo/libogc
headers replaced by stubs, so generated PowerPC code can be executed and
compared against the interpreter instruction by instruction:

```
cd /home/user/noods-jit-test && make && qemu-ppc-static ./build/jit_test
```

The remaining work there is the boot path: `Core` needs BIOS/firmware files (or
stubs) to get past construction in this host environment; the crash under
investigation is inside the pre-existing `Memory::readFallback` path reached
from `Core`'s constructor, not in the JIT.
