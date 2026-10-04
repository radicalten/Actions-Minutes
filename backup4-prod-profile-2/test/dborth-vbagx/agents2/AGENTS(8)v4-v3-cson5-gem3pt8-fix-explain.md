Here is the optimized `AGENTS.md`. 

### Key Optimizations Made:
1. **Agent Directives & Token Conservation (Section 0 added)**: Explicitly disables subagent/child agent spawning, mandates single-thread sequential execution, bans full-file context dumps (`cat`), enforces targeted log filtering (`grep`, `tail`), and sets a terse communication style to prevent context bloat.
2. **Structural Consolidation**: Reorganized into a logical execution hierarchy (Rules $\to$ Architecture $\to$ Execution $\to$ Verification $\to$ Traps), tightening verbose prose into crisp technical imperatives without losing any hardware constants, register mappings, or flags.
3. **Command Noise Suppression**: Added silent/quiet flags (`-sSL`, quiet builds) to command patterns to keep standard terminal output from filling the agent's context window.

***

```markdown
# AGENTS.md — NooDS-Wii: ARMv4/v5 → PowerPC (GCN/Wii) Dynamic Recompiler

## 0. Agent Directives & Token Conservation

- **No Subagents**: Operate strictly as a single sequential worker. Do not spawn child, background, or parallel subagents. All commands and tool calls must execute in the primary thread.
- **Context Economy**:
  - Never dump full source files or long disassembly listings into context; inspect specific line ranges with `sed -n` or targeted tools.
  - Never pipe raw test/build logs into stdout. Redirect to local log files and inspect failures using `grep`, `tail -n 40`, or `awk`.
  - Download silently (`curl -sSL`).
  - Keep code edits strictly surgical; do not rewrite entire files for small changes.
  - Write run evidence and matrices directly to disk (`evidence/`, `handoff.md`) rather than emitting large tables to the session.
- **Communication Style**: Zero conversational filler. Report status in concise, machine-readable summaries (PASS/FAIL/HANG/NOT_RUN with exact metrics).

---

## 1. Mission & Non-Negotiables

Write a **new dynamic recompiler from scratch**: PowerPC instructions emitted at runtime into a W/X memory region and executed natively. Wrapping or renaming the interpreter fails the task. The interpreter serves as **fallback** for unsupported opcodes and as the **reference baseline** (identical source/flags, varying only via `-DNOODS_JIT=0|1`).

- **Target**: Wii target only (`-mrvl`); never GameCube or physical console.
- **Dolphin**: Run headlessly and strictly serialized (one instance at a time).
- **Environment**: Only `/home/user` persists across sessions.
- **Integrity**: Report PASS/FAIL/HANG/NOT_RUN honestly; never invent a pass. Work on `feature/arm-ppc-jit` branch only. No unrequested scope.

---

## 2. Acceptance Gates (Enforced in Order)

1. **G0 (Build)**: `make JIT=1` and `make JIT=0` both link. Assembly bridge `.S` is compiled into both. Clean rebuild yields reproducible hashes.
2. **G1 (Encoder)**: Every emitted PPC instruction form is backed by an assembler oracle (`powerpc-eabi-as` + `objdump`) and verified against in-situ stub disassembly.
3. **G2 (Accuracy)**: 
   - `rockwrestler.nds` (45 frames) and `suite.gba` (50, 60, and 120 frames across pool wrap).
   - Zero mismatches vs. interpreter: `cycles: 0`, `state: 0`, `dumps: 0`. Framebuffers and RAM byte-identical.
4. **G3 (Suite)**: All 14 mGBA-suite groups pass matching baseline: identical ordered verdict lines (13 groups) + byte-identical final screens (14/14). Identical baseline FAILs are not regressions.
5. **G4 (Performance)**: Honest host ticks/frame logged for both variants. Direction clearly stated (a first-correct per-instruction JIT **is slower** than the interpreter).
6. **Deliverables**: `deliverables/jit.dol` (+ sha256), reference DOL, `handoff.md`, `evidence/`.

---

## 3. Environment & Toolchain Setup

### Base Dependencies
```bash
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends dolphin-emu zstd xvfb mtools binutils-arm-none-eabi
```

### devkitPPC Toolchain
- **Source**: Use **only** `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`).
- **Required Packages**: devkitPPC-r50, binutils 2.46.0, crtls 2.1.0, gcc 16.1.0, newlib 4.6.0, rules 1.2.1, gamecube-tools 1.0.7, libfat-ogc 2.1.0, libogc 3.1.0.
- Re-bootstrap under `/home/user` if `/opt` is wiped.
- Note: `arm-none-eabi-objdump -m thumb` is unsupported; use `-m arm -M force-thumb`.

### Build Rules & Toolchain Gotchas
- **Target Flags**: `-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections -DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1`
- **Linker Flags**: `--gc-sections -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm`
- **Makefile**: Support `make JIT=1|0 -j4`. Keep outputs in separate `obj-jit1/` and `obj-jit0/` trees with a flags-stamp file.
- **Trap — PPC Macro Collision**: devkitPPC predefines `PPC=1`. Name the encoder namespace `JitPpc`, never `PPC`.
- **Trap — No `-O3`**: Tree fails linking on `-O3` (`undefined reference to Memory::writeFallback<unsigned char>`). Stay on `-O2`.

---

## 4. Execution & Architecture Contracts

### Tree & Repository Setup
```bash
cd /home/user
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

