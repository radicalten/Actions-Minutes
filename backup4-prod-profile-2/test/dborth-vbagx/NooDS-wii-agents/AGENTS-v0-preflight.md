# AGENTS.md — preflight edition (v0)

> **What this is.** This is the briefing that *should have existed before the
> first line of JIT code was written*. It is a retrospective reconstruction: the
> original AGENTS.md (231 lines) was an excellent specification of **what** to
> build and almost silent on **how to find things out, in what order, and which
> paths waste days**. Everything below is either a hard constraint carried over
> from that document, or a fact/trap that was discovered the expensive way.
>
> Read all of it before the first commit. The sections are ordered by how much
> time they save, not by how interesting they are.
>
> Sections marked **[GIVEN]** were verified before real progress started and can
> be treated as facts. **[PICKUP]** is the counterfactual: what you'd have to
> reproduce if you start from zero without this file.

---

## 0. Decisions to make before writing any code

These block everything else. Resolve them first, in this order.

1. **Is a devkitPro toolchain available?** If yes, work on the devkitPro machine
   and verify with real builds. If no — which is the common case in a sandbox —
   then:
   - You **cannot** build this repo and must never claim you did. The Makefile
     hard-codes `export DEVKITPRO := /opt/devkitpro` and needs
     `powerpc-eabi-g++`, `libogc`, `elf2dol`.
   - Adopt the offline verification stack in §2 instead, and state clearly in
     every status update which claims are proven and which are source-level only.
   - **Do not try to install devkitPro.** `apt.devkitpro.org`,
     `download.devkitpro.org` and `pkg.devkitpro.org` are blocked from typical
     sandboxes (403/000). This is a dead end; treat it as one from minute one.
2. **Who writes the code — you, or a spawned coding agent?** If your working
   instructions forbid hand-coding in orchestrator mode (the supplied SKILL.md
   does: "respect tool choice… do NOT hand-code patches yourself"), check for an
   installed agent *before* starting. If none is installed, installing one
   globally needs the user's explicit confirmation — **ask now, not at hour six.**
   This single question determines the whole shape of the workstream. Do not
   silently substitute hand-coding for the documented workflow; surface it and
   let the user decide.
3. **Which environment is authoritative for correctness?** In descending order:
   real Wii hardware > Dolphin > qemu-ppc semantics probes. Two consequences:
   cache-coherency bugs (`DCFlushRange`/`ICInvalidateRange`) are **only**
   confirmable on hardware, and instruction-level semantics (`rlwinm` bit
   numbering, flag behaviour) *are* confirmable offline with qemu-ppc — so
   confirm those offline rather than guessing and debugging later.
4. **Confirm scope.** The target is at least VBA-GX parity **plus ARM mode**,
   because VBA-GX is THUMB-only and most NDS code is ARM. Confirm with the user
   that ARM is in scope before designing around a THUMB-first schedule.

---

## 1. What "done" means

Written down up front so it can't drift:

- **Coverage ≥ VBA-GX**, THUMB: ALU/shift/immediate, `LDR`/`STR` with immediate
  and register offsets including SP- and PC-relative, `PUSH`/`POP`,
  `LDMIA`/`STMIA`, conditional `B`, `B`, the `BL` pair, `BX`. **Plus ARM mode**
  (VBA-GX has none): data processing, `B`/`BL`/`BX`/`BLX`, single/half/doubleword
  transfers with all addressing modes, `LDM`/`STM`, multiplies.
- **Differential-tested** against the interpreter in lockstep: compare
  r0–r15, CPSR and cycles; log the first mismatch with the ARM PC and opcode.
- **Two independent off-switches**, both of which must produce a working
  emulator: runtime `Settings::jitEnabled` (in `noods.ini`) and build-time
  `make NOODS_JIT=0` (`EXCLUDE` the `jit_*.cpp` files + `-DNOODS_NO_JIT`).
