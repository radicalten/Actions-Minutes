# AGENTS.md — NooDS-Wii: ARMv4/v5 → PowerPC (GCN/Wii) dynamic recompiler

## Mission

Write a **new dynamic recompiler from scratch** for NooDS-Wii: PowerPC instructions emitted
at run time into a Writable/Executable region and executed natively. Renaming or wrapping
the interpreter is not a JIT and fails the task. The interpreter stays as **fallback** for
every opcode the emitter doesn't support, and as the **reference** the JIT is judged against
— same source, same flags, differing only in `-DNOODS_JIT=0|1`.

## Acceptance criteria (checked in this order)

1. **G0 — build/provenance.** `make JIT=1` and `make JIT=0` both link; the `.S` bridge is
   assembled and linked into both; a clean rebuild reproduces the delivered bytes; hashes
   recorded.
2. **G1 — encoder.** Every PowerPC form the emitter uses is backed by an assembler oracle
   (`powerpc-eabi-as` + `objdump`) and in-situ stub disassembly.
3. **G2 — accuracy.** `rockwrestler.nds` (45 frames) and `suite.gba` (50/60/120 frames — 120
   spans two code-pool wraps): **cycles 0, state 0, dumps 0** mismatches vs. the reference
   interpreter; RAM/framebuffer/screen dumps byte-identical.
4. **G3 — suite.** The mGBA suite's own verdicts for **all 14 groups**, driven by real key
   input: identical ordered verdict lines in both builds (13 groups) plus byte-identical
   final screens (14/14). Baseline FAIL rows must appear identically in both builds — a
   *difference* is the regression, not the FAIL itself.
5. **G4 — performance.** Honest host ticks/frame for both builds, same workload, direction
   stated plainly. (A first-correct per-instruction JIT **is slower** — see Performance.)
6. **Deliverables.** `deliverables/jit.dol` (exact tested bytes + sha256 in
   `deliverables/SHA256SUMS`), the reference DOL, `handoff.md`, and `evidence/`.

## Non-negotiables

- Report **PASS/FAIL/HANG/NOT_RUN** honestly; keep failed attempts; never invent a pass.
- Feature branch only — never push main/production/release; preserve existing work.
- No unrequested roadmap work (lazy flags, block linking, idle detection, growable pool,
  verifier hardening) — list these as *next steps* in `handoff.md`, don't smuggle them in.
- `-mrvl` proves a **Wii** target. Never claim GameCube or physical-console success — every
  number is a Dolphin observation.
- Dolphin runs are serial and short (≈2 vCPU, ≲2 GiB RAM, ≪25 min each); one writer at a time.
- Only `/home/user` persists (snapshot cap ≈128 MiB — prune evidence).

---

## 1. The tree

