The biggest token saving is to keep `AGENTS.md` short because it is loaded routinely, and move detailed JIT/testing facts to an on-demand reference. Use the first block as `AGENTS.md` and add the second as `docs/jit-reference.md`.

`AGENTS.md` can instruct an agent not to use subagents, but it cannot technically disable a runner feature. For enforcement, also set your agent runner’s concurrency limit to one, if it supports that setting.

````markdown
# AGENTS.md — NooDS-Wii ARM-to-PowerPC JIT

## Agent rules

- **Single agent only:** do not spawn, invoke, or delegate to subagents, workers, or parallel agents. If the runner has a concurrency setting, set it to one.
- **Minimize context and tool output:** inspect only relevant files; prefer `rg` and narrow excerpts over dumping whole files or logs. Avoid redundant builds and tests. Reuse evidence only when its commit, binaries, ROMs, and settings match the current run.
- Work on the feature branch only. Do not add unrequested roadmap work; record proposed follow-ups in `handoff.md`.
- Check the repository state before editing. Treat source code as the current truth; verify reference notes against the checked-out revision when they matter.
- Report results honestly: PASS, FAIL, HANG, or NOT_RUN. Never infer a pass from missing or empty evidence.
- Keep Dolphin runs serial, one writer at a time. Only `/home/user` persists. `-mrvl` establishes a Wii target, not GameCube or physical-console support.

## Mission and invariants

Implement a **new dynamic recompiler** that emits PowerPC code at run time and executes it natively. Wrapping or renaming the interpreter is not a JIT. Keep the interpreter as both the unsupported-opcode fallback and the reference implementation. The comparison builds must use the same source and flags, differing only in `-DNOODS_JIT=0|1`.

Preserve interpreter scheduling and pipeline semantics. A first implementation uses one native stub per guest instruction; do not combine instructions into blocks. Guard JIT execution with a runtime layout check and fall back to the interpreter on a miss.

## Acceptance gates

| Gate | Required evidence |
|---|---|
| G0 — Build | `make JIT=1` and `make JIT=0` link, including the `.S` bridge; clean builds reproduce delivered DOL hashes. |
| G1 — Encoder | Every emitted PPC form has an assembler/objdump oracle and in-situ stub disassembly. |
| G2 — Accuracy | `rockwrestler.nds` (45 frames) and `suite.gba` (50/60/120 frames; 120 spans two pool wraps): zero cycle, state, or dump mismatches against the interpreter; required dumps are byte-identical. |
| G3 — Suite | All 14 mGBA groups: identical ordered verdicts for the 13 logging groups and byte-identical final screens for all 14. Identical baseline failures in both builds are not regressions. |
| G4 — Performance | Report host ticks/frame for both builds and state the ratio direction. Expect the first correct per-instruction JIT to be slower. Measure in Dolphin, not on a physical Wii. |

Deliver `deliverables/jit.dol` with SHA-256, the reference DOL, `handoff.md`, and `evidence/`.

## Workflow

1. Establish the interpreter baseline before changing execution.
2. Implement the smallest working JIT and prove execution with counters.
3. Enable differential verification from the start.
4. Complete G2, then G3, then G4.
5. Record commands, hashes, outcomes, and limitations in `handoff.md`.

Load [`docs/jit-reference.md`](docs/jit-reference.md) only when doing JIT implementation, build/toolchain work, or rig/evidence work. It contains detailed dispatch facts, PPC invariants, and test procedures.
````

````markdown
# NooDS-Wii JIT reference

Load this reference when working on JIT implementation, the PowerPC encoder, builds, or test evidence. Keep `AGENTS.md` as the short, always-loaded policy file.

## 1. Repository and interpreter facts

Fresh starting point:

```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii
git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

A fresh clone has no JIT, rig, or test tooling. Main files: `interpreter.{h,cpp}`, `interpreter_alu/branch/transfer.cpp`, `interpreter_lookup.cpp`, `core.{h,cpp}`, `memory.{h,cpp}`, `gpu.*`, and `dma.cpp`.

Dispatch uses private static member-pointer tables:

```cpp
ARM  : (this->*armInstrs [((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
THUMB: (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
```

ARM rows: `0x00–0x1F` data-processing register operand 2; `0x20–0x3F` immediate; `0x40–0x7FF` single transfers; `0x800–0x8FF` block transfers; `0xA00` B; `0xB00` BL; `0xF00` SWI. MRS/MSR, multiply, BX, and CLZ are within `0x00–0x3F`. The B/BL masks also match ARMv5 BLX immediate; reject `op >> 28 == 0xF` first.

`runOpcode()` removes `pipeline[0]` and advances `*registers[15]` by the instruction size before calling its handler. Handler-entry r15 is therefore `instr + 2 × size` (ARM size 4, THUMB size 2); recover the instruction address as `r15 - 2 × size`. Mix the T bit into the cache key. `flushPipeline()` is the only PC-refill path.

Before calling any handler, `runOpcode()` evaluates:

```cpp
condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]
```

A false condition returns one cycle; reserved instructions go through `handleReserved`. Hook the JIT only after this gate so neither case allocates a stub.

Relevant members include `registers[32]` (pointers that change with banking), `cpsr`, `spsr`, `cycles`, `runOpcode()`, `flushPipeline()`, `exception()`, `setCpsr()`, and `condition[]`. Add `friend class ArmJit;`; do not guess offsets.

`Core::runCore()` is not one frame. Count frames in `Core::endFrame()`. Expected cycles/frame: NDS first frame 408,960, then 560,190; GBA first frame 197,120, then 280,896 (`228 × 308 × 4`).

## 2. Build environment

Install host dependencies:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  dolphin-emu zstd xvfb mtools binutils-arm-none-eabi
```

Use only `https://wii.leseratte10.de/devkitPro/`, never `pkg.devkitpro.org`. Pin and SHA-256 verify devkitPPC-r50 (metadata only; skip `opt/` extraction), binutils 2.46.0, crtls 2.1.0, GCC 16.1.0, newlib 4.6.0, rules 1.2.1, gamecube-tools 1.0.7, libfat-ogc 2.1.0, and libogc 3.1.0. `/opt` may disappear; only `/home/user` persists. `arm-none-eabi-objdump -m thumb` is unsupported; use `-m arm -M force-thumb`.

The Makefile must support:

```bash
make JIT=1 -j4
make JIT=0 -j4
```

Produce `jit.dol` and `NooDS-Wii-interp.dol`, using separate `obj-jit1`/`obj-jit0` trees and a flags stamp to prevent stale objects. Use the same flags except for `-DNOODS_JIT=0|1`:

```text
-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17
-fsigned-char -ffast-math -ffunction-sections -fdata-sections
-DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1
```

Link with `--gc-sections -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm`. Compile `.S` files with the cross GCC using `-x assembler-with-cpp`, and link them into both variants. devkitPPC predefines `PPC=1`: call the encoder namespace `JitPpc`, never `PPC`. Always cross-compile; a host-only harness will miss this collision. `-O3` does not link this tree (`undefined reference to Memory::writeFallback<unsigned char>`); stay on `-O2`. Clean rebuilds must reproduce delivered bytes exactly.

## 3. JIT execution contract

- `Interpreter::runDecoded(uint32_t)` is a pure table-lookup fallback. Never fetch or advance PC twice. Fallback returns the handler’s actual cycle cost.
- One guest instruction equals one native stub. Multi-instruction blocks are future work.
- Cache: direct-mapped, 8,192 entries per CPU. Key by guest PC and T bit; compare the prefetched opcode on every dispatch to handle stores, DMA, and CP15 remaps without write protection. Start the epoch nonzero. On pool reuse, reset, or toggle, advance the epoch and clear tags. Cache unsupported instructions as negative entries that call `runDecoded`.
- Pool: one shared 4 MiB `.bss` word array for both CPUs; cap stubs at 160 words and fail compilation if a generator runs away. Before execution, flush generated code with `dcbst` per line, `sync`, `icbi` per line, `sync`, `isync`. A D-cache-only flush can leave stale I-cache lines and cause SIGILL.
- On pool wrap, invalidate the entire pool region and its translations, not just the newly written stub. Use the same invalidation path in `reset()`.
- Bridge ABI: `int jit_enter(void *code, Interpreter *cpu)` receives r3/r4, saves caller LR, allocates a 32-byte frame, saves r31=cpu, then `mtctr`/`bctr`. `jit_return` restores and `blr`s. Stubs put cycle cost in r3 and tail to `jit_return`; never return through a helper’s LR. Use r3–r12 as scratch; do not touch r2/r13/r14–r30 or use r0 as a D-form base.
- Add `friend class ArmJit;`. Place `ArmJit` after the other `Interpreter` fields. Call `attach()` from the constructor and derive offsets from a live instance on every attach. Reject offsets outside signed D-form range (`[-32768, 32767]`). Check the runtime layout before enabling JIT execution; a failed check falls back to the interpreter, never corrupt state.

### Flags and PPC encoding

`rlwinm` rotates left; PPC MB/ME use MSB numbering.

| Guest flag | Source | Source bit → guest bit | SH | MB=ME |
|---|---|---:|---:|---:|
| N | CR0 LT (`mfcr` bit 31) | 31→31 | 0 | 0 |
| Z | CR0 EQ (`mfcr` bit 29) | 29→30 | 1 | 1 |
| C | XER CA (bit 29) | 29→29 | 0 | 2 |
| V | XER OV (bit 30) | 30→28 | 30 | 3 |

Merge using `rlwimi cpsr, flags, 0, 0, 3`; preserve CPSR bits 27–0. Harvest `mfcr`/`mfxer` immediately, before another compare. Seed XER[CA] from guest C before ADC/SBC; clear XER SO/OV before arithmetic. For condition tests, `mtcrf 0x80, cpsr` maps guest N/Z/C/V to CR0 LT/GT/EQ/SO. This differs from a compare’s EQ→Z mapping; do not share predicate helpers.

Encoding hazards to verify with the assembler oracle:

- Carry forms: `addc`, `subfc`, `adde`, and `subfe`; verify OE/XO bits (for example, `addco` XO=10 and `subfco` XO=8).
- `BO_BIT_SET` and `BO_BIT_CLEAR` differ: `beq` BO=12, `bne` BO=4.
- A `bc` skip displacement is relative to the branch itself: 12 bytes, not 8.
- Branch stubs write the raw PC and then call `flushPipeline()`. Non-AL stubs still need the runtime condition gate.
- GE=`creqv`, LT=`crxor`, LS=`crnor(crandc(C,Z))`.
- A zero shift amount leaves C untouched. Materialize r15 with a full 32-bit load, not a 16-bit truncation.

Initial native inventory:

- ARM data processing with immediate or immediate-shift operand, `Rd != r15`.
- ARM9 MUL/MLA/MULS/MLAS only; ARM7 multiply timing depends on operands, so fall back there.
- ARM B, BL, BX, BLX(register).
- THUMB immediate shifts; ADD/SUB register and imm3; MOV/CMP/ADD/SUB imm8; format-4 ALU except NEG, register shifts, and MUL; high-register ADD/CMP/MOV; BX/BLX(register); all 14 conditional branches; B; BL/BLX long.

Everything else is a negative cache entry initially, including loads/stores, LDM/STM, PUSH/POP, SWI, CP15, PC-writing ALU, register shifts, RRX, ARM7 multiplies, and THUMB NEG. Add memory operations later by calling existing `Memory` methods; do not reimplement the map or treat guest addresses as host pointers. Do not translate semantics that have not been reproduced literally.

## 4. Differential verification and performance

The verifier snapshots register values, CPSR/SPSR, pipeline, and `pcData`; runs the stub; snapshots again; restores the state; runs `runDecoded`; and compares copied values, not pointers. Log the first N mismatches.

- `verify=0`: off.
- `verify=1`: verify the first execution of each new stub.
- `verify=2`: verify every dispatch; roughly 3.5× slower.

`verify=1` is a sampler, not proof: a stub can agree once while the interpreter is wrong, and divergence may surface much later. Use `verify=2` or end-to-end ROM tests for flag/carry changes. When JIT and interpreter flags disagree, inspect the interpreter CPSR mask before changing the JIT; a prior handler used `~0xC0000000` where siblings used `~0xF0000000`. Also ensure tests actually exercise every branch path.

Work in this order: baseline, smallest executable JIT, verifier, G2, G3, G4. The first correct per-instruction JIT is expected to be slower (previous measurements: JIT/interpreter host-time ratio 1.12×–1.99× across NDS/GBA workloads). The main cost is per-dispatch overhead, not verification or pool/cache size. Future work, only if requested: basic blocks, block linking, lazy flags in CR0+XER, growable pool, then more native instructions. Report guest cycles/frame and host ticks/frame separately, with the ratio direction explicit.

## 5. ROMs, rig, and Dolphin tests

Fetch and verify ROMs before every run:

```bash
curl -fL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds \
  -o nds/rockwrestler.nds
curl -fL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba \
  -o gba/suite.gba
```

```text
rockwrestler.nds  f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  (39,433 bytes)
suite.gba         8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035  (524,288 bytes)
```

Build a DUT rig that is inert unless `sd:/noods/autoboot.txt` exists. Make a fresh 128 MiB MBR/FAT16 SD image per run. Config keys: `path`, `jit=0|1`, `frames=N`, `verify=0|1|2`, `dump=f[,f]`, `hb=N`; provide a frame-indexed key-input file. Write to `sd:/noods/debug.log` and `sd:/noods/dump/`. Force direct boot, ROM-in-RAM, and disable limiter, frameskip, threaded rendering, and audio HLE in both variants.

Checkpoint the log about every five frames (libfat commits size on close). Log per frame:

```text
frame: n=… ticks=… cycles=…
state: n=… hash=…
```

The state hash is FNV-1a over registers/banks, CPSR, halt state, and cycles. Implement the mGBA GamePak debug console at `0x04FFF600/700/780` in the memory path and emit completed lines as `gbaout: <line>`. Call `Gpu::getFrame()` only once per frame or screen dumps may be zeroed.

Run the mGBA suite with a fresh boot, then `DOWN × group_index`, then `A`; wait until the result list is displayed. Groups: 0 memory, 1 io-read, 2 timing, 3 timers, 4 timer-irq, 5 shifter, 6 carry, 7 multiply-long, 8 bios-math, 9 dma (budget for 718 result rows), 10 sio-read, 11 sio-timing, 12 misc-edge, 13 video. Video logs nothing; compare its rendered screen. Known baseline failures identical in both builds (including ROM-from-RAM memory tests, write-only-register readback, and timing calibration) are not regressions.

Run Dolphin headless with `xvfb-run` and `timeout`, OGL backend, one run at a time. Use `Dolphin.ini` settings `CPUCore=1`, `CPUThread=False`, `EmulationSpeed=0.0`, `SyncGPU=True`, `DeterministicGPUThread=True`; set `WiiSDCard=True`, `WiiSDCardPath`, `SDCard=True`, and `SDCardPath` to the run-local image. Without these keys Dolphin may not mount `sd:/`. `CPUCore=0` selects Dolphin’s own interpreter core and can help distinguish a DUT defect from a Dolphin block-cache or I-cache issue.

`dolphin exit=132` (SIGILL) after the DUT’s own `exit(0)` is normal for this rig. Preserve timeout partial logs; a timeout is evidence, not a reason to raise the limit. Pull artifacts only from the run-local SD image. Since `mcopy` cannot glob inside an image, enumerate with `mdir -b`. Missing or empty artifacts are NOT_RUN, never “identical.”

Required tooling, if absent: digest-gated devkitPPC bootstrap, dual-variant Makefile, SD-image builder, serialized Dolphin launcher with provenance, run comparator, suite driver/comparator, screen-to-text menu decoder, `ppc_bridge.S`, and assembler-oracle `.S` files. Gate comparisons on the recorded DOL SHA matching the current build. Strip each build’s own JIT counters before state hashing.

## 6. Evidence and handoff

CSV schema:

```text
run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,status,notes,evidence
```

`status` is PASS/FAIL/SKIP/HANG/NOT_RUN. Keep evidence small enough for the snapshot budget and hash every retained artifact into `SHA256SUMS`.

`handoff.md` must record gate statuses and hashes, native/fallback inventory, exact build/run commands, defects found (symptom → cause → fix), the verifier blind spot, the first failing gate and next step, and an explicit NOT VERIFIED list. Include that physical-console behavior, save/load JIT parity, and video-vs-expected are unverified unless actually tested.
````
