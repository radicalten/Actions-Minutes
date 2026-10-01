# AGENTS.md (v04) — NooDS-Wii ARM→PPC JIT project runbook

Purpose: develop, build and verify the ARM9/ARM7 → PowerPC dynamic recompiler in NooDS-Wii
(Wii homebrew NDS/GBA emulator). Read §6 before touching JIT code. Never claim a result you
didn't verify with a fresh run (screenshot, dump file, or jittest log).

## 0. Quick start (fresh session)
```bash
sudo apt-get install -y zstd dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri   # if missing
/home/user/setup-devkitppc.sh                                                    # if /opt/devkitpro missing
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
cd /home/user/NooDS-Wii && make -j4 && /home/user/tools/jittest/build.sh
cd /home/user/tools/jittest && ../mksd.sh sd.raw ~/nds/rockwrestler.nds \
  && ../run-dolphin.sh jittest.dol 300 sd.raw && mtype -i sd.raw.out@@1M ::/jittest.txt | tail -3
```
ROMs if missing: rockwrestler from `github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds`
(md5 `dfd1770daba69955031c0699d33b1dc6`). jsmolka tests from `github.com/jsmolka/gba-tests/raw/master/<t>/<t>.gba`.

## 1. Environment (as observed)
- Debian 13 sandbox, 2 vCPU, ~1.9 GiB RAM, no GPU. `/opt` and apt packages may vanish between
  sessions; `/home/user` persists. Long Dolphin runs: keep each tool call under ~25 min.
- Key paths: repo `/home/user/NooDS-Wii` (sources in `NooDS-Wii/NooDS-Wii/`), tools `/home/user/tools`,
  test ROMs `/home/user/nds/rockwrestler.nds`, `/home/user/gba/{arm,thumb,memory}.gba`, deliverables `/home/user/deliverables`.

## 2. Toolchain (devkitPPC r50, libogc 3.1, libfat)
- `pkg.devkitpro.org` returns 403. Use the mirror `https://wii.leseratte10.de/devkitPro/` via
  `/home/user/setup-devkitppc.sh` (idempotent; needs `zstd`). Installs into `/opt/devkitpro`.
- Env: `export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH`
- Build (≈22 s): `cd /home/user/NooDS-Wii && make -j4` → `jit.dol` (JIT default).
  `make JIT=0` → `NooDS-Wii-interp.dol` (interpreter default). Objects go to `obj-jit1/` / `obj-jit0/`.
- The Makefile compiles `*.cpp` plus `*.S`/`*.s` (via `powerpc-eabi-gcc -x assembler-with-cpp`) with `-MMD` deps.
  Use register numbers (`stw 0, 4(1)`) in `.S`. `-mregnames` is passed, but numbers are the house style.

## 3. Headless Dolphin (2503) test rig
- Packages: `apt install dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri zstd`.
- `tools/mksd.sh OUT.raw files…` builds a 128 MiB FAT16 image (MBR type 0x0E, partition at 1 MiB),
  with files in `::/noods/`. Inspect with `mdir -i img@@1M ::/` and `mtype -i img@@1M ::/file`.
- `tools/run-dolphin.sh DOL SECS SD.raw [capture-secs…]`: Xvfb :99, OGL on llvmpipe, HLE, Dolphin
  config written fresh, SD copied to `$DATA/Load/WiiSD.raw` and back to `SD.raw.out`, `cap-<t>.png`, `dolphin.log`.
  ALSA errors in the log are harmless. Dolphin exits on `SYS_ResetSystem(SYS_POWEROFF)` (sometimes slowly).
- Input: xdotool doesn't reach Dolphin. Avoid needing input; use the autoboot hook (§4). The DSU
  (cemuhook UDP 26760, `DSUClient.ini`, `WiimoteNew.ini`/`GCPadNew.ini`) path is only needed for interactive tests.
- Timing numbers from Dolphin are relative only (its timebase, not real Gekko caches).

## 4. Verification workflow (do all three after any JIT change)
1. **Differential test** (instruction level, the main oracle):
   `make -j4 && tools/jittest/build.sh`, then
   `cd tools/jittest && ../mksd.sh sd.raw ~/nds/rockwrestler.nds && ../run-dolphin.sh jittest.dol 300 sd.raw`,
   `mtype -i sd.raw.out@@1M ::/jittest.txt`. Expect `TOTAL FAILURES: 0` (22k cases, ~15 s).
   Optional seed: put `seed.txt` (a number) on the SD. Failures print opcodes, initial regs and the diff words
   (State layout: usr[0..15], fiq[16..22], svc 23-24, abt 25-26, irq 27-28, und 29-30, cpsr 31, spsr 32-36).
2. **End-to-end autodump**: `tools/autodump.sh ROM JIT(0|1) FRAMES OUTDIR [timeout]` writes
   `sd:/noods/autoboot.txt` (`path`, `jit=N`, `dump=N`). jit.dol boots the ROM, dumps fps, regs and JIT stats to
   `autodump.txt` plus the frame to `autodump.raw`, then powers off. Compare JIT vs interpreter dumps; they must match.
3. **Screenshot**: autoboot without `dump=` and `run-dolphin.sh jit.dol 50 sd.raw 45`, then
   `compare -metric AE a.png b.png null:` against the interpreter run (rockwrestler: AE 0).