- **The interpreter path stays untouched and selectable.** It is the reference
  implementation forever, not a stepping stone to be deleted.
- **Cycle-exact scheduling.** The scheduler must not be able to tell a JIT'd CPU
  from an interpreted one. Every block exit reports the exact integer sum of the
  handler returns it stands in for.
- **SMC-correct.** Every write path — fast stores, `Memory::write*`, DMA,
  cartridge→RAM loads, save-state load — invalidates translated code before it
  can run.
- File names keep the `jit_` prefix so they can be filtered from the build:
  `jit.h`/`jit.cpp`, `jit_ppc_emitter.h`, `jit_compiler_arm.cpp`,
  `jit_compiler_thumb.cpp`, `jit_cache.h`/`jit_cache.cpp`, `jit_trampoline.cpp`,
  `jit_debug.cpp`.

**Explicit anti-goals.** If you find yourself doing any of these, stop:
approximating an instruction "close enough"; reimplementing interpreter logic
for fallbacks instead of calling it; caching the guest PC in a host register;
lazy flags that don't materialise before a call, a slow path or an exit; growing
the arena; vendoring VBA-GX code wholesale.

---

## 2. Build the verification stack FIRST **[GIVEN]**

Before any emulator code. Each of these takes minutes to build and each one
catches a class of bug that costs days on hardware.

### 2.1 Encoder oracle (do this first — it is the cheapest correctness win)
`tools/jit_asm_oracle.cpp` holds `asm|word` vectors; `tools/run_jit_asm_oracle.sh`
assembles the `asm` lines with `powerpc-linux-gnu-as`, extracts `.text` with
`powerpc-linux-gnu-objcopy -O binary --only-section=.text`, and compares
big-endian words against the emitter's encoders.

Rules:
- Every new or changed encoder gets a vector. The script must stay green.
- **Do not parse `objdump` output.** `--no-show-raw-insn` rejects arguments and
  the raw bytes come out split across tokens, so naive field extraction silently
  compares one byte. `objcopy` is the reliable path. **[PICKUP]** — this was
  discovered by wasting time on a "passing" oracle that was comparing garbage.
- This oracle found two real encoder bugs on first contact (`addic.`'s primary
  opcode; a bogus `mfcr` field). Expect it to find yours.

### 2.2 PowerPC semantics probes
For anything ambiguous about the *host* ISA — flags above all — write a
dedicated `.s` file plus a small C driver (`tests/sem.s`, `tests/flags2.s`),
build with `powerpc-linux-gnu-gcc -O1 -static -mcpu=750`, run under
`qemu-ppc-static` (big-endian, SYSV ABI), and compare against the **interpreter's
own expressions copied verbatim** from the source.

- **Never test flag behaviour with inline asm and `"+r"` constraints.** The
  operands get clobbered, you get phantom mismatches, and you will chase them.
  Dedicated `.s` files only. **[PICKUP]**
- Drive the comparison from the interpreter's formulas, not from your memory of
  the architecture manual: 16 operand pairs × 2 carry-ins × 6 operations is
  about 200 cases and it costs one file.

### 2.3 Host syntax check
`tools/hostcheck/check.sh` + `tools/hostcheck/stub/` runs
`powerpc-linux-gnu-g++ -fsyntax-only` over the `jit_*.cpp` files with minimal
stand-ins for the libogc headers. Not a link test and not a build, but it catches
header/implementation drift, signature mismatches and typos with no devkitPro.

### 2.4 Suggested work order
1. Emitter + oracle to green. (§2.1)
2. Flag semaphore probes; freeze the flag model. (§2.2)
3. Extract interpreter semantics into a notes file (§4) **before** writing the
   translator — this is the single biggest time sink and it is pure reading.
4. Freeze the interface: context layout, register contract, exit contract.
5. Framework (emission, labels, flags, shifts, conditions, memory, epilogue).
6. THUMB decoder, then ARM decoder.
7. `JIT_DIFFTEST` lockstep, then Dolphin, then hardware.

