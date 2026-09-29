# AGENTS.md

A Wii port of Hydr8gon's NooDS (Nintendo DS / GBA emulator). Boots from the
Homebrew Channel as `boot.dol` (built as `NooDS-Wii.dol`) and runs NDS and GBA
ROMs from SD card. This AGENTS.md covers the port as it is now, **plus the
work in progress: an ARMv4/v5 → PowerPC (Broadway/Gekko) dynamic recompiler
("JIT")**. The JIT has to match or beat dborth's VBA-GX THUMB trace JIT
(`vbagx/source/vba/gba/JIT*.{h,cpp,S}`).

---

## Setup

- Build from the **devkitPro MSYS2 shell** (Start → devkitPro → MSYS2) on
  Windows, or from a Linux/macOS shell with devkitPro installed at
  `/opt/devkitpro`. Plain Git Bash won't work: it doesn't set
  `DEVKITPRO`/`DEVKITPPC` or put `powerpc-eabi-*` / `elf2dol` on `PATH`.
- The Makefile **hard-codes** `export DEVKITPRO := /opt/devkitpro` (with `:=`,
  so it overrides the environment). The devkitPro MSYS2 install maps
  `/opt/devkitpro` correctly. If your devkitPro lives somewhere else, fix the
  Makefile. Don't work around it with env vars, because they get ignored.
- Required packages: `wii-dev` (devkitPPC, libogc, libfat, wiiuse, bte, asnd,
  wiikeyboard). Nothing else. There's no submodule, no patch step and no
  sibling clone, so a fresh clone builds immediately.
- CI: `.github/workflows/main.yml` builds in the `devkitpro/devkitppc`
  container. **It only triggers on pushes to `master`, but the repo's branch
  is `main`**, so CI does not run right now. Fix the trigger if you depend on it.

---

## Build commands

Run these from the repo root (the directory that holds `Makefile`).

| Command | Output |
|---|---|
| `make` | `NooDS-Wii.dol` (+ `.elf`, `.elf.map`) in repo root |
| `make clean` | removes `.dol/.elf/.map` and every `NooDS-Wii/*.o` |

Notes on the current Makefile (keep these in mind before editing it):
- It compiles **only `NooDS-Wii/*.cpp`** (a wildcard over `SRCDIR`), using
  `powerpc-eabi-g++` at `-O2`. It has **no `.S`/`.s`/`.c` rule**. If the JIT
  adds a hand-written trampoline (`jit_trampoline.S`, like VBA-GX's
  `JITTrampoline.S`), you **must** add an `ASFILES`/`%.o: %.S` rule and add
  it to `OBJECTS`, or the link fails with an undefined `jit_enter` symbol. A
  simpler option: write the trampoline as a naked `asm()` block inside a
  `.cpp` so the Makefile doesn't change.
- Objects are written **next to their sources** (`NooDS-Wii/*.o`). There is
  no `build/` dir.