### Dispatch Facts
- Dispatch tables: `armInstrs[0x1000]` and `thumbInstrs[0x400]`.
- Handler PC offset: `runOpcode()` advances `*registers[15] += size` (ARM: 4, THUMB: 2) **prior** to calling handlers.
- At handler entry: `r15 = instruction_address + 2 * size`.
- Cache key: `(r15 - 2 * size) | T_bit`.
- Invalidation / pipeline refill: `flushPipeline()` is the **only** valid refill path.
- Condition gate: `runOpcode` evaluates `condition[]` before handlers. Hook the JIT **after** this condition check to prevent allocating stubs for skipped/reserved opcodes.
- Frame timing: Count frames exclusively via `Core::endFrame()`.
  - NDS cycles/frame: 408,960 (initial), then 560,190.
  - GBA cycles/frame: 197,120 (initial), then 280,896 (= 228 × 308 × 4).

### Execution Invariants
- **1 Guest Instruction = 1 Native Stub**: Never translate basic blocks yet; maintain single-instruction equivalence for scheduling parity.
- Fallback must invoke `Interpreter::runDecoded(uint32_t)` without refetching or double-advancing the PC.
- Fallback returns the handler's **actual** cycle cost, not a synthetic constant.
- Dynamic layout: `ArmJit` must be declared as a member field *after* standard interpreter fields. `attach()` must derive member offsets dynamically at runtime from a live instance. Fail compilation if offset $\ge 32768$ (signed 16-bit D-form limit).

---

## 5. JIT Design & PPC Encoding Details

### Cache & Pool Structure
- **Cache**: Direct-mapped, 8192 entries/CPU. Tag = `(guest PC | T) ^ opcode`. Non-zero initial epoch; increment tags on flush/wrap.
- **Pool**: 4 MiB `.bss` word array shared by ARM9/ARM7. Stub limit $\le 160$ words.
- **Cache Invalidation**: On emit, flush icache/dcache per line: `dcbst` $\to$ `sync` $\to$ `icbi` $\to$ `sync` $\to$ `isync`.
- **Pool Wrap**: When the pool wraps or resets, invalidate the **entire** pool range, not just the newly emitted stub.

### Bridge ABI
- Prototype: `int jit_enter(void *code, Interpreter *cpu)` (`r3` = code, `r4` = cpu).
- Frame: Allocate 32 bytes (`stwu r1, -32(r1)`), save LR, save `r31 = cpu`, branch via `mtctr`/`bctr`.
- Epilogue (`jit_return`): Restore LR/r31, release stack, `blr`.
- Register usage: Scratches `r3–r12`. Callee-saved `r14–r30` and system registers `r2`/`r13` must remain untouched. Never use `r0` as base register in D-form loads/stores.

### NZCV Flag Mapping
Merge into CPSR using `rlwimi cpsr, flags, 0, 0, 3` (preserves bits 27–0):

| Guest Bit | Host Source | LSB (Src $\to$ Dst) | Shift (`SH`) | Mask (`MB=ME`) |
|---|---|---:|---:|---:|
| **N** (31) | CR0 LT (`mfcr` bit 31) | 31 $\to$ 31 | 0 | 0 |
| **Z** (30) | CR0 EQ (`mfcr` bit 29) | 29 $\to$ 30 | 1 | 1 |
| **C** (29) | XER CA (bit 29)        | 29 $\to$ 29 | 0 | 2 |
| **V** (28) | XER OV (bit 30)        | 30 $\to$ 28 | 30 | 3 |