```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii      # if absent
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

Repo root holds `Makefile`; sources live in `NooDS-Wii/`; build products land next to the
Makefile (`jit.dol`, `NooDS-Wii-interp.dol`, `obj-jit0/1`). There is no JIT, rig, or `tools/`
in a fresh clone — you build all of it.

**Relevant files:** `interpreter.{h,cpp}`, `interpreter_alu.cpp`, `interpreter_branch.cpp`,
`interpreter_transfer.cpp`, `interpreter_lookup.cpp` (dispatch tables); `core.{h,cpp}`,
`memory.{h,cpp}` (fast paths, DMA/CP15 remaps), `main.cpp`, `settings.*`, `gpu.*`, `dma.cpp`,
`input.*`, `save_states.*`.

### Dispatch facts (verified — load-bearing for the JIT's dispatch gate)

Private static member-pointer tables `armInstrs[0x1000]` / `thumbInstrs[0x400]` in
`interpreter_lookup.cpp`:

```cpp
ARM  : (this->*armInstrs [((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
THUMB: (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
```

ARM row map: 0x00–0x1F data-processing register op2, 0x20–0x3F immediate; 0x40–0x7FF single
transfers; 0x800–0x8FF block transfers; 0xA00 `B`, 0xB00 `BL`, 0xF00 `SWI`. MRS/MSR/multiply/
BX/CLZ live inside 0x00–0x3F. `B`/`BL` mask `(op & 0x0E000000)==0x0A000000` also matches
ARMv5 `BLX(imm)` — reject `op>>28==0xF` first.

**PC/pipeline convention (do not copy PC math from memory).** `runOpcode()` pops
`pipeline[0]`, advances `*registers[15] += size` (ARM 4 / THUMB 2) **before** calling the
handler. At handler entry r15 = instruction + 2×size (ARM +8, THUMB +4). Cache key =
`r15 − 2×size` with the T bit always mixed in. `flushPipeline()` is the only PC-refill path
— any stub that changes PC must call the real one.

**Condition gate.** `runOpcode` evaluates
`condition[((opcode>>24)&0xF0) | (cpsr>>28)]`, returns 1 cycle for false or calls
`handleReserved`, **before** any handler. Hook the JIT after this gate so cond-false/reserved
opcodes never allocate a stub.

**Private members** you'll need access to: `core`, `arm7`, `pcData`, `pipeline[2]`,
`registers[32]` (pointers — go through them, banking swaps them), banked arrays, `cpsr`,
`spsr`, `cycles`, `runOpcode()`, `flushPipeline()`, `exception()`, `setCpsr()`,
`swapRegisters()`, `condition[]`. Add `friend class ArmJit;` — never guess offsets.

**Frame boundary.** `Core::runCore()` is not one frame. Count frames via a counter
incremented only in `Core::endFrame()`. Verified cycle counts: NDS first frame 408 960 then
**560 190**; GBA first 197 120 then **280 896** (=228×308×4).

---

## 2. devkitPPC environment

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends dolphin-emu zstd xvfb mtools binutils-arm-none-eabi
```

Use only `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`/dkp-pacman).
Pin: devkitPPC-r50, binutils 2.46.0, crtls 2.1.0, gcc 16.1.0, newlib 4.6.0, rules 1.2.1,
gamecube-tools 1.0.7, libfat-ogc 2.1.0, libogc 3.1.0 — record a sha256 manifest and verify
every download. `devkitPPC-r50` itself is metadata-only (no `opt/` payload) — skip it when
extracting. Only `/home/user` persists; `/opt` can vanish — be ready to re-bootstrap.

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-gcc --version     # GCC 16.1.0
command -v elf2dol
```

Note: `arm-none-eabi-objdump -m thumb` is unsupported; use `-m arm -M force-thumb`.

---

## 3. The Makefile (new; stock one is replaced)

- `make JIT=1 -j4` → `jit.elf/.dol`; `make JIT=0 -j4` → `NooDS-Wii-interp.elf/.dol`.
  `.SUFFIXES:` first so the built-in `.S` rule never fires.
- Separate object trees `obj-jit1`/`obj-jit0`, `-MMD -MP`, and a flags-stamp file so a flags
  change can't silently reuse stale objects.
- `.S` compiled by the cross GCC as `-x assembler-with-cpp`; **the resulting object links
  into both variants**.
- Flags: `-mrvl -mcpu=750 -meabi -mhard-float`; C++
  `-O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections`;
  `-DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1 -DLOG_LEVEL=3`; link `--gc-sections` with
  `-L/opt/devkitpro/libogc/lib/wii -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm`.
- devkitPPC predefines `PPC=1` — name the encoder namespace `JitPpc`, never `PPC`, and
  always cross-compile (a host-only harness won't see the collision).
- `-O3` does **not** link this tree (`undefined reference to
  Memory::writeFallback<unsigned char>`) — stay on `-O2` unless you add the missing
  out-of-line instantiation.
- A clean rebuild must reproduce delivered bytes exactly; record revision/dirty
  state/flags/DOL sha256 before every run.

---

## 4. Execution contract (what the JIT must not change)

- Keep `runOpcode()` semantics; implement `Interpreter::runDecoded(uint32_t)` as a pure
  table-lookup fallback (caller already passed the condition gate) — never fetch/advance PC
  twice.
- Fallback returns the handler's **actual** cycle cost, never a guessed constant.
- **One guest instruction = one native stub.** A stub retiring several guest instructions
  breaks scheduling equivalence; block translation is a documented future step, not a free
  optimization here.
- `flushPipeline()` stays the only PC-refill path.
- Guard the whole JIT behind a runtime layout check (§5) — a miss degrades to the
  interpreter, never to corrupt state.
- A JIT-enabled build must produce byte-identical guest state to `JIT=0` for the same
  ROM/config/frames.

---

## 5. JIT design contract (the shape that works)

- **Cache:** direct-mapped, 8192 entries/CPU, key = `guest PC | T-bit`, plus the prefetched
  opcode **compared on every dispatch** (makes stores/DMA/CP15 remaps safe with zero write
  protection). Epoch starts non-zero so a zeroed entry never matches; bump epoch + clear tags
  on pool reuse/reset/toggle. Unsupported opcodes cache as negative entries → `runDecoded`.
- **Pool:** one 4 MiB `.bss` word array shared by both CPUs; cap each stub (≤160 words is
  plenty) and fail compilation on a runaway generator. Flush before execution: `dcbst` per
  line, `sync`, `icbi` per line, `sync`, `isync` — a d-cache-only flush leaves stale icache
  lines (symptom: SIGILL inside the pool).
- **Pool wrap:** when the pool offset wraps, invalidate the **entire** region, not just the
  new stub — a surviving block otherwise runs against rewritten bytes (symptom: guest stalls
  silently at the first wrap). Same path needed in `reset()`.
- **Bridge ABI:** `int jit_enter(void *code, Interpreter *cpu)` (r3/r4) saves caller LR,
  `stwu` 32 bytes, saves r31 = cpu, `mtctr`/`bctr`; `jit_return` restores and `blr`s. Stubs
  set r3 = cycle cost and tail to `jit_return` — never return through a helper's LR.
  Scratch r3–r12; never touch r2/r13/r14–r30; never use r0 as a D-form base.
- **Interpreter layout:** `ArmJit` is a member *after* other fields; `attach()` runs from the
  `Interpreter` constructor and derives offsets from a live instance. Reject any offset
  ≥32768 (signed D-form). **Re-derive offsets at runtime — never hardcode them** (a prior
  bring-up's single most expensive bug was `attach()` never being called, leaving offsets 0).
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
  one predicate helper between "test flags" and "compare result".
- **Encode-form facts that were each wrong once (silent corruption):** only
  `addc/subfc/adde/subfe` write XER[CA] — `addco` XO=10, `subfco` XO=8; `BO_BIT_SET`/`CLEAR`
  are not interchangeable (`beq` BO=12, `bne` BO=4); `bc` skip displacement is relative to the
  branch itself (12 bytes, not 8); branch stubs write the raw PC then call `flushPipeline()`;
  non-AL stubs need the runtime condition gate; GE=`creqv`, LT=`crxor`,
  LS=`crnor(crandc(C,Z))`; a zero shift amount leaves C untouched; r15 materialization needs
  a full 32-bit load (`lis/ori`), never a 16-bit truncation (invisible on NDS, fatal on GBA).
- **Conservative first inventory:** ARM data-processing with immediate/immediate-shift operand
  (Rd≠r15); ARM9 `MUL/MLA/MULS/MLAS` (ARM7 timing is operand-dependent → fallback); `B`, `BL`,
  `BX`, `BLX(reg)`; THUMB imm shifts, ADD/SUB reg+imm3, MOV/CMP/ADD/SUB imm8, format-4 ALU
  minus NEG/reg-shifts/MUL, hi-reg ADD/CMP/MOV, BX/BLX(reg), all 14 conditional B, B, BL/BLX
  long. Everything else (loads/stores, LDM/STM/PUSH/POP, SWI, CP15, PC-writing ALU, register
  shifts, RRX, ARM7 multiplies, THUMB NEG) is a negative entry. Expand only while the
  verifier stays silent. Memory/transfer ops come later: delegate to existing `Memory`
  methods, never reimplement the map or treat a guest address as a host pointer.
- **Do not translate an op whose flag semantics you haven't reproduced literally** — but the
  *interpreter handler* can itself be wrong (see §6).

---

## 6. The differential verifier — and its blind spot

Snapshot registers/cpsr/spsr/pipeline/pcData; run the stub; snapshot again; restore; run
`runDecoded`; compare. Keep the interpreter's result; log the first N mismatches. Rig keys:
`verify=0` off, `verify=1` (first execution of each new stub + current-level dispatches),
`verify=2` (every dispatch; expensive).

**Blind spot:** a stub executed once can agree with the interpreter while the *interpreter*
is wrong, with the divergence showing up only much later in guest state. `verify=1` can read
0 mismatches for hundreds of frames while an end-to-end ROM test suite still proves
divergence. Rules:

- `verify=1` is a **sampler, not a proof**; `verify=2` is what closes it (measured ≈3.5×
  slower).
- For flag/carry-dependent work, always also run the ROM's own test suite (G3) as the oracle.
- **On a JIT-vs-interpreter flag mismatch, inspect the interpreter handler's CPSR mask
  first** — one prior bug was an interpreter handler masking `~0xC0000000` instead of the
  `~0xF0000000` every sibling handler used; the JIT was right. Don't "fix" the JIT to match
  a buggy reference.
- Compare value copies, not pointers; a stub whose missing branch is never taken passes
  forever — exercise it.

---

## 7. Order of work and the performance truth

1. Rig + interpreter baseline first (no baseline → can't tell "JIT bug" from "ROM behavior").
2. Smallest JIT that executes (one ALU class + bridge + `attach` + fallback), proven by
   counters, not logs.
3. Verifier on from day one.
4. Expand inventory only while it stays silent.
5. G2 accuracy → G3 suite → G4 performance.

**Expect the first-correct per-instruction JIT to be *slower* than the interpreter** — plan
for it instead of discovering it at G4. Measured previously: 1.12×–1.99× slower across
NDS/GBA workloads. Where the time goes: not the verifier (<0.2% difference at verify=1);
mostly the **per-dispatch constant** (~54 host cycles/guest instruction: cross-TU
`jit_enter`/`jit_return`, cache probe, executing out of a 4 MiB pool, C++ round-trips for
unsupported ops). Cache/pool size tuning buys nothing. `-O3` doesn't link.

Future levers, in priority order (document as next steps, don't implement unasked):
1. basic-block translation (attacks the per-dispatch constant),
2. block linking/branch following,
3. lazy flags / flags in CR0+XER,
4. growable pool (last — current cost is ≈0.5%),
5. widening native inventory to loads/stores/PUSH/POP.

Report both guest cycles/frame (must match interpreter exactly) and host ticks/frame
(performance, Dolphin-only, never physical-Wii). State the ratio direction explicitly
(JIT÷interp).

---

## 8. ROMs, SD image, rig contract

```bash
cd /home/user; mkdir -p nds gba evidence tmp tools
curl -fL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -fL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite.gba
printf '%s\n' \
 'f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  nds/rockwrestler.nds' \
 '8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035  gba/suite.gba' \
 | sha256sum -c -
```

`rockwrestler.nds` 39 433 B, `suite.gba` 524 288 B. Re-verify before every run.

**SD image:** fresh per run, never reused; 128 MiB raw, MBR partition 1 at 1 MiB (type 0x0E),
FAT16, files under `::/noods/`; `mformat -i IMG@@1M` (no `-F`); stage via mtools, no mount.

**Rig contract** (implement in the DUT; inert without `sd:/noods/autoboot.txt`). Keys:
`path`, `jit=0|1`, `frames=N`, `log=1`, `trace`, `verify=0|1|2`, `stubdump=N`, `id`,
`dump=f[,f]`, `hb=N`, `scanhb=N`, `jitfreeze=N`, `autodump=N`, `suite=N`. Input file:
`FRAME down|up KEY[,KEY…]`. Output: `sd:/noods/debug.log`, dumps in `sd:/noods/dump/`.
Force direct boot, ROM-in-RAM, disable limiter/frameskip/threaded 2D+3D/audio/ARM7-HLE/
DSi/filters in both variants.

**Key rig behaviors:**
- Checkpoint the log every ~5 frames (`fclose`+`fopen("a")`), never per line — libfat only
  commits size on close, and per-line commits cost ≈0.4 s/line through Dolphin's SD emulation.
- Per-frame log: `frame: n=… runTicks=… ndsCycles=… fps=…` plus `state: n=… hash=… …` (a
  per-CPU FNV-1a over registers/banks/pipeline/cpsr/spsr/halt/cycles) — this hash is what
  makes a divergence bisectable.
- `Gpu::getFrame()` may be called only **once per frame**; a second caller yields
  empty/zero screens.
- **GBA console shim (the G3 oracle):** implement the mGBA GamePak debug console
  (`0x04FFF600` text, `0x04FFF700` control, `0x04FFF780` handshake) and emit each completed
  line as `gbaout: <line>` — this is how the suite's PASS/FAIL verdicts become
  machine-readable.

---

## 9. Dolphin launcher notes

- Unique run id; refuse to overwrite; `flock` serialize; close the lock FD in the child.
- Isolated per-run `user/{Config,Load}`; hash DUT/ROM/config before launch.
- `Dolphin.ini`: `CPUCore=1`, `CPUThread=False`, `EmulationSpeed=0.0`, `DSPHLE=True`,
  `SyncGPU=True`, `DeterministicGPUThread=True`, plus `WiiSDCard=True` +
  `WiiSDCardPath`/`SDCard=True`/`SDCardPath` pointed at the run-local image — without these
  Dolphin mounts no card.
- Launch under `xvfb-run` + `timeout`, OGL backend, headless (`-u user -p x11 -v OGL -a HLE`).
- `CPUCore=0` (Dolphin's pure interpreter) is the oracle for "DUT bug vs. Dolphin bug" — a
  JIT that only misbehaves on block-caching cores points at your icache/pool invalidation.
- `dolphin exit=132` (SIGILL) after the DUT's own `exit(0)` is this rig's **normal**
  termination.
- Pull artifacts from the run-local `WiiSD.raw`; `mcopy` can't glob inside an image —
  enumerate with `mdir -b` and copy each name (`-w` silently loses entries). Missing/empty
  artifacts are NOT_RUN, never "identical."
- Budgets: GBA 120f interpreter ≈3s, JIT ≈13s; NDS 45f ≈5s; full 14-group suite sweep
  ≈8 min/variant. A timeout is a signal, not an invitation to raise the limit.

---

## 10. GBA suite automation (strongest accuracy evidence)

`suite.gba` logs verdicts through the mGBA console shim (§8). Menu: **DOWN** moves the
cursor one row, **A** runs the group, RIGHT/LEFT do nothing — fresh boot → `DOWN×index` → `A`
(~40 frames apart, held ~4, first press ≈frame 200). One boot per group, same frames both
variants. Frame budget: 1400 default, `timing` 1000, `dma` 3000 (718 rows), `sio-timing`/
`video` 2000.

Groups (menu index): 0 memory, 1 io-read, 2 timing, 3 timers, 4 timer-irq, 5 shifter,
6 carry, 7 multiply-long, 8 bios-math, 9 dma, 10 sio-read, 11 sio-timing, 12 misc-edge,
13 video.

A group finishes when its **result list** is on screen, not when it starts. Acceptance is
parity: identical ordered verdict lines (13 groups) + byte-identical final screens (14/14).
Known baseline failures appearing identically in both builds (ROM-from-RAM memory tests,
io-read write-only-register readback, timing calibration, sio timeouts) are not regressions.
The video group logs nothing — compare dumped final screens instead; correctness vs. the
ROM's own expected images is out of scope unless you build that oracle.

Screen decoding: `screen-N.bin` is raw `uint32` pixels, ink = `(px & 0xFFFFFF) < 0x800000`,
glyph cells 8×8 at origin (0,28), 30×15 grid; build a font map keyed by the raw 64-char cell,
extending it only from screens you've already decoded.

---

## 11. Gates, evidence, deliverables

| Gate | Evidence shape |
|---|---|
| G0 | both variants build; `.S` object in the link line; clean rebuild reproduces both sha256s; a 20-frame run of `deliverables/` bytes with 0/0/0 mismatches vs. the in-tree build |
| G1 | `.S` + `objdump` reference bytes for every emitted form, plus in-situ stub disassembly |
| G2 | per cell: explicit `cycles=/state=/dumps=` mismatch counts; dump-level parity (every ram/fb/screen file byte-identical) |
| G3 | per-group ROM verdicts for both variants, a summary table, final screens for all 14 groups |
| G4 | fresh pairs, same ROM/frames/settings; mean **and** median ticks/frame; ratio stated with direction spelled out |

CSV schema, one row per run: `run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,`
`status,notes,evidence`; status ∈ PASS/FAIL/SKIP/HANG/NOT_RUN.

`deliverables/`: `jit.dol` (+SHA256SUMS), reference DOL, `handoff.md`, headline compare
files. `handoff.md` must contain: status vs. gates with hashes; native/fallback inventory;
exact build/SD/run/pull commands; hashed evidence index; the ticks table; every defect found
(symptom→cause→fix); the verifier blind spot; the first failing gate and next step; and an
explicit "not verified" list (no physical console, no GameCube build, no save/load JIT
parity, video-vs-expected NOT_RUN).

`evidence/` must be self-contained but small — prune run directories (keep `debug.log`,
digests, `dump/`, `run-info.txt`; drop `WiiSD.raw` copies and `user/`), then hash everything
into `SHA256SUMS`.

---

## 12. Trap catalogue

| Symptom | Cause / fix |
|---|---|
| JIT writes garbage / crashes only natively | `attach()` never ran, offsets 0; dump the layout every attach |
| `jit=1` but nothing is compiled | check `jit.enabled` and the actual call site; trust counters, not intent |
| Guest stalls after a few instructions | a wrong early stub; verify at compile time, not via longer runs |
| cmp/bne never terminates | wrong CR0/XER harvest or rotate; check first-diff frame |
| NZ depends on scratch state | comparing against r0 instead of `cmpwi r,0`; nothing may run inside the mfcr/mfxer live range |
| Wild PC/LR | LDM/STM decoded as branch; `B/BL` needs `op>>28 != 0xF` too |
| SIGILL after any store/wrap | incomplete line flush, or stale cache key missing the T bit |
| Registers corrupt after helper call | helper LR/stack misuse, no parameter area, clobbered r2/r13 |
| Cycle drift, data identical | fallback returning a guessed cost, or double fetch/PC advance |
| ARM7/ARM9 NDS budget wrong | scheduler ×2/>>1 applied twice or inside the JIT |
| "Guest hang" that isn't | sampling alias; use `endFrame` counters + full state snapshots |
| Compile errors on private members | `friend class ArmJit;`, never guess offsets |
| Host harness green, target red | `PPC=1` macro collision; always cross-compile |
| Stops producing frames ~frame 51 | pool wrap left prior translations live; invalidate the **whole** pool |
| Interpreter-only flag failures | reference handler's CPSR mask is wrong; check it before blaming the JIT |
| `verify=1` says 0 mismatches, ROM still diverges | stub ran once from a clean state; use `verify=2` or a suite run |
| All video rows "PASS" | video logs nothing — compare pixels, not log lines |
| Full clean rebuild gives different bytes | non-deterministic input (paths/dates); pin flags, keep `__DATE__` out |
| `-O3` link failure | missing `Memory::writeFallback<T>` instantiation; stay on `-O2` |
| Suite comparison reads NOT_RUN / wrong counts | compared mid-sweep; wait for the sweep process to exit |

---

## 13. Order of work, rough budgets

1. Bootstrap toolchain, write/verify the rig contract and dual-variant Makefile; one
   20-frame `suite.gba` run must print `frame:`/`state:` lines. *(~2–3h)*
2. Baseline the interpreter on `suite.gba` 50/60/120 and `rockwrestler.nds` 45; save
   per-frame state hashes as the oracle.
3. Build encoder + bridge + cache + pool + one ALU class; prove native execution via
   counters; turn the verifier on; grow the inventory while it stays silent. *(the long
   stretch — most debugging is encoder forms and flag math, §5)*
4. G2 matrix + dump parity; GBA suite driver, both variants, compare.
5. G4 ticks/frame table, `handoff.md`, `deliverables/`, pruned `evidence/`.

Never leave the tree in a state where `JIT=1` builds but `JIT=0` doesn't — the interpreter
build is the reference for every claim.

---

## 14. Supporting tooling you'll need to write

None of this exists in a fresh clone; build it as you go (none of it is the graded
deliverable, but each gate above depends on it):

- **`tools/setup-devkitppc.sh`** — idempotent toolchain bootstrap, digest-gated (§2).
- **`Makefile`** — dual-variant build (§3).
- **`tools/mksd.sh`** — builds a fresh MBR/FAT16 SD image per run, stages ROM + config (§8).
- **`tools/run-dolphin.sh`** — flock-serialized headless Dolphin launch, provenance capture,
  artifact pull (§9).
- **`tools/test-case.sh`** — one matched {config, image, run} per test case.
- **`tools/compare-runs.py`** — cycle/state/dump mismatch counts between two runs; strip each
  build's own JIT counters before hashing state dumps.
- **`tools/suite-case.py`** / **`tools/g3-sweep.sh`** / **`tools/g3-compare.py`** — drive and
  compare the 14 GBA suite groups (§10); gate every comparison on `dol_sha256` matching the
  current tree build, and `rmtree` stale run dirs first.
- **`tools/screen-text.py`** + a font map — decode dumped screens into text for menu
  navigation and verdict screens (§10).
- **`NooDS-Wii/NooDS-Wii/ppc_bridge.S`** — the `jit_enter`/`jit_return` bridge (§5's ABI).
- **`evidence/g1/*.S`** — hand-written assembly exercising every PPC form the encoder uses,
  for `powerpc-eabi-as`/`objdump` oracle comparison (G1).
