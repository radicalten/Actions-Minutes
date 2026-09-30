# AGENTS.md — JIT preflight briefing (v2, condensed)

> The briefing that should have existed before the first line of JIT code was
> written: what to build, what order to find things in, and which paths waste
> days. Self-contained for this sandbox's environment. Coding-agent operation
> rules live in **SKILL.md** (provided alongside this file).
>
> **[GIVEN]** = verified fact. **[PICKUP]** = trap discovered the expensive way.
> v2: condensed from v1; environment section folded in from the devkitPPC
> install doc (no longer a separate file); agent-ops detail delegated to SKILL.md.

---

## 0. Decide before writing any code

1. **Toolchain — two layers; in a fresh sandbox instance both are missing, bootstrap first.**
   - **A: devkitPPC** (`powerpc-eabi-g++`, `libogc`, `elf2dol`) at `/opt/devkitpro` —
     builds the emulator → `boot.dol`. **Sanctioned install: the Docker Hub
     workaround** (official `devkitpro/devkitppc` image): 3 commands if
     `/home/user/dkp-work/` exists — `fetch-layers.sh` (pulls image layers,
     sha256-verified) → `extract-layers.sh` (extracts `opt/` to
     `dkp-work/stage`) → `install.sh` (`sudo cp -a stage/opt/devkitpro
     /opt/devkitpro`). If `dkp-work/` is gone, by hand: pull token from
     `auth.docker.io` → fetch the linux/amd64 manifest from
     `registry-1.docker.io/v2/` → download each layer blob → `tar -xzf` only
     `opt/*` from each, in order → copy to `/opt/devkitpro`.
     **Never `dkp-pacman`**: `pkg.devkitpro.org` returns 403 to this sandbox's
     IP (`apt.`/`download.devkitpro.org` too). **`/opt` is not persisted in
     workspace snapshots** — reinstall per instance (needs Docker Hub network).
   - **B: host cross toolchain** (`powerpc-linux-gnu-gcc/as/objcopy`,
     `qemu-ppc-static`) for the §3 verification stack — Debian apt, not blocked:
     `apt-get install -y gcc-powerpc-linux-gnu binutils-powerpc-linux-gnu
     qemu-user-static`.
   - Verify: `source /home/user/wii-env.sh && powerpc-eabi-gcc --version` (expect
     devkitPPC 16.x). If Docker Hub *and* Debian are unreachable: source-level
     work only — never claim a build you didn't run.
2. **Who writes the code.** Orchestrator mode: **do NOT hand-code patches
   yourself**. Check for an installed agent *before* starting (none preinstalled
   by default; node/npm at `/usr/bin`; installs are npm-registry-only) — a global
   install needs the user's **explicit confirmation now, not at hour six**.
   **No API keys exist in env/config** — ask where the key lives; never invent
   one, never put it in a prompt/argument. Full agent-operation rules: SKILL.md.
3. **Authoritative correctness order:** real hardware > Dolphin > qemu-ppc
   semantics probes. This sandbox has **no hardware and no display** — the
   offline ceiling is build + oracle + qemu-ppc probes; cache-coherency bugs
   confirmable on hardware only. Never claim hardware/Dolphin results from here.
   Label every claim: verified / source-level / needs hardware.
4. **Scope:** at least VBA-GX parity **plus ARM mode** (VBA-GX is THUMB-only;
   most NDS code is ARM). Confirm ARM is in scope before designing a THUMB-first
   schedule.

---

## 1. What "done" means

- **Build level (headless sandbox):** `make` → valid `boot.dol`, ELF entry
  `0x80003f00`. Compile + link + `elf2dol` success **is** the build test suite;
  running the `.dol` needs a real Wii or a Dolphin outside the sandbox.
- **Coverage ≥ VBA-GX:** THUMB ALU/shift/immediate, `LDR`/`STR` with immediate
  and register offsets incl. SP- and PC-relative, `PUSH`/`POP`, `LDMIA`/`STMIA`,
  conditional `B`, `B`, the `BL` pair, `BX`. **Plus ARM mode** (VBA-GX has none):
  data processing, `B`/`BL`/`BX`/`BLX`, single/half/doubleword transfers with all
  addressing modes, `LDM`/`STM`, multiplies.