---

## 3. Environment facts **[GIVEN]**

- **Build shell:** devkitPro MSYS2 shell, or a real devkitPro Linux env with
  `/opt/devkitpro`. Never Git Bash, never plain MSYS2/Cygwin/WSL without
  devkitPro. The Makefile overrides `DEVKITPRO` with `:=`, so environment
  variables are ignored — fix the Makefile if devkitPro lives elsewhere.
- **`make clean` after every header edit.** There is no `-MMD` dependency
  tracking; stale objects cause silent ABI mismatch and random crashes.
- **No `.S` rule in the Makefile.** Write hand-written host assembly as a
  top-level `__asm__(…)` block inside a `.cpp` (that is what `jit_trampoline.cpp`
  does). Adding an `ASFILES` rule is the alternative, and forgetting it produces
  an undefined-symbol link failure.
- **Flags you must not drop:** `-DENDIAN_BIG`, `-mcpu=750`, `-mhard-float`,
  `-fsigned-char`. Emitter target is 750CL (Broadway/Gekko): no AltiVec, no
  `isel`, no `popcntb`, no 64-bit ops.
- **`<tuxedo/thread.h>` is upstream libogc**, not a private fork: devkitPPC r49
  ships "Tuxedo", a Calico-based low-level support library replacing lwp. Just
  require a recent devkitPro. **[PICKUP]** — do not go looking for a
  `radicalten/tuxedo` repo; it does not exist and you will find nothing.
- **CI never runs.** `.github/workflows/main.yml` triggers on `master` but the
  branch is `main`. Do not rely on it.
- **Git history is useless here.** The fork was uploaded in bulk commits; blame
  and archaeology tell you nothing. Read the code, not the log.
- **MEM2:** the port has its own allocator. `Noods_MEM2_Alloc` is defined in
  `main.cpp` (MEM2 arena via `SYS_GetArena2Lo/Hi`) and `Noods_MEM2_Free` is a
  **no-op stub**. Allocate the code arena through the existing allocator rather
  than inventing one; treat frees as advisory. `Core::operator new` also routes
  to MEM2.
- **Exit to HBC with `exit(0)`.** Never `SYS_ResetSystem` (that goes to the
  system menu).

---

## 4. Read the interpreter first — it is the specification **[PICKUP]**

The JIT's job is to reproduce `interpreter*.cpp` exactly. Do the extraction
*before* designing, and write the results into a notes file so you can grep
semantics later instead of re-reading 20k lines.

Reading order and what each file gives you:

| Source | What you extract |
|---|---|
| `interpreter.cpp` (`runOpcode`, `flushPipeline`, `halt`, `unhalt`, `exception`, `setCpsr`, `swapRegisters`) | The pipeline model, the halt/cycle conventions, how a mode change re-banks registers |
| `interpreter.h` | Banked `uint32_t *registers[32]`, `cpsr`/`*spsr`, `cycles`, `halted`, `pipeline[2]`, `isThumb()` |
| `interpreter_alu.cpp` | Every data-processing handler, the shifter helpers, the PC-bias rule for register shifts, multiply cycle ramps |
| `interpreter_transfer.cpp` | `HALF_FUNCS`/`FULL_FUNCS` addressing-mode matrix, misalignment rotates, PC-destination behaviour, block-transfer cycles |
| `interpreter_branch.cpp` | Branch targets, THUMB `BL` pair mechanics, `BX` mode switching |
| `interpreter_lookup.cpp` | The authoritative dispatch tables — including which opcodes are *unknown* |
| `memory.h` / `memory.cpp` | `read<T>`/`write<T>` fast path, the in-page mask, `updateMap9/7`, `updateVram`, the I/O region, the GBA open-bus path that reads `registers[15]` |
| `core.h` / `core.cpp` | `runFunc` selection, `updateRun()`, `globalCycles`, the event queue, the run loops you must mirror |

