# AGENTS.md — NooDS-Wii ARM↔PowerPC JIT improvement

## Mission

Improve the dynamic recompiler in `NooDS-Wii/NooDS-Wii/arm_jit.cpp` + `arm_jit.h` (+ `jit_bridge.S`).
It translates the Nintendo DS guest CPUs — **ARM7 (ARMv4T)** and **ARM9 (ARMv5TE)**, in **both ARM
and Thumb** — into native **PowerPC 750CL / "Broadway"** code at runtime on the Wii.

Work the tasks in §4 in order. Tasks T1–T2 are **correctness fixes**; T3–T7 are additive perf.
Everything else is lower priority / optional.

## 1. Environment constraints — read first

- **Target toolchain:** devkitPPC (`powerpc-eabi-`), `-mcpu=750` family, GEKKO. Code emitted at
  runtime must be valid 750CL. No 64-bit integer ops, no `isel` (that's e500/book-E), no AltiVec.
- **Host ABI:** PPC32 SysV. Volatile `r0-r12`, non-volatile `r13-r31`. Generated code runs under
  `jit_enter()` from `jit_bridge.S`, which owns the frame and calls into C++ helpers.
- **The JIT's own register convention in `arm_jit.cpp` (do not break it):**
  - `R31` = pointer to the guest `Interpreter` object (base for all `lwz`/`stw` of guest state).
  - `R3..R5` = guest operands/result for emitters; `R6` = scratch CPSR; `R7`/`R8`/`R9` = scratch.
  - `R11` = scratch pointer for banked (`registers[i]`) guest reg indirection.
  - `R12` = helper address for `mtctr`/`bctrl` calls. `R28..R30`, `R1` = saved/restore block.
- **Broadway has incoherent split I/D caches.** Any newly emitted code must be flushed with
  `ArmPpcJit::flushCodeRange()` before it can be executed. `compileBlock()`/`compileStub()` already
  do this at the end — if you add a new compile path, you must call it too, or you get
  intermittent garbage execution on real hardware that Dolphin may not reproduce.
- Code pool lives in MEM2, 1 MiB (`DEFAULT_POOL_BYTES`), wraps via `wrapPool()` which bumps
  `currentEpoch` (30-bit). Stale entries are rejected by epoch comparison on lookup.

## 2. Files

| File | Role |
|---|---|
| `NooDS-Wii/NooDS-Wii/arm_jit.h` | `JitPpc::` PPC encoder helpers, `ArmPpcJit` class, tables, `Stats` |
| `NooDS-Wii/NooDS-Wii/arm_jit.cpp` | Classification, emitters, block compiler, dispatch, SMC invalidation |
| `NooDS-Wii/NooDS-Wii/jit_bridge.S` | `jit_enter()` asm trampoline: saves PPC regs, loads `R31`, calls code |
| `NooDS-Wii/NooDS-Wii/interpreter*.cpp` | The fallback interpreter — authoritative reference for semantics |
| `NooDS-Wii/NooDS-Wii/defines.h` | `U8TO32` / `U8TO16` little-endian guest memory reads |

Build flags: `NOODS_JIT` (enable), `NOODS_JIT_BLOCKS` (Phase-2 block compiler), `NOODS_JIT_POLL_SKIP`
(fixed-point poll-loop skip), `NOODS_JIT_PROFILE` (timebase profiling buckets).

## 3. Invariants — do NOT change these

The design **fails toward correctness**: anything unclassifiable falls through to the interpreter.
Do not weaken a classifier to widen coverage — only widen it by adding a correct emitter.

1. `tryExecuteBlock()` must keep refusing to enter a cached block whose `prefixCycles` would cross
   `core->events[0].cycles`. Breaking this lets a block skip a timer/DMA/IRQ boundary.
2. Keep `bit 27 == 1` (LDM/STM, SWI, B/BL, coprocessor/CP15) and `Rd == 15` instructions out of
   block compilation, as today, unless you implement them fully and correctly.
3. Keep ARM9 CP15 ops gated out of blocks (existing `Gate G6`).
4. Page-epoch SMC invalidation (`invalidateAddr` / `invalidateRange` / `invalidateSharedAddr`)
   must still fire on every guest write path the JIT doesn't own.
5. Memory accesses must go through the same guarded bus helpers the interpreter uses. **Never
   inline raw pointer reads/writes** — that breaks MMIO and VRAM mirroring.
6. In ARM mode, non-initial instructions in a block must be unconditional (`cond == 0xE`) — unless
   you also implement full condition evaluation for that instruction.

## 4. Tasks

### T1 — BUG: `BLX Rm` (register form) never writes LR  ·  ARM9 only  ·  medium impact, low risk

**Where:** `ArmPpcJit::execBranchArm()`, final "BX Rn" tail case at the end of the function.
**Problem:** that tail is reached by both `BX` (`0x012FFF1x`) and `BLX` (`0x012FFF3x`, bit 5 set) but
only ever performs BX semantics — the link register is silently dropped.
**Fix:** detect bit 5 and write LR first, gated on `!cpu->arm7`. Mirror the Thumb `BLX Rs` handling
a few lines above it in `execBranchThumb()` — that one is already correct.

```cpp
// replacement for the existing "BX Rn" tail in execBranchArm()
const bool isBlxReg = !cpu->arm7 && (opcode & (1u << 5)) != 0;
cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
uint32_t target = *cpu->registers[opcode & 0xFu];
if (isBlxReg) *cpu->registers[14] = cpu->registersUsr[15] - 4u;
cpu->cpsr = (cpu->cpsr & ~(1u << 5)) | ((target & 1u) << 5);
cpu->registersUsr[15] = target;
cpu->flushPipeline();
return prefixCycles + 3;
```

Also confirm `isTerminatorBranchArm()` / `isUncondBranchArm()` treat `BLX Rm` as a terminator so a
block can't be built *through* it.

### T2 — BUG: `BLX <label>` (immediate, `cond == 0xF`) is condition-tested  ·  ARM9 only  ·  medium impact, medium risk

**Where:** top of `execBranchArm()` and the block-termination classifiers.
**Problem:** `cond == 0b1111` is not a condition on ARMv5 — it's the unconditional-extension encoding
space. The current `if (cond != 0xEu)` guard routes it through the 16-entry condition LUT, where its
behaviour is accidental, plus the existing `0x0A000000` mask test won't even match the BLX encoding.
**Fix:** early-out on `cond == 0xF` before any condition lookup.

```cpp
if (cond == 0xFu) {                       // BLX <label> — ARMv5 unconditional extension space
    if (cpu->arm7) {                      // ARMv4T: UNDEFINED encoding, defer to interpreter
        cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
        cpu->flushPipeline();
        return prefixCycles + cpu->handleReserved(opcode);
    }
    uint32_t r15 = oldR15 + (uint32_t)remDelta;
    int32_t  off  = ((int32_t)(opcode << 8) >> 6) | (int32_t)((opcode >> 23) & 2u); // H -> +2
    *cpu->registers[14] = r15 - 4u;
    cpu->cpsr |= (1u << 5);               // force Thumb
    cpu->registersUsr[15] = r15 + (uint32_t)off;
    cpu->flushPipeline();
    return prefixCycles + 3;
}
```

Verify the offset decode above against `interpreter_branch.cpp`'s real BLX handler — the `H` bit
encoding is easy to get wrong. If it differs, copy the interpreter's version verbatim.

### T3 — Native `STR` / `STRB` fast path  ·  both CPUs  ·  **HIGH impact**, low risk  ← biggest win

**Problem:** only `STRH` (immediate, pre-index, no writeback) is fast-pathed today
(`isNativeStoreArm`). Ordinary word/byte stores — the most common store in compiler-generated ARM9
code — always fall back to the interpreter.
**Where:** add classification next to `isNativeStoreArm()`, an emitter next to `emitNativeStoreArm()`,
and register it in `canTranslateNativeArm()` + `compileBlock()`'s per-instruction classification loop.
**Pattern:** mirror `isNativeLoadArm()` exactly — pre-indexed, `W=0`, `Rn != PC`, `Rd != PC`,
register offsets restricted to unshifted/`LSL #imm`, no `Rm == PC`, no bit 4 (reg-shift-by-reg).

```cpp
static inline bool isNativeStoreWordByteArm(uint32_t op) {
    if ((op >> 28) != 0xEu) return false;                // AL only (see invariant 6)
    if ((op & 0x0C000000u) != 0x04000000u) return false; // single data transfer
    if (op & (1u << 20)) return false;                   // L = 0
    if (!(op & (1u << 24))) return false;                // P = 1
    if (op & (1u << 21)) return false;                   // W = 0
    if (((op >> 16) & 0xFu) == 15u) return false;        // Rn != PC
    if (((op >> 12) & 0xFu) == 15u) return false;        // Rd != PC
    if (op & (1u << 25)) {                               // register offset
        if (op & (1u << 4)) return false;
        if ((op & 0xFu) == 15u) return false;
        if (((op >> 5) & 3u) != 0u) return false;        // LSL only
    }
    return true;
}
```

The emitter must reuse the **same base+offset computation and the same guarded bus-write helper**
`emitNativeLoadArm()` already calls for its slow path. Do not hand-roll the store.

### T4 — Native `CLZ`  ·  ARM9 only  ·  low impact, lowest risk  ← do this first as a smoke test

`armWrittenFlagsMask()` already special-cases `0x016F0F10` as "no flags written", so the
classification side is already anticipated; there's just no emitter. PPC `cntlzw` is a 1:1 match.

```cpp
static inline bool isClzArm(uint32_t op) { return (op & 0x0FFF0FF0u) == 0x016F0F10u; }

// in emitNativeArm(), gated on !cpu->arm7
emitLoadGuestReg(p, JitPpc::R4, opcode & 0xFu);
*p++ = JitPpc::cntlzw(JitPpc::R3, JitPpc::R4);
emitStoreGuestReg(p, JitPpc::R3, (uint8_t)((opcode >> 12) & 0xFu));
outCycles = 1;   // CLZ never updates NZCV — nothing further to emit
```

### T5 — Native `MUL` / `MLA`  ·  both CPUs  ·  medium impact, medium risk

`armPurity()` already recognises the multiply encodings but nothing emits them. `mullw` is 1:1.
MLA adds one accumulator `add`. When `S=1` only **N and Z** are set (C is UNPREDICTABLE on real
hardware; the interpreter leaves it) — reuse `emitUpdateNZ()`.

```cpp
static inline bool isMulArm(uint32_t op) {
    if ((op & 0x0FC000F0u) != 0x00000090u) return false;  // bits 27:22 = 0, 7:4 = 1001
    uint8_t rd = (uint8_t)((op >> 16) & 0xFu), rm = (uint8_t)(op & 0xFu);
    return rd != 15u && rm != 15u && rd != rm;            // rd == rm is UNPREDICTABLE
}
// mullw R3, R4(Rm), R5(Rs)  ->  optional add R3, R3, R6(Rn) for MLA
// -> emitStoreGuestReg -> if (emitFlags) emitUpdateNZ(p, R3)
```

Note: multiply **aliases the data-processing encoding space**, so `isMulArm()` must be checked
*before* the generic ALU switch in `emitNativeArm()`.

### T6 — Native register-specified shift amounts (`LSL Rd, Rm, Rs`)  ·  both  ·  medium impact, medium risk

Currently rejected because `canTranslateNativeArm()` bails on bit 4 = 1. PPC `slw`/`srw`/`sraw`
already produce the correct all-zeros / sign-fill result for shift counts ≥ 32, but ARM reads the
count as **bits 7:0 of Rs**, so mask to a byte then clamp to 32 with `cmplwi` + conditional select.
Carry-out needs one extra shift by `(amount - 1)` guarded for `amount == 0` — that guard is the
trickiest part of the patch; get it wrong and every `ADC`/`SBC`/`ROR`-through-shift is wrong.

### T7 — Re-measure native halfword loads now that blocks exist  ·  both  ·  low impact, low risk

`kNativeHalfLoads` is `false` with a comment saying it was measured and gave no speedup — but that
measurement predates the Phase-2 block compiler. Flip it on behind a build flag and re-run
`NOODS_JIT_PROFILE`. Chained in a block, the per-dispatch overhead it used to compete against is
gone, so the old conclusion may no longer hold.

### Deferred (don't do unless asked)

`UMULL/UMLAL/SMULL/SMLAL` (needs `mulhw[u]` + `mullw` pairs), `QADD/QSUB/QDADD/QDSUB` (branchless
clamp + sticky Q bit, CPSR bit 27), `SMLAxy/SMULxy/SMLAWy` DSP family, native `LDM`/`STM`.

## 5. Validation — required after every task

`ArmPpcJit::dumpStats()` and the `NOODS_JIT_PROFILE` buckets already exist; use them.

1. **Compile:** `powerpc-eabi-gcc` via the repo's normal Wii build. Zero warnings — the JIT uses
   `#pragma GCC diagnostic ignored "-Winvalid-offsetof"`, so a warning there is real signal.
2. **Stats must move in the right direction:**
   - `fbInsn9` / `fbInsn7` (fallback instructions) should **drop** for the classes you just made native.
   - `natInsn9` / `natInsn7` should rise correspondingly.
   - `blkComp*` / `blkHit*` up, `blkFail*` roughly flat.
   - For T3 specifically: `fbClassArm[cpu][2]` (the LDR/STR/halfword bucket) and the `fbOpc7`
     histogram entries for store opcodes should fall sharply.
3. **Correctness:** several titles per CPU, including at least one ARM9 game that uses `BLX`
   trampolines (mixed ARM/Thumb libraries), one GBA-mode title (different code paths in
   `tryExecuteBlock` — note the `gbaMode`/`dsiMode` guards in `pollExecArm`), and one with known
   self-modifying code or DMA-timed code.
4. **Hardware:** final pass on a real Wii or cycle-accurate Dolphin. Broadway `dcbst`/`icbi`
   ordering and alignment issues routinely hide on non-cycle-accurate hosts.
5. **SMC regression:** after T3 especially, verify DMA-driven code upload (e.g. ARM7 loading ARM9
   code, or decompression stubs) still invalidates correctly — stores are the write path most
   likely to interact with `pageEpoch` invalidation.

## 6. Guardrails recap

- One task per commit. T1 and T2 change behaviour and need their own regression pass — don't
  bundle them with perf work.
- Never widen a classifier without adding a matching emitter.
- Never inline raw memory accesses.
- If a guest instruction's semantics are ambiguous, read the actual handler in
  `interpreter_alu.cpp` / `interpreter_transfer.cpp` / `interpreter_branch.cpp` and copy it
  verbatim rather than reasoning from the ARM ARM from memory.
- My analysis was made from reading the repo at one point in time — **function names, line
  positions and the exact table/bucket indices in §5 must be re-verified against the current
  source before you rely on them.** The task *descriptions* and the encoder/convention facts in §1
  are the durable part.
