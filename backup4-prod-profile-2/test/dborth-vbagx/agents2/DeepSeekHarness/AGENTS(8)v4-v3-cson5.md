# AGENTS.md — NooDS-Wii: ARMv4/v5 → PowerPC (GCN/Wii) dynamic recompiler

## Mission

Write a **new dynamic recompiler from scratch**: PowerPC instructions emitted at run time
into a Writable/Executable region and executed natively. Wrapping/renaming the interpreter
is not a JIT and fails the task. The interpreter stays as **fallback** for unsupported
opcodes and as the **reference** the JIT is judged against — same source, same flags,
differing only in `-DNOODS_JIT=0|1`.

## Acceptance criteria (checked in order)

1. **G0 build** — `make JIT=1`/`JIT=0` both link; `.S` bridge assembled into both; clean
   rebuild reproduces delivered bytes; hashes recorded.
2. **G1 encoder** — every PPC form the emitter uses is backed by an assembler oracle
   (`powerpc-eabi-as`+`objdump`) plus in-situ stub disassembly.
3. **G2 accuracy** — `rockwrestler.nds` (45 frames) and `suite.gba` (50/60/120 — 120 spans
   two pool wraps): **cycles 0, state 0, dumps 0** mismatches vs. the interpreter;
   RAM/framebuffer/screen dumps byte-identical.
4. **G3 suite** — all 14 mGBA-suite groups: identical ordered verdict lines (13 groups) +
   byte-identical final screens (14/14). Baseline FAILs identical in both builds are not
   regressions.
5. **G4 performance** — honest host ticks/frame, both builds, direction stated plainly (a
   first-correct per-instruction JIT **is slower** — see §6).
6. **Deliverables** — `deliverables/jit.dol` (+sha256), reference DOL, `handoff.md`,
   `evidence/`.

## Non-negotiables

Report PASS/FAIL/HANG/NOT_RUN honestly, never invent a pass. Feature branch only. No
unrequested roadmap work — list as next steps in `handoff.md`. `-mrvl` proves a **Wii**
target only, never GameCube/physical-console. Dolphin runs serial, one writer at a time.
Only `/home/user` persists.

---

## 1. The tree and dispatch facts

```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```
Sources live in `NooDS-Wii/`; fresh clone has no JIT/rig/tools. Key files:
`interpreter.{h,cpp}`, `interpreter_alu/branch/transfer.cpp`, `interpreter_lookup.cpp`
(dispatch tables), `core.{h,cpp}`, `memory.{h,cpp}`, `gpu.*`, `dma.cpp`.

**Dispatch (verified).** Private static member-pointer tables `armInstrs[0x1000]` /
`thumbInstrs[0x400]`:
```cpp
ARM  : (this->*armInstrs [((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
THUMB: (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
```
ARM row map: 0x00–0x1F data-proc register op2, 0x20–0x3F immediate, 0x40–0x7FF single
transfers, 0x800–0x8FF block transfers, 0xA00 `B`, 0xB00 `BL`, 0xF00 `SWI`
(MRS/MSR/multiply/BX/CLZ live inside 0x00–0x3F). `B`/`BL` mask also matches ARMv5
`BLX(imm)` — reject `op>>28==0xF` first.