**The two facts that make this tractable:**
- **THUMB** dispatches through `thumbInstrs[(op >> 6) & 0x3FF]` — 1024 entries but
  only ~81 distinct handlers, i.e. large contiguous ranges. Extract the range map
  and classify from *that*, so the JIT's notion of "unknown" matches the
  interpreter's exactly. (Doing this by hand from the opcode diagram instead is
  how you end up treating `unkThumb` ranges as instructions.)
- **ARM** dispatches through `armInstrs[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)]`
  after a condition pre-check where **a false condition still costs 1 cycle** and
  case 2 is `handleReserved`.

**Extraction format that worked:** dump each handler body into one grep-able
notes file, then annotate the cycle costs and the edge cases in a table. The
cycle table is not optional — cycle-exactness is an acceptance criterion, and the
returns are scattered across macro-generated families.

---

## 5. Semantics traps — the expensive ones **[GIVEN]**

Each line below cost real time to establish. Treat them as given.

### 5.1 Flags
- **The carry is *not* inverted.** `subfc rD, rB=op2, rA=op1` computes `op1 - op2`
  and its carry equals the ARM carry directly. An earlier revision of this
  document asserted an inversion; that is only true if you swap the operand
  fields. Get the field order right and there is nothing to XOR.
- **`addo`/`subfo` (non-carrying forms) do not reliably update `XER[CA]`.** Use
  `addc`/`subfc`/`adde`/`subfe` with the `oe` bit for flag updates.
- **`mcrxr` is unusable** (returns CR = 0). Materialise with `mfxer` + `rlwimi`:
  `XER[CA]` is bit 29 and `XER[OV]` bit 30, so C needs no move and V is one
  rotate by 30. `XER[SO]` is sticky and never read by this scheme.
- **`rlwinm`/`rlwimi` MB/ME are IBM bit numbers** (bit 0 = MSB). CPSR bit *p* in
  LSB numbering corresponds to mask `31 - p`: N=0, Z=1, C=2, V=3. Shift amount is
  `(dst - src) & 31`. Getting this backwards yields code that fails on exactly
  one flag — the worst kind of bug.
- **N and Z** come from `or. rD,rD,rD` + `mfcr` + two `rlwimi` (CR0[LT] tracks
  bit 31, CR0[EQ] tracks zero).
- **Carry-in** for `adde`/`subfe` requires XER[CA] to be loaded from CPSR:
  `rlwinm rX, cpsr, 3, 31, 31` → `slwi rX, rX, 29` → `mtxer rX`.

### 5.2 Shifter
`slw`/`srw` mask the count to 6 bits and return **0 for counts ≥ 32**; `sraw`
sign-fills; `rlwnm` masks to 5 bits. The ARM's rules differ in the corners, so
**shift-by-32 and shift-by->32 need explicit branches**:
- LSL 0: pass-through, **carry unchanged**. LSL 1–32: carry = bit(32−n).
  LSL > 32: result 0, carry 0.
- LSR #0 *in the encoding* means **#32**; carry = bit(min(n,32)−1).
- ASR #0 *in the encoding* means **#32**; sign fill, carry = bit 31.
- ROR #0 *in the encoding* is **RRX**: `(C << 31) | (v >> 1)`, carry = bit 0.
- Register shifts read the **bottom byte** of the shift register; ARM adds +4 to
  the PC bias when r15 is the shifted register, and another +4 when the count
  register is read.

### 5.3 PC and pipeline
- Handler-time `registers[15]` = instruction + 8 (ARM) / + 4 (THUMB);
  `pipeline[0]` is the executing instruction.
- PC as a *value*: ARM +8, THUMB +4, plus +4 more for ARM operands using a
  register-specified shift.