- **Differential-tested** against the interpreter in lockstep (r0–r15, CPSR,
  cycles; log first mismatch with PC + opcode).
- **Two independent off-switches**, each must yield a working emulator: runtime
  `Settings::jitEnabled` (`noods.ini`) and build-time `make NOODS_JIT=0`
  (EXCLUDE the `jit_*.cpp` + `-DNOODS_NO_JIT`).
- **Interpreter path stays untouched and selectable** — the reference forever,
  not a stepping stone to delete.
- **Cycle-exact scheduling:** no scheduler can tell JIT from interpreter; every
  block exit reports the exact integer sum of the handler returns it stands for.
- **SMC-correct:** every write path (fast stores, `Memory::write*`, DMA,
  cartridge→RAM loads, save-state load) invalidates translated code before it can
  run.
- File names keep the `jit_` prefix: `jit.h`/`.cpp`, `jit_ppc_emitter.h`,
  `jit_compiler_arm.cpp`, `jit_compiler_thumb.cpp`, `jit_cache.h`/`.cpp`,
  `jit_trampoline.cpp`, `jit_debug.cpp`.

**Anti-goals — stop if doing any:** approximating an instruction "close enough";
reimplementing interpreter logic for fallbacks instead of calling it; caching the
guest PC in a host register; lazy flags that don't materialise before a call,
slow path or exit; growing the arena; vendoring VBA-GX wholesale.

---

## 2. Environment **[GIVEN]**

- **Every new shell:** `source /home/user/wii-env.sh` (exports `DEVKITPRO`,
  `DEVKITPPC`, `PATH` + `wiivars.sh` flags). Env does not persist across `bash`
  calls — put it at the top of every command chain and every agent prompt.
- Makefile overrides `DEVKITPRO` with `:=` — env vars ignored; fix the Makefile
  if devkitPro lives elsewhere.
- **`make clean` after every header edit** — no `-MMD` tracking; stale objects
  = silent ABI mismatch and random crashes.
- **No `.S` rule in the Makefile** — hand-written host asm goes in a top-level
  `__asm__(…)` block inside a `.cpp` (what `jit_trampoline.cpp` does); adding an
  `ASFILES` rule is the alternative, and forgetting it = undefined-symbol link
  failure.
- **Flags you must not drop:** `-DENDIAN_BIG -mcpu=750 -mhard-float
  -fsigned-char`. Emitter target 750CL (Gekko): **no AltiVec, no `isel`, no
  `popcntb`, no 64-bit ops**.
- **libogc 3.x:** link with `-specs=$DEVKITPRO/libogc/share/rvl.specs`
  (defines `__bss_end`, `__Arena1Lo`, … via `rvl.ld`) or plain `-logc` fails on
  undefined references. Wiimote code adds `-lwiiuse -lbte`; GameCube builds use
  `cubevars.sh` + `ogc.specs`.
- **`<tuxedo/thread.h>` is upstream libogc** (devkitPPC r49 "Tuxedo", Calico
  replacing lwp). Do **not** hunt for a `radicalten/tuxedo` repo — it doesn't
  exist.
- **CI never runs** (workflow triggers on `master`, branch is `main`).
  **Git history is useless** (bulk-upload commits — read the code, not the log).
- **MEM2:** the port has its own allocator — `Noods_MEM2_Alloc` in `main.cpp`
  (MEM2 arena via `SYS_GetArena2Lo/Hi`); `Noods_MEM2_Free` is a **no-op stub**.
  Allocate the code arena through it once; frees are advisory. `Core::operator
  new` also routes to MEM2.
- **Exit to HBC with `exit(0)`** — never `SYS_ResetSystem` (goes to system menu).
- **Tooling present:** git, node, npm, sudo at `/usr/bin`; **`gh` not installed**
  (PRs = plain `git` + HTTPS API). Agent CLIs not preinstalled (§0.2).