**PC/pipeline (don't copy PC math from memory).** `runOpcode()` pops `pipeline[0]`, advances
`*registers[15] += size` (ARM 4 / THUMB 2) **before** calling the handler, so at handler
entry r15 = instr+2×size. Cache key = `r15 − 2×size`, T bit always mixed in.
`flushPipeline()` is the only PC-refill path.

**Condition gate.** `runOpcode` evaluates `condition[((opcode>>24)&0xF0)|(cpsr>>28)]`,
returns 1 cycle for false / calls `handleReserved` **before** any handler — hook the JIT
after this gate so cond-false/reserved opcodes never allocate a stub.

**Members you'll touch:** `registers[32]` (pointers — banking swaps them), `cpsr`, `spsr`,
`cycles`, `runOpcode()`, `flushPipeline()`, `exception()`, `setCpsr()`, `condition[]`. Add
`friend class ArmJit;` — never guess offsets.

**Frame boundary.** `Core::runCore()` is *not* one frame — count via a counter incremented
only in `Core::endFrame()`. Verified cycles/frame: NDS first 408 960 then **560 190**; GBA
first 197 120 then **280 896** (=228×308×4).

---

## 2. Build environment

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends dolphin-emu zstd xvfb mtools binutils-arm-none-eabi
```
Use only `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`). Pin and
sha256-verify: devkitPPC-r50 (metadata-only, skip `opt/` extraction), binutils 2.46.0,
crtls 2.1.0, gcc 16.1.0, newlib 4.6.0, rules 1.2.1, gamecube-tools 1.0.7, libfat-ogc 2.1.0,
libogc 3.1.0. Only `/home/user` persists — `/opt` can vanish, re-bootstrap as needed.
`arm-none-eabi-objdump -m thumb` is unsupported; use `-m arm -M force-thumb`.

**New Makefile:** `make JIT=1|0 -j4` → `jit.dol` / `NooDS-Wii-interp.dol`, separate
`obj-jit1`/`obj-jit0` trees, a flags-stamp to prevent stale-object reuse. Flags:
`-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17 -fsigned-char -ffast-math
-ffunction-sections -fdata-sections -DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1`; link
`--gc-sections -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm`. `.S` files compile via
cross GCC `-x assembler-with-cpp` and **link into both variants**. Two traps: devkitPPC
predefines `PPC=1` — name the encoder namespace `JitPpc`, never `PPC`, and always
cross-compile (a host-only harness won't catch the collision); **`-O3` does not link this
tree** (`undefined reference to Memory::writeFallback<unsigned char>`) — stay on `-O2`.
Clean rebuild must reproduce delivered bytes exactly.

---

## 3. Execution contract (what the JIT must not change)

- Keep `runOpcode()` semantics; `Interpreter::runDecoded(uint32_t)` is a pure table-lookup
  fallback — never fetch/advance PC twice.
- Fallback returns the handler's **actual** cycle cost, never a guessed constant.
- **One guest instruction = one native stub.** A stub retiring several guest instructions
  breaks scheduling equivalence; block translation is a documented future step, not a
  shortcut here.
- `flushPipeline()` stays the only PC-refill path.
- Guard the whole JIT behind a runtime layout check (§4) — a miss degrades to the
  interpreter, never to corrupt state.
- JIT-enabled build must produce byte-identical guest state to `JIT=0` for the same
  ROM/config/frames.

---

## 4. JIT design contract (the shape that works)

- **Cache:** direct-mapped, 8192 entries/CPU, key = `guest PC | T-bit`, plus the prefetched
  opcode **compared on every dispatch** (makes stores/DMA/CP15 remaps safe with no write
  protection). Epoch starts non-zero so a zeroed entry never matches; bump + clear tags on
  pool reuse/reset/toggle. Unsupported opcodes cache as negative entries → `runDecoded`.
- **Pool:** one 4 MiB `.bss` word array shared by both CPUs; cap each stub (≤160 words);
  fail compilation on a runaway generator. Flush before execution: `dcbst`/line, `sync`,
  `icbi`/line, `sync`, `isync` — a d-cache-only flush leaves stale icache lines (symptom:
  SIGILL inside the pool).
- **Pool wrap:** invalidate the **entire** pool region on wrap, not just the new stub — a
  surviving block otherwise runs against rewritten bytes (symptom: guest stalls silently at
  the first wrap). Same path needed in `reset()`.
- **Bridge ABI:** `int jit_enter(void *code, Interpreter *cpu)` (r3/r4) saves caller LR,
  `stwu` 32 bytes, saves r31=cpu, `mtctr`/`bctr`; `jit_return` restores and `blr`s. Stubs
  set r3 = cycle cost and tail to `jit_return` — never return through a helper's LR.
  Scratch r3–r12; never touch r2/r13/r14–r30; never use r0 as a D-form base.
- **Interpreter layout:** `ArmJit` is a member *after* other fields; `attach()` runs from
  the `Interpreter` constructor and derives offsets from a live instance. Reject offsets
  ≥32768 (signed D-form). **Always re-derive offsets at runtime — never hardcode them** (the
  classic failure: `attach()` never called, offsets stay 0, JIT writes to wrong addresses).
- **NZCV mapping** (`rlwinm` rotates left; MB/ME are PPC MSB numbers):

| Guest bit | Source | LSB src→dst | SH | MB=ME |
|---|---|---:|---:|---:|
| N (31) | CR0 LT (mfcr bit31) | 31→31 | 0 | 0 |
| Z (30) | CR0 EQ (mfcr bit29) | 29→30 | 1 | 1 |
| C (29) | XER CA (bit29) | 29→29 | 0 | 2 |
| V (28) | XER OV (bit30) | 30→28 | 30 | 3 |

  Merge with `rlwimi cpsr, flags, 0, 0, 3`; preserve CPSR bits 27–0 exactly. Harvest
  `mfcr`/`mfxer` immediately, before any other compare. Seed XER[CA] from guest C before
  ADC/SBC; clear XER SO/OV before arithmetic. For condition tests, `mtcrf 0x80, cpsr` maps
  N/Z/C/V onto CR0 LT/GT/EQ/SO — **a different mapping from a compare's EQ→Z**; never share
  a predicate helper between "test flags" and "compare result".
- **Encode-form facts, each wrong once (silent corruption):** only
  `addc/subfc/adde/subfe` write XER[CA] — `addco` XO=10, `subfco` XO=8;
  `BO_BIT_SET`/`CLEAR` aren't interchangeable (`beq` BO=12, `bne` BO=4); `bc` skip
  displacement is relative to the branch itself (12 bytes, not 8); branch stubs write the
  raw PC then call `flushPipeline()`; non-AL stubs need the runtime condition gate;
  GE=`creqv`, LT=`crxor`, LS=`crnor(crandc(C,Z))`; zero shift amount leaves C untouched;
  r15 materialization needs a full 32-bit load, never a 16-bit truncation (invisible on
  NDS, fatal on GBA).
- **Conservative first inventory:** ARM data-processing with immediate/immediate-shift
  operand (Rd≠r15); ARM9 `MUL/MLA/MULS/MLAS` (ARM7 timing is operand-dependent →
  fallback); `B`, `BL`, `BX`, `BLX(reg)`; THUMB imm shifts, ADD/SUB reg+imm3, MOV/CMP/
  ADD/SUB imm8, format-4 ALU minus NEG/reg-shifts/MUL, hi-reg ADD/CMP/MOV, BX/BLX(reg),
  all 14 conditional B, B, BL/BLX long. Everything else (loads/stores, LDM/STM, PUSH/POP,
  SWI, CP15, PC-writing ALU, register shifts, RRX, ARM7 multiplies, THUMB NEG) is a
  negative entry. Expand only while the verifier stays silent. Memory ops come later:
  delegate to existing `Memory` methods, never reimplement the map or treat a guest address
  as a host pointer.
- **Never translate an op whose flag semantics you haven't reproduced literally** — but the
  *interpreter handler itself* can be wrong (§5).

---

## 5. The differential verifier — and its blind spot

Snapshot registers/cpsr/spsr/pipeline/pcData; run the stub; snapshot again; restore; run
`runDecoded`; compare value copies (not pointers); log the first N mismatches. Rig keys:
`verify=0` off, `verify=1` (first execution of each new stub), `verify=2` (every dispatch,
~3.5× slower — the cost of closing the blind spot).

**Blind spot:** a stub executed once can agree with the interpreter while the *interpreter*
is wrong — divergence surfaces hundreds of frames later in guest state, not in mismatch
counters. So: `verify=1` is a **sampler, not a proof**; use `verify=2` or an end-to-end ROM
suite run (G3) for flag/carry-dependent work. **On a JIT-vs-interpreter flag mismatch, check
the interpreter handler's CPSR mask first** — a prior bug was one handler masking
`~0xC0000000` where every sibling masked `~0xF0000000`; the JIT was right. Don't "fix" the
JIT to match a buggy reference. Also: a stub whose missing branch path is never exercised
passes forever — make sure test inputs actually hit it.

---

## 6. Order of work and the performance truth

1. Rig + interpreter baseline first (no baseline → can't tell "JIT bug" from "ROM
   behavior").
2. Smallest JIT that executes (one ALU class + bridge + `attach` + fallback), proven by
   counters, not logs.
3. Verifier on from day one; expand inventory only while it stays silent.
4. G2 accuracy → G3 suite → G4 performance.

**Expect the first-correct per-instruction JIT to be *slower* than the interpreter** — plan
for it instead of discovering it at G4 (prior measurement: 1.12×–1.99× slower across
NDS/GBA workloads). The cost is almost entirely the **per-dispatch constant** (cross-TU
bridge call, cache probe, executing out of a multi-MB pool), not the verifier (<0.2%
overhead at verify=1) or cache/pool sizing. `-O3` doesn't link. Future levers, in order
(document, don't implement unasked): basic-block translation (the one thing that attacks
the per-dispatch constant) → block linking → lazy flags in CR0+XER → growable pool → widen
native inventory to loads/stores. Report both guest cycles/frame (must match exactly) and
host ticks/frame (Dolphin-only, never physical-Wii), with ratio direction stated explicitly.

---

## 7. Evidence infrastructure (ROMs, rig, Dolphin, suite automation)

```bash
curl -fL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -fL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite.gba
# rockwrestler.nds sha256 f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a (39 433 B)
# suite.gba        sha256 8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035 (524 288 B)
```
Re-verify before every run.

Build an in-DUT **rig**, inert unless `sd:/noods/autoboot.txt` is present: fresh 128 MiB
MBR/FAT16 SD image per run; config keys `path`, `jit=0|1`, `frames=N`, `verify=0|1|2`,
`dump=f[,f]`, `hb=N` (checkpoint interval); a frame-indexed key-input file; output to
`sd:/noods/debug.log` + `sd:/noods/dump/`. Force direct boot, ROM-in-RAM, disable
limiter/frameskip/threaded-rendering/audio-HLE in both variants. Checkpoint the log every
~5 frames (libfat only commits size on close). Log one `frame: n=… ticks=… cycles=…` line
plus a `state: n=… hash=…` (FNV-1a over registers/banks/cpsr/halt/cycles) per frame — this
hash is what makes a divergence bisectable. Implement the mGBA GamePak debug console
(`0x04FFF600/700/780`) in the memory path, emitting completed lines as `gbaout: <line>` —
this turns `suite.gba`'s own PASS/FAIL verdicts into machine-readable text, the strongest
accuracy evidence available (G3's oracle). `Gpu::getFrame()` may be called only once per
frame or screen dumps come back zeroed.

Drive the suite menu with fresh boot → `DOWN×group_index` → `A` (DOWN moves the cursor one
row, A runs the group; RIGHT/LEFT do nothing). Groups by index: 0 memory, 1 io-read,
2 timing, 3 timers, 4 timer-irq, 5 shifter, 6 carry, 7 multiply-long, 8 bios-math, 9 dma
(needs a larger frame budget — 718 result rows), 10 sio-read, 11 sio-timing, 12 misc-edge,
13 video (logs nothing — compare its rendered screen instead). A group is done when its
result list is on screen, not when it starts. Known baseline failures identical in both
builds (ROM-from-RAM memory tests, write-only-register readback, timing calibration) are not
regressions.

Run Dolphin headless under `xvfb-run`+`timeout`, OGL backend, serialized one-at-a-time.
`Dolphin.ini` needs `CPUCore=1`, `CPUThread=False`, `EmulationSpeed=0.0`, `SyncGPU=True`,
`DeterministicGPUThread=True`, plus `WiiSDCard=True`/`WiiSDCardPath`/`SDCard=True`/
`SDCardPath` pointed at the run-local image — without these keys Dolphin mounts no `sd:/`.
`CPUCore=0` (Dolphin's own interpreter core) is the oracle for "DUT bug vs. Dolphin bug" — a
JIT that only misbehaves on block-caching cores points at your icache/pool invalidation.
`dolphin exit=132` (SIGILL) after the DUT's own `exit(0)` is this rig's **normal**
termination. Pull artifacts from the run-local SD image only (`mcopy` can't glob inside an
image — enumerate with `mdir -b` first); missing/empty artifacts are NOT_RUN, never
"identical." A timeout is a signal (preserve the partial log), not an invitation to raise
the limit.

---

## 8. Gates, evidence, deliverables

| Gate | Evidence shape |
|---|---|
| G0 | both variants build; clean rebuild reproduces both sha256s |
| G1 | `.S`+`objdump` reference bytes for every emitted form + in-situ stub disassembly |
| G2 | explicit `cycles=/state=/dumps=` mismatch counts; every dump byte-identical |
| G3 | per-group ROM verdicts both variants + summary table + final screens, all 14 groups |
| G4 | fresh pairs, same settings; mean+median ticks/frame; ratio with direction spelled out |

CSV schema: `run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,status,notes,`
`evidence`; status ∈ PASS/FAIL/SKIP/HANG/NOT_RUN.

`handoff.md` must contain: status vs. gates with hashes; native/fallback inventory; exact
build/run commands; every defect found (symptom→cause→fix); the verifier blind spot; the
first failing gate and next step; an explicit "not verified" list (no physical console, no
save/load JIT parity, video-vs-expected NOT_RUN). Prune `evidence/` to stay inside the
snapshot budget; hash everything kept into `SHA256SUMS`.

---

## 9. Trap catalogue (highest-value, non-obvious)

| Symptom | Cause / fix |
|---|---|
| JIT writes garbage / crashes only natively | `attach()` never ran, offsets 0 |
| cmp/bne never terminates | wrong CR0/XER harvest or rotate (§4 table) |
| NZ depends on scratch state | something ran inside the mfcr/mfxer live range |
| Wild PC/LR | LDM/STM decoded as a branch; `B/BL` needs `op>>28 != 0xF` too |
| SIGILL after any store/wrap | incomplete line flush, or stale cache key missing T bit |
| Stops producing frames mid-suite | pool wrap left prior translations live — invalidate the **whole** pool |
| Interpreter-only flag failures | the reference handler's CPSR mask is wrong — check before blaming the JIT |
| `verify=1` says 0 mismatches, ROM still diverges | stub ran once from a clean state; use `verify=2` or a suite run |
| All video rows "PASS" | video logs nothing — compare pixels, not log lines |
| Host harness green, target red | `PPC=1` macro collision — always cross-compile |
| `-O3` link failure | missing `Memory::writeFallback<T>` instantiation — stay on `-O2` |

---

## 10. Tooling you'll need to write

None of this exists in a fresh clone; it supports the gates above but isn't the deliverable
itself: a devkitPPC bootstrap script (digest-gated), the dual-variant Makefile, an SD-image
builder, a serialized Dolphin launcher with provenance capture, a run comparator (cycle/
state/dump mismatch counts, stripping each build's own JIT counters before hashing), a suite
driver+comparator for the 14 GBA groups (gate every comparison on recorded `dol_sha256`
matching the current build), a screen-to-text decoder for menu navigation, the
`ppc_bridge.S` ABI file, and hand-written `.S` reference files exercising every PPC form the
encoder uses (for the G1 oracle). Keep each simple and honest about PASS/FAIL/NOT_RUN.