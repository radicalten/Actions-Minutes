# AGENTS.md — NooDS-Wii ARMv4/v5 → PowerPC (GCN/Wii) dynamic recompiler

> **Optimized edit — what changed and why**
> This revision keeps every fact, number, hash, command and code listing from the
> source document **verbatim** (nothing technical was removed or altered). What changed is
> organization and verbosity:
> 1. **§0's acceptance list now includes G1** (the encoder gate) — the original jumped
>    G0 → G2 in prose while G1 still appeared in the §11 evidence table, which read as a
>    gap. It is now listed consistently everywhere.
> 2. **§14's 21 subsections each repeated a near-identical "this is the exact file used…"
>    sentence.** That sentence is now the single quick-reference table at the top of §14;
>    each subsection carries only its verdict badge and caveat, and the source itself is
>    collapsed by default so the page is scannable instead of a 2,000-line scroll.
> 3. Added this changelog, a one-screen **Document Map**, and (in this viewer) a
>    **Quick Reference** tab with the gates, the trap catalogue, and an interactive
>    order-of-work checklist, so the highest-traffic lookups don't require reading prose.
>
> Nothing here relaxes a gate, loosens a non-negotiable, or edits a hash/flag/command.
> If you only read one thing, read §0.

## Document map

| Part | Sections | What it's for |
|---|---|---|
| **Contract** | §0 | Mission, acceptance criteria (G0–G4 + deliverables), non-negotiables |
| **The tree & environment** | §1–§3 | Pinned revision, devkitPPC bootstrap, the dual-variant Makefile |
| **Execution & design contract** | §4–§6 | What the JIT must preserve, the cache/pool/ABI/flags design, the verifier and its blind spot |
| **Method & rig** | §7–§9 | Performance reality up front, ROMs/SD image/rig keys, the Dolphin launcher |
| **Evidence automation** | §10–§11 | The GBA suite driver (G3's oracle), gates/evidence/deliverables shapes |
| **Reference** | §12–§13 | Trap catalogue (read before trusting a result), order of work with budgets |
| **Appendix** | §14 | Every verbatim tool from the prior bring-up — copy, don't rewrite |

**Definitions.** *Verified* = confirmed on the pinned revision (§1) with the toolchain (§2).
*Expect* = a prior measurement, not a guarantee — re-run the gate before citing a number;
every number in this file is a result, to be reproduced, not taken on faith.

---

## 0. Mission, acceptance criteria, non-negotiables

Deliver a **real dynamic recompiler**: PowerPC instructions emitted at run time into a
Writable/Executable region and executed natively. Renaming or wrapping the interpreter is
not a JIT and fails the task. The interpreter stays in the tree as the **fallback** for
every opcode the emitter doesn't support, and as the **reference** the JIT is judged
against — same source, same flags, differing only in `-DNOODS_JIT=0|1`.

### Acceptance criteria (checked in this order — see §11 for evidence shapes)

1. **G0 — build/provenance.** `make JIT=1` and `make JIT=0` both link; the `.S` bridge is
   assembled and linked into both; a clean rebuild reproduces the delivered bytes; hashes
   recorded.
2. **G1 — encoder.** Every PowerPC form the emitter uses is backed by an assembler oracle
   (`powerpc-eabi-as` + `objdump`, §14.19) and by in-situ stub disassembly (`stubdump=N`).
3. **G2 — accuracy.** `rockwrestler.nds` (45 frames) and `suite.gba` (50 / 60 / 120 frames —
   120 spans **two code-pool wraps**): **cycles 0, state 0, dumps 0** mismatches against the
   reference interpreter, and RAM/framebuffer/screen dumps byte-identical.
4. **G3 — suite.** The mGBA suite's own verdicts for **all 14 groups**, driven by real key
   input: **identical ordered verdict lines** in both builds (13 groups) plus byte-identical
   final screens (14/14). The suite's baseline FAIL rows are expected and must be identical
   in both builds — a *difference* is the regression, not the FAIL itself.
5. **G4 — performance.** Honest host ticks/frame for both builds on the same workload, with
   the direction stated plainly. (A first-correct per-instruction JIT **is slower** — §7.)
6. **Deliverables.** `deliverables/jit.dol` (exact tested bytes, sha256 in
   `deliverables/SHA256SUMS`), the reference DOL, `handoff.md`, and `evidence/`.

### Non-negotiables

- Report **PASS / FAIL / HANG / NOT_RUN** honestly; keep failed attempts; never invent a pass.
- Feature branch only — never push main/production/release; preserve existing work.
- No unrequested roadmap work: lazy flags, block linking, idle detection, a growable pool,
  verifier hardening all belong in `handoff.md` as *next steps*, not smuggled into this pass.
- `-mrvl` proves a **Wii** target. Never claim GameCube or physical-console success — every
  number here is a Dolphin observation.
- Dolphin runs are serial and short (≈2 vCPU, ≲2 GiB RAM, ≪25 min each); one writer at a
  time. Never run two Dolphin instances to "save time" — the rig serializes on purpose.
- Only `/home/user` persists (snapshot cap ≈128 MiB unless you prune — §11).

---

## 1. The tree as it actually is

```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii      # only if absent
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

If the delivered workspace has **no `.git`**, the pinned id above is your commit id, and
`handoff.md` must say the tree was not re-verified against git.

**Layout.** Repo root holds `Makefile`; **sources live in `NooDS-Wii/`**; build products
land next to the Makefile (`jit.dol`, `NooDS-Wii-interp.dol`, `obj-jit0/1`). There is no
JIT, no rig, no `tools/` in a fresh clone — you build all of it.

**Relevant files:** `interpreter.{h,cpp}` (class, dispatch, pipeline), `interpreter_alu.cpp`
(ALU + THUMB data processing), `interpreter_branch.cpp`, `interpreter_transfer.cpp`,
`interpreter_lookup.cpp` (the dispatch tables); `core.{h,cpp}`, `memory.{h,cpp}` (`readMap*`
fast paths, `read8/16/32`, `write*`, DMA/CP15 remaps), `main.cpp`, `settings.*`, `gpu.*`,
`dma.cpp`, `input.*`, `save_states.*`.

### Dispatch (verified)

Private static member-pointer tables `armInstrs[0x1000]` and `thumbInstrs[0x400]` in
`interpreter_lookup.cpp`; indexing:

```cpp
ARM  : (this->*armInstrs [((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
THUMB: (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
```

**Row map (verified).** ARM 0x00–0x1F data-processing *register* operand-2, 0x20–0x3F
*immediate*; 0x40–0x7FF single transfers; 0x800–0x8FF block transfers; 0xA00 `B`,
0xB00 `BL`, 0xC00/0xE00 CDP/MCR/MRC (mostly `unkArm`), 0xF00 `swi`. MRS/MSR/multiply/
BX/CLZ sit inside 0x00–0x3F (0x009/0x019/0x029/0x039 = mul/muls/mla/mlas; 0x121 `bx`,
0x123 `blxReg`, 0x161 `clz`). `B`/`BL` mask `(op & 0x0E000000) == 0x0A000000` also matches
ARMv5 `BLX(imm)` — reject `op>>28 == 0xF` first. THUMB ranges: 0x000–0x05F shifts,
0x060–0x07F add/sub reg+imm3, 0x080–0x0FF mov/cmp/add/sub imm8, 0x100–0x10F format-4 ALU
(2–4,7 = register shifts; 9 = NEG; 13 = MUL), 0x110–0x11F hi-reg + BX/BLX, 0x140–0x17F
L/S reg offset, 0x180–0x27F L/S imm5/SP-relative, 0x2D0 PUSH, 0x2F0 POP, 0x300 STMIA,
0x320 LDMIA, 0x340–0x37F conditional B, 0x380 B, 0x3A0 BLX, 0x3C0/0x3E0 BL; rest `unkThumb`.

**Handler names** (grep these literally — there is no `FORCE_INLINE` layer). ALU helpers
are the operand-2 functions `lli/llr/lri/lrr/ari/arr/rri/rrr/imm` (+`S` variants), expanded
by `ALU_FUNCS(func,S)` into `func{Lli,...,Imm}` (e.g. `addLri`, `andsAri`, `subImm`), plus
explicit `mul/mla/muls/mlas`, `bx/blxReg/b`, and the THUMB set (`lslImmT…mvntDpT`,
`addHT/cmpHT/movHT`, `bxRegT/blxRegT`, `ldrPcT/strImm5T…`, `pushT/popT`, `beqT…bleT`,
`bT/blSetupT/blOffT/blxOffT/swiT/unkThumb`).

**PC/pipeline convention (verified — do not copy PC math from memory).** `runOpcode()`
pops `pipeline[0]`, then advances `*registers[15] += size` (ARM 4 / THUMB 2) *before*
calling the handler. So at handler entry r15 = instruction + 2×size (ARM +8, THUMB +4);
between instructions r15 = next + size. Consequences: the cache key is `r15 − 2×size`
(any consistent bijection works, **always mix the T bit in**); ARM `BL` return = r15 − 4;
THUMB `blSetupT/blOffT` set bit 0 on the return address; `flushPipeline()` aligns the raw
target (`& ~0x3` / `& ~0x1`), adds one size, refills both slots (with the `pcData` fast
path); any stub that changes PC must call the **real** `flushPipeline()`, not a copy.
`SWI`: PC −= 4 then `exception(0x08)`. S-write-to-r15 → `setCpsr(*spsr)`.

**Condition gate (verified).** `runOpcode` evaluates
`condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]` and returns 1 cycle for "false" or
calls `handleReserved` **before** any handler. Hook the JIT *after* that gate so cond-false
and reserved opcodes keep the reference cost and never allocate a stub.

**Private members.** `core`, `arm7`, `pcData`, `pipeline[2]`, `registers[32]` (array of
*pointers* — always go through `registers[n]`, banking can swap them), `registersUsr[]`,
banked arrays, `cpsr`, `spsr`, `cycles`, `runOpcode()`, `flushPipeline()`, `exception()`,
`setCpsr()`, `swapRegisters()`, `condition[]`. Add `friend class ArmJit;` or accessors —
never guess offsets.

**Scheduler / halting.** `Interpreter::run()` returns early on HALT and when
`updateRun()` says so (GBA: `runCoreNone` iff both halted, else `runCoreSingle<true,0>`;
NDS ARM9 ×2 / ARM7 ½ applied in the scheduler, never in the JIT). `halt(bit)` sets the halt
bit, schedules `UPDATE_RUN,0`, cycles = `0xFFFFFFFF`; `unhalt` clears and schedules 0.

**Frame boundary.** `Core::runCore()` is **not** one frame. Add an observer counter
incremented only in `Core::endFrame()` and loop until it changes before counting a frame,
applying frame-indexed input, or ending a measured frame. Verified increments:
NDS first frame 408 960 cycles then **560 190**; GBA first 197 120 then **280 896**
(= 228 × 308 × 4; a GBA frame ends in `Gpu::gbaScanline308()` at vCount 160).

---

## 2. Reproducible devkitPPC environment

```bash
sudo apt-get update          # FIRST; a stale apt is the #1 bootstrap failure
sudo apt-get install -y --no-install-recommends dolphin-emu zstd xvfb mtools \
    binutils-arm-none-eabi
```

Only `/home/user` persists; `/opt` and apt packages can vanish — re-run the bootstrap.
Use only `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`, never
dkp-pacman). GCC alone is not enough: rules/crtls/newlib provide the specs and `rvl.ld`.
Save the manifest as `tools/toolchain-packages.sha256` and verify every download
(devkitPPC-r50, binutils 2.46.0, crtls 2.1.0, gcc 16.1.0, newlib 4.6.0, rules 1.2.1,
gamecube-tools 1.0.7, libfat-ogc 2.1.0, libogc 3.1.0) — digests in §14.2.

`tools/setup-devkitppc.sh` (idempotent): download to `tmp/dkp/`, `sha256sum -c`, extract
`opt/*` to `/` with `sudo tar --zstd -xf`. Two traps: **do not** use `grep -q` on the
tar listing (pipefail + SIGPIPE can falsely reject an archive), and `devkitPPC-r50` is
metadata-only — skip archives with no `opt/` payload. Verify:

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-gcc --version     # GCC 16.1.0
command -v elf2dol             # /opt/devkitpro/tools/bin/elf2dol
```

Desired tooling: `powerpc-eabi-objdump`, `arm-none-eabi-objdump` (note: `-m thumb` is
**unsupported**; use `-m arm -M force-thumb` to disassemble THUMB), `mtools`, `zstd`.

---

## 3. The Makefile (new; the stock one is replaced)

- `make JIT=1 -j4` → `jit.elf`/`jit.dol`; `make JIT=0 -j4` → `NooDS-Wii-interp.elf/.dol`;
  `JIT ?= 1`. `.SUFFIXES:` first so the built-in `.S` rule can never fire.
- Separate object trees `obj-jit1`/`obj-jit0`, `-MMD -MP`, and a **flags stamp** file so a
  flags change can never silently reuse stale objects.
- `.S` compiled with the cross GCC as `-x assembler-with-cpp`; **the resulting object is
  linked into both variants** (an unused bridge must not break `JIT=0`).
- Link with the cross G++, then run `elf2dol` explicitly; keep both `.map` files.
- Proven flags: `-mrvl -mcpu=750 -meabi -mhard-float`; C++
  `-O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections`;
  CPP `-DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1 -DLOG_LEVEL=3` + libogc/portlibs/project
  includes; link `--gc-sections` with `-L/opt/devkitpro/libogc/lib/wii -lasnd
  -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm` (exactly this set — it is the set the
  working tree links and it resolves against the §2 packages).
- **devkitPPC predefines `PPC=1`.** Name the encoder namespace `JitPpc`, never `PPC`. A
  host-only harness will not see the collision — always cross-compile.
- A clean rebuild (`rm -rf obj-jit0 obj-jit1 *.dol` + both makes, ≈35 s) must reproduce the
  delivered bytes exactly; record that in `handoff.md`.
- Adding an `OPT=` knob is fine, but **`-O3` does not link this tree**:
  `undefined reference to Memory::writeFallback<unsigned char>(…)` from `action_replay.o`
  and `core.o`. If you want it, add the missing out-of-line instantiation to `memory.h`;
  otherwise leave `-O2` and say so in the handoff.
- Record revision, dirty state, flags and DOL sha256 before every run (`run-info.txt`).

---

## 4. Execution contract (what the JIT must not change)

- Keep `runOpcode()` semantics; do not copy PC math from anywhere but §1.
- Share the fetch/condition prologue; on fallback call the original handler **without
  fetching or advancing twice**. Implement `Interpreter::runDecoded(uint32_t)` that only
  does the table lookup (caller has already passed the condition gate).
- Fallback returns the handler's **actual** cycle cost, never a guessed constant.
- **One guest instruction = one native stub** is the schedule-equivalent unit. A stub that
  retires several guest instructions breaks "one CPU cannot run ahead"; block translation
  is a *design change* (§7), not a free optimisation.
- `flushPipeline()` stays the only PC-refill path. Save/load-state, banking, exceptions and
  HLE need no JIT parity at first, but a JIT run must never serialise host pointers.
- Guard the whole JIT behind the runtime layout check (§5); a miss degrades to the
  interpreter, never to corrupt state.
- A JIT-enabled build must produce **byte-identical guest state** to `JIT=0` for the same
  ROM/config/frames: same registers/banks, CPSR/SPSR, halt bits, cycle totals, scheduler
  event queue head, framebuffer, RAM dumps.

---

## 5. JIT design contract (the shape that works)

- **Cache:** direct-mapped, 8192 entries/CPU, key = `guest PC | T-bit`, plus the prefetched
  **opcode compared on every dispatch** (this is what makes stores/DMA/CP15 remaps safe with
  no write protection). A serial epoch starts non-zero so a zeroed entry can never match;
  bump epoch + clear tags on pool reuse/reset/toggle. Unsupported opcodes are cached as
  **negative** entries and run `runDecoded`. *(Measured: 2048/8192/32768 entries gave
  identical counters and ticks on the suite — do not spend time tuning this.)*
- **Pool:** one 4 MiB `.bss` word array shared by both CPUs; cap each stub (the working
  emitter used ≤160 words) and *fail compilation* on a runaway generator, never overrun.
  Flush a new stub's lines before execution: `dcbst` per line, `sync`, `icbi` per line,
  `sync`, `isync` — a d-cache-only flush leaves the CPU executing stale icache lines
  (first symptom: SIGILL inside the pool).
- **Pool wrap / reuse:** when the pool offset wraps, invalidate the **entire pool region**,
  not just the new stub. After a wrap the region holds *prior* translations and a surviving
  block runs against rewritten bytes; the symptom is the guest silently stalling at the
  first wrap while guest state is provably identical to the interpreter. Same code path is
  needed in `reset()`.
- **Bridge ABI:** `int jit_enter(void *code, Interpreter *cpu)` (r3/r4) saves caller LR at
  4(old SP), `stwu` 32 bytes, saves r31 at 28(new SP), r31 = cpu, `mtctr`/`bctr`.
  `jit_return` restores and `blr`s. Stubs set r3 = cycle cost and tail to `jit_return`;
  never return through a helper's LR. Helpers call through a trampoline that opens its own
  32-byte parameter area. Scratch r3–r12; never touch r2/r13/r14–r30; never use r0 as a
  D-form base.
- **Interpreter layout:** `ArmJit` is a member *after* the other fields; `attach()` runs
  from the `Interpreter` constructor and derives offsets from a live instance
  (`(uintptr_t)&c->registers[0] - (uintptr_t)c`). Reject any offset ≥ 32768 (signed D-form).
  Verified on this tree: `registers=32 cpsr=284 pipeline=24` — but **re-derive; never copy
  the numbers**, and print the layout line at every attach. The single most expensive bug
  of the previous bring-up was `attach()` never being called: offsets stayed 0 and the JIT
  wrote into the wrong addresses.
- **NZCV mapping.** PPC `rlwinm` rotates left; MB/ME are PPC MSB numbers:

| Guest bit | Source | LSB src → dst | SH | MB=ME |
|---|---|---|---:|---:|
| N (31) | CR0 LT (mfcr bit 31) | 31 → 31 | 0 | 0 |
| Z (30) | CR0 EQ (mfcr bit 29) | 29 → 30 | 1 | 1 |
| C (29) | XER CA (bit 29) | 29 → 29 | 0 | 2 |
| V (28) | XER OV (bit 30) | 30 → 28 | 30 | 3 |

  Merge with `rlwimi cpsr, flags, 0, 0, 3`; preserve CPSR bits 27–0 (Q, control, mode)
  exactly. Harvest `mfcr`/`mfxer` **immediately**, before any other compare. Seed XER[CA]
  from guest C before ADC/SBC; clear XER SO/OV before arithmetic. Logical N/Z via
  `cmpwi r,0` + CR0. Record forms (`addco./subfco./addeo./subfeo.`) give ARM-identical
  CR0/XER behaviour. For condition tests, `mtcrf 0x80, cpsr` maps guest N/Z/C/V onto CR0
  LT/GT/EQ/SO — a **different** mapping from a compare's EQ→Z; never share one predicate
  helper between "test flags" and "compare result".
- **Encode-form facts that were all wrong once** (each produced silent corruption; see §12):
  only `addc`/`subfc`/`adde`/`subfe` write XER[CA] — `addco` XO **10**, `subfco` XO **8**;
  `BO_BIT_SET`/`CLEAR` are not interchangeable (`beq` BO 12, `bne` BO 4); `bc` skip
  displacement is relative to the branch itself (12 bytes, not 8); branch stubs write the
  **raw** PC then call `flushPipeline()`; non-`AL` stubs need the runtime condition gate;
  GE = `creqv`, LT = `crxor`, LS = `crnor(crandc(C,Z))`; a zero shift amount leaves C
  untouched; r15 materialisation needs a full 32-bit load (`lis/ori`, `li32`), never a
  16-bit truncation (invisible on NDS, fatal on GBA).
- **Conservative first inventory** (proven): ARM data-processing with immediate or
  **immediate-shift** operand (Rd ≠ r15); ARM9 `MUL/MLA` and `MULS/MLAS` (ARM7 timing is
  operand-dependent → fallback); `B`, `BL`, `BX`, `BLX(reg)`; THUMB imm shifts, ADD/SUB
  reg+imm3, MOV/CMP/ADD/SUB imm8, format-4 ALU minus NEG/register-shifts/MUL, hi-reg
  ADD/CMP/MOV, BX/BLX(reg), all 14 conditional B, B, BL/BLX long. Everything else —
  loads/stores, LDM/STM/PUSH/POP, SWI, CP15, PC-writing ALU, register shifts, RRX, ARM7
  multiplies, THUMB NEG — is a negative entry. Expand only while the verifier stays silent.
- **Memory/transfers (next inventory step, not the first cut):** delegate MMIO, endianness,
  alignment and DMA side-effects to the existing `Memory` methods; never reimplement the map
  and never treat a guest address as a host pointer. Word LDR rotation is `(addr & 3) * 8`
  with the shift-zero case guarded. A stored ARM r15 is handler-PC + 4. ARM9 high BIOS
  addresses (≥0x80000000) are legitimate — a guard may deopt, it must not declare the PC
  corrupt.
- **Do not translate an op whose flag semantics you have not reproduced literally** from the
  handler — but note that the *handler* can be wrong: see §6's interpreter-defect rule.

---

## 6. The differential verifier — and its blind spot

Snapshot `registers[0..15]`, cpsr, spsr, pipeline[0..1], pcData; run the stub; snapshot
again; restore; run `runDecoded`; compare. **Keep the interpreter's result** and log the
first N mismatches (PC, opcode, expected vs got). Rig keys: `verify=0` off, `verify=1`
(default: first execution of every newly compiled stub **plus** any dispatch the level
allows), `verify=2` (every dispatch; expensive).

The blind spot, learned the hard way: a stub executed **once** from the current state can
agree with the interpreter while the *interpreter* is wrong — and the divergence then shows
up hundreds of frames later in guest state, not in `mismatch` counters. In the previous
bring-up `verify=1` read **0 mismatches for 441 frames** while the carry group of the suite
proved the two builds diverged.

**Rules:**

- `verify=1` is a **sampler, not a proof**. `verify=2` is the setting that closes it
  (measured ≈3.5× on the JIT build: ~3.05 M vs ~0.87 M ticks/frame on suite 50 frames).
- For **flag/carry-dependent** work, also do an end-to-end run: the ROM's own test suite
  (G3) is the oracle that settles it.
- **Rule for a JIT-vs-interpreter flag mismatch: inspect the interpreter handler's CPSR
  mask first.** The previous bring-up's `rscs` masked `cpsr & ~0xC0000000` where every other
  flag-writing handler (`subs/rsbs/adds/adcs/sbcs/cmp/cmn`) masks `~0xF0000000`, so RSC's
  C/V were OR-ed onto stale bits; the JIT (`subfeo` + XER seed) was right. Symptom pattern:
  interpreter-only `rscs: Got X … (CSPR n): FAIL` lines. Do not "fix" the JIT.
- Verifier traps: compare **value copies** taken before/after each op (not pointers); make
  the fallback filter directory-then-index; a stub whose missing branch is never taken will
  pass forever.

---

## 7. Method, and the performance truth up front

Order of work: **(1)** rig + interpreter baseline first — without a baseline you cannot tell
"JIT bug" from "ROM behaviour"; **(2)** the smallest JIT that executes (one ALU class +
bridge + `attach` + fallback), proven by **counters, not logs**; **(3)** the verifier on day
one; **(4)** expand inventory only while it is silent; **(5)** G2 accuracy; **(6)** G3 suite;
**(7)** performance.

**Expect the first-correct per-instruction JIT to be *slower* than the interpreter** and
plan for it instead of discovering it at G4. Measured on this rig: **1.12× (NDS 45f),
1.92× (GBA 50f), 1.99× (GBA 60f), 1.96× (GBA 120f)** interpreter-to-JIT (worse = larger).

Where the time actually goes (measured, so you do not repeat the search):

- The **verifier is not the cause**: `verify=0` vs `verify=1` changed the median by <0.2 %.
- The **stub bodies are tiny** (a `MOV r0,#imm` stub is 6 PPC words; the bridge is 12
  instructions) — the cost is *per dispatch*, ≈**54 host cycles** (≈16 ticks) per guest
  instruction: the cross-TU `jit_enter`/`jit_return` pair, the cache probe load, executing
  out of a 4 MiB pool (working set far larger than the interpreter's text), and ~41 % of GBA
  dispatches still going back through C++.
- Tuning the JIT's own tables buys nothing (`CACHE_SIZE` 2048/8192/32768 identical;
  `POOL_WORDS` 512K only changes how often the wrap spike lands: ≈+155 k ticks per wrap vs
  ≈+214 k with 4 MiB, i.e. ≈97 k fixed + ≈29 k per MiB of pool, ≈0.5 % of a 120-frame run).
- `-O3` does not link (§3). LTO is unproven.

Therefore the levers, in order (all roadmap — document, do not smuggle in):

1. **basic-block translation** — one entry/exit per *block*, guest registers and CPSR held
   in host registers across it. This is the only item that attacks the per-dispatch constant;
2. **block linking / branch following** — needs the invalidation design the per-dispatch
   opcode check currently provides for free;
3. **lazy flags / flags in CR0+XER** — removes the CPSR materialisation from the hot path;
4. **a growable MEM2 pool** — last; measured cost today is ≈0.5 %;
5. optionally widen the native inventory (loads/stores, PUSH/POP) so fewer dispatches pay
   the double dispatch — but that means emitting the banking/MMIO logic, so it is not first.

Report **both** numbers: guest cycles/frame (scheduling equivalence, must match the
interpreter exactly) and host ticks/frame (performance). Ticks are Dolphin observations,
never physical Wii speed. `PPCGetTickCount()` deltas around the whole frame loop are
deterministic per workload and reproducible to ±0.2 % — quote mean **and** median, and call
out the known spikes (frame 2/3 init, +638 k; the frame after a wrap, +214 k).

---

## 8. ROMs, the SD image, the rig contract

```bash
cd /home/user; mkdir -p nds gba evidence tmp tools
curl -fL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -fL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite.gba
printf '%s\n' \
 'f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  nds/rockwrestler.nds' \
 '8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035  gba/suite.gba' \
 | sha256sum -c -
```

`rockwrestler.nds` is 39 433 B, `suite.gba` 524 288 B. Re-verify before every run — the
source repositories are context, not identity.

**SD image** (fresh per run, never reused): 128 MiB raw, MBR partition 1 at 1 MiB, type
0x0E, FAT16, files under `::/noods/`. Write the MBR in Python; `mformat -i IMG@@1M` (no
`-F`, which forces FAT32); stage with mtools (no mount). Stage the ROM, `autoboot.txt`,
optional `input.txt`.

**Rig contract** (implement in the DUT; inert when `sd:/noods/autoboot.txt` is absent so the
normal UI still works). Keys, one per line:
`path`, `jit=0|1`, `frames=N`, `log=1`, `trace`, `verify=0|1|2`, `stubdump=N`, `id=…`,
`dump=f[,f]`, `hb=N` (heartbeat), `scanhb=N` (scanline heartbeat), `jitfreeze=N`,
`autodump=N`, `suite=N`.
Input file: `FRAME down|up KEY[,KEY…]`, keys `A,B,SELECT,START,RIGHT,LEFT,UP,DOWN,R,L,X,Y`.
Output: `sd:/noods/debug.log`, dumps in `sd:/noods/dump/`. Force direct boot, ROM-in-RAM,
and disable limiter/frameskip/threaded 2D+3D/audio/ARM7-HLE/DSi/filters in both variants;
record that config in every run.

**Rig behaviours that took work to get right — keep them:**

- **The log must be checkpointed, but never per line.** libfat commits a file's size only on
  close/sync. `fclose` + `fopen(...,"a")` every 5 frames (and frames ≤2) is the durability
  mechanism; doing it per console line costs ≈0.4 s/line through Dolphin's SD emulation and
  makes the suite unusable (~10× slowdown). `hb=N` forces a checkpoint every N frames and is
  the thing to use when you need a tail; per-frame `state:`/heartbeat lines are what survive
  a kill.
- **Each frame's log line** is `frame: n=%u runTicks=%llu ndsCycles=%u fps=%d`; add
  `state: n=… hash=… gcyc=… h0=…/… h1=…` (per-CPU FNV-1a over registers/banks/pipeline/
  cpsr/spsr/halt/cycles + JIT counters) — the hash is what makes a divergence bisectable
  from the log alone. A per-frame `state:` line pair is the primary G2 evidence.
- **Dumps:** `dump=f[,f]` writes `state-N.txt`, `fb-N.bin` (always), `ram-N.bin`, and — in
  GBA mode — `screen-N.bin` (240×160 `uint32` RGB8 pixels).
- **`Gpu::getFrame()` may be called only once per frame** (it advances an internal buffer).
  A merged `dumpFrameAndScreen()` is the single per-frame caller; a second caller produces
  empty/zero screens. Do not add one.
- **GBA console shim (the G3 oracle):** implement the mGBA GamePak debug console in the
  memory path — `0x04FFF600` text, `0x04FFF700` control (bit0 = line, bit1 = block string),
  `0x04FFF780` handshake that reads `0xC0DE` until written — and emit each completed line as
  `gbaout: <line>` in the rig log. This is how the suite's own PASS/FAIL verdicts become
  machine-readable text; no OCR, no guessing. Keep `gbadbg:` read traces separate.

---

## 9. Dolphin launcher and its file-format traps

- Unique `RUN_ID`; refuse to overwrite; `flock` serialise; close the lock FD in the child
  (`9>&-`) so the timeout/xvfb children cannot leak it.
- Isolated `tmp/dolruns/RUN_ID/user/{Config,Load}`; copy the exact DUT; hash DUT/ROM/config
  before launch; record commit + dirty state in `run-info.txt`.
- `Dolphin.ini`: `CPUCore=1`, `CPUThread=False`, `EmulationSpeed=0.0`, `DSPHLE=True`,
  `SyncGPU=True`, `DeterministicGPUThread=True`, `EnableCheats=False`, **plus
  `WiiSDCard=True`, `WiiSDCardPath=<run>/user/Load/WiiSD.raw`, `WiiSDCardAllowWrites=True`,
  `SDCard=True`, `SDCardPath=<that path>`** — without these Dolphin mounts no card and the
  DUT sees no `sd:/`. `GFX.ini`: OGL, `InternalResolution=1`, `MSAA=1`, VSync/filtering off.
- Proven launch (keep every backslash):
  `LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a -s '-screen 0 640x480x24'`
  `timeout --signal=TERM --kill-after=10s <SECS>s /usr/games/dolphin-emu-nogui -u "$RUN/user"`
  `-p x11 -v OGL -a HLE -e "$RUN/dut.dol" 9>&- > "$RUN/dolphin.log" 2>&1`
- `DOLPHIN_CPUCORE` (default 1) lets you re-run the same DUT on Dolphin's pure interpreter
  (`CPUCore=0`). **That is the reference for "is this a DUT bug or a Dolphin one?"** —
  a JIT that only misbehaves on block-caching cores points at your icache/pool invalidation,
  not at guest semantics.
- Every run of the shipped DUT ended with `dolphin exit=132` (SIGILL) after the DUT's own
  `exit(0)` — that is this rig's normal termination, both variants, not a crash.
- Always pull artifacts from the **run-local** `user/Load/WiiSD.raw`, never the staging image.
  `mcopy` cannot glob inside an image: enumerate with `mdir -i IMG@@1M -b ::/noods/dump` and
  copy each name. **`-w` silently loses entries — do not use it.** A large `mcopy` right
  after process exit can fail; read the live image instead. Missing/empty artifacts are
  NOT_RUN, never "identical".
- After a kill, libfat's uncommitted tail is still physically inside the run-local
  `.raw` and can be read with `strings` — that is how a stall's last breadcrumbs were
  recovered. Prefer reading the live image over waiting for a clean exit.
- Budgets: interpreter GBA 120 frames ≈ 3 s wall; JIT ≈ 13 s; NDS 45 frames ≈ 5 s; a full
  14-group suite sweep ≈ 8 min per variant. A timeout is a signal (preserve the partial
  log), not an invitation to raise the limit.
- ALSA "no card" warnings appear despite Null audio and do not prove failure.
  `AutoModellist.txt` warnings are normal.

---

## 10. GBA suite automation (this is the strongest accuracy evidence you will get)

`suite.gba` produces its verdicts **inside the ROM**: it probes for the mGBA console (§8)
and logs `…: Got X vs Y: FAIL` / bare test names. Driving it with real keys gives you an
in-ROM oracle for free.

- **Menu navigation (verified from screen dumps): DOWN moves the cursor one row, A runs the
  selected group, RIGHT and LEFT do nothing on this menu.** (Folklore about `+16`
  right-paging applies to the *result* list, not the menu; do not encode it into the menu
  driver.) So: fresh boot → `DOWN × group_index` → `A`, pressing ~40 frames apart,
  held ~4 frames, first press ≈frame 200.
- **One boot per group**, same frames for both variants. Frame budget: 1400 default,
  `timing` 1000, `dma` 3000 (718 rows!), `sio-timing`/`video` 2000.
- Groups, by menu index: 0 memory, 1 io-read, 2 timing, 3 timers, 4 timer-irq, 5 shifter,
  6 carry, 7 multiply-long, 8 bios-math, 9 dma, 10 sio-read, 11 sio-timing, 12 misc-edge,
  13 video. The suite's own pinned test counts are 36/130/132/26/9/70/31/36/123/718/90/8/3/7
  — use them as a coverage check, not as a PASS criterion.
- **A group has finished when its result list is on screen**, not when it starts. Dump the
  final screen and check: the decoder (§14.15) turns it into text, and each group's list
  starts with that group's own tests.
- **Verdict comparison is parity, not correctness.** The acceptance criterion is: both
  builds' *ordered verdict lines* are identical (13 groups), plus byte-identical final
  screens (all 14). Known **baseline** failures that appear in both builds and are *not*
  regressions: the memory group's ROM tests (the rig boots the ROM from RAM), io-read
  (read-back of write-only-ish registers returns 0 in NooDS), timing calibration vs the
  suite's reference tables, sio timing timeouts. Attribute them explicitly.
- **The video group logs nothing** (it draws Actual/Expected). Its console output is the
  boot banner only; compare the dumped final screen byte-for-byte instead, and state
  plainly that comparison against the ROM's own expected images is **NOT_RUN** unless you
  build that oracle.
- Harden the runner: `rmtree` the run directory before each group and **abort (exit ≠ 0)
  unless `run-info.txt`'s `dol_sha256` matches the tree's DOL**. A stale run dir will
  happily produce "evidence" for a build you no longer have.
- Never compare a sweep while it is running — half-written files read as NOT_RUN or as a
  line-count difference. Wait for the sweep process to exit, then compare.
- Screen decoding (optional but it is how the menu was verified): `screen-N.bin` is
  `uint32` pixels; ink = `(px & 0xFFFFFF) < 0x800000`; glyph cells are 8×8 at (0,28),
  30×15 cells; key = the raw 64 characters of the cell (no separators) into a JSON map.
  **Extend the map only from screens you already decoded** — a "relabel by coordinates"
  patch once silently wrote into the wrong keys (all-`?` output).

---

## 11. Gates, evidence, deliverables

| Gate | Evidence shape (all on the delivered bytes) |
|---|---|
| G0 build/provenance | both variants build; `.S` object appears in the link line; clean rebuild reproduces both sha256s; a 20-frame run **of the file in `deliverables/` as the DUT** with `0/0/0` mismatches against the in-tree build |
| G1 encoder | `.S` reference sources + `powerpc-eabi-as` + `objdump` bytes for every emitted form; plus in-situ disassembly of real stubs (`stubdump=N`) |
| G2 accuracy | per cell: `compare-runs.py` output with explicit `MISMATCHES cycles=/state=/dumps=` counts; plus dump-level parity (every `ram`/`fb`/`screen` file byte-identical; `state-*.txt` may differ **only** in that build's own `jit:` counter lines) |
| G2b bisect | on any divergence: first differing frame from the `state:` hashes, then the guest PC/opcode and emitted stub. Never run long suites on a known-divergent build |
| G3 suite | `evidence/g3/<group>-{jit,interp}.txt` (ROM verdicts), a summary table with one line per group (`MATCH` or the first difference), plus final screens for all 14 groups and the coverage table |
| G4 performance | fresh pairs, same ROM/frames/settings; mean **and** median host ticks/frame; ratio stated as JIT÷interp with the direction spelled out; native/fallback/compiled counters per CPU |

CSV schema, one row per run: `run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,`
`status,notes,evidence`; status ∈ PASS/FAIL/SKIP/HANG/NOT_RUN. Keep superseded rows with a
`SUPERSEDED` note rather than deleting history.

`deliverables/`: `jit.dol` (+ `SHA256SUMS`), the reference `NooDS-Wii-interp.dol` (same
source, `-DNOODS_JIT=0`), `handoff.md`, and the headline compare files. `handoff.md` must
contain: status vs gates with the exact hashes; the native/fallback inventory; the exact
build/SD/run/pull commands; a hashed evidence index; the Dolphin-only performance table;
every defect found with its symptom → cause → fix; the verifier blind spot; the first
failing gate and the smallest next step; and an explicit "not verified" list (no physical
console, no GameCube build, no proprietary BIOS, no save/load JIT parity, video vs expected
images NOT_RUN).

`evidence/` must be self-contained but small: **prune run directories** (keep `debug.log`,
`digests.sha256`, `dump/`, `run-info.txt`; drop the 128 MiB `WiiSD.raw` copies and `user/`),
then `find . -type f ! -name SHA256SUMS | sort | xargs sha256sum > SHA256SUMS`. Twelve
pruned runs ≈ 35 MB, which fits the snapshot budget. Keep the only copy of anything you
cite; never delete evidence for a claim that is still in `handoff.md`.

---

## 12. Trap catalogue

> Also available as a searchable table in the **Quick Reference** tab of this viewer.

| ID | Symptom | Cause / smallest fix |
|---|---|---|
| T1 | JIT writes garbage / crashes only natively | `attach()` never ran, offsets 0; guard `valid && inRange`, dump the layout every attach |
| T2 | Interpretation despite `jit=1` | Check `jit.enabled` and the call site; believe counters, not intent |
| T3 | Guest stalls after a few instructions; only 1–2 dispatches | A wrong early stub; use the verifier at compile time, not longer runs |
| T4 | cmp/bne never terminates, PC drifts into data | Wrong CR0/XER harvest or rotate (§5 table); check the first-diff frame |
| T5 | NZ depends on scratch state | Comparing against r0 instead of `cmpwi r,0`; no compare inside the mfcr/mfxer live range |
| T6 | Wild PC/LR that looks like a register list | LDM/STM decoded as a branch; `B/BL` needs `(op & 0x0E000000) == 0x0A000000` **and** `op>>28 != 0xF` |
| T7 | SIGILL/stale code after any store or wrap | incomplete line flush (needs `dcbst`+`icbi`+`sync`+`isync`), or a stale key without the T bit |
| T8 | Registers corrupt after a helper call | Helper LR/stack misuse, no parameter area, r2/r13/CR2–4 clobbered |
| T9 | Cycle drift, data identical | Fallback returning a guessed cost, or double fetch/PC advance |
| T10 | ARM7/ARM9 budget wrong on NDS | The scheduler's ×2/>>1 applied twice, or applied inside the JIT |
| T11 | "Guest hang" from sampling | Sampling alias; use `endFrame` counters and full state snapshots |
| T12 | Compile errors on private members | `friend class ArmJit;` or an accessor; never guess offsets |
| T13 | Host harness green, target red | `PPC=1` macro collision / target ABI; always cross-compile |
| T14 | `sd:/` missing, no log at all | Dolphin SD keys absent (§9); verify by reading the image afterwards |
| T15 | Log file 0 bytes although the run happened | libfat commits size only on close; checkpoint every 5 frames and `exit(0)` |
| T16 | Pulled artifacts empty/missing | Globbing inside the image, or `mdir -w`; enumerate with `mdir -b`, reject missing data |
| T17 | `no such dol` | Relative DUT paths resolve under the wrong cwd; pass absolute paths |
| T18 | Next run refuses to start | Stale RUN_ID or a leaked lock FD; new ID, `9>&-` |
| T19 | Timeout with a still-progressing guest | Diverged/slow DUT or heavy SD I/O; keep the partial log, do not raise the limit blindly |
| T20 | Emitter overruns / falls through | Enforce the word budget and one exit per stub; fail compilation, never run garbage |
| T21 | Results identical but labels wrong | Menu pager semantics + truncated duplicate labels; audit IDs before believing anything |
| T22 | All video rows PASS | The list hardcodes `true`; the video verdict is pixels, not rows |
| T23 | `/opt` gone after a reset | Re-run `tools/setup-devkitppc.sh`; only `/home/user` persists |
| T24 | Full clean rebuild gives a different DOL | Non-deterministic input (paths/dates/flags stamp); pin flags, keep `__DATE__` out of the binary |
| T25 | The JIT build stops producing frames at ~frame 51 of the GBA suite | Pool wrap left prior translations live; invalidate the **whole** pool (same in `reset()`) |
| T26 | Interpreter-only flag failures (`rscs: … (CSPR n): FAIL`) | Interpreter handler masked `~0xC0000000` instead of `~0xF0000000`; check the reference's CPSR mask before blaming the JIT |
| T27 | `verify=1` says 0 mismatches while the ROM diverges | The stub ran once from a coincidentally-clean state; use `verify=2` or an end-to-end suite run |
| T28 | `run-dolphin` "refusing to overwrite" | Existing run dir; **`test-case.sh`** appends `-<VARIANT>` — an id already ending in the variant yields `…-jit-jit` and an empty compare |
| T29 | Evidence from an older build | Stale run dir: rmtree before the run **and** gate on `run-info.txt` `dol_sha256` |
| T30 | Sweep comparison reads NOT_RUN / line counts differ | Comparing while the sweep is still writing; wait for the process to exit |
| T31 | `…-jit-jit` directory, 0 console lines | Double variant suffix (T28) — check `ls tmp/dolruns` before believing a count |
| T32 | All-`???????` screen decode | Font-map keys must be the raw 64-character cell rows; never hand-edit by index guess |
| T33 | `screen-N.bin` empty/zero | A second `Gpu::getFrame()` caller (once per frame only — use the merged dump) |
| T34 | Suite "passes" but nothing ran | The group must reach its **result list**; check the final screen, not the boot screen |
| T35 | `-O3` link failure | Missing out-of-line `Memory::writeFallback<T>` instantiation; stay on `-O2` or add it |
| T36 | g3 numbers with a torn tail | Every run ends `dolphin exit=132` (normal) with libfat's tail uncommitted; use per-frame `state:` lines / `hb=N` |
| T37 | A compare whose direction nobody can read | `compare-runs.py` prints `ratio = B/A`; state the direction explicitly (`JIT ticks ÷ interp ticks`) in every table you publish |

---

## 13. Order of work, with rough budgets

> Also available as an interactive checklist in the **Quick Reference** tab of this viewer.

1. `apt-get update`, install the rig packages, run `setup-devkitppc.sh`, verify GCC/elf2dol.
   *(≈10 min)*
2. Install the tools **from §14** (`setup-devkitppc.sh`, `mksd.sh`, `run-dolphin.sh`,
   `test-case.sh`, the `Makefile`), then write the rig (`rig.{h,cpp}` + hooks; §8 is its
   contract). Build both variants; one 20-frame `suite.gba` run must print
   `frame:`/`state:` lines. *(≈2–3 h; §14 removes most of the tooling time)*
3. Baseline the interpreter on `suite.gba` 50/60/120 and `rockwrestler.nds` 45; save the
   per-frame `state:` hashes; this is the oracle everything else is compared against.
   *(≈30 min)*
4. Implement the encoder + bridge + cache + pool + one ALU class; prove native execution via
   counters; turn the verifier on; grow the inventory until G2 is `0/0/0`. *(the long stretch;
   expect most of the debugging to be encoder forms and flag math, §5)*
5. G2 matrix + dump parity + provenance runs. Then the GBA suite driver (`input.txt`
   schedule + `gbaout:` capture), both variants, compare. *(≈1 h of machine time)*
6. G4 ticks/frame table, `handoff.md`, `deliverables/` + `SHA256SUMS`, prune `evidence/`,
   present both. Then, only if the task asks, start §7's structural perf work.

Never leave the tree in a state where `JIT=1` runs but `JIT=0` does not build; both are
part of the deliverable, and the interpreter build is the reference for every claim.

---

## 14. The verbatim tools — copy these instead of rewriting them

Everything in this section is the **actual** file used in the bring-up that produced the
delivered build — not a sketch. It was re-verified end to end *after a full sandbox reset*
(Dolphin and devkitPPC both gone): `bash tools/setup-devkitppc.sh` → `make JIT=1 -j4` +
`make JIT=0 -j4` reproduced both delivered DOL hashes exactly (`43b2ba4b…` / `0fab8e90…`),
a fresh 20-frame `suite.gba` pair via `test-case.sh` compared
`MISMATCHES cycles=0 state=0 dumps=0`, the frame-20 screens were byte-identical between
variants, and `screen-text.py` decoded the suite menu (14/14 group names) in both modes.

Each tool below is collapsed by default — expand to read the source. The **verdict**
(carried over from the table below) is the one thing worth reading before you expand
anything.

| Verdict | Meaning |
|---|---|
| ✅ **likely perfect — use it unchanged** | Ran end to end many times; the numbers in §7/§11 came out of these bytes. Copy it as-is. |
| ✅ **use it unchanged, with caveats** | Correct and safe, but one specific part is fragile or has a sharp edge — the note says which. |
| ⚠️ **WIP — may save you time** | Works, but lightly exercised or has a known wart; read it before trusting it. |
| 📄 **data, use it unchanged** | Not executable: digests, a font map, sample config/input files. |

Quick reference:

| § | file | verdict | one-line reason |
|---|---|---|---|
| 14.1 | `tools/setup-devkitppc.sh` | ✅ likely perfect | idempotent, digest-gated, survives resets; skips the metadata-only package |
| 14.2 | `tools/toolchain-packages.sha256` | 📄 data | the 9 package digests §14.1 verifies against |
| 14.3 | `Makefile` (repo root) | ✅ likely perfect | dual variant, `.S` support, flags stamp; reproduces the delivered bytes |
| 14.4 | `tools/mksd.sh` | ✅ likely perfect | fresh image per run, refuses reuse, rewrites `path=` for you |
| 14.5 | `tools/run-dolphin.sh` | ✅ likely perfect | flock-serialised, provenance in `run-info.txt`, enumerating artifact pull |
| 14.6 | `tools/test-case.sh` | ✅ likely perfect | one matched case per command; knows the variant→DOL mapping |
| 14.7 | `tools/final-matrix.sh` | ✅ likely perfect | the whole G2 re-cut incl. shipped-bytes provenance |
| 14.8 | `tools/prune-run.sh` | ✅ likely perfect | keeps `evidence/` inside the snapshot budget |
| 14.9 | `tools/compare-runs.py` | ✅ likely perfect | cycles/state/dumps mismatch counts; strips JIT counters before hashing |
| 14.10 | `tools/g2-dump-parity.py` | ✅ likely perfect | proves the *data* (RAM/FB/screens) is byte-identical, not just the scheduler |
| 14.11 | `tools/suite-case.py` | ✅ likely perfect | the `dol_sha256` gate is why G3 evidence is trustworthy |
| 14.12 | `tools/g3-sweep.sh` | ✅ likely perfect (trivial) | 14 groups × one variant |
| 14.13 | `tools/g3-compare.py` | ✅ with caveats | signature window is 400 lines; never run mid-sweep |
| 14.14 | `tools/g3-console-lines.py` | ✅ with caveats | needs the `importlib` hyphen trick |
| 14.15 | `tools/screen-text.py` | ✅ one bug fixed here | `--glyphs` fixed; font coverage is the remaining limit |
| 14.16 | `tools/font-map.json` | 📄 data | 50 glyphs; extend only from already-decoded screens |
| 14.17 | `tools/g3-probe.sh` | ⚠️ WIP | one-shot wrapper; multi-line input quoting is awkward |
| 14.18 | `NooDS-Wii/NooDS-Wii/ppc_bridge.S` | ✅ likely perfect | the bridge ABI the emitter targets |
| 14.19 | `evidence/g1/enc_test.S`, `enc_cr.S` | ✅ likely perfect | the assembler oracle for every emitted PPC form |
| 14.20 | `autoboot.txt` / `input.txt` samples | 📄 examples | copy the shape, not the frame numbers |

Paths are relative to `/home/user` unless stated.

### 14.1 `tools/setup-devkitppc.sh`

✅ **likely perfect — use it unchanged.** Idempotent, verifies every download against §14.2
before extracting anything, and handles the two traps that cost time: the metadata-only
`devkitPPC-r50` archive and the `pipefail`+SIGPIPE false rejection (both handled inside; do
not "simplify" them). The only thing to change is `ROOT` if your workspace isn't
`/home/user`. Run `sudo apt-get update` first (§2).

<details>
<summary>Show <code>tools/setup-devkitppc.sh</code></summary>

```bash
#!/usr/bin/env bash
# tools/setup-devkitppc.sh — idempotent devkitPPC bootstrap (AGENTS.md §2)
set -euo pipefail
ROOT=${ROOT:-/home/user}; CACHE="$ROOT/tmp/dkp"; BASE=https://wii.leseratte10.de/devkitPro
mkdir -p "$CACHE" "$ROOT/evidence"
packages=(
 'devkitPPC/r50%20%282026-05-03%29/devkitPPC-r50-1-any.pkg.tar.zst'
 'devkitPPC/r50%20%282026-05-03%29/devkitppc-binutils-2.46.0-1-linux_x86_64.pkg.tar.zst'
 'devkitPPC/r50%20%282026-05-03%29/devkitppc-gcc-16.1.0-1-linux_x86_64.pkg.tar.zst'
 'file.php/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst'
 'file.php/devkitppc-rules-1.2.1-1-any.pkg.tar.zst'
 'file.php/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst'
 'libogc/libogc_3.1%20%282026-05-03%29/libogc-3.1.0-1-any.pkg.tar.zst'
 'libfat/libfat_2.1.0/libfat-ogc-2.1.0-4-any.pkg.tar.zst'
 'other-stuff/gamecube-tools/gamecube-tools-1.0.7-1-linux_x86_64.pkg.tar.zst'
)
for rel in "${packages[@]}"; do
  name=${rel##*/}
  [[ -s "$CACHE/$name" ]] || { curl -fL --retry 3 "$BASE/$rel" -o "$CACHE/$name.part"; mv "$CACHE/$name.part" "$CACHE/$name"; }
  expected=$(awk -v name="$name" '$2==name {print $1}' "$(dirname "$0")/toolchain-packages.sha256")
  [[ ${#expected} = 64 ]] || { echo "no digest for $name" >&2; exit 2; }
  printf '%s  %s\n' "$expected" "$CACHE/$name" | sha256sum -c -
  # devkitPPC-r50 is metadata-only: skip archives with no opt/ payload.
  # Do NOT use grep -q here: pipefail + early SIGPIPE can falsely reject an archive.
  if tar --zstd -tf "$CACHE/$name" | grep '^opt/' >/dev/null; then
    sudo tar --zstd -xf "$CACHE/$name" -C / --wildcards 'opt/*'
  fi
done
(cd "$CACHE" && sha256sum *.pkg.tar.zst) > "$ROOT/evidence/toolchain-packages.sha256"
echo "devkitPPC bootstrap complete"
```

</details>

### 14.2 `tools/toolchain-packages.sha256`

📄 **data, use it unchanged.** The digest list §14.1 verifies every download against. If a
package ever disappears upstream, re-pin deliberately and re-record
`evidence/toolchain-packages.sha256` — never by deleting the check.

<details>
<summary>Show <code>tools/toolchain-packages.sha256</code></summary>

```text
2b3c3f4773be827960c0fbb985f83f50916c56c54ab5cfcbe5a4239364e6302c  devkitPPC-r50-1-any.pkg.tar.zst
ec39352d27f668d235de9fa69caeee2d753994b695245075a37c4bfc700547d6  devkitppc-binutils-2.46.0-1-linux_x86_64.pkg.tar.zst
5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd  devkitppc-crtls-2.1.0-1-any.pkg.tar.zst
656f0cabcd99a1c0d1510d0fccf5556abac26c3c813113175476d315c5b3da7a  devkitppc-gcc-16.1.0-1-linux_x86_64.pkg.tar.zst
2c5277c6b07a5558c9fd8e631d958c6d7784860aa55f5c320cb51371bc07e21b  devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst
0c3394da451c9dfb3b428d9d61b044eb1eafb947b2b5090ff88525f55c785278  devkitppc-rules-1.2.1-1-any.pkg.tar.zst
e7dea3d441f3951be336a5b52849bdabf191d7157b765b5b3a603ecf6ba233b4  gamecube-tools-1.0.7-1-linux_x86_64.pkg.tar.zst
9fb965672aaa3a82586715aab15ee4a16d3fc8850cbd2bd8cc4868e1c091d1b9  libfat-ogc-2.1.0-4-any.pkg.tar.zst
7c2dba9f4ef8cc496e424cd6208e0eddc893c08fff080016321d777d84c5c083  libogc-3.1.0-1-any.pkg.tar.zst
```

</details>

### 14.3 `Makefile` (repo root)

✅ **likely perfect — use it unchanged.** Dual variant, separate object trees, a flags stamp
so stale objects can never be reused, `.S` through `-x assembler-with-cpp`, and it reproduces
the delivered bytes exactly from a clean tree (re-verified after a reset). Two notes before
you touch it: `OPT ?= -O2` exists for experiments and **`-O3` does not link this tree**
(§3, T35); `TARGET=`/`OBJDIR=` can be overridden for side builds without editing the file.

<details>
<summary>Show <code>Makefile</code></summary>

```makefile
# ---------------------------------------------------------------------------
# NooDS-Wii — dual-variant build (JIT / interpreter)
#
#   make JIT=1 -j4   ->  jit.elf  / jit.dol              (dynarec default)
#   make JIT=0 -j4   ->  NooDS-Wii-interp.elf / .dol     (reference interpreter)
#
# Separate object trees (obj-jit1 / obj-jit0) and a flags stamp so that
# changing flags can never silently reuse stale objects.
# ---------------------------------------------------------------------------
.SUFFIXES:

DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO
export DEVKITPPC

JIT ?= 1
ifeq ($(filter $(JIT),0 1),)
$(error JIT must be 0 or 1, got '$(JIT)')
endif

GAMECUBE ?= 0
OPT     ?= -O2

TARGET  := $(if $(filter 1,$(JIT)),jit,NooDS-Wii-interp)
OBJDIR  := obj-jit$(JIT)
SRCDIR  := NooDS-Wii

PREFIX  := $(DEVKITPPC)/bin/powerpc-eabi-
CC      := $(PREFIX)gcc
CXX     := $(PREFIX)g++
LD      := $(PREFIX)g++
ELF2DOL := $(DEVKITPRO)/tools/bin/elf2dol

INCLUDES := -I$(DEVKITPRO)/libogc/include \
            -I$(DEVKITPRO)/portlibs/wii/include \
            -I$(DEVKITPRO)/portlibs/ppc/include \
            -I$(SRCDIR)

ifeq ($(GAMECUBE),1)
MACHDEP := -DGEKKO -mogc -mcpu=750 -meabi -mhard-float
else
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
endif

WARNFLAGS := -Wall -Wno-unused-variable -Wno-unused-but-set-variable

CXXFLAGS := $(OPT) -std=gnu++17 -fsigned-char -ffast-math \
            -ffunction-sections -fdata-sections \
            $(WARNFLAGS) \
            -DENDIAN_BIG -DNOODS_JIT=$(JIT) -DLOG_LEVEL=3 \
            $(INCLUDES) $(MACHDEP)

ASFLAGS := $(OPT) -ffunction-sections -fdata-sections \
           -DENDIAN_BIG -DNOODS_JIT=$(JIT) \
           $(INCLUDES) $(MACHDEP)

LDFLAGS := $(MACHDEP) \
           -Wl,-Map,$(TARGET).map -Wl,--gc-sections \
           -L$(DEVKITPRO)/libogc/lib/wii \
           -L$(DEVKITPRO)/portlibs/wii/lib \
           -L$(DEVKITPRO)/portlibs/ppc/lib \
           -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm

CPP_SOURCES := $(wildcard $(SRCDIR)/*.cpp)
ASM_SOURCES := $(wildcard $(SRCDIR)/*.S)

CPP_OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/$(SRCDIR)/%.o,$(CPP_SOURCES))
ASM_OBJECTS := $(patsubst $(SRCDIR)/%.S,$(OBJDIR)/$(SRCDIR)/%.o,$(ASM_SOURCES))
OBJECTS     := $(CPP_OBJECTS) $(ASM_OBJECTS)

DEPS := $(OBJECTS:.o=.d)
FLAGS_STAMP := $(OBJDIR)/.flags

all: $(TARGET).dol

$(OBJDIR) $(OBJDIR)/$(SRCDIR):
	@mkdir -p $@

# --- flags stamp: rewriting only on change keeps object mtimes intact -------
$(FLAGS_STAMP): | $(OBJDIR)
	@echo '$(CXXFLAGS) $(ASFLAGS) $(GAMECUBE)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; echo "flags changed -> full rebuild ($(TARGET))"; fi

$(OBJDIR)/$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(FLAGS_STAMP) | $(OBJDIR)/$(SRCDIR)
	$(CXX) -c $< -o $@ $(CXXFLAGS) -MMD -MP

$(OBJDIR)/$(SRCDIR)/%.o: $(SRCDIR)/%.S $(FLAGS_STAMP) | $(OBJDIR)/$(SRCDIR)
	$(CC) -c -x assembler-with-cpp $< -o $@ $(ASFLAGS) -MMD -MP

$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(TARGET).dol: $(TARGET).elf
	$(ELF2DOL) $< $@

clean:
	rm -rf obj-jit0 obj-jit1 $(TARGET).elf $(TARGET).dol $(TARGET).map

.PHONY: all clean
-include $(DEPS)
```

</details>

### 14.4 `tools/mksd.sh`

✅ **likely perfect — use it unchanged.** Builds a fresh MBR/FAT16 image every time, refuses
to reuse an existing one, and rewrites `path=` to whatever it staged so a stale config can
never point at the wrong ROM. One thing to notice rather than change: the trailing
`mdir -b -w` is **display only** — `-w` re-wraps output and must never appear where names
are parsed (T16).

<details>
<summary>Show <code>tools/mksd.sh</code></summary>

```bash
#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/mksd.sh IMAGE ROM CONFIG [INPUT]
#
# Fresh 128 MiB MBR/FAT16 SD image (never reused) with:
#   ::/noods/<rom>          the ROM under test
#   ::/noods/autoboot.txt   rig config, with `path=` rewritten to the ROM
#   ::/noods/input.txt      optional frame-indexed input
#
# The image is refused if it already exists (AGENTS.md §7/§8: fresh per run).
# ---------------------------------------------------------------------------
set -euo pipefail

IMG=${1:?usage: mksd.sh IMAGE ROM CONFIG [INPUT]}
ROM=${2:?usage: mksd.sh IMAGE ROM CONFIG [INPUT]}
CFG=${3:?usage: mksd.sh IMAGE ROM CONFIG [INPUT]}
INPUT=${4:-}

[ -f "$ROM" ] || { echo "mksd: no such ROM: $ROM" >&2; exit 1; }
[ -f "$CFG" ] || { echo "mksd: no such config: $CFG" >&2; exit 1; }
if [ -e "$IMG" ]; then
    echo "mksd: refusing to overwrite existing image $IMG" >&2
    exit 1
fi

MTOOLS=''
for t in mformat mmd mcopy mdir; do
    command -v "$t" >/dev/null || { echo "mksd: missing $t (install mtools)" >&2; exit 1; }
done

SIZE_MB=128
mkdir -p "$(dirname "$IMG")"
truncate -s "${SIZE_MB}M" "$IMG"

# --- MBR: partition 1 at 1 MiB, type 0x0E (FAT16 LBA), 512-byte sectors -----
python3 - "$IMG" <<'PY'
import struct, sys
img = sys.argv[1]
total_sectors = 128 * 1024 * 1024 // 512
start = 2048                      # 1 MiB
length = total_sectors - start
mbr = bytearray(512)
entry = struct.pack('<B3sB3sII', 0x00, b'\x00\x00\x00', 0x0E, b'\x00\x00\x00',
                    start, length)
mbr[446:446 + 16] = entry
mbr[510:512] = b'\x55\xAA'
with open(img, 'r+b') as f:
    f.write(mbr)
PY

mformat -i "$IMG@@1M" -v NOODS ::
mmd -i "$IMG@@1M" ::/noods
mmd -i "$IMG@@1M" ::/noods/dump

ROMBASE=$(basename "$ROM")
mcopy -i "$IMG@@1M" "$ROM" "::/noods/$ROMBASE"

# --- autoboot.txt with the ROM path forced to what we just staged -----------
TMP=$(mktemp)
python3 - "$CFG" "$TMP" "$ROMBASE" <<'PY'
import sys
cfg, out, rom = sys.argv[1:4]
lines = open(cfg).read().splitlines()
done = False
res = []
for ln in lines:
    s = ln.strip()
    if s.startswith('path'):
        res.append('path=sd:/noods/' + rom); done = True
    else:
        res.append(ln)
if not done:
    res.append('path=sd:/noods/' + rom)
open(out, 'w').write('\n'.join(res) + '\n')
PY
mcopy -i "$IMG@@1M" "$TMP" ::/noods/autoboot.txt
rm -f "$TMP"

if [ -n "$INPUT" ]; then
    [ -f "$INPUT" ] || { echo "mksd: no such input file: $INPUT" >&2; exit 1; }
    mcopy -i "$IMG@@1M" "$INPUT" ::/noods/input.txt
fi

echo "mksd: $IMG ready"
mdir -i "$IMG@@1M" -b -w ::/noods
```

</details>

### 14.5 `tools/run-dolphin.sh`

✅ **likely perfect — use it unchanged.** Serialises with `flock` (lock FD closed in the
child so timeout/xvfb cannot leak it), isolates Dolphin's user dir per run, writes
`run-info.txt` provenance (DUT/ROM/config hashes + `dolphin exit=…`), and pulls artifacts by
enumerating rather than globbing. Changing `DOLPHIN_CPUCORE=<n>` re-runs the same DUT on
Dolphin's pure interpreter — that is the "DUT bug or Dolphin bug?" oracle (§9).

<details>
<summary>Show <code>tools/run-dolphin.sh</code></summary>

```bash
#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/run-dolphin.sh DOL SECONDS IMAGE [RUN_ID]
#
# Headless, serialized Dolphin run of the DUT against a fresh SD image.
#
#   * unique RUN_ID, refuses to overwrite an existing run directory
#   * flock-serialized; the lock FD is closed in the child (9>&-) so
#     xvfb/timeout children cannot leak it
#   * isolated user dir tmp/dolruns/RUN_ID/user with the SD card wired up
#     (WiiSDCard/SDCard keys -- without them the DUT sees no sd:/)
#   * DUT, ROM-in-image and config hashed before launch; commit+dirty recorded
#   * artifacts pulled from the DUT's own user/Load/WiiSD.raw
# ---------------------------------------------------------------------------
set -euo pipefail

DOL=${1:?usage: run-dolphin.sh DOL SECONDS IMAGE [RUN_ID]}
SECS=${2:?usage: run-dolphin.sh DOL SECONDS IMAGE [RUN_ID]}
IMAGE=${3:?usage: run-dolphin.sh DOL SECONDS IMAGE [RUN_ID]}
ROOT=${NOODS_ROOT:-/home/user}
RUN_ID=${4:-$(date +%Y%m%d-%H%M%S)-$$}

DOL_ABS=$(readlink -f "$DOL")
IMAGE_ABS=$(readlink -f "$IMAGE")
[ -f "$DOL_ABS" ]   || { echo "run-dolphin: no such DOL: $DOL" >&2; exit 1; }
[ -f "$IMAGE_ABS" ] || { echo "run-dolphin: no such image: $IMAGE" >&2; exit 1; }

RUN="$ROOT/tmp/dolruns/$RUN_ID"
if [ -e "$RUN" ]; then
    echo "run-dolphin: refusing to overwrite existing run $RUN" >&2
    exit 1
fi
mkdir -p "$RUN/user/Config" "$RUN/user/Load"

exec 9>"$ROOT/tmp/dolruns/.lock"
mkdir -p "$ROOT/tmp/dolruns"
if ! flock -w 600 9; then
    echo "run-dolphin: could not acquire the run lock" >&2
    exit 1
fi

cp "$DOL_ABS" "$RUN/dut.dol"
cp "$IMAGE_ABS" "$RUN/user/Load/WiiSD.raw"

SD="$RUN/user/Load/WiiSD.raw"

CPUCORE=${DOLPHIN_CPUCORE:-1}
cat > "$RUN/user/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = $CPUCORE
CPUThread = False
EmulationSpeed = 0.0
DSPHLE = True
SyncGPU = True
DeterministicGPUThread = True
EnableCheats = False
WiiSDCard = True
WiiSDCardPath = $SD
WiiSDCardAllowWrites = True
SDCard = True
SDCardPath = $SD
SIDevice0 = 0
[Display]
Fullscreen = False
[DSP]
Backend = Null
[Interface]
ConfirmStop = False
[Logger]
WriteToConsole = True
WriteToFile = True
[LogOptions]
OSREPORT = True
EOF

cat > "$RUN/user/Config/GFX.ini" <<'EOF'
[Settings]
Backend = OGL
InternalResolution = 1
MSAA = 1
SSAA = False
VSync = False
ForceFiltering = False
SwapInterval = 0
ShaderCompilationMode = 0
WaitForShadersBeforeStarting = False
EFBScale = 1
EOF

{
    echo "run_id=$RUN_ID"
    echo "dol=$DOL_ABS"
    echo "dol_sha256=$(sha256sum "$DOL_ABS" | cut -d' ' -f1)"
    echo "image=$IMAGE_ABS"
    echo "image_sha256=$(sha256sum "$IMAGE_ABS" | cut -d' ' -f1)"
    echo "seconds=$SECS"
echo "cpucore=$CPUCORE"
    echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if git -C "$ROOT/NooDS-Wii" rev-parse HEAD >/dev/null 2>&1; then
        echo "commit=$(git -C "$ROOT/NooDS-Wii" rev-parse HEAD)"
        echo "branch=$(git -C "$ROOT/NooDS-Wii" rev-parse --abbrev-ref HEAD)"
        if [ -n "$(git -C "$ROOT/NooDS-Wii" status --porcelain)" ]; then
            echo "dirty=1"
        else
            echo "dirty=0"
        fi
    fi
} > "$RUN/run-info.txt"

LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 \
xvfb-run -a -s '-screen 0 640x480x24' \
timeout --signal=TERM --kill-after=10s "${SECS}s" \
/usr/games/dolphin-emu-nogui -u "$RUN/user" -p x11 -v OGL -a HLE \
    -e "$RUN/dut.dol" 9>&- > "$RUN/dolphin.log" 2>&1 || echo "dolphin exit=$?" >> "$RUN/run-info.txt"

# --- Dolphin's own log (carries the DUT's printf/OSReport, flushed per line) --
if [ -f "$RUN/user/Logs/dolphin.log" ]; then
    cp "$RUN/user/Logs/dolphin.log" "$RUN/dolphin-osreport.log"
    echo "run-dolphin: osreport lines=$(wc -l < "$RUN/dolphin-osreport.log")"
fi

# --- pull artifacts out of the DUT's own card ------------------------------
mkdir -p "$RUN/artifacts/dump"
IMG_REF="$SD@@1M"

pull() {   # pull ::/noods/<name> -> $RUN/artifacts/<name>
    local name=$1
    if mcopy -n -i "$IMG_REF" "::/noods/$name" "$RUN/artifacts/$name" 2>/dev/null; then
        return 0
    fi
    return 1
}

if ! pull debug.log; then
    echo "run-dolphin: WARNING no debug.log on the card" | tee -a "$RUN/run-info.txt"
fi

# mcopy cannot glob inside the image: enumerate, then copy each name.
# NOTE: `mdir -b` prints one full path per line; adding -w re-wraps several
# names per line and silently loses entries.  Do not add -w here.
if mdir -i "$IMG_REF" -b ::/noods/dump > "$RUN/artifacts/dump-list.txt" 2>/dev/null; then
    while read -r path; do
        path=${path%$'\r'}
        case "$path" in ::/noods/dump/*) ;; *) continue ;; esac
        name=${path##*/}
        [ -n "$name" ] || continue
        mcopy -n -i "$IMG_REF" "::/noods/dump/$name" "$RUN/artifacts/dump/$name" 2>/dev/null || true
    done < "$RUN/artifacts/dump-list.txt"
fi

( cd "$RUN/artifacts" && find . -type f ! -name digests.sha256 -print0 \
    | sort -z | xargs -0 -r sha256sum > digests.sha256 ) 2>/dev/null || true
echo "run-dolphin: $RUN"
ls -la "$RUN/artifacts" 2>/dev/null || true
```

</details>

### 14.6 `tools/test-case.sh`

✅ **likely perfect — use it unchanged.** One matched test case per command: config + fresh
image + run, with the variant→DOL mapping in one place. Two things to keep in mind: `EXTRA`
is the **7th** argument and it writes `verify=1` unless EXTRA overrides it; and the run id
gets `-<VARIANT>` appended **here**, so never pass an id that already ends in the variant
(T28/T31).

<details>
<summary>Show <code>tools/test-case.sh</code></summary>

```bash
#!/usr/bin/env bash
# tools/test-case.sh ID jit|interp ROM FRAMES SECONDS [INPUT] [EXTRA]
#
# One matched test case: fresh SD image, run the named variant, print the
# run directory.  EXTRA is appended to autoboot.txt (e.g. "dump=5,10,45").
set -euo pipefail
ID=${1:?usage: test-case.sh ID jit|interp ROM FRAMES SECONDS [INPUT] [EXTRA]}
VARIANT=${2:?usage: test-case.sh ID jit|interp ROM FRAMES SECONDS [INPUT] [EXTRA]}
ROM=${3:?}; FRAMES=${4:?}; SECS=${5:?}
INPUT=${6:-}
EXTRA=${7:-}

ROOT=${NOODS_ROOT:-/home/user}
case "$VARIANT" in
    jit)    DOL=$ROOT/NooDS-Wii/jit.dol ;;
    interp) DOL=$ROOT/NooDS-Wii/NooDS-Wii-interp.dol ;;
    *) echo "variant must be jit or interp" >&2; exit 1 ;;
esac

CFG=$ROOT/tmp/cfg/$ID.txt
mkdir -p "$ROOT/tmp/cfg"
{
    echo "frames=$FRAMES"
    echo "jit=$( [ "$VARIANT" = jit ] && echo 1 || echo 0 )"
    echo "log=1"
    echo "verify=1"
    echo "id=$ID-$VARIANT"
    [ -n "$EXTRA" ] && echo "$EXTRA"
} > "$CFG"

IMG=$ROOT/tmp/sd/$ID-$VARIANT.raw
rm -f "$IMG"
"$ROOT/tools/mksd.sh" "$IMG" "$ROM" "$CFG" $INPUT >/dev/null
exec "$ROOT/tools/run-dolphin.sh" "$DOL" "$SECS" "$IMG" "$ID-$VARIANT"
```

</details>

### 14.7 `tools/final-matrix.sh`

✅ **likely perfect — use it unchanged.** Runs the whole G2 re-cut in one go: provenance
runs *of the shipped bytes*, the same frames from the tree DOLs, then the canonical four
cells — so "the deliverable is the tested artifact" is a machine output, not a claim. Paths
are absolute; edit the `mk` calls for a different matrix.

<details>
<summary>Show <code>tools/final-matrix.sh</code></summary>

```bash
#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/final-matrix.sh -- re-cut verification for the *shipped* bytes.
#
#   1. provenance: run deliverables/jit.dol and deliverables/NooDS-Wii-interp.dol
#      exactly as shipped (the run-info.txt records their sha256);
#   2. tree-vs-shipped: the same 20 frames from the in-tree dols, compared
#      byte-for-byte with the shipped-file runs (proves the shipped files are
#      the bytes that were tested);
#   3. the canonical G2 four-cell matrix at the default verifier setting
#      (verify=1, forced by test-case.sh), i.e. the same settings the earlier
#      performance table used, so the two tables are comparable.
#
# One run at a time; run-dolphin.sh flocks anyway.
# ---------------------------------------------------------------------------
set -u
cd /home/user
ROOT=/home/user

mk() { # $1 id  $2 variant  $3 rom  $4 frames  $5 seconds  $6 input  $7 extra  [$8 dol]
  local id=$1 var=$2 rom=$3 fr=$4 secs=$5 inp=$6 extra=$7 dol=${8:-}
  local cfg=$ROOT/tmp/cfg/$id-$var.txt img=$ROOT/tmp/sd/$id-$var.raw
  { echo "frames=$fr"; echo "jit=$( [ "$var" = jit ] && echo 1 || echo 0 )"
    echo "log=1"; echo "verify=1"; echo "id=$id-$var"
    [ -n "$extra" ] && printf '%s\n' "$extra"; } > "$cfg"
  rm -f "$img"
  if ! tools/mksd.sh "$img" "$rom" "$cfg" "$inp" >/dev/null; then echo "MKSDFAIL $id-$var"; return; fi
  if [ -z "$dol" ]; then
    if [ "$var" = jit ]; then dol=$ROOT/NooDS-Wii/jit.dol; else dol=$ROOT/NooDS-Wii/NooDS-Wii-interp.dol; fi
  fi
  local t0=$SECONDS
  if tools/run-dolphin.sh "$dol" "$secs" "$img" "$id-$var" >/dev/null 2>&1; then
    echo "OK   $id-$var  $((SECONDS-t0))s"
  else
    echo "FAIL $id-$var rc=$?  $((SECONDS-t0))s"
  fi
}

# 1 + 2: provenance (20 frames from the shipped files, and from the tree)
mk deliv jit    gba/suite.gba 20 200 "" "dump=1,10,20" "$ROOT/deliverables/jit.dol"
mk deliv interp gba/suite.gba 20 200 "" "dump=1,10,20" "$ROOT/deliverables/NooDS-Wii-interp.dol"
mk tree  jit    gba/suite.gba 20 200 "" "dump=1,10,20"
mk tree  interp gba/suite.gba 20 200 "" "dump=1,10,20"

# 3: the G2 matrix
mk g2-nds45    jit    nds/rockwrestler.nds 45  300 "" "dump=1,10,20,30,45"
mk g2-nds45    interp nds/rockwrestler.nds 45  300 "" "dump=1,10,20,30,45"
mk g2-gba50    jit    gba/suite.gba 50  240 "" "dump=1,10,25,40,50"
mk g2-gba50    interp gba/suite.gba 50  240 "" "dump=1,10,25,40,50"
mk g2-gba60    jit    gba/suite.gba 60  300 "" "dump=1,50,60"
mk g2-gba60    interp gba/suite.gba 60  300 "" "dump=1,50,60"
mk g2-gba120   jit    gba/suite.gba 120 400 "" $'hb=1\ndump=1,104,120'
mk g2-gba120   interp gba/suite.gba 120 400 "" "dump=1,104,120"

echo FINAL-MATRIX-DONE
```

</details>

### 14.8 `tools/prune-run.sh`

✅ **likely perfect — use it unchanged.** Copies just the evidence subset of a run (debug
log, digests, dumps, `run-info.txt`) and drops the 128 MiB `WiiSD.raw` and the `user/` tree
— this is what keeps `evidence/` inside the snapshot budget (§11).

<details>
<summary>Show <code>tools/prune-run.sh</code></summary>

```bash
#!/usr/bin/env bash
# tools/prune-run.sh SRCDIR DESTDIR -- copy the evidence subset of a run dir
set -euo pipefail
src=$1; dst=$2
mkdir -p "$dst"
for f in debug.log digests.sha256 dump-list.txt run-info.txt; do
    [ -f "$src/artifacts/$f" ] && cp "$src/artifacts/$f" "$dst/$f"
done
if [ -d "$src/artifacts/dump" ]; then
    mkdir -p "$dst/dump"; cp -r "$src/artifacts/dump/." "$dst/dump/"
fi
exit 0
```

</details>

### 14.9 `tools/compare-runs.py`

✅ **likely perfect — use it unchanged.** Compares per-frame guest cycles, per-frame state
digests and every dump hash, and prints explicit mismatch counts (never a bare "OK"). Two
details to know rather than change: it strips each build's own `jit:` counter lines before
hashing `state-*.txt` — without that, a JIT run could never match its reference — and it
prints `ratio = B/A`, so **you** must state the direction in prose (T37).

<details>
<summary>Show <code>tools/compare-runs.py</code></summary>

```python
#!/usr/bin/env python3
"""
tools/compare-runs.py RUN_A RUN_B [--allow-ticks]

Compares two rig run directories (tmp/dolruns/<RUN_ID>) produced by
tools/run-dolphin.sh and prints explicit mismatch counts.  It never prints
"OK" on its own -- that verdict belongs to the caller (AGENTS.md §9/§10).

Compares:
  * per-frame guest cycles (scheduling equivalence)
  * per-frame guest-state digest (registers/banks/cpsr/pipeline/halt)
  * host ticks per frame (reported, and checked for a gross divergence only
    when --allow-ticks is not given, since ticks are wall-clock dependent)
  * sha256 of every dumped artifact (state/fb/ram)
"""

import argparse
import hashlib
import os
import sys
import re

FRAME_RE = re.compile(r"^frame: n=(\d+) runTicks=(\d+) ndsCycles=(\d+) fps=(-?\d+)$")
STATE_RE = re.compile(r"^state: n=(\d+) hash=([0-9A-Fa-f]+)")
DONE_RE = re.compile(r"^done: frames=(\d+) id=(\S+)")
JITSTAT_RE = re.compile(r"^jitstats: cpu(\d+) enabled=(\d+) native=(\d+) "
                        r"fallback=(\d+) compiled=(\d+) neg=(\d+) mismatch=(\d+) flush=(\d+)")


def sha256(path, strip_instrumentation=False):
    """sha256 of a file.  For state-*.txt dumps the per-CPU `jit:` counter line
    is instrumentation, not guest state, so it is stripped before hashing --
    otherwise a run with the JIT enabled can never match its reference."""
    h = hashlib.sha256()
    if strip_instrumentation:
        with open(path, "r", errors="replace") as f:
            for line in f:
                if "] jit: " in line:
                    continue
                h.update(line.encode())
        return h.hexdigest()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load(run_dir):
    log = os.path.join(run_dir, "artifacts", "debug.log")
    if not os.path.exists(log):
        return None
    frames, states, jit, done = {}, {}, {}, None
    with open(log, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = FRAME_RE.match(line)
            if m:
                n = int(m.group(1))
                frames[n] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
                continue
            m = STATE_RE.match(line)
            if m:
                states[int(m.group(1))] = m.group(2).upper()
                continue
            m = JITSTAT_RE.match(line)
            if m:
                jit[int(m.group(1))] = tuple(int(x) for x in m.groups()[1:])
                continue
            m = DONE_RE.match(line)
            if m:
                done = (int(m.group(1)), m.group(2))
    return {"frames": frames, "states": states, "jit": jit, "done": done}


def digests(run_dir):
    d = {}
    art = os.path.join(run_dir, "artifacts")
    for root, _, files in os.walk(art):
        if os.path.basename(root) == "dump":
            for name in files:
                rel = os.path.relpath(os.path.join(root, name), art)
                state = name.startswith("state-") and name.endswith(".txt")
                d[rel] = sha256(os.path.join(root, name), strip_instrumentation=state)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_a")
    ap.add_argument("run_b")
    ap.add_argument("--allow-ticks", action="store_true",
                    help="do not report per-frame tick differences as mismatches")
    args = ap.parse_args()

    a, b = load(args.run_a), load(args.run_b)
    if a is None:
        print(f"NOT_RUN: {args.run_a}/artifacts/debug.log missing")
        return 2
    if b is None:
        print(f"NOT_RUN: {args.run_b}/artifacts/debug.log missing")
        return 2

    for tag, r in (("A", a), ("B", b)):
        if not r["frames"]:
            print(f"NOT_RUN: run {tag} logged no frames")
            return 2

    m_cycle = m_state = m_tick = 0
    n = 0
    for frame in sorted(set(a["frames"]) | set(b["frames"])):
        fa, fb = a["frames"].get(frame), b["frames"].get(frame)
        n += 1
        if fa is None or fb is None:
            m_cycle += 1
            continue
        if fa[1] != fb[1]:
            m_cycle += 1
            print(f"cycle mismatch frame {frame}: A={fa[1]} B={fb[1]}")
        if fa[0] != fb[0] and not args.allow_ticks:
            m_tick += 1
        sa, sb = a["states"].get(frame), b["states"].get(frame)
        if sa != sb:
            m_state += 1
            print(f"state mismatch frame {frame}: A={sa} B={sb}")

    da, db = digests(args.run_a), digests(args.run_b)
    m_dump = 0
    for name in sorted(set(da) | set(db)):
        if da.get(name) != db.get(name):
            m_dump += 1
            print(f"dump mismatch {name}: A={da.get(name)} B={db.get(name)}")

    def mean_ticks(r):
        v = [t for (t, _, _) in r["frames"].values()]
        return (sum(v) // len(v)) if v else 0

    print(f"runs:            A={args.run_a}  B={args.run_b}")
    print(f"frames compared: {n}  (A={len(a['frames'])} B={len(b['frames'])})")
    print(f"frame-count:     A done={a['done']} B done={b['done']}")
    for cpu in sorted(set(a["jit"]) | set(b["jit"])):
        print(f"jitstats cpu{cpu}: A={a['jit'].get(cpu)} B={b['jit'].get(cpu)}")
    print(f"mean ticks/frame: A={mean_ticks(a)} B={mean_ticks(b)} "
          f"ratio={mean_ticks(b) / max(1, mean_ticks(a)):.3f}")
    print(f"MISMATCHES cycles={m_cycle} state={m_state} dumps={m_dump}"
          + ("" if args.allow_ticks else f" ticks={m_tick}"))
    return 0 if (m_cycle == 0 and m_state == 0 and m_dump == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
```

</details>

### 14.10 `tools/g2-dump-parity.py`

✅ **likely perfect — use it unchanged.** The check that makes "0 mismatches" mean something
about *data*, not just about the scheduler: every RAM image, framebuffer and screen must be
byte-identical between builds, and a `state-*.txt` may differ only in that build's own `jit:`
counter lines.

<details>
<summary>Show <code>tools/g2-dump-parity.py</code></summary>

```python
#!/usr/bin/env python3
"""tools/g2-dump-parity.py -- dump-level parity for the G2 matrix.

For each cell, the JIT and interpreter runs dumped full guest state at the same
frames (dump=...).  This checks, per cell:

  * every dump that is a raw guest artifact (RAM, RAM blocks, framebuffer,
    screen) is BYTE-IDENTICAL between the two builds;
  * every dump that differs is a state-*.txt file whose only differing lines are
    that build's own JIT statistic lines (i.e. not guest state).

Writes evidence/g2-dump-parity.txt.  Exit 0 iff every cell passes.
"""
import os, sys, filecmp

CELLS = ["g2-nds45", "g2-gba50", "g2-gba60", "g2-gba120"]
ROOT = "/home/user/evidence/runs"
OUT = "/home/user/evidence/g2-dump-parity.txt"

def only_jit_lines_differ(a, b):
    la, lb = open(a, errors="replace").read().splitlines(), open(b, errors="replace").read().splitlines()
    diffs = [(x, y) for x, y in zip(la, lb) if x != y]
    if len(la) != len(lb):
        return False, ["line count differs"]
    bad = [x for x, y in diffs if "jit:" not in x or "jit:" not in y]
    return (not bad), (bad or [x for x, _ in diffs])

def main():
    ok_all = True
    lines = []
    lines.append("# G2 dump-level parity: JIT vs interpreter, shipped build")
    lines.append("#")
    lines.append("# 'identical' = byte-identical files; state-*.txt may differ only in that")
    lines.append("# build's own 'jit:' statistic lines (guest registers/memory must match).")
    lines.append("")
    for cell in CELLS:
        a = f"{ROOT}/{cell}-jit/dump"
        b = f"{ROOT}/{cell}-interp/dump"
        if not os.path.isdir(a) or not os.path.isdir(b):
            lines.append(f"{cell:10s} NOT_RUN (missing dump dir)"); ok_all = False; continue
        names = sorted(os.listdir(a))
        ident, state_ok, bad = [], [], []
        for n in names:
            fb = os.path.join(b, n)
            if not os.path.exists(fb):
                bad.append(f"{n}: missing in interp"); continue
            if filecmp.cmp(os.path.join(a, n), fb, shallow=False):
                ident.append(n)
            elif n.startswith("state-"):
                good, detail = only_jit_lines_differ(os.path.join(a, n), fb)
                (state_ok if good else bad).append(n if good else f"{n}: {detail[:2]}")
            else:
                bad.append(f"{n}: differs")
        verdict = "MATCH" if not bad else "FAIL"
        ok_all &= not bad
        lines.append(f"{cell:10s} {verdict}  identical={len(ident):2d}  "
                     f"state-files-with-jit-counters-only={len(state_ok):2d}  files={len(names):2d}")
        for x in bad:
            lines.append(f"            ! {x}")
        lines.append(f"            identical: {', '.join(ident)}")
    lines.append("")
    lines.append(f"# cells matched: {sum(1 for l in lines if ' MATCH ' in l)}/{len(CELLS)}")
    open(OUT, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nwrote {OUT}")
    return 0 if ok_all else 1

if __name__ == "__main__":
    sys.exit(main())
```

</details>

### 14.11 `tools/suite-case.py`

✅ **likely perfect — use it unchanged.** Fresh boot per group, `DOWN × index` then `A`,
captures the ROM's own `^gbaout:` verdict lines, and — the part that matters — `rmtree`s the
run dir and **exits 2 unless `run-info.txt`'s `dol_sha256` matches the tree's DOL**. That
gate is why the G3 evidence is trustworthy and why stale-run "evidence" is impossible (T29).
Keep it if you ever rewrite this file.

<details>
<summary>Show <code>tools/suite-case.py</code></summary>

```python
#!/usr/bin/env python3
"""
tools/suite-case.py -- drive one mGBA-test-suite group through the rig and
capture the ROM's own console output (AGENTS.md 11, gate G3).

The suite ROM logs every test verdict through the mGBA GamePak debug console
(0x04FFF600 / 0x04FFF700), which the rig's shim translates into `gbaout:` lines
in the rig log.  That is the authoritative result channel: it does not depend on
OCR or on counting rows on screen.

Menu navigation on this ROM (verified from screen dumps, see
evidence/g3/README.md):
  * the group list is entered immediately after boot; the selected row is
    marked with a '>' cursor, readable with tools/screen-text.py;
  * DOWN moves the cursor one row (RIGHT/LEFT do nothing on this menu);
  * A runs the selected group; the ROM then logs each test as it completes and
    finally shows the group's result list.

So one run per group = DOWN x group_index, then A.  Fresh boot per group keeps
every run independent and comparable between the JIT and interpreter builds.

Usage:
  suite-case.py --variant jit --group 3 [--frames 1400] [--seconds 400]
  suite-case.py --variant jit --all [--outdir evidence/g3]

Exit status is 0 when the run produced group output; the verdict (PASS/FAIL per
test) is *not* decided here -- that is a comparison between variants.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

GROUPS = [
    ("memory",        "Memory tests"),
    ("io-read",       "I/O read tests"),
    ("timing",        "Timing tests"),
    ("timers",        "Timer count-up tests"),
    ("timer-irq",     "Timer IRQ tests"),
    ("shifter",       "Shifter tests"),
    ("carry",         "Carry tests"),
    ("multiply-long", "Multiply long tests"),
    ("bios-math",     "BIOS math tests"),
    ("dma",           "DMA tests"),
    ("sio-read",      "SIO register R/W tests"),
    ("sio-timing",    "SIO timing tests"),
    ("misc-edge",     "Misc. edge case tests"),
    ("video",         "Video tests"),
]

# Frame budget per group (the DMA group is by far the largest: 718 result rows).
DEFAULT_FRAMES = 1400
FRAMES = {"dma": 3000, "timing": 1000, "sio-timing": 2000, "video": 2000}

PRESS_FIRST = 200      # first DOWN press (menu is up by ~frame 100)
PRESS_STEP = 40        # frames between presses
HOLD = 4               # frames a key stays down


def schedule(group):
    lines = ["# rig input schedule: DOWN x%d, then A" % group]
    f = PRESS_FIRST
    for _ in range(group):
        lines.append("%d down DOWN" % f)
        lines.append("%d up DOWN" % (f + HOLD))
        f += PRESS_STEP
    lines.append("%d down A" % f)
    lines.append("%d up A" % (f + HOLD))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", choices=("jit", "interp"), required=True)
    ap.add_argument("--group", type=int)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--rom", default="gba/suite.gba")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--seconds", type=int, default=400)
    ap.add_argument("--outdir", default="evidence/g3")
    ap.add_argument("--keep-run", action="store_true")
    args = ap.parse_args()

    groups = range(len(GROUPS)) if args.all else [args.group]
    if args.group is None and not args.all:
        sys.exit("--group N or --all required")

    os.makedirs(os.path.join(ROOT, args.outdir), exist_ok=True)
    for g in groups:
        name, title = GROUPS[g]
        frames = args.frames or FRAMES.get(name, DEFAULT_FRAMES)
        case_id = "g3-%s" % name
        run_id = "%s-%s" % (case_id, args.variant)
        inp = os.path.join(ROOT, "tmp/inputs/%s.txt" % run_id)
        os.makedirs(os.path.dirname(inp), exist_ok=True)
        open(inp, "w").write(schedule(g))

        rundir = os.path.join(ROOT, "tmp/dolruns", run_id)
        shutil.rmtree(rundir, ignore_errors=True)      # fresh case, no stale log
        cmd = [os.path.join(HERE, "test-case.sh"), case_id, args.variant, args.rom,
               str(frames), str(args.seconds), inp, "dump=%d" % frames]
        print("== %s: %s frames=%d" % (run_id, title, frames), flush=True)
        rc = subprocess.call(cmd, cwd=ROOT,
                             env=dict(os.environ,
                                      DOLPHIN_CPUCORE=os.environ.get("DOLPHIN_CPUCORE", "1")))
        log = os.path.join(rundir, "artifacts/debug.log")
        lines = []
        if os.path.exists(log):
            raw = open(log, "rb").read().decode("latin-1")
            lines = [m.group(1) for m in re.finditer(r"^gbaout: (.*)$", raw, re.M)]

        # refuse to record a stale run as this variant's evidence
        import hashlib
        dol = {"jit": "NooDS-Wii/jit.dol", "interp": "NooDS-Wii/NooDS-Wii-interp.dol"}[args.variant]
        want = hashlib.sha256(open(os.path.join(ROOT, dol), "rb").read()).hexdigest()
        info = os.path.join(rundir, "run-info.txt")
        got = ""
        if os.path.exists(info):
            for line in open(info):
                if line.startswith("dol_sha256="):
                    got = line.strip().split("=", 1)[1]
        if got != want:
            print("   ERROR: run used dol %s, tree has %s" % (got[:16] or "(none)", want[:16]))
            sys.exit(2)

        out = os.path.join(ROOT, args.outdir, "%s-%s.txt" % (name, args.variant))
        with open(out, "w") as f:
            f.write("# group %d: %s  variant=%s  frames=%d  run_id=%s\n" % (g, title, args.variant, frames, run_id))
            if os.path.exists(info):
                for line in open(info):
                    f.write("# %s" % line)
            f.write("# console lines: %d (first is the banner)\n" % len(lines))
            for line in lines:
                f.write(line + "\n")
        print("   rc=%d console_lines=%d -> %s" % (rc, len(lines), out), flush=True)

        if not args.keep_run:
            shutil.rmtree(os.path.join(rundir, "user"), ignore_errors=True)


if __name__ == "__main__":
    main()
```

</details>

### 14.12 `tools/g3-sweep.sh`

✅ **likely perfect (trivial) — use it unchanged.** Two lines that run all 14 groups for one
variant; it leans on `suite-case.py`'s own hash gate and exit codes, which is the point. Run
the two variants sequentially, never in parallel (§0).

<details>
<summary>Show <code>tools/g3-sweep.sh</code></summary>

```bash
#!/usr/bin/env bash
# tools/g3-sweep.sh jit|interp  -- run every suite group for one variant.
set -euo pipefail
V=${1:?usage: g3-sweep.sh jit|interp}
cd /home/user
for g in $(seq 0 13); do python3 tools/suite-case.py --variant "$V" --group "$g" || echo "group $g rc=$?"; done
```

</details>

### 14.13 `tools/g3-compare.py`

✅ **use it unchanged, with caveats.** Per group it checks: both variants produced output,
the output looks like group *N* (signature), the two runs used *different* DOLs, and the
ordered verdict lines are identical; `SCREEN_GROUPS` (the video group) falls back to
byte-identical final screens. Caveat: the signature is matched against the **first 400
lines**, so a signature regex must appear near the top of that group's output, and it must
never be run while a sweep is still writing (T30).

<details>
<summary>Show <code>tools/g3-compare.py</code></summary>

```python
#!/usr/bin/env python3
"""
tools/g3-compare.py -- G3 verdict: compare the test suite's own console output
between the JIT and the interpreter builds, group by group.

Inputs are the files written by tools/suite-case.py:
    evidence/g3/<group>-<variant>.txt

What it checks per group:
  * both variants produced output at all, and the run IDs/dol hashes are recorded;
  * the group actually ran: the console lines look like that group's tests
    (signature regex per group, taken from the ROM's own format strings);
  * the *verdict lines*, compared as an ordered list, are identical between
    variants.  A difference is a JIT regression; an identical list is parity --
    including identical FAILs, which are baseline behaviour of this emulator on
    this ROM under the rig settings and are reported as such.

Exit status is 0 only when every group is present, ran, and matched.
Prints explicit counts; never prints a bare "OK".
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GROUPS = [
    ("memory",        r"^Memory test:"),
    ("io-read",       r"^(BG\d|WININ|SOUND|INVALID|.*: Got .* vs .*: FAIL)"),
    ("timing",        r"^Timing test:"),
    ("timers",        r"^Timer count-up test:"),
    ("timer-irq",     r"^Timer IRQ test:"),
    ("shifter",       r"^Shifter test:"),
    ("carry",         r"^Carry test:"),
    ("multiply-long", r"^Multiply Long test:"),
    ("bios-math",     r"^Math test:"),
    ("dma",           r"^DMA test:"),
    ("sio-read",      r"^(M: |N8: |N32: |U: |G: |J: |SIO register)"),
    ("sio-timing",    r"^SIO timing test:"),
    ("misc-edge",     r"^Break:"),
    ("video",         r"."),
]

# The video tests draw Actual/Expected images and log nothing, so there is no
# console verdict to compare (AGENTS.md 11: "video tests are a separate oracle").
# What can be compared without a pixel oracle is the rendered result: the final
# screen of the run must be byte-identical between the two variants.  That is
# parity, not correctness -- correctness against the ROM's expected images stays
# NOT_RUN until someone builds that oracle.
SCREEN_GROUPS = {"video"}


def load(path):
    """Returns (meta dict, [lines])."""
    if not os.path.exists(path):
        return None, []
    meta, lines = {}, []
    for raw in open(path, errors="replace"):
        line = raw.rstrip("\n")
        if line.startswith("# "):
            m = re.match(r"# (\w+)=(.*)", line)
            if m:
                meta[m.group(1)] = m.group(2)
            continue
        if line.startswith("#"):
            continue
        lines.append(line)
    return meta, lines


def verdicts(lines):
    """Drop the boot banner; keep everything the group logged."""
    return [ln for ln in lines[1:] if ln.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="evidence/g3")
    ap.add_argument("--out", default="evidence/g3-summary.txt")
    args = ap.parse_args()

    base = os.path.join(ROOT, args.dir)
    rows, failures = [], 0

    for idx, (name, sig) in enumerate(GROUPS):
        v = {}
        for variant in ("jit", "interp"):
            meta, lines = load(os.path.join(base, "%s-%s.txt" % (name, variant)))
            v[variant] = (meta, lines, verdicts(lines or []))
        jm, jl, jv = v["jit"]
        im, il, iv = v["interp"]

        problems = []
        if jm is None:
            problems.append("jit output missing")
        if im is None:
            problems.append("interp output missing")
        if not problems and name not in SCREEN_GROUPS:
            if not jv:
                problems.append("no group output (banner only)")
            if not iv:
                problems.append("no interpreter output (banner only)")
        if not problems and name not in SCREEN_GROUPS and not re.search(sig, "\n".join(jv[:400]), re.M):
            problems.append("jit output does not look like group %d" % idx)
        if not problems and name not in SCREEN_GROUPS and not re.search(sig, "\n".join(iv[:400]), re.M):
            problems.append("interp output does not look like group %d" % idx)
        if not problems and jm.get("dol_sha256") == im.get("dol_sha256"):
            problems.append("both variants ran the same DOL")

        if not problems and name in SCREEN_GROUPS:
            # console carries only the banner; fall back to screen parity
            js = os.path.join(base, "screens", "%s-jit.bin" % name)
            isc = os.path.join(base, "screens", "%s-interp.bin" % name)
            if not (os.path.exists(js) and os.path.exists(isc)):
                problems.append("no screen dumps for screen-parity check")
            elif open(js, "rb").read() != open(isc, "rb").read():
                problems.append("final screen differs between variants")
            else:
                rows.append((idx, name, "MATCH(screen)", len(jv), 0, 0,
                             jm.get("dol_sha256", "?")[:12], im.get("dol_sha256", "?")[:12],
                             "console has no verdicts (video tests draw, do not log); final screen byte-identical; correctness vs the ROM's expected images = NOT_RUN"))
                continue
        if not problems:
            if len(jv) != len(iv):
                problems.append("line count differs: jit=%d interp=%d" % (len(jv), len(iv)))
            else:
                diff = [i for i, (a, b) in enumerate(zip(jv, iv)) if a != b]
                if diff:
                    problems.append("%d differing lines, first at %d: jit=%r interp=%r"
                                    % (len(diff), diff[0], jv[diff[0]], iv[diff[0]]))
                else:
                    fails = sum(1 for l in jv if "FAIL" in l or "TIMED OUT" in l)
                    skips = sum(1 for l in jv if "SKIP" in l)
                    rows.append((idx, name, "MATCH", len(jv), fails, skips,
                                 jm.get("dol_sha256", "?")[:12], im.get("dol_sha256", "?")[:12]))
                    continue
        failures += 1
        rows.append((idx, name, "FAIL-COMPARE" if not problems or "missing" not in problems[0]
                     else "NOT_RUN", 0, 0, 0, "-", "-", "; ".join(problems)))

    out = []
    out.append("# G3 (mGBA test suite) JIT vs interpreter -- per-group verdict comparison")
    out.append("# Columns: group index, name, result, console lines, lines containing FAIL/TIMED OUT,")
    out.append("#          lines containing SKIP, jit dol sha256[:12], interp dol sha256[:12]")
    for r in rows:
        out.append("%2d %-14s %-12s lines=%-6s flags=%-5s skips=%-4s jit=%s interp=%s%s"
                   % (r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                      ("  [" + r[8] + "]") if len(r) > 8 else ""))
    matched = sum(1 for r in rows if r[2].startswith("MATCH"))
    out.append("#")
    out.append("# groups matched: %d/%d -- groups with a problem: %d" % (matched, len(GROUPS), failures))
    text = "\n".join(out) + "\n"
    print(text)
    open(os.path.join(ROOT, args.out), "w").write(text)
    sys.exit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
```

</details>

### 14.14 `tools/g3-console-lines.py`

✅ **use it unchanged, with caveats.** Renders the side-by-side verdict appendix and marks
any differing row `**DIFF**`. Caveat: it imports `g3-compare` through `importlib` because
the filename contains a hyphen — keep that trick if you rename files.

<details>
<summary>Show <code>tools/g3-console-lines.py</code></summary>

```python
#!/usr/bin/env python3
"""tools/g3-console-lines.py -- render evidence/g3/<group>-<variant>.txt into a
side-by-side markdown appendix (evidence/g3-console-lines.md)."""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from importlib import import_module
groups = import_module("g3-compare").GROUPS

out = ["# G3 appendix -- the test suite's own console output, per group", "",
       "Produced by `tools/g3-console-lines.py` from `evidence/g3/<group>-<variant>.txt`.",
       "Both columns are the ROM's own log lines; the JIT column and the interpreter",
       "column are identical in every group (see `evidence/g3-summary.txt`).", ""]

for idx, (name, _sig) in enumerate(groups):
    out.append("## %d. %s" % (idx, name))
    lines = {"jit": [], "interp": []}
    for v in lines:
        p = os.path.join(ROOT, "evidence/g3", "%s-%s.txt" % (name, v))
        if os.path.exists(p):
            lines[v] = [l.rstrip("\n") for l in open(p, errors="replace")
                        if not l.startswith("#")]
    banner = lines["jit"][:1]
    j, i = lines["jit"][1:], lines["interp"][1:]
    out.append("runs: jit=%d lines, interp=%d lines (banner: %r)" % (len(j), len(i), banner))
    out.append("")
    n = max(len(j), len(i))
    if n == 0:
        out.append("_no output_")
        out.append("")
        continue
    out.append("| # | JIT | interpreter |")
    out.append("|---|-----|-------------|")
    for k in range(n):
        a = j[k] if k < len(j) else ""
        b = i[k] if k < len(i) else ""
        mark = "" if a == b else " **DIFF**"
        out.append("| %d | %s%s | %s |" % (k + 1, a.replace("|", "\\|"), mark, b.replace("|", "\\|")))
    out.append("")

open(os.path.join(ROOT, "evidence/g3-console-lines.md"), "w").write("\n".join(out) + "\n")
print("wrote evidence/g3-console-lines.md")
```

</details>

### 14.15 `tools/screen-text.py`

✅ **use it unchanged — one bug fixed in this listing.** The version below is the corrected
one: in the original, `--glyphs` mode referenced the font map before loading it
(`NameError`) and joined key rows with `|` instead of the raw 64-char key; both are fixed and
re-verified here (it decoded the real suite menu, 14/14 group names, and emitted 35 distinct
glyphs from a real screen). Remaining caveat, not a bug: unknown glyphs decode as `?` and the
shipped map does not cover every digit/punctuation cell, so dense numeric rows need map
extension — extend **only** from screens you have already decoded (T32).

<details>
<summary>Show <code>tools/screen-text.py</code></summary>

```python
#!/usr/bin/env python3
"""
tools/screen-text.py -- read the text off a dumped rig screenshot.

The rig writes one raw 240x160 buffer of 32-bit 0xRRGGBBAA pixels per request
(sd:/noods/dump/screen-N.bin, pulled to <run>/artifacts/dump/).  This tool finds
the text grid, segments it into 8x8 cells, and renders the cell bitmaps through
a font map learned from the ROM itself (tools/font-map.json).

Usage:
  screen-text.py SCREEN.bin                 decode to text
  screen-text.py SCREEN.bin --glyphs        print distinct glyphs with indices
                                            (for building/extending the font map)
  screen-text.py SCREEN.bin --art           full-resolution ASCII art

The mGBA test suite draws with an 8x8 cell font and marks the selected menu row
with a '>' cursor, so decoding the menu is also how the driver knows which group
is selected.
"""

import argparse
import json
import os
import struct
import sys

W, H = 240, 160
PITCH = 8
ORIGIN_X = 0
ORIGIN_Y = 28          # first text row of the menu; row 0 of the grid
GRID_ROWS = 15         # 28..148
GRID_COLS = 30         # 240 / 8

FONT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "font-map.json")


def load(path):
    data = open(path, "rb").read()
    if len(data) % 4:
        sys.exit("%s: not a 32-bit pixel dump" % path)
    return struct.unpack("<%dI" % (len(data) // 4), data)


def cell(px, cx, cy):
    """8x8 cell bitmap as 8 strings of '.'/'#'.  Ink = anything dark enough."""
    bits = []
    for y in range(PITCH):
        row = ""
        for x in range(PITCH):
            p = px[(ORIGIN_Y + cy * PITCH + y) * W + ORIGIN_X + cx * PITCH + x]
            row += "#" if (p & 0xFFFFFF) < 0x800000 else "."
        bits.append(row)
    return bits


def key(bits):
    return "".join(bits)


def trim_blank(bitmap):
    """Mark whether a cell carries no ink."""
    return all(set(r) == {"."} for r in bitmap)


def art(px):
    for y in range(H):
        print("".join("#" if (px[y * W + x] & 0xFFFFFF) < 0x800000 else "."
                      for x in range(W)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screen")
    ap.add_argument("--glyphs", action="store_true")
    ap.add_argument("--art", action="store_true")
    args = ap.parse_args()

    px = load(args.screen)
    if args.art:
        art(px)
        return

    cells = [[cell(px, cx, cy) for cx in range(GRID_COLS)] for cy in range(GRID_ROWS)]

    font = {}
    if os.path.exists(FONT_PATH):
        font = json.load(open(FONT_PATH))

    if args.glyphs:
        seen = {}
        for cy in range(GRID_ROWS):
            for cx in range(GRID_COLS):
                b = cells[cy][cx]
                if trim_blank(b):
                    continue
                seen.setdefault(tuple(b), (cy, cx))
        print("# %d distinct glyphs (row,col of first sighting)" % len(seen))
        for k, (cy, cx) in sorted(seen.items(), key=lambda kv: kv[1]):
            # the map key is the raw 64-character cell (8 rows x 8 cols), dots and
            # hashes, no separators -- anything else silently mislabels every glyph
            print("first at row %d col %d   label=%r" % (cy, cx, font.get(key(k), "?")))
            for r in k:
                print("   " + r)
        print()
        print("# label= ? means the cell is not in tools/font-map.json yet; add it with")
        print("# the character you know the screen shows, keyed by the raw 64-char rows.")
        return

    for cy in range(GRID_ROWS):
        line = ""
        for cx in range(GRID_COLS):
            b = cells[cy][cx]
            if trim_blank(b):
                line += " "
                continue
            line += font.get(key(b), "?")
        if line.strip():
            print("%2d|%s" % (cy, line.rstrip()))


if __name__ == "__main__":
    main()
```

</details>

### 14.16 `tools/font-map.json`

📄 **data, use it unchanged.** 50 glyphs learned from the ROM's own menu and result screens.
Keys are the raw 64-character cell contents (8 rows × 8 cols of `.`/`#`, no separators). Add
entries only by running `screen-text.py SCREEN.bin --glyphs` on a screen whose text you
already know (T32).

<details>
<summary>Show <code>tools/font-map.json</code></summary>

```json
{
".######....##......##......##......##......##......##...........": "T",
".#####...##..##..##..##..#####...##..##..##..##..#####..........": "B",
".#####...##..##..##..##..#####...##..##..##..##..##..##.........": "R",
".#####...##..##..##..##..##..##..##..##..##..##..#####..........": "D",
".##..##..##..##..##..##..##..##..##..##..##..##...####..........": "U",
".##..##..##..##..##..##..##..##..##..##...####.....##...........": "V",
".##...##.###.###.##.#.##.##...##.##...##.##...##.##...##........": "M",
".##...##.##...##.##...##.##...##.##.#.##..#####....#.#..........": "W",
".##......##......#####...##..##..##..##..##..##..#####..........": "b",
".##......##......#####...##..##..##..##..##..##..##..##.........": "h",
".##......##......##......##......##......##......######.........": "L",
".##.......##.......##.......##.....##.....##.....##.............": ">",
"..####...##..##..##.###..###.##..##..##..##..##...####..........": "0",
"..####...##..##..##..##..######..##..##..##..##..##..##.........": "A",
"..####...##..##..##..##..##..##..##.###..##..##...####.#........": "Q",
"..####...##..##..##..##..##..##..##..##..##..##...####..........": "O",
"..####...##..##..##..##...####...##..##..##..##...####..........": "8",
"..####...##..##..##......##......##......##..##...####..........": "C",
"..####...##..##..##.......####.......##..##..##...####..........": "S",
"..####...##..##......##....###....##.....##......######.........": "2",
"..####...##..##......##....###.......##..##..##...####..........": "3",
"..####....##......##......##......##......##......####..........": "[",
"..####.....##......##......##......##......##.....####..........": "I",
"..####......##......##......##......##......##....####..........": "]",
"..###......##......##......##......##......##......##...........": "l",
"..##.##...##.##..##..##..######......##......##......##.........": "4",
"...####......##......##......##......##..##..##...####..........": "J",
"...###....##.##...##.....####.....##......##......##............": "f",
"...##.##...##.##.#######..##.##...##.##..#######.##.##...##.##..": "#",
"...##.............###......##......##......##.....####..........": "i",
"....###.....##.....###.....##.....###.....##.....###............": "/",
".....##......##...#####..##..##..##..##..##..##...#####.........": "d",
"...........##.....####.....##......##......##.......##..........": "t",
".................#####...##..##..##..##..##..##..#####...##.....": "p",
".................#####...##..##..##..##..##..##..##..##.........": "n",
".................#####...##..##..##..##..##......##.............": "r",
".................###.##..#######.##.#.##.##.#.##.##.#.##........": "m",
".................##..##..##..##..##..##..##..##...####..........": "u",
".................##..##..##..##..##..##...#####......##...####..": "y",
".................##..##...####.....##.....####...##..##.........": "x",
".................##...##.##...##.##...##.##.#.##..##.##.........": "w",
"..................#####..###......####......###..#####..........": "s",
"..................####...##..##..######..##.......####..........": "e",
"..................####...##..##..##..##..##..##...#####.........": "a",
"..................####...##..##..##..##..##..##...####..........": "o",
"..................####...##..##..##..##...#####......##...####..": "g",
"..................####...##..##..##......##..##...####..........": "c",
"..........................####..................................": "-",
"..........................................##.....##.............": ",",
"..........................................##......##............": "."
}
```

</details>

### 14.17 `tools/g3-probe.sh`

⚠️ **WIP — may save you time.** A thin one-shot wrapper (config + image + run + count). It
worked once, then was superseded by direct `test-case.sh` calls. Known wart: the optional
INPUT argument must be a single pre-existing quoted file path — a multi-line string inline
does not survive the way it looks — and `mksd.sh` will simply say `no such input file`.
Treat it as a starting shape, not a tool.

<details>
<summary>Show <code>tools/g3-probe.sh</code></summary>

```bash
#!/usr/bin/env bash
# tools/g3-probe.sh ID FRAMES SECONDS 'INPUT' [EXTRA]  -- one exploratory run
set -euo pipefail
ID=$1; FRAMES=$2; SECS=$3; INPUT=${4:-}; EXTRA=${5:-}
cd /home/user
printf 'frames=%s\njit=1\nlog=1\nverify=1\nid=%s\n%s\n' "$FRAMES" "$ID" "$EXTRA" > tmp/cfg/$ID.txt
rm -f tmp/sd/$ID.raw
tools/mksd.sh tmp/sd/$ID.raw gba/suite.gba tmp/cfg/$ID.txt "$INPUT" >/dev/null
tools/run-dolphin.sh NooDS-Wii/jit.dol "$SECS" tmp/sd/$ID.raw "$ID" >/dev/null 2>&1 || true
echo "$ID: console=$(grep -c '^gbaout:' tmp/dolruns/$ID/artifacts/debug.log 2>/dev/null || echo 0) screens=$(ls tmp/dolruns/$ID/artifacts/dump/screen-*.bin 2>/dev/null | wc -l)"
```

</details>

### 14.18 `NooDS-Wii/NooDS-Wii/ppc_bridge.S`

✅ **likely perfect — use it unchanged.** The file that makes "the Makefile must handle
`.S`" real: `jit_enter`/`jit_return` plus the helper-call trampolines, with the ABI
documented in its own header comment. That ABI is the contract the emitter is written
against (§5). Assemble with the cross GCC (`-x assembler-with-cpp`) and link into **both**
variants so `JIT=0` keeps building.

<details>
<summary>Show <code>NooDS-Wii/NooDS-Wii/ppc_bridge.S</code></summary>

```asm
/*
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS-Wii is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS-Wii. If not, see <https://www.gnu.org/licenses/>.
*/

/*
    ppc_bridge.S -- native entry/exit bridge between the C++ interpreter and
    the emitted PowerPC stubs (AGENTS.md §5 "Bridge ABI").

    ABI:
      int  jit_enter(void *code, Interpreter *cpu);   // r3 = stub, r4 = cpu
      void jit_return(void);                          // stub epilogue

    jit_enter() is a normal C function: it keeps the EABI stack discipline so
    that helpers called *from* the stub (which use the C ABI) see a valid
    frame with a 32-byte parameter area.  r31 holds the Interpreter pointer
    for the whole native session and is restored on the way out.

    Stubs must never return through a helper's LR; they set r3 to the guest
    cycle cost and branch to jit_return.
*/

    .text

    .globl  jit_enter
    .type   jit_enter, @function
jit_enter:
    mflr    %r0
    stw     %r0, 4(%r1)             /* caller LR, at 4(old SP) */
    stwu    %r1, -32(%r1)           /* 32-byte frame + parameter area */
    stw     %r31, 28(%r1)           /* save callee-saved r31 */

    mr      %r31, %r4               /* r31 = Interpreter* (stub context) */
    mtctr   %r3                     /* ctr  = native stub */
    bctr                            /* enter the stub; it returns via jit_return */
    .size   jit_enter, . - jit_enter

    .globl  jit_return
    .type   jit_return, @function
jit_return:
    lwz     %r0, 36(%r1)            /* caller LR (4(old SP) of this frame) */
    lwz     %r31, 28(%r1)
    mtlr    %r0
    addi    %r1, %r1, 32
    blr
    .size   jit_return, . - jit_return

/*
    jit_call_abs0(void *fn)  -- call a C function with no arguments from a
    stub while keeping r1's frame valid.  Opens its own 32-byte parameter
    area and restores the caller's r1.  Returns whatever the callee returns
    in r3.

    Not strictly required by the first inventory (helpers take the Interpreter
    in r31 and are called through the same frame), but kept for callers that
    must not clobber the stub's own parameter area.
*/
    .globl  jit_call_abs0
    .type   jit_call_abs0, @function
jit_call_abs0:
    mflr    %r0
    stw     %r0, 4(%r1)
    stwu    %r1, -32(%r1)
    mtctr   %r3
    bctrl
    lwz     %r0, 36(%r1)
    mtlr    %r0
    addi    %r1, %r1, 32
    blr
    .size   jit_call_abs0, . - jit_call_abs0
```

</details>

### 14.19 `evidence/g1/enc_test.S` and `evidence/g1/enc_cr.S`

✅ **likely perfect — use them unchanged.** Every PowerPC form the emitter uses, written as
real assembly so `powerpc-eabi-as` + `powerpc-eabi-objdump` produce reference bytes your
encoder table must match. Assemble them first (G1), and re-run after every encoder edit —
this is how the `addco`/`subfco` XO and inverted-BO defects were caught (§5).

<details>
<summary>Show <code>evidence/g1/enc_test.S</code></summary>

```asm
    .text
    .globl t
t:
    mflr 3
    mtlr 4
    mtctr 6
    mfcr 7
    mfxer 8
    mtxer 9
    mtcrf 128, 10
    sync
    isync
    dcbst 11, 12
    icbi 11, 12
    add 3,4,5
    addo. 3,4,5
    addc 3,4,5
    addco. 3,4,5
    adde 3,4,5
    addeo. 3,4,5
    subf 3,4,5
    subfo. 3,4,5
    subfc 3,4,5
    subfco. 3,4,5
    subfe 3,4,5
    subfeo. 3,4,5
    mullw 3,4,5
    mullw. 3,4,5
    neg 3,4
    rlwinm 3,4,5,6,7
    rlwinm. 3,4,5,6,7
    rlwimi 3,4,5,6,7
    slwi 3,4,5
    srwi 3,4,5
    srawi 3,4,5
    srawi. 3,4,5
    slw 3,4,5
    srw 3,4,5
    sraw 3,4,5
    and 3,4,5
    and. 3,4,5
    andc 3,4,5
    or 3,4,5
    orc 3,4,5
    xor 3,4,5
    ori 3,4,0x1234
    oris 3,4,0x1234
    xori 3,4,0x1234
    andi. 3,4,0x1234
    andis. 3,4,0x1234
    addi 3,4,-1234
    addis 3,4,1234
    lis 3,0x1234
    li 3,0x1234
    lwz 3,100(4)
    stw 3,100(4)
    lwzu 3,100(4)
    stwu 3,100(4)
    lbz 3,100(4)
    stb 3,100(4)
    lhz 3,100(4)
    sth 3,100(4)
    lha 3,100(4)
    cmpwi 4,10
    cmplwi 4,10
    cmpw 4,5
    cmplw 4,5
    cntlzw 3,4
    blr
    b 0x100
    beq 0x100
    bne 0x100
```

</details>

<details>
<summary>Show <code>evidence/g1/enc_cr.S</code></summary>

```asm
    .text
    .globl t2
t2:
    crxor 24,0,3
    crnor 24,1,1
    crand 24,26,25
    crandc 24,2,1
    cror 24,1,25
    crnand 24,2,1
    creqv 24,0,3
    mtcrf 128,5
    rlwimi 6,7,1,1,1
    rlwimi 6,7,30,3,3
    rlwimi 6,7,5,26,26
    rlwinm 6,7,0,2,31
    rlwinm 6,7,0,3,31
    rlwinm 6,7,5,26,26
    rlwinm 6,7,0,2,2
    rlwinm 6,7,30,31,31
    li 5,0
    mtxer 5
    addco. 4,5,6
    subfco. 4,4,5
    addeo. 4,5,6
    subfeo. 4,4,5
    mullw. 4,5,6
    nor. 4,5,5
    or. 4,5,5
    xor. 4,5,6
    andc. 4,5,6
    srawi 4,5,31
```

</details>

### 14.20 The two rig input files, as actually written

📄 **examples from working runs — copy the shape, not the numbers.** `autoboot.txt` is the
rig config (`mksd.sh` rewrites `path=` itself, so you do not put a ROM path in your config),
and `input.txt` is the frame-indexed press schedule that `suite-case.py` generates for group
6 (carry): DOWN ×6 at 40-frame spacing, then A.

<details>
<summary>Show sample <code>autoboot.txt</code> / <code>input.txt</code></summary>

```text
# tmp/cfg/dbg7.txt -- the shape test-case.sh writes (EXTRA appended last)
frames=20
jit=1
log=1
verify=1
id=dbg7-jit
hb=10
```

```text
# tmp/inputs/g3-carry-jit.txt -- generated by suite-case.py for group 6 (carry)
# rig input schedule: DOWN x6, then A
200 down DOWN
204 up DOWN
240 down DOWN
244 up DOWN
280 down DOWN
284 up DOWN
320 down DOWN
324 up DOWN
360 down DOWN
364 up DOWN
400 down DOWN
404 up DOWN
440 down A
444 up A
```

</details>

### 14.21 What is *not* here, and why

- **The rig itself (`rig.h` / `rig.cpp` and its hooks).** It is ~730 lines of C++ inside the
  DUT, not a script; §8 specifies its contract (keys, outputs, checkpointing, the GBA
  console shim) and §12 lists the traps. Write it against that contract.
- **The JIT (`jit_ppc.h`, `arm_jit.{h,cpp}`).** Deliberately absent: the task is to write
  the recompiler, and §4–§6 give the contract, the encode traps and the verifier design it
  must satisfy. Nothing in this file hands you a shortcut past that work.
- **`autodump.sh` / `perf-report.py`** (mentioned in an older guide): never written here.
  `final-matrix.sh` + `compare-runs.py` + the `state:`/`frame:` log lines cover both jobs.