- Scratch-project template (just `make`):
  `/opt/devkitpro/examples/wii/templates/makefile/application/`. Keep a
  `hello.c` → `boot.dol` smoke project as the toolchain sanity check.

---

## 3. Verification stack FIRST **[GIVEN]**

Build before any emulator code; each layer catches a bug class that costs days
on hardware.

1. **Encoder oracle (cheapest correctness win — do first).**
   `tools/jit_asm_oracle.cpp` holds `asm|word` vectors; `tools/run_jit_asm_oracle.sh`
   assembles the `asm` lines with `powerpc-linux-gnu-as`, extracts `.text` with
   `powerpc-linux-gnu-objcopy -O binary --only-section=.text`, compares
   big-endian words against the emitter. Every new/changed encoder gets a
   vector; the script must stay green. **Do not parse `objdump` output** —
   `--no-show-raw-insn` rejects arguments and raw bytes split across tokens, so
   naive extraction silently compares garbage. It found two real bugs on first
   contact (`addic.`'s primary opcode; a bogus `mfcr` field).
2. **PowerPC semantics probes.** For ambiguous *host*-ISA behaviour (flags above
   all): dedicated `.s` + small C driver (`tests/sem.s`, `tests/flags2.s`), build
   `powerpc-linux-gnu-gcc -O1 -static -mcpu=750`, run under `qemu-ppc-static`
   (big-endian, SYSV), compare against the **interpreter's own expressions
   copied verbatim** (16 operand pairs × 2 carry-ins × 6 ops ≈ 200 cases, one
   file). **Never test flags with inline asm and `"+"` constraints** — operands
   get clobbered, phantom mismatches follow.
3. **Host syntax check.** `tools/hostcheck/check.sh` + `tools/hostcheck/stub/`:
   `powerpc-linux-gnu-g++ -fsyntax-only` over the `jit_*.cpp` with minimal
   libogc header stand-ins. Not a link test; catches drift/signature/typos with
   no devkitPro.

**Work order:** bootstrap toolchain (§0) → emitter + oracle green → flag probes,
freeze the flag model → extract interpreter semantics into a notes file (§4)
**before** writing the translator — the single biggest time sink, pure reading →
freeze the interface (context layout, register contract, exit contract) →
framework (emission, labels, flags, shifts, conditions, memory, epilogue) → THUMB
decoder → ARM decoder → `JIT_DIFFTEST` lockstep → Dolphin → hardware (the last
two from outside this sandbox, §0.3).

---

## 4. Read the interpreter first — it is the specification **[PICKUP]**

The JIT's job is to reproduce `interpreter*.cpp` exactly. Extract *before*
designing, into one grep-able notes file with the cycle table annotated (cycle
exactness is an acceptance criterion; returns are scattered across
macro-generated families).

| Source | What you extract |
|---|---|
| `interpreter.cpp` (`runOpcode`, `flushPipeline`, `halt`/`unhalt`, `exception`, `setCpsr`, `swapRegisters`) | pipeline model, halt/cycle conventions, how a mode change re-banks registers |
| `interpreter.h` | banked `uint32_t *registers[32]`, `cpsr`/`*spsr`, `cycles`, `halted`, `pipeline[2]`, `isThumb()` |
| `interpreter_alu.cpp` | every data-processing handler, shifter helpers, PC-bias rule for register shifts, multiply cycle ramps |
| `interpreter_transfer.cpp` | `HALF_FUNCS`/`FULL_FUNCS` addressing matrix, misalignment rotates, PC-destination behaviour, block-transfer cycles |
| `interpreter_branch.cpp` | branch targets, THUMB `BL` pair mechanics, `BX` mode switching |
| `interpreter_lookup.cpp` | authoritative dispatch tables — incl. which opcodes are *unknown* |
| `memory.h` / `memory.cpp` | `read<T>`/`write<T>` fast path, in-page mask, `updateMap9/7`, `updateVram`, I/O region, GBA open-bus path reading `registers[15]` |
| `core.h` / `core.cpp` | `runFunc` selection, `updateRun()`, `globalCycles`, event queue, the run loops to mirror |

**The two facts that make it tractable:**
- **THUMB** dispatches via `thumbInstrs[(op >> 6) & 0x3FF]` — 1024 entries but
  ~81 distinct handlers in large contiguous ranges. Extract the range map and
  classify from *that*, so "unknown" matches the interpreter exactly (deriving
  from the opcode diagram by hand is how `unkThumb` ranges become "instructions").
- **ARM** dispatches via `armInstrs[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)]`
  after a condition pre-check where **a false condition still costs 1 cycle**;
  case 2 is `handleReserved`.

---

## 5. Semantics traps — the expensive ones **[GIVEN]**

Each line below cost real time to establish. Treat as given.

**Flags.**
- **The carry is *not* inverted.** `subfc rD, rB=op2, rA=op1` computes
  `op1 − op2`; its carry equals the ARM carry directly. (The inversion claim was
  for swapped operand fields — get field order right and there is nothing to XOR.)
- **`addo`/`subfo` do not reliably update `XER[CA]`** — use `addc`/`subfc`/
  `adde`/`subfe` with the `oe` bit for flag updates.
- **`mcrxr` is unusable** (returns CR = 0). Materialise with `mfxer` + `rlwimi`:
  `XER[CA]` = bit 29, `XER[OV]` = bit 30 → C needs no move, V is one rotate by
  30. `XER[SO]` is sticky, never read.
- **`rlwinm`/`rlwimi` MB/ME are IBM bit numbers** (bit 0 = MSB). CPSR bit *p*
  (LSB numbering) = mask `31 − p`: N=0, Z=1, C=2, V=3; shift amount
  `(dst − src) & 31`. Backwards = fails on exactly one flag.
- **N and Z:** `or. rD,rD,rD` + `mfcr` + two `rlwimi` (CR0[LT] tracks bit 31,
  CR0[EQ] tracks zero).
- **Carry-in** for `adde`/`subfe`: load XER[CA] from CPSR —
  `rlwinm rX, cpsr, 3, 31, 31` → `slwi rX, rX, 29` → `mtxer rX`.

**Shifter.** `slw`/`srw` mask the count to 6 bits and return **0 for counts ≥ 32**;
`sraw` sign-fills; `rlwnm` masks to 5 bits. ARM differs in the corners, so
shift-by-32 / shift-by->32 need explicit branches:
- LSL 0: pass-through, **carry unchanged**. LSL 1–32: carry = bit(32−n).
  LSL > 32: result 0, carry 0.
- LSR #0 in the encoding = **#32**; carry = bit(min(n,32)−1).
- ASR #0 in the encoding = **#32**; sign fill, carry = bit 31.
- ROR #0 in the encoding = **RRX**: `(C << 31) | (v >> 1)`, carry = bit 0.
- Register shifts read the **bottom byte** of the shift register; ARM adds +4 to
  the PC bias when r15 is the shifted register, and another +4 when the count
  register is read.

**PC and pipeline.**
- Handler-time `registers[15]` = instruction + 8 (ARM) / + 4 (THUMB);
  `pipeline[0]` is the executing instruction.
- PC as a *value*: ARM +8, THUMB +4, plus +4 for ARM operands using a
  register-specified shift.
- Hand control back at address `A`: `*registers[15] = A` + `flushPipeline()` —
  exactly the "about to execute A" state.
- Next instruction from interpreter state: `(r15 − 2) & ~1` (THUMB),
  `(r15 − 4) & ~3` (ARM).
- **Guest r15 is never cached in a host register** — every PC use is a
  compile-time constant.
- **Loads/stores that write r15** change mode on ARM9 (T bit from bit 0 of the
  value), cost 5 cycles, and must end the block.

**Memory.**
- Fast paths mirror `Memory::read<T>`/`write<T>`: same page tables (the ARM9 has
  **two** read maps, TCM and non-TCM), same in-page mask
  `address & (0x1000 − sizeof(T))` — a mask, not a wrap.
- Little-endian guest on big-endian host: `lhbrx`/`lwbrx`/`sthbrx`/`stwbrx`;
  bytes `lbzx`/`stbx`.
- **Misalignment is ISA behaviour, not an error:** word loads rotate by
  `(addr & 3) << 3` on **both** CPUs; half loads rotate by 8
  (`(v << 24) | (v >> 8)`) on the **ARM7 only**; `LDRSH` sign-extends first, then
  arithmetic-shifts right 8. Emit in the fast path.
- Everything not plainly mapped → C++ helper calling the same
  `Memory::read/write<T>`.
- **Stores always end the block** (can raise an interrupt, halt, start DMA,
  reschedule a task).
- **Slow loads may stay in the block**, but the helper must detect a side effect
  (task due before this run's deadline, `halted`, pending enabled interrupt) and
  end the block if one occurred.
- `li` is `addi rD, 0, imm` → **r0 reserved as literal zero**, keep out of the
  scratch set.

**Cycles.**
- Handlers return exact counts; sum statically. Multiply ramp
  `1 + ((a >> 8) != 0) + ((a >> 16) != 0) + ((a >> 24) != 0)` — compute inline,
  don't end the block.
- **Two clock domains:** `Interpreter::cycles` is in CPU units (global for ARM9/
  GBA, **twice** global for ARM7); `Core::globalCycles` + event queue are global.
  Convert exactly where the interpreter's run loops do.
- Blocks check the budget **before entering**; a block whose worst case doesn't
  fit must not start. Running one opcode through the interpreter reproduces its
  own overshoot granularity.

**Modes, banked registers, HLE.**
- `registers[i]` are **pointers**; a mode change swaps them. **S-form ALU with
  Rd == r15 is an interpret exit when `spsr` is non-null** — that is the mode
  switch; in user mode it's a plain dynamic-PC exit.
- End the block on: I/O-space writes, `SWI`, `MCR`/`MRC`, any CPSR mode/I-bit
  change, `halted` set, any r15-writing load/store, any instruction whose exact
  semantics you haven't implemented.
- **HLE BIOS / ARM7 HLE** intercept at specific PCs — those addresses must end
  blocks or be checked at block entry.

**Self-modifying code.**
- Cheapest correct design: **one byte counter per guest 4 KB page**;
  `Memory::write<T>` tests it inline (one lookup + one compare; only pages that
  *currently hold code* are non-zero); the hook runs **before the write lands**,
  so stale code can never execute.
- A compiled block covers only its own page (stop the translator at page
  boundaries) → invalidation is exact: drop that page's bucket.
- **All DMA routes through `core->memory.read/write`** — one hook on
  `Memory::write` covers DMA, cartridge→RAM loads, fill registers. Don't hunt
  for a separate DMA path.
- `updateMap9`, `updateMap7`, `updateVram`, reset, ROM load, save-state load —
  all flush everything.

---

## 6. Design constraints **[GIVEN]**

- **Register contract:** guest r0–r14 → host r14–r28, CPSR r29, `JitContext*`
  r30, `Interpreter*` r31; r3–r12 scratch; r0 reserved; r1/r2/r13 never touched.
- **Exit model:** block ends by branching to one shared epilogue with
  `r3` = cycles, `r11` = resume address (pipeline bias removed), `r12` = state;
  epilogue spills, folds cycles, checks halt + budget, chains inline or returns
  to C++. Slow memory paths jump to a *second* entry of the same epilogue (their
  helpers already set the exit fields).
- **Interpret-exit is the fallback mechanism** — "run one opcode in C++". Keeps
  HLE BIOS, DLDI, reserved opcodes, mode changes identical to a non-JIT build
  for free.
- **`JitContext` offsets** are literal macros with
  `static_assert(offsetof(...))`; the trampoline stringifies those macros.
  Never hand-write an offset.
- **Trampoline:** save/restore every nonvolatile host register used + LR + the
  whole CR (`mfcr`/`mtcr` around the block is simpler and provably correct);
  helper calls via `mtctr` + `bctrl`; never assume branch range.
- **Coherency after every code write/patch:** `DCFlushRange` (or
  `DCStoreRange`) **then** `ICInvalidateRange`, both 32-byte aligned.
- **Arena:** 32-byte aligned, cached MEM1/MEM2, one `Noods_MEM2_Alloc`, never
  grown; 4 MB NDS / 2 MB GBA; on overflow flush everything and restart.
- **Static scratch, not stack** — translation buffers are large; Wii thread
  stacks are shared with audio/GPU. File-scope statics (translation is
  single-threaded on the emulation thread).
- **Dispatch:** direct-mapped block table keyed by `(pc << 1) | thumb` + an
  inline one-entry cache in the context for chaining; linked entries revocable
  by key on invalidation — never create a patch you cannot find again.

---

## 7. Dead ends — do not spend time here **[PICKUP]**

- `dkp-pacman` / `pkg.devkitpro.org` / `apt.devkitpro.org` /
  `download.devkitpro.org` (403 IP-blocked) — use the Docker Hub workaround (§0.1).
- Hunting a `tuxedo` repo (it's upstream libogc); relying on CI (wrong branch);
  git blame archaeology (bulk uploads).
- Parsing `objdump --no-show-raw-insn`; inline-asm flag probes with tied
  constraints (§3).
- `addo`/`subfo`, `mcrxr`, the "carry inversion XOR"; trusting `slw`/`srw`/
  `rlwnm` for ARM's shift-by-32 corners (§5).
- Piecing THUMB decoding from the opcode diagram instead of the interpreter's
  table (§4).
- Approximating an instruction instead of ending the block; growing the arena
  or freeing blocks individually.
- Launching coding agents interactively — no PTY/stdin in this sandbox; one-shot
  flags only (SKILL.md).

---

## 8. Working agreements

- One logical change per commit; encoder oracle green at every step;
  `make clean` after every header edit — every time.
- JIT diagnostics → `sd:/noods/jit_log.txt` (append, `fflush` per line); never
  per-instruction in release builds.
- Status reports in three buckets: **verified** (a tool/test behind it),
  **written but unverified**, **not started** — never blend them.
- Agent output routes through feature branches/PRs; **no secrets in prompts,
  args, or process I/O**; no global tool installs without user confirmation;
  registry-only package installs.

**Agent operation: follow SKILL.md** (provided with this file). Project-specific
additions on top of it: every spawned-agent prompt starts with *"Read
/home/user/AGENTS.md first, source /home/user/wii-env.sh, and verify your work
by building boot.dol"*, is fully self-contained (context, edit approval,
definition of done), and ends by echoing a `BUILD_OK` / `BUILD_FAIL` marker so
runs can be waited on.

---

## 9. Performance

- Baseline to beat (README): interpreter **GBA 10–35 fps, NDS 3–15 fps**.
  Measure before/after with the same ROM and scene.
- **Profile before optimising** — on NDS `gpu_3d_renderer`/`gpu_2d` often
  dominate; the JIT won't fix a renderer-bound game.
- Yield with a quota (VBA-GX does) so the event queue stays prompt.

---

## Appendix A — cheat sheet (this workspace)

```sh
# --- bootstrap (per fresh instance — /opt is not snapshotted) ---
# devkitPPC: sanctioned path only (NEVER dkp-pacman)
bash /home/user/dkp-work/fetch-layers.sh && bash /home/user/dkp-work/extract-layers.sh \
  && bash /home/user/dkp-work/install.sh        # or the manual Docker Hub steps, §0.1
# verification stack
apt-get install -y gcc-powerpc-linux-gnu binutils-powerpc-linux-gnu qemu-user-static

# --- every new shell ---
source /home/user/wii-env.sh && powerpc-eabi-gcc --version   # expect devkitPPC 16.x

# --- build (done = valid boot.dol, ELF entry 0x80003f00) ---
source /home/user/wii-env.sh && make clean && make

# --- verification ---
bash tools/run_jit_asm_oracle.sh                 # must stay green
qemu-ppc-static build/<probe>                    # §3.2 semantics probes

# --- agents (one-shot only — see SKILL.md) ---
codex exec --full-auto "Read /home/user/AGENTS.md first. source /home/user/wii-env.sh.
  <self-contained task>. Done when: boot.dol builds. End by echoing BUILD_OK or BUILD_FAIL."
```