- Handing control back at address `A`: set `*registers[15] = A` and call
  `flushPipeline()`; that builds exactly the "about to execute A" state.
- Next instruction address from interpreter state: `(r15 − 2) & ~1` (THUMB),
  `(r15 − 4) & ~3` (ARM).
- **Guest r15 is never cached in a host register.** Every PC use is a
  compile-time constant. This removes an entire class of bug.
- **Loads/stores that write r15** change the mode on ARM9 (T bit from bit 0 of
  the value), cost 5 cycles, and must end the block.

### 5.4 Memory
- Fast paths mirror `Memory::read<T>`/`write<T>`: same page tables (the ARM9 has
  **two** read maps, TCM and non-TCM), same in-page mask
  **`address & (0x1000 - sizeof(T))`** — a mask, not a wrap.
- Little-endian guest on big-endian host: `lhbrx`/`lwbrx`/`sthbrx`/`stwbrx`;
  `lbzx`/`stbx` for bytes.
- **Misalignment is ISA behaviour, not an error:** word loads rotate by
  `(addr & 3) << 3` on **both** CPUs; half loads rotate by 8
  (`(v << 24) | (v >> 8)`) on the **ARM7 only**; `LDRSH` sign-extends first then
  arithmetic-shifts right by 8. Emit these in the fast path.
- Everything not plainly mapped goes to a C++ helper that calls the same
  `Memory::read/write<T>`.
- **Stores always end the block.** A store can raise an interrupt, halt the CPU,
  start DMA or reschedule a task.
