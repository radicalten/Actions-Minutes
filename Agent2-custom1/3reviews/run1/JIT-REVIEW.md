# NooDS-Wii ARM→Broadway JIT review

Reviewed the repository snapshot at commit `26c56db` (`Add files via upload`). The baseline is `default-NooDS-Wii-v1pt5`; the two JIT variants are `b4-pp2-NooDS-Wii` and `Agents2-5-NooDS-Wii`.

## Summary

- **`default-NooDS-Wii-v1pt5`** is the clean interpreter reference: no dynamic recompiler. Keep it as the correctness oracle.
- **`b4-pp2-NooDS-Wii`** translates much more code, but it adds a custom scheduler and code-cache machinery with concrete timing and invalidation gaps in this snapshot. I would not use it as the correctness base.
- **`Agents2-5-NooDS-Wii`** is the better starting point: fallback semantics stay in the interpreter and it wires memory-map/write invalidation into the JIT. Its one-instruction mode is conservative, but enabling `BLOCKS=1` reopens unresolved event-boundary, aliasing, and self-modifying-code risks.
- I created **`Conservative-JIT-v3-NooDS-Wii/`** as a separate fork of the Agents implementation. It defaults to interpreter-only, can run generated fallback stubs without native translation, and offers an opt-in single-instruction arithmetic/logic subset. Multi-instruction blocks and poll-loop skipping are rejected at compile time.

## Implementation comparison

### `b4-pp2-NooDS-Wii`

**What it does well**

- `NooDS-Wii/jit_ppc.cpp` has a broad ARM/Thumb→PPC emitter, a generated-code arena, interpreter fallbacks, and separate NDS/GBA runners.
- The JIT is selected from `Core::updateRun()` (`NooDS-Wii/core.cpp`), so the JIT path replaces the normal scheduler rather than just the per-opcode dispatch.

**Correctness risks found in the checked-in snapshot**

1. **NDS cycle scaling differs from the interpreter scheduler.** `jit_ppc.cpp:1927–1929` scales each ARM9 instruction by 2 and ARM7 by 4 global cycles. The interpreter reference at `interpreter.cpp:171–177` uses ARM9 unshifted and ARM7 `<< 1`. `runCpu()` returns normal interpreter/JIT guest cycles, so this is a timing divergence, not just an emitter detail.
2. **The SMC invalidation implementation is not wired to guest writes.** `invalidateJitWrite()` and `invalidateJitRange()` exist in `jit_ppc.cpp`, and `Core::invalidateJitPage()` exists in `core.cpp`, but searches of the checked-in `memory.cpp/.h` show no call from a write path. Guest code modified after translation can therefore leave old cached code reachable.
3. **The custom scheduler makes a large correctness surface.** `JitHelp_tick()` advances events separately from `Interpreter::runCoreNds()`, while `g_cpuTime` is not rebased in `Core::resetCycles()` in this snapshot. These paths need timing and long-run wrap tests before use.

### `Agents2-5-NooDS-Wii`

**What it improves**

- ARM/Thumb dispatch is inserted inside `Interpreter::runOpcode()` (`interpreter.cpp:284–310`), preserving the normal core scheduler.
- Unsupported instructions go through interpreter handlers; the native translation whitelist is much smaller than the `b4-pp2` emitter.
- Write and map-change hooks call `invalidateSharedAddr()` / `invalidateCpu()` (`memory.cpp`, `memory.h`, `interpreter.cpp`), which is a stronger SMC foundation than the unhooked `b4-pp2` code.

**Remaining hazards**

1. **The Makefile's default JIT setting is not native translation.** `Agents2-5-NooDS-Wii/Makefile` defaults `BLOCKS=0`, while `compileStub()` only enters its native emitter under `#if NOODS_JIT_BLOCKS` (`arm_jit.cpp:1676`). Thus `JIT=1 BLOCKS=0` exercises generated fallback stubs/dispatch, not native ARM/Thumb translation.
2. **Block timing guard is not a safe upper bound as written.** `tryExecuteBlock()` compares `cpu->cycles + scaledPrefix` to the next event (`arm_jit.cpp:2334`); that can be stale relative to `core->globalCycles` after a scheduled event. The stored no-terminator budget is also `prefixCycles - 1` (`arm_jit.cpp:2268`), while branch/PC-load terminator costs are not included in the boundary check. A block can therefore run across an event boundary.
3. **A store can modify code that the current compiled block continues to execute.** The block emitter can include native STRH. Its write slow path invalidates the cache epoch, but then rejoins the current generated block; invalidating future lookups does not stop that block from executing already-emitted stale instructions.
4. **Virtual-page invalidation does not fully model physical aliases.** `Memory::updateMap9()` maps main-RAM mirrors to the same backing `ram[]` pages, while JIT invalidation is keyed by guest address/page. A write through one alias may fail to invalidate a block compiled through another alias.
5. **Fallback function resolution decodes a compiler-specific member-function-pointer layout.** `resolveArmHandler()` / `resolveThumbHandler()` inspect a presumed `{function pointer, adjustment}` representation. That is an ABI assumption that is unnecessary for correctness.

## What v3 changes

`Conservative-JIT-v3-NooDS-Wii/` is a separate full source snapshot; the original three folders are untouched.

- Adds an explicit `NATIVE` switch separate from block mode. Defaults: `JIT=0`, `NATIVE=0`, `BLOCKS=0`.
- Uses stable `extern "C"` fallback trampolines instead of decoding member-function-pointer internals.
- When explicitly built with `JIT=1 NATIVE=1`, emits only the existing single-instruction arithmetic/logic subset. Guest loads, stores, control-flow changes, and unsupported opcodes remain interpreted.
- Disables native guest stores, which avoids continuing a block after a self-modifying write.
- Routes ARM7 CLZ to the interpreter rather than silently emitting a native no-op on an ARMv4T CPU.
- Rejects `BLOCKS=1` and poll-loop skipping at compile time. That intentionally avoids the unresolved scheduler and alias-invalidation hazards rather than pretending they are fixed.
- Adds 32-bit pointer-width checking for target JIT builds and portable PPC-encoding/configuration tests.

## Validation status and next step

`./Conservative-JIT-v3-NooDS-Wii/tests/run_portable_checks.sh` passes: it checks representative PPC encodings and confirms unsafe modes are rejected. This environment does not have devkitPPC (`powerpc-eabi-g++`) or Wii hardware, so I could not compile the DOL or claim ARM7/ARM9 instruction-level equivalence. Treat `NATIVE=1` as experimental until it passes side-by-side Wii tests against `JIT=0`; use the interpreter default for known-correct behavior.
