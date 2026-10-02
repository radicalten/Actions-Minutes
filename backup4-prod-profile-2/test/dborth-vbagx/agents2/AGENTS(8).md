# AGENTS.md — NooDS-Wii ARMv4/v5 → PowerPC (GCN/Wii) dynamic recompiler

You are adding a real dynarec for ARMv4/v5 (GBA ARM7, NDS ARM9 + ARM7) to NooDS-Wii,
keeping the interpreter as the fallback, and proving it on a headless Dolphin rig.
This file is everything that cost the previous agent time, written down so you do not
pay it again. Read §0, then §1–§5 (facts about the tree you are about to change and
the contract you must keep), then §6–§9 (method and rig), then §10–§11 (gates and
deliverables). §12 is the trap table — read it before believing any result.

"**Verified**" = confirmed on the pinned revision in §1 with the toolchain in §2.
"**Expect**" = a prior. Re-run the gate before you claim any number; every number in
this file is a result, not a guarantee.

---

## 0. Mission, acceptance criteria, non-negotiables

Deliver a **real dynamic recompiler**: PowerPC instructions emitted at run time into a
Writable/Executable region and executed natively. Renaming or wrapping the interpreter is
not a JIT and fails the task. The interpreter stays in the tree, becomes the **fallback**
for every opcode the emitter does not support, and is the **reference** the JIT is judged
against — the reference is built from the same source and the same flags, differing only
in `-DNOODS_JIT=0|1`.

Acceptance criteria, in the order they are checked (see §10 for the gate definitions):

1. **G0 build/provenance** — `make JIT=1` and `make JIT=0` both link, `.S` assembled and
   linked into both, a clean rebuild reproduces the delivered bytes, hashes recorded.
2. **G2 accuracy** — `rockwrestler.nds` (45 frames) and `suite.gba` (50 / 60 / 120 frames,
   the last spanning **two code-pool wraps**): **cycles 0, state 0, dumps 0** mismatches
   against the reference interpreter, *and* the RAM/framebuffer/screen dumps byte-identical.
3. **G3 suite** — the mGBA suite's own verdicts for **all 14 groups**, driven by real key
   input, **identical ordered verdict lines** in both builds (13 groups) plus byte-identical
   final screens (14/14). The suite's baseline FAIL rows are expected and must be identical
   in both builds; a difference *is* the regression.
4. **G4 performance** — honest host ticks/frame for both builds on the same workload, with
   the direction stated plainly (a first-correct per-instruction JIT **is slower**; §7).
5. **Deliverables** — `deliverables/jit.dol` (exact tested bytes, sha256 in
   `deliverables/SHA256SUMS`), the reference DOL, `handoff.md`, and `evidence/`.

Non-negotiables:

- Report **PASS / FAIL / HANG / NOT_RUN** honestly; keep failed attempts; never invent a pass.
- Feature branch only; never push main/production/release; preserve existing work.
- No unrequested roadmap work: lazy flags, block linking, idle detection, a growable pool,
  verifier hardening all belong in `handoff.md` as *next steps*, not in this block.
- `-mrvl` proves a **Wii** target. Never claim GameCube or physical-console success; every
  number is a Dolphin observation.
- Dolphin runs are serial and short (≈2 vCPU, ≲2 GiB RAM, ≪25 min each); one writer at a
  time. Never run two Dolphin instances to "save time" — the rig serializes on purpose.
- Only `/home/user` persists (and the snapshot cap is ~128 MiB unless you prune; §11).

## 1. The tree as it actually is