- Harvest `mfcr` and `mfxer` immediately after arithmetic, before any intermediate branches or compares execute.
- Condition testing (`mtcrf 0x80, cpsr`) maps N/Z/C/V to CR0 LT/GT/EQ/SO. **Do not** conflate this with arithmetic result testing.

### Critical Encoding Rules
- Only `addc`, `addco`, `subfc`, `subfco`, `adde`, `subfe` update XER[CA]. (`addco` XO=10, `subfco` XO=8).
- Relative branch `bc`: skip displacement is relative to the branch instruction itself (typically 12 bytes forward, not 8).
- Branch stubs must write updated target PC to state, then call `flushPipeline()`.
- Condition logic: GE = `creqv`, LT = `crxor`, LS = `crnor(crandc(C, Z))`.
- Shifts: Zero shift count must leave Carry flag untouched.
- Loading `r15`: Must perform full 32-bit load (prevent 16-bit truncation).

---

## 6. Verification & Test Rig

### ROM Assets
```bash
curl -sSfL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -sSfL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite.gba
```
- `rockwrestler.nds` sha256: `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`
- `suite.gba` sha256: `8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`

### Differential Verification Levels
- `verify=0`: Off (JIT runs at native speed).
- `verify=1`: Verify stub state against `runDecoded` on first execution only.
- `verify=2`: Verify every dispatch (~3.5× slowdown; use to isolate divergent states).
- **Reference Blind Spot**: If interpreter logic has a faulty mask (e.g., masking `~0xC0000000` instead of `~0xF0000000`), the verifier may report false divergence. Check the interpreter's bitmask before modifying the JIT.

### Dolphin Headless Automation
- Configuration in `Dolphin.ini`:
  `CPUCore=1`, `CPUThread=False`, `EmulationSpeed=0.0`, `SyncGPU=True`, `DeterministicGPUThread=True`, `WiiSDCard=True`, `SDCard=True`.
- Point `SDCardPath` and `WiiSDCardPath` directly to the test-specific 128 MiB FAT16 image.
- Exit code 132 (SIGILL) after target `exit(0)` is expected normal termination under this rig.
- Extract files via `mdir -b` followed by explicit `mcopy` (wildcard expansion inside disk images is unsupported).

### GBA Test Suite Navigation
- Send keypress sequence: Boot $\to$ `DOWN × group_index` $\to$ `A`.
- Read debug output from mGBA console addresses (`0x04FFF600/700/780`) logged as `gbaout: <line>`.
- Group 13 (`video`): Produces no text logs; evaluate solely via pixel dumps (`Gpu::getFrame()`).

---

## 7. Trap Catalogue

| Symptom | Cause & Solution |
|---|---|
| Memory corruption on native execution | `attach()` did not execute; field offsets default to 0. |
| Endless loop on `cmp`/`bne` | Incorrect CR0/XER rotate or extraction. Verify mapping table in §5. |
| Inconsistent flags | Instructions executed between arithmetic operation and `mfcr`/`mfxer`. |
| Wild jumps via LDM/STM | Decoded as branch. Validate `op >> 28 != 0xF` on `B`/`BL`/`BLX`. |
| Intermittent SIGILL in pool | Stale icache lines or missing T-bit in tag. Ensure `icbi`+`sync`+`isync` loop. |
| Freeze on long runs | Partial invalidation during pool wrap. Must invalidate **all** 4 MiB on wrap. |
| False flag mismatch in verifier | Interpreter handler CPSR mask bug. Verify reference mask first. |
| Video tests pass with bad visuals | Video group emits no console text. Compare screen buffers directly. |
| Host links, cross-build fails | Global macro `PPC=1` conflicts with identifier. Use `JitPpc`. |
| Linker: `undefined writeFallback<T>` | `-O3` removes template instantiations. Compile with `-O2`. |

---

## 8. Tooling Scripts to Implement

All test tools must write machine-readable output to disk:
1. `tools/bootstrap_toolchain.sh`: Download and verify devkitPPC packages with sha256 checks.
2. `tools/build_sd_image.sh`: Generate fresh 128 MiB FAT16 MBR images with test configurations.
3. `tools/run_dolphin.sh`: Launch headless Dolphin under `xvfb-run` with serialized execution lock.
4. `tools/compare_runs.py`: Output mismatch metrics for cycles, state hashes, and memory dumps.
5. `tools/run_suite_gba.sh`: Automate input sequence, extract `gbaout` logs, and compare screenshots.
6. `tools/check_g1_encoder.sh`: Compare JIT emitted stubs against `powerpc-eabi-as` references.
```