- Known-good references: rockwrestler menu + green ARM7 light. thumb/memory/arm.gba end states identical in
  both cores. arm.gba fails #234 in both (interpreter lacks ARMv3 CMP-pc mode change). That isn't a JIT bug.

## 5. Source map (JIT-relevant)
| File | Role |
|---|---|
| `jit_ppc_emitter.h` | PPC encoder. `bcFwd/bFwd` + `patchBc/patchHere` for forward labels; `call/jump` use ctr via r12 when out of ±32 MB |
| `jit.h/.cpp` | `ArmJit`: `run()` dispatch → `compile()` (analyze → emit) → `jit_enter` |
| `jit_asm.S` | `jit_enter(code,cpu)` (saves r14–r31, 96-byte frame), `jit_return` (r3 = cycles), `jit_sync_icache` |
| `interpreter.*` | `armJit` member (LAST field), `jitExecArm/Thumb`, `interpretOne` (batch ≤32 while unmapped), run loops `<…,jit>` |
| `memory.h` | `jitGen[]` per 512 B over contiguous private arrays `bios9…vramI`, bumped in `Memory::write` |
| `core.cpp` `updateRun` | picks JIT/interpreter loops from `Core::jitEnabled` (= `Settings::jit` at Core creation) |
| `main.cpp` | `Settings::jit = NOODS_JIT_DEFAULT`, menu "ARM JIT (reload)", autoboot/autodump hook |

## 6. JIT invariants (break these and things fail silently)
- Host regs: guest r0–r14 = r14–r28, r29 cycles, r30 cached CPSR, r31 `Interpreter*`, scratch r0,r3–r12.
  **Never use r0 as a base in D-form/X-form loads, stores or addi** (it reads as literal 0). Never touch r2/r13.
  Only cr0/cr1 (cr2–cr4 are callee-saved). `8(r1)` is a free stack scratch slot.
- Field offsets into `Interpreter` must stay < 32 KB, so keep `armJit` as the last member.
- Between blocks: `registersUsr[15] = next_pc + 2·size` (interpreter convention). The JIT does NOT
  maintain `pipeline[]`; `saveState` calls `refreshPipeline()`, `interpretOne` calls `flushPipeline()`.
- Every exit path must store written regs + r30→cpsr (`exitWB`) unless a fallback already did (`exitNoWB`).
- Fallback contract: store state, set r15 = cur+2·size, call handler, r29 += ret. Exit if r15 changed,
  `halted` != 0, or `(cpsr ^ r30) & 0x3F`. Otherwise reload cpsr and regs.
- Analysis (`analyzeArm/Thumb`) and emission must agree. Add new natives as a new `kind`, never re-decode differently.
- Cycle costs must equal the interpreter's per instruction; the jittest compares cycles exactly.
  Conditional instrs: +1 before the condition check, cost−1 inside.
- PPC flag mapping: `mtcrf 0x80,r30` → cr0 LT=N GT=Z EQ=C SO=V. SUB carry = PPC CA (no inversion). Use `o.` forms + `mfxer`.
- Code buffers and block pools must be static `.bss` in MEM1 (helpers within `bl` range). POD structs only
  (initializers moved the pools into `.data` and grew the DOL by 1 MB). Call `jit_sync_icache` after emitting.
- Stores must bump `jitGen` (inline in emitted code and in `Memory::write`). `loadState` must call `armJit.reset()`.

## 7. Lessons learned / pitfalls
- The interpreter had real bugs that the differential test exposed: RSCS mask, THUMB NEG V, ARM7 multiply
  timing (UB `1<<32` in the loop; UMULL/UMLAL compared unsigned against negative bounds). They're fixed in
  `interpreter_alu.cpp`. Expect more; when a mismatch appears, check the interpreter against the ARM ARM before "fixing" the JIT.
- LDRT/STRT (post-index with W=1) are handled differently by the interpreter, so they go to the fallback.
- Test-harness pointer hygiene: random index registers must stay small and non-negative for LSR/ASR-scaled
  offsets, or stores escape the restored 64 KB data region (false failures via ITCM).
- libfat only commits file data on `fclose`. For logs that must survive a kill, reopen/close per write.
- `new Core` for a GBA ROM on the main thread of the jittest DOL hangs (unknown; app thread works). Use autodump for GBA.
- MEM2 is a bump allocator (no free); only one `Core` fits per session in practice.
- Dolphin's poweroff can take minutes; budget the timeout, the dump is already on SD.
- `sed -n "$(grep …),+Np"` breaks when grep returns nothing or several lines; grep first.

## 8. Next work (see 01_JIT_PROGRESS.md §6)
Block linking with a cycle budget → idle-loop detection → native THUMB reg shifts/MSR flags/BX →
lazy flags → real-hardware profiling. Re-run §4 after each step and keep the 66k-case result at zero failures.

## 9. Deliverable conventions
Numbered docs: `NN_TOPIC.md` (write-up) and `AGENTS_NN.md` (this file, increment per revision, 100–200 lines).
`/home/user/deliverables/` holds `jit.dol`, the write-up, the AGENTS file, `noods-wii-jit.patch`, `tools/` and `evidence/`.