- There is **no header dependency tracking** (`-MMD` isn't used). After you
  edit any `.h` (especially `interpreter.h`, `core.h`, `memory.h`, `defines.h`
  or any `jit_*.h`), **run `make clean`**, or objects go stale → silent ABI
  mismatch → random crashes on hardware.
- `EXCLUDE :=` exists to filter sources. Use it for a JIT on/off escape hatch
  (see "JIT rules" below) rather than deleting files.
- Endianness: `-DENDIAN_BIG` is required. NooDS was written little-endian
  and the Wii port depends on this define everywhere memory is read as
  multi-byte values.

Deploy:
- `sd:/apps/NooDS-Wii/boot.dol` (rename `NooDS-Wii.dol` → `boot.dol`)

Data required (all on SD, since the port starts in `sd:/noods/`):
- `sd:/noods/bios/bios9.bin`, `bios7.bin`, `firmware.bin` (original DS, not DSi/3DS)
- `sd:/noods/bios/gba_bios.bin`
- ROMs anywhere, conventionally `sd:/noods/roms`, `sd:/noods/nds`, `sd:/noods/gba`

---

## Code layout

Everything is flat in `NooDS-Wii/`. Upstream NooDS files were vendored and
patched in place, so edit them directly.

- `wii_video.*`, `wii_audio.h`, `console_ui.*`, `main.cpp`, `input.*`, `settings.*`:
  Wii port layer (GX/VI, ASND audio, file browser UI, wiimote/GC/classic input).
- `core.*`: owns everything. `Core::runFunc` is a function pointer
  (`Interpreter::runCoreNds`, `runCoreSingle<…>`, `runCoreNone`, `runCoreDsi`)
  selected by mode. `core.gbaMode` means only `interpreter[1]` (ARM7) runs.
  **This pointer is the JIT's hook point.**
- `interpreter.*`, `interpreter_alu.cpp`, `interpreter_branch.cpp`,
  `interpreter_transfer.cpp`, `interpreter_lookup.cpp`: the ARM9 (ARMv5TE) /
  ARM7 (ARMv4T) interpreter. Lookup tables: `armInstrs[0x1000]` indexed by
  `((op>>16)&0xFF0)|((op>>4)&0xF)`, `thumbInstrs[0x400]` indexed by
  `op>>6`. Registers are **banked through `uint32_t *registers[32]`
  pointers** (not a flat array). `cpsr`, `*spsr`, `cycles`, `halted` are
  members. Handlers return cycle counts.
- `memory.*`: bus. Has read/write page maps plus slow-path I/O handlers.
  The JIT's memory fast path must use the same page maps.
- `cp15.*`: ARM9 coprocessor (ITCM/DTCM, which affect the ARM9 memory map;
  the JIT must respect them).
- `gpu*.cpp`, `spu.*`, `dma.*`, `timers.*`, `ipc.*`, `rtc.*`, `spi.*`,
  `cartridge.*`, `dldi.*`, `wifi.*`, `save_states.*`, `action_replay.*`,
  `hle_bios.*`, `hle_arm7.*`: upstream emulation, lightly patched.
- **New (JIT), planned names. Keep the `jit_` prefix so it's easy to `EXCLUDE`:**
  - `jit.h` / `jit.cpp`: public entry, dispatch loop, `runCore*Jit` variants
  - `jit_ppc_emitter.h`: PPC instruction encoders (header-only)
  - `jit_compiler_arm.cpp`, `jit_compiler_thumb.cpp`: block compilers
  - `jit_cache.h` / `jit_cache.cpp`: code arena, block lookup, SMC page tracking, linking
  - `jit_trampoline.cpp` (or `.S` + Makefile rule): host↔guest ABI glue
  - `jit_debug.cpp`: differential tester (JIT vs interpreter lockstep), SD logging

Reference implementation (read-only, don't vendor wholesale):
`dborth/vbagx/source/vba/gba/`: `JIT.h`, `JITCompiler.cpp` (THUMB only,
≤42-instr traces), `JITCache.*`, `JITPPCEmitter.h`, `JITTrampoline.S`,
`JITDifferential.*`. It's GPL like NooDS, so porting ideas and code with
attribution is fine. Its GBA memory model (`map[]`, `cpuMemoryWait`) is
**different** from NooDS's `Memory` class, so adapt it; don't paste it.

---

## Debugging on hardware

No debugger. Dolphin is usable for a first pass (enable "Enable MMU" and use
the Dolphin JIT, not the interpreter, for speed), but **Dolphin doesn't model
Broadway's split I/D caches faithfully**. Missing `DCStoreRange` +
`ICInvalidateRange` bugs *only show up on real hardware*. Always confirm on a
real Wii.

- Log to `sd:/noods/jit_log.txt` (append + `fflush` per line; the last line
  is the failing point). Don't log per instruction in release builds.
- A differential mode (`JIT_DIFFTEST`) runs each block in the JIT, then
  re-runs it from a snapshot in the interpreter, compares
  r0–r15/CPSR/cycles, and logs the first mismatch with the ARM PC + opcode.
  This is the main correctness tool. VBA-GX's `JITDifferential.cpp` is the
  model.
- Crash handler: libogc prints a register dump on DSI/ISI. Record `SRR0`,
  then look it up in `NooDS-Wii.elf.map`, or inside the JIT arena, subtract
  the arena base and cross-reference the block log.

---

## Critical rules (do not break these)

### Build / environment
- **Build shell:** always the devkitPro MSYS2 shell (or a real devkitPro
  Linux env). Never Git Bash, never plain MSYS2/Cygwin/WSL without devkitPro.
- **`make clean` after any header change.** No dep tracking exists.
- **Don't drop `-DENDIAN_BIG`, `-mcpu=750`, `-mhard-float`, `-fsigned-char`.**
  The JIT emitter assumes 750CL (Broadway): **no AltiVec, no `isel`, no
  `popcntb`, no 64-bit ops**. Paired-single instructions are allowed but
  unneeded.
- **Exit to HBC with `exit(0)`.** Never `SYS_ResetSystem` (that goes to the
  system menu).

### JIT: correctness
- **The interpreter stays the source of truth and the fallback.** Any opcode
  the JIT doesn't handle *exactly* is emitted as a call into the matching
  `Interpreter` handler (`(this->*armInstrs[idx])(op)`) or ends the block.
  Never guess semantics.
- **Cache coherency:** after writing a block, call
  `DCFlushRange(ptr,len)` (or `DCStoreRange`) **then**
  `ICInvalidateRange(ptr,len)`, both 32-byte aligned. This also applies to
  every *patch* (block linking, SMC invalidation stubs).
- **Code arena alignment:** the arena must be 32-byte aligned and in MEM1 or
  MEM2 cached memory. Branch range: `b`/`bl` reach ±32 MB, so keep the whole
  arena and all C helper functions it calls within 32 MB, or use
  `mtctr`/`bctrl` for helper calls. **Default to `bctrl` for calls into C++.**
- **ABI (SVR4 PPC EABI):** r1 = SP (16-byte aligned), r2/r13 = SDA bases
  (**never touch**), r3–r10 args/volatile, r14–r31 callee-saved. Guest
  state pointer lives in a callee-saved reg (e.g. r31 = `Interpreter*`).
  The trampoline saves/restores every non-volatile the JIT uses, plus LR and
  CR fields cr2–cr4.
- **Banked registers:** NooDS uses `registers[i]` *pointers*. Load through
  the pointer at block entry, or (preferred) flush/reload guest regs around
  any instruction that can change mode (MSR, exceptions, `LDM ^`, S-suffixed
  data-processing with Rd=PC). Mode changes end the block.
- **Flags:** ARM C flag for SUB/CMP is *inverted* relative to PPC `CA` after
  `subfc`. Handle it explicitly. V = PPC `XER[OV]` via `o.` forms; clear
  `XER[SO]` (`mtxer`) before relying on it. Lazy flag evaluation is allowed
  but must materialize into `cpsr` before any fallback call, memory slow
  path, or block exit.
- **Shifter edge cases:** LSL/LSR by 32 and by >32, ASR ≥32, ROR by 0 = RRX,
  register shifts use the bottom byte, and immediate-shift LSR/ASR #0 means
  #32. PPC `slw/srw` handle 0–63 natively; `sraw` handles ≥32. Test every
  case in the diff tester.
- **PC reads:** ARM = instr+8, THUMB = instr+4. Bake them in as constants at
  compile time.
- **ARMv4 vs ARMv5:** ARM7 / GBA = ARMv4T. ARM9 = ARMv5TE (adds `BLX`,
  `CLZ`, `QADD` family, `SMLAxy`, `LDRD/STRD`, `PLD`, and LDR-to-PC
  interworking). The compiler takes the CPU's arch as a compile-time template
  param. **Never emit v5 behaviour for the ARM7.**
- **Cycle accounting:** every block exit reports exact cycles consumed (sum
  of what the interpreter handlers would have returned, including memory wait
  states from the `Memory` timing tables). The scheduler must not be able to
  tell JIT and interpreter apart. Blocks check the remaining cycle budget and
  yield (VBA-GX "quota yield").
- **IRQs / halts:** blocks end on writes to I/O space (`0x04xxxxxx`), on
  `SWI`, on `MCR/MRC` (ARM9 CP15 can remap TCM), on any CPSR mode/I-bit
  change, and on `halted` becoming set.

### JIT: self-modifying code and cache
- **SMC tracking is mandatory.** GBA games copy code into IWRAM/EWRAM, and
  DS games constantly load overlays into main RAM/ITCM. Track compiled pages
  (bitmap per 256 B–4 KB page). Every write path (the JIT's fast store,
  `Memory::write*`, **DMA**, cartridge→RAM loads, save-state load) must check
  the bitmap and invalidate blocks.
- **Block linking patches must be undone** on invalidation. Linked
  predecessors get re-pointed to the dispatcher stub.
- **Cache flush on reset / ROM load / save-state load / CP15 TCM remap.**
  Wipe the whole arena.
- **Memory budget:** Wii has 24 MB MEM1 + 64 MB MEM2. NDS main RAM (4 MB)
  plus VRAM, framebuffers and the 3D renderer already use a lot. Default arena:
  **4 MB for NDS, 2 MB for GBA**, allocated once with `memalign(32, …)` and
  never grown. On overflow, flush everything and restart (as VBA-GX does).

### JIT: integration
- Hook via `Core::runFunc`. Add `Interpreter::runCoreNdsJit`,
  `runCoreSingleJit<…>` (GBA uses the single-CPU path) so the interpreter
  path stays untouched and selectable.
- **Escape hatch:** keep a runtime setting (`Settings::jitEnabled`, default
  on once stable, saved in `noods.ini`) **and** a build flag
  (`make NOODS_JIT=0` → `EXCLUDE` the `jit_*.cpp` files and `-DNOODS_NO_JIT`).
  Both must produce a working emulator.
- **Save states** store only guest state, never JIT pointers. Loading one
  flushes the arena.
- **HLE BIOS / ARM7 HLE** paths (`hle_bios.cpp`, `hle_arm7.cpp`) intercept at
  specific PCs. The JIT must end blocks at those addresses (or check the HLE
  flag at block entry) so HLE still fires.

### Performance
- Target baseline: **GBA 10–35 fps, NDS 3–15 fps** (interpreter, from the
  README). Measure before and after with the same ROM/scene; log FPS to SD.
- The JIT must at least cover what VBA-GX covers (THUMB ALU/shift/imm,
  LDR/STR w/ imm & reg offsets incl. SP/PC-relative, PUSH/POP, LDMIA/STMIA,
  conditional B, B, BL pair, BX, block chaining, SMC, and quota yields) **and
  add ARM mode**, since VBA-GX doesn't do ARM at all and NDS code is mostly ARM.
- Don't chase JIT speed when the bottleneck is elsewhere. Profile first: on
  NDS, `gpu_3d_renderer` and `gpu_2d` often dominate. The JIT won't fix a
  renderer-bound game.