```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii      # only if absent
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

The delivered workspace may have **no `.git`** — then the pinned id above is your commit
id and `handoff.md` must say the tree was not re-verified against git.

Layout: repo root holds `Makefile`; **sources are in the subdirectory `NooDS-Wii/`**;
build products land next to the Makefile (`jit.dol`, `NooDS-Wii-interp.dol`, `obj-jit0/1`).
There is no JIT, no rig, no `tools/` in the fresh clone — you build all of it.

Relevant files: `interpreter.{h,cpp}` (class, dispatch, pipeline), `interpreter_alu.cpp`
(ALU + THUMB data processing), `interpreter_branch.cpp`, `interpreter_transfer.cpp`,
`interpreter_lookup.cpp` (the dispatch tables); `core.{h,cpp}`, `memory.{h,cpp}` (`readMap*`
fast paths, `read8/16/32`, `write*`, DMA/CP15 remaps), `main.cpp`, `settings.*`, `gpu.*`,
`dma.cpp`, `input.*`, `save_states.*`.

**Dispatch (verified).** Private static member-pointer tables `armInstrs[0x1000]` and
`thumbInstrs[0x400]` in `interpreter_lookup.cpp`; indexing:

```cpp
ARM  : (this->*armInstrs [((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
THUMB: (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
```

Row map (verified): ARM 0x00–0x1F data-processing *register* operand-2, 0x20–0x3F
*immediate*; 0x40–0x7FF single transfers; 0x800–0x8FF block transfers; 0xA00 `B`,
0xB00 `BL`, 0xC00/0xE00 CDP/MCR/MRC (mostly `unkArm`), 0xF00 `swi`. MRS/MSR/multiply/
BX/CLZ sit inside 0x00–0x3F (0x009/0x019/0x029/0x039 = mul/muls/mla/mlas; 0x121 `bx`,
0x123 `blxReg`, 0x161 `clz`). `B`/`BL` mask `(op & 0x0E000000) == 0x0A000000` also matches
ARMv5 `BLX(imm)` — reject `op>>28 == 0xF` first. THUMB ranges: 0x000–0x05F shifts,
0x060–0x07F add/sub reg+imm3, 0x080–0x0FF mov/cmp/add/sub imm8, 0x100–0x10F format-4 ALU
(2–4,7 = register shifts; 9 = NEG; 13 = MUL), 0x110–0x11F hi-reg + BX/BLX, 0x140–0x17F
L/S reg offset, 0x180–0x27F L/S imm5/SP-relative, 0x2D0 PUSH, 0x2F0 POP, 0x300 STMIA,
0x320 LDMIA, 0x340–0x37F conditional B, 0x380 B, 0x3A0 BLX, 0x3C0/0x3E0 BL; rest `unkThumb`.

**Handler names** (grep these literally; there is no `FORCE_INLINE` layer): ALU helpers are
the operand-2 functions `lli/llr/lri/lrr/ari/arr/rri/rrr/imm` (+`S` variants), expanded by
`ALU_FUNCS(func,S)` into `func{Lli,...,Imm}` (e.g. `addLri`, `andsAri`, `subImm`), plus
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
gamecube-tools 1.0.7, libfat-ogc 2.1.0, libogc 3.1.0):

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

Rules:

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

rockwrestler.nds is 39 433 B, suite.gba 524 288 B. Re-verify before every run — the source
repositories are context, not identity.

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

Rig behaviours that took work to get right — keep them:

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

## 10. GBA suite automation (this is the strongest accuracy evidence you will get)

`suite.gba` produces its verdicts **inside the ROM**: it probes for the mGBA console (above)
and logs `…: Got X vs Y: FAIL` / bare test names. Driving it with real keys gives you an
in-ROM oracle for free.

- **Menu navigation (verified from screen dumps): DOWN moves the cursor one row, A runs the
  selected group, RIGHT and LEFT do nothing on this menu.** (AGENTS.md folklore about
  `+16` right-paging applies to the *result* list, not the menu; do not encode it into the
  menu driver.) So: fresh boot → `DOWN × group_index` → `A`, pressing ~40 frames apart,
  held ~4 frames, first press ≈frame 200.
- **One boot per group**, same frames for both variants. Frame budget: 1400 default,
  `timing` 1000, `dma` 3000 (718 rows!), `sio-timing`/`video` 2000.
- Groups, by menu index: 0 memory, 1 io-read, 2 timing, 3 timers, 4 timer-irq, 5 shifter,
  6 carry, 7 multiply-long, 8 bios-math, 9 dma, 10 sio-read, 11 sio-timing, 12 misc-edge,
  13 video. The suite's own pinned test counts are 36/130/132/26/9/70/31/36/123/718/90/8/3/7
  — use them as a coverage check, not as a PASS criterion.
- **A group has finished when its result list is on screen**, not when it starts. Dump the
  final screen and check: the decoder below turns it into text, and each group's list
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

## 12. Trap catalogue

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
| T28 | `run-dolphin` "refusing to overwrite" | Existing run dir; the runner appends `-<VARIANT>` — an id already ending in the variant yields `…-jit-jit` and an empty compare |
| T29 | Evidence from an older build | Stale run dir: rmtree before the run **and** gate on `run-info.txt` `dol_sha256` |
| T30 | Sweep comparison reads NOT_RUN / line counts differ | Comparing while the sweep is still writing; wait for the process to exit |
| T31 | `…-jit-jit` directory, 0 console lines | Double variant suffix (T28) — check `ls tmp/dolruns` before believing a count |
| T32 | All-`???????` screen decode | Font-map keys must be the raw 64-character cell rows; never hand-edit by index guess |
| T33 | `screen-N.bin` empty/zero | A second `Gpu::getFrame()` caller (once per frame only — use the merged dump) |
| T34 | Suite "passes" but nothing ran | The group must reach its **result list**; check the final screen, not the boot screen |
| T35 | `-O3` link failure | Missing out-of-line `Memory::writeFallback<T>` instantiation; stay on `-O2` or add it |
| T36 | g3 numbers with a torn tail | Every run ends `dolphin exit=132` (normal) with libfat's tail uncommitted; use per-frame `state:` lines / `hb=N` |
| T37 | A compare whose direction nobody can read | `compare-runs.py` prints `ratio = B/A`; state the direction explicitly (`JIT ticks ÷ interp ticks`) in every table you publish |

## 13. Order of work, with rough budgets

1. `apt-get update`, install the rig packages, run `setup-devkitppc.sh`, verify GCC/elf2dol.
   *(≈10 min)*
2. Write `tools/{setup-devkitppc.sh,mksd.sh,run-dolphin.sh,test-case.sh}` and the rig
   (`rig.{h,cpp}` + hooks + `Makefile` with `.S`). Build both variants; one 20-frame
   `suite.gba` run must print `frame:`/`state:` lines. *(≈2–3 h)*
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