- **Slow loads may stay in the block**, but the helper must detect a side effect
  (task due before this run's deadline, `halted`, pending enabled interrupt) and
  end the block if one occurred.
- **`li` is `addi rD, 0, imm`, so r0 must be reserved as a literal zero** for
  base addressing. Keep r0 out of the scratch set.

### 5.5 Cycles
- Handlers return exact cycle counts; sum them statically. The multiply ramp is
  `1 + ((a >> 8) != 0) + ((a >> 16) != 0) + ((a >> 24) != 0)` and is worth
  computing inline rather than ending the block.
- **Two clock domains.** `Interpreter::cycles` is in *CPU units* — global cycles
  for the ARM9 and GBA, twice global for the NDS ARM7. `Core::globalCycles` and
  the event queue are global. Convert with the shift at exactly the points the
  interpreter's run loops do.
- Blocks check the budget **before entering**; a block whose worst case doesn't
  fit must not start. Running one opcode through the interpreter instead
  reproduces the interpreter's own overshoot granularity.

### 5.6 Modes, banked registers, HLE
- `registers[i]` are **pointers**; a mode change swaps them.
  **S-form ALU with Rd == r15 must be an interpret exit when `spsr` is non-null**
  — that is the mode switch. In user mode it is a plain dynamic-PC exit.
- End the block on: I/O-space writes, `SWI`, `MCR`/`MRC`, any CPSR mode/I-bit
  change, `halted` becoming set, any r15-writing load/store, and any instruction
  whose exact semantics you have not implemented.
- **HLE BIOS / ARM7 HLE** intercept at specific PCs; those addresses must end
  blocks or be checked at block entry.

### 5.7 Self-modifying code
- **The cheapest correct design:** one byte counter per guest 4 KB page.
  `Memory::write<T>` tests it inline (one lookup + one compare; only pages that
  *currently hold code* are non-zero) and the hook runs **before the write
  lands**, so stale code can never execute.
- Because a compiled block only ever covers its own page (stop the translator at
  page boundaries), invalidation is exact: drop the bucket for that page.
- **All DMA routes through `core->memory.read/write`** — one hook on
  `Memory::write` covers DMA, cartridge→RAM loads and the fill registers. Do not
  go hunting for a separate DMA path.
- Remap sites (`updateMap9`, `updateMap7`, `updateVram`) flush everything.
  So do reset, ROM load and save-state load.

---

## 6. Design constraints to follow **[GIVEN]**

- **Register contract:** guest r0–r14 in host r14–r28, CPSR in r29,
  `JitContext*` in r30, `Interpreter*` in r31; r3–r12 scratch; r0 reserved;
  r1/r2/r13 never touched.
- **Exit model:** a block ends by branching to one shared epilogue with
  `r3` = cycles, `r11` = resume address (pipeline bias already removed), `r12` = state.
  The epilogue spills, folds cycles, checks halt + budget, then chains inline or
  returns to C++. Slow memory paths jump to a *second* entry point of the same
  epilogue because their helpers have already set the exit fields.
- **Interpret-exit is the fallback mechanism:** end the block with
  "run one opcode in C++". That keeps HLE BIOS, DLDI, reserved opcodes and mode
  changes identical to a non-JIT build for free.
- **`JitContext` offsets are literal macros with `static_assert(offsetof(...))`**,
  and the trampoline stringifies those macros. Struct edits then either compile
  or fail loudly. Never hand-write an offset in either place.
- **Trampoline:** save/restore every nonvolatile host register used plus LR and
  the whole CR (`mfcr`/`mtcr` around the block is simpler and provably correct
  than tracking individual CR fields). Helper calls use `mtctr` + `bctrl`; never
  assume branch range.
- **Cache coherency after every code write or patch:** `DCFlushRange` (or
  `DCStoreRange`) **then** `ICInvalidateRange`, both 32-byte aligned.
- **Arena:** 32-byte aligned, in cached MEM1/MEM2, allocated once through
  `Noods_MEM2_Alloc`, never grown; 4 MB NDS / 2 MB GBA; on overflow flush
  everything and restart.
- **Static scratch, not stack.** Translation buffers are large; Wii thread
  stacks are shared with the audio and GPU threads. Put the translation output
  and fixup arrays in file-scope statics (translation is single-threaded on the
  emulation thread).
- **Dispatch:** direct-mapped block table keyed by `(pc << 1) | thumb`, plus an
  inline one-entry cache in the context for chaining. Linked entries must be
  revocable by key on invalidation — never create a patch you cannot find again.

---

## 7. Dead ends — do not spend time here **[PICKUP]**

- Installing devkitPro from a sandbox (endpoints blocked).
- Searching for a `tuxedo` repository (it is upstream libogc).
- Relying on CI (wrong branch).
- Git blame / history archaeology (bulk upload commits).
- Parsing `objdump --no-show-raw-insn` output (§2.1).
- Inline-asm flag probes with tied constraints (§2.2).
- `addo`/`subfo`, `mcrxr`, and the "carry inversion XOR".
- Assuming `slw`/`srw`/`rlwnm` handle ARM's shift-by-32 corners.
- Piecing THUMB decoding together from the opcode diagram instead of the
  interpreter's own table.
- Approximating an instruction instead of ending the block.
- Growing the arena, or freeing blocks individually rather than flushing.

---

## 8. Working agreements

- One logical change per commit; keep the encoder oracle green at every step.
- `make clean` after every header edit. Every time. No exceptions.
- Log JIT diagnostics to `sd:/noods/jit_log.txt` (append, `fflush` per line);
  never log per instruction in release builds.
- Report status in three buckets: **verified** (has a tool or a test behind it),
  **written but unverified**, **not started**. Never blend them.
- Route autonomous agent output through a feature branch / PR, per the working
  instructions; never send secrets through process-write/input channels; get
  confirmation before any global tool install.

---

## 9. Performance

- Baseline to beat, from the README: **GBA 10–35 fps, NDS 3–15 fps** with the
  interpreter. Measure before and after with the same ROM and scene.
- **Profile before optimising.** On NDS, `gpu_3d_renderer` and `gpu_2d` often
  dominate; the JIT will not fix a renderer-bound game.
- Yield with a quota (VBA-GX does this) so the event queue stays prompt.
