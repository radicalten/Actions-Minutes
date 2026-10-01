# AGENTS.md (v07) — NooDS-Wii: ARM9/ARM7 → PowerPC JIT recompiler

**Purpose.** Single project runbook and coding-agent operating rules for adding a dynamic
recompiler (JIT) that translates ARMv4T/ARMv5 guest code (NDS/ARM9, NDS/ARM7, GBA) into
PowerPC code (GameCube/Wii) inside NooDS-Wii. The existing interpreter stays in the tree as a
**fallback path only**; the JIT is the default execution path.

**Read §6 and §8 before you touch JIT code.** §5 defines what counts as done. §9 is the
debugging ladder that localises a divergence in minutes instead of days.

**Prime directive.** Never report a result you did not see in a fresh run, and never report a
result without the artifact that produced it. A compile is not a verification. `NOT_RUN` and
`HANG` are legitimate, useful outcomes; guesses are not.

v07 supersedes v06 (the v06 text is preserved at `/home/user/uploads/AGENTS.md`). v07 was
written after the first bring-up attempt and front-loads the parts that cost the most time:
the cycle/flag contract (§6), the trap catalogue (§8), and the bisect ladder (§9).

---

## 0. Task and definition of done

Build and validate an ARM→PowerPC JIT for NooDS-Wii, with these acceptance criteria:

1. devkitPPC toolchain environment configured and reproducible (§2).
2. A makefile that builds `.S` assembly sources and both variants:
   `jit.dol` (JIT default) and `NooDS-Wii-interp.dol` (§2.3).
3. A headless Dolphin test harness configured and driven from scripts (§3).
4. `rockwrestler.nds` (NDS) and `suite.gba` (GBA) both run, and emulation accuracy is
   verified against the interpreter build (§4, §5).
5. Performance compared between JIT and interpreter builds from fresh Dolphin runs,
   evidence from the in-emulator debug log (§4 gate 4).
6. Deliverables in `/home/user/deliverables/`: **`jit.dol`** and **`handoff.md`**, plus the
   GBA suite evidence CSV (§10).

Not in scope unless the user asks: roadmap items beyond the above (see §10.5).

---

## 1. Operating rules (hard constraints)

- This sandbox has **no PTY and no stdin**. Never launch interactive REPLs. Use one-shot
  modes only: `codex exec`, `claude -p`, `opencode run`, `pi -p`.
- Delegated prompts must be self-contained: read this file first, then state task, scope,
  approval boundaries, constraints, output location, and definition of done.
- **Do not install global tools or packages without user approval.** The documented bootstrap
  set in §2.2 is pre-approved for this project; anything else needs a question first. Never
  put secrets, keys, or tokens in prompts, arguments, or process I/O.
- Bash default timeout is 30 s. Use `1800` for long foreground agent runs; keep each Dolphin
  invocation under about 25 minutes.
- Long jobs run as background processes (`start_process`); monitor with
  `get_process_output` (`wait_for: exit|log|port`), never with `ps`/`sleep` loops. A wait
  timeout is not a completion.
- **Verify ROM hashes before comparing runs** (§B). A different ROM invalidates every
  comparison in the session.
- Route changes through reviewable feature branches. Do not push to `main`, `production`, or
  `release/*` before human review. Parallel agents use isolated worktrees under `/home/user`
  and never share a writable checkout.
- Avoid concurrent Dolphin runs — this sandbox has ~2 vCPU / 1.9 GiB and Dolphin writes back
  to the SD image at shutdown.
- Protect existing work: inspect `git status` (if present) before editing; never overwrite
  unrelated changes, evidence, or deliverables.
- Report commands, outcomes, and fresh artifacts. Send a short start note for background
  work; update on milestones, failures, user action needed, and completion.
- Shell gotchas that have bitten: `sed -n "$(grep …),+Np"` fails when grep matches zero or
  more than one line; `awk` has no `strtonum` — use a `python3` one-liner instead; the
  workspace is not guaranteed to keep build artifacts between turns (§2.1).
- Naming: findings write-ups use `NN_TOPIC.md`; this runbook is versioned `AGENTS_NN.md`.
  Keep the **status ledger (§11) short.** Do not overwrite existing deliverables or advance
  the version without checking scope.

---

## 2. Environment: bootstrap, toolchain, build

### 2.1 What persists, what does not

- `/home/user` (sources, tools, ROMs, `tmp/dkp` package cache, dumps) persists across turns
  **in principle**, but treat it as unverified: compiled DOLs, `tmp/dolruns/`, and pulled
  dump folders have gone missing between turns. `git` history is not present in
  `/home/user/NooDS-Wii` (verified: `git` is installed but the tree has no `.git`); the
  source tree is the source of truth.
- `/opt` and apt-installed packages **do not persist** (`/opt/devkitpro`, Dolphin, Xvfb,
  mtools, zstd can all disappear). Re-run §2.2 at the start of any turn that needs to build
  or run something.
- Before relying on an artifact, `ls` it. Before relying on a behaviour, re-run it.

### 2.2 Bootstrap from a bare sandbox (idempotent)

```bash
# 1. system packages (pre-approved set for this project)
sudo apt-get update && sudo apt-get install -y zstd dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri
command -v dolphin-emu-nogui || ls /usr/games/dolphin-emu-nogui   # Dolphin 2503

# 2. devkitPPC r50 + libogc 3.1 + libfat 2.1.0 + elf2dol (cached in ~/tmp/dkp)
bash /home/user/setup-devkitppc.sh

# 3. toolchain path — must be re-exported in EVERY bash call (env does not persist)
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
powerpc-eabi-gcc --version        # expect devkitPPC r50 / gcc 16.1.0
```

- Only sanctioned package source is the Wii mirror `https://wii.leseratte10.de/devkitPro/`
  (`pkg.devkitpro.org` returns 403; do not use `dkp-pacman`). Missing files whose names are
  not listed in any index can be fetched through the mirror's `file.php/<pkg>` path — that is
  how `devkitppc-newlib` was obtained (see §C).
- `setup-devkitppc.sh` is idempotent and offline when `~/tmp/dkp` still has the packages.

### 2.3 Makefile contract

- From `/home/user/NooDS-Wii`, in one shell:
  ```bash
  export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
  make -j4 JIT=1      # -> jit.dol / jit.elf / jit.dol.map      (obj-jit1/)
  make -j4 JIT=0      # -> NooDS-Wii-interp.dol                  (obj-jit0/)
  ```
  Clean build ≈ 20–25 s; incremental ≈ 2 s. `JIT=0` link output is `NooDS-Wii-interp.dol`
  **in the repo root only** — it is not copied into `obj-jit0/`.
- The two variants use separate object dirs and a `.flags` stamp; switching `JIT=` triggers a
  rebuild of the affected variant automatically. Use `make clean`/`make clean-jit0` etc. only
  if the stamp looks stale.
- Assembly sources: `*.S`/`*.s` are compiled with `$(CC) -x assembler-with-cpp`. The tree's
  `jit_asm.S` uses **`%rN` register syntax** (`stw %r0, 116(%r1)`) and assembles fine with
  devkitPPC r50 — note that v06 claimed the opposite ("use register numbers, not register
  names"); the tree is the source of truth.
- Build check: grep the log for `error:`; success ends with `ELF2DOL jit.dol`. Never treat a
  failed build as a failed fix — and never test a stale DOL: check `ls -l` timestamps.
- Deliverable copy: `cp NooDS-Wii/jit.dol /home/user/deliverables/jit.dol` only when gates pass.

### 2.4 Host tools

`powerpc-eabi-objdump`, `powerpc-eabi-nm -C`, `elf2dol`, `mcopy/mdir`, `compare` (ImageMagick),
`python3`. Useful: `tools/ppcdump.py` (decodes `blocks.bin` + guest↔host address labels).

---

## 3. Test rig: SD image, Dolphin, artifacts

### 3.1 The two scripts (read their headers first; they are authoritative)

```bash
# 128 MiB FAT16 image, MBR partition 1 at 1 MiB (type 0x0E), all files under ::/noods/
bash tools/mksd.sh tmp/rockwrestler.raw nds/rockwrestler.nds

# boot config the DUT reads: sd:/noods/autoboot.txt  (keys: path=, jit=0|1, dump=N)
printf 'path=/noods/rockwrestler.nds\njit=1\ndump=45\n' > /tmp/ab45.txt
mcopy -o -i tmp/rockwrestler.raw@@1M /tmp/ab45.txt ::/noods/autoboot.txt

# headless run (Xvfb + dolphin-emu-nogui, deterministic settings, HLE, OGL/llvmpipe)
bash tools/run-dolphin.sh jit.dol 150 /home/user/tmp/rockwrestler.raw 45
```

- `run-dolphin.sh DOL TIMEOUT_SECONDS SD.raw [FRAMES]`: relative DOL paths resolve to
  `/home/user/NooDS-Wii/`. With `FRAMES` it rewrites the `dump=` line in the image itself.
  It sets `CPUThread=False`, `EmulationSpeed=0`, `SyncGPU=True`,
  `DeterministicGPUThread=True` — keep those, they make runs comparable.
- Per-DOL user dir: `/home/user/tmp/dolruns/<dol-basename>/user`. The emulated SD card the
  DUT actually wrote is `<user>/Load/WiiSD.raw`; the console log is
  `/home/user/tmp/dolruns/<name>/dolphin.log`. (`WiiSD.raw` at the run root is not what the
  app saw — pull from `user/Load/`.)
- Exit code 124 with `WARNING timeout … did not self-exit` means the watchdog fired; that is
  a symptom, not automatically a failure: pull the artifacts and read `debug.log` first.
- Recreate the SD image **before every run** and re-copy `autoboot.txt`: Dolphin writes the
  image back on shutdown, so a stale image silently changes the next run's config.
- Pull artifacts the same way (add 2>/dev/null; a missing file usually means the run failed
  earlier and tells you where):
  ```bash
  mcopy -n -o -i tmp/dolruns/jit/user/Load/WiiSD.raw@@1M ::/noods/autodump.txt  tmp/dumps/jit/
  mcopy -n -o -i tmp/dolruns/jit/user/Load/WiiSD.raw@@1M ::/noods/autodump.raw  tmp/dumps/jit/
  ```
- Artifacts the app can leave under `sd:/noods/` (all are written with `fclose` so they
  survive a kill): `autodump.txt`, `autodump.raw`, `debug.log`, `timeline.log`, `ck.log`,
  `ring.log`, `jitperf.log`, `blocks.bin`, `divergence.log`, `bread.log`.
- Screenshots: `compare -metric AE jit.png interp.png null:` (reference for rockwrestler:
  `AE 0`). Dolphin timing is Dolphin timing — never present it as real-hardware performance.
- Interactive GBA suite: autoboot `suite.gba`, **omit `dump=`**, drive input over the
  existing DSU/cemuhook UDP 26760 path. `xdotool` does not reach Dolphin.

### 3.2 Time budgets (measured)

| scenario | wall time |
| --- | --- |
| clean build, one variant | 20–25 s |
| incremental build | ~2 s |
| 10 emulated frames, working JIT | ~4 s |
| 45 emulated frames, working JIT | ~10 s |
| 45 emulated frames, throttle/debug build | ~40–200 s |
| boot to menu, interpreter | fps ≈ 8 |

If a run exceeds ~5× the budget above, stop it, pull `debug.log`, and read the `frame:` lines
before changing any code.

---

## 4. Verification gates (run after every JIT change)

Report every gate explicitly, including `NOT_RUN` with the reason.

**Gate 1 — encoder harness (fast, host-side).** `tools/jitdis/emit_test.cpp` emits one of
each PPC form used by the emitter and compares against `powerpc-eabi-objdump`:
```bash
cd /home/user/tools/jitdis
g++ -O0 -o emit_test emit_test.cpp -I/home/user/NooDS-Wii/NooDS-Wii
./emit_test out.bin && powerpc-eabi-objdump -D -b binary -m powerpc -EB out.bin
```
> v06 references `/home/user/tools/jittest` (a DOL-side instruction oracle). **It does not
> exist in this workspace.** Gate 1 here means the encoder harness plus the differential
> gates below; if an instruction-level oracle is wanted, that is roadmap work (§10.5).

**Gate 2 — JIT vs interpreter autodump (the accuracy gate).**
```bash
tools/autodump.sh --diff tmp/dumps/interp tmp/dumps/jit    # exit 0 == identical
```
- `tools/autodump.sh SD.raw OUTDIR` pulls `autodump.txt` + `autodump.raw`.
- `tools/autodump_diff.py` compares cpsr/spsr/halted/cycles, r0–r15, usr bank and banked
  registers, plus the framebuffer hash. It ignores `frames`/`fps`/`jit_*` by design and
  treats `pipeline=` as info-only.
- **Known diff noise:** a JIT dump contains `trace_total`/`trace[i]` lines the interpreter
  dump does not, so they print as `None != …` diffs. Fix `autodump_diff.py` to treat
  `trace_*` as info-only (or strip it) before trusting a diff count; 257 of 281 reported
  diffs in the last run were this artefact.
- Both runs must use the **same ROM, same frame count, same variant settings**, and the
  interpreter build must be fresh (same source revision).

**Gate 2b — first-difference bisect (use this *before* Gate 2 when anything diverges).**
`Interpreter::bringupCheckpoint()` writes one `ck.log` line every 524 288 global cycles
(both CPUs, both variants; 400 lines max). Compare the two trajectories and print the first
index where they differ, then bisect from there:
```bash
python3 - <<'EOF'
def parse(p):
    d = {}
    for l in open(p, errors="replace"):
        if l.startswith("ck "): d[int(l.split()[1])] = l.strip()
    return d
a, b = parse("tmp/ck.jit.log"), parse("tmp/ck.interp.log")
for i in sorted(set(a) & set(b)):
    if a[i] != b[i]:
        print("first diff at ck", i); print("JIT  ", a[i][:110]); print("INT  ", b[i][:110]); break
else:
    print("trajectories identical over", len(set(a) & set(b)), "checkpoints")
EOF
```

**Gate 3 — GBA suite regression (interactive).**
Stage `gba/suite.gba`, autoboot it (no `dump=`), run every group (`memory`, `io-read`,
`timing`, `timers`, `timer-irq`, `shifter`, `carry`, `multiply-long`, `bios-math`, `dma`,
`sio-read`, `sio-timing`, `misc-edge`, `video`), and append one row per test to
`/home/user/deliverables/evidence/gba-suite-results.csv`:
`run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,status,notes,evidence`
Statuses: `PASS`, `FAIL`, `SKIP`, `HANG`, `NOT_RUN`. Unique `run_id` per session; stable
`test_id` = menu path + displayed label/order. Compare by `test_id` against the baseline and
report totals, regressions, and improvements. Do not count booting as a pass. If input
navigation is broken, record `NOT_RUN`/`HANG` — never infer.

**Gate 4 — performance comparison (JIT vs interpreter, fresh runs).**
Read the `frame:` lines from `debug.log`:
`frame: n=%u runTicks=%llu ndsCycles=%u fps=%d` (`runTicks` = PPC ticks inside `runCore()`;
`ndsCycles` should be ≈ 408 960 per frame). Report both variants at the same frame count and
the JIT block statistics. Dolphin-only numbers.

---

## 5. JIT inventory (files, data structures, ABI)

- `NooDS-Wii/jit.h/.cpp` — analysis + emission (`ArmJit`, `ArmJit::Ctx`), block cache, SMC
  bookkeeping, `run()` dispatcher, out-of-line helpers.
- `NooDS-Wii/jit_ppc_emitter.h` — PPC encoders + branch patching (`Emitter::`); `li32`.
  `li` cannot build 16-bit mask literals — use `loadImm`/`rlwinm` sequences.
- `NooDS-Wii/jit_asm.S` — `jit_enter` / `jit_return` / `jit_sync_icache`.
- `NooDS-Wii/jit_state.h` — JIT ABI constants and `JIT_OFF_MEMBER()`; static-asserts in
  `ArmJit::reset()` freeze the offsets into the build.
- `NooDS-Wii/interpreter*.cpp` — guest semantics **and cycle costs** (see §6.3). The JIT must
  agree with these, not with an idealised ARM.
- `NooDS-Wii/memory.h` — `Memory::write` applies the SMC write barrier
  (`jitSmcNoteStore()`).
- `NooDS-Wii/main.cpp` — settings/menu/autoboot, `LogLine()` → `sd:/noods/debug.log`,
  `WriteAutodump()`, frame timing.
- Pools: `codePool[2 M]` words (8 MiB) and `Block blocks[16384]` + `blockHash[]` are `.bss`
  (POD, zero-init only — never give them initialisers or they move to `.data` and inflate the
  DOL). Both CPUs share the pools.
- Limits: `MAX_INSN_PER_BLOCK 32`, `MAX_CYCLES_PER_BLOCK 48`, `EXIT_SITE_MAX 160`
  (a per-instruction scheduling check adds one exit site per instruction — see §6.1).
- `ArmJit` must stay the **last** member of `Interpreter`; offsets must stay < 32 KiB.

---

## 6. Guest-execution contract (read before writing JIT code)

### 6.1 Scheduling granularity — the contract that is easy to miss

`Interpreter::runCoreNds()` (interpreter.cpp) advances `core.globalCycles` **per
instruction**:

```cpp
while (core.events[0].cycles > core.globalCycles) {
    if (core.globalCycles >= arm9.cycles) arm9.cycles = core.globalCycles + arm9.stepOpcode();
    if (core.globalCycles >= arm7.cycles) arm7.cycles = core.globalCycles + (arm7.stepOpcode() << 1);
    core.globalCycles = std::min(arm9.cycles, arm7.cycles);
}
core.globalCycles = core.events[0].cycles;   // then run all due scheduled tasks
```

So the ARM9, the half-rate ARM7 (`<< 1`), and hardware events (VBLANK, timers, DMA, IPC)
interleave at **instruction** granularity. A JIT block that retires N instructions in one go
hands control back late: the guest reads hardware state N instructions stale. Rockwrestler's
boot is a VBLANK polling loop and diverges at the first poll if this is violated (observed:
JIT at `0x02000270`, interpreter at `0x020001A4`, both at `globalCycles = 524 288`).

Mechanism used in this tree: the dispatcher stores the cycle deadline in
`Interpreter::jitLimit` (earliest of the next scheduled event and the other CPU's due time;
halved for ARM7) and every compiled block checks it **after each instruction**
`Ctx::schedCheck()`. When `r29 >= jitLimit` the block exits with `exitNormal(nextPc)`, exactly
where the interpreter would have yielded.

Status: implemented, **not yet verified** (§11). If you change it, re-run gate 2b first.

### 6.2 PC / pipeline conventions

| context | r15 (`registersUsr[15]`) |
| --- | --- |
| between instructions (dispatcher input) | `next_pc + 2·size` (size = 2 THUMB / 4 ARM) |
| during an instruction (guest reads PC) | `instr + 2·size` (ARM +8, THUMB +4) |
| `interpretOne()` input | the **instruction address** (it calls `flushPipeline()` first) |

Link/save encodings, taken from `interpreter_branch.cpp` (handler time r15 = `instr + 2·size`):

| instruction | saved value |
| --- | --- |
| ARM `bl`, ARM `blx` (imm/reg) | `r15 − 4` = `instr + 4` |
| THUMB `blx` (register) | `r15 − 1` = `(instr + 2) \| 1` (bit 0 = return to THUMB) |
| THUMB `bl` / `blx #imm` | next instruction, THUMB conventions — check `blT`/`blxT` before changing |
| `swi` / exceptions | `swi` does `r15 −= 4` first, then `exception()` saves `r15 + ((spsr & T) >> 4)` |

The JIT does **not** maintain `pipeline[]`; `saveState` refills it and `interpretOne()`
flushes it. Never call `stepOpcode()` from generated code — you would re-enter the
dispatcher. `stepOpcode()` is the single guest entry point: it runs `bringupCheckpoint()`,
then `armJit.run()` when `interp->jit().enabled`, else `runOpcode()`.

### 6.3 Cycle costs (must match `interpreter_*.cpp`, not a textbook)

| class | cost |
| --- | --- |
| ARM data-processing (no S), logical/test | 1 |
| ARM/THUMB load, Rd ≠ PC | `(arm7 << 1) + 1` |
| ARM/THUMB store | `arm7 + 1` |
| load to PC | 5 (fallback) |
| MUL/MLA (ARM9) | 2 (4 with `S`) |
| MUL/MLA (ARM7) | data-dependent — always fall back |
| branch taken / not taken | 3 / 1 |
| LDM/STM, SWI, unhandled | interpreter decides (fallback) |
| conditional instruction | 1 before the condition test + `cost−1` inside |

Rules:
- A **fallback must charge what the interpreter returns**, not a flat constant
  (`ArmJit::emitFallback()` currently still emits `+3`; switching it to `add r29, r29, r3`
  with the call's return value is an open item — §11).
- Conditional guards: `condGuardBegin(cond, cost)` charges 1 + `(cost−1)`, `condGuardEnd`
  patches the skip target. Keep that shape for new instructions.
- Compile-time `block->cycles` is only an estimate for fallback slots; the dispatcher returns
  the runtime `r29`.

### 6.4 Flags

CPSR bits 31..28 are N, Z, C, V.

**Guest flags → CR0** (for condition guards): `mtcrf 0x80, r30` maps

| CR0 bit | guest flag |
| --- | --- |
| LT | N |
| GT | Z |
| EQ | C |
| SO | V |

**CR0/XER → guest flags** (after a flag-producing instruction) — the mapping that actually
bites. `rlwinm(rt, rs, SH, MB, ME)` rotates left by SH, then keeps bits MB..ME **counted from
the MSB**, and the CPSR nibble is rlwinm bits 0..3:

| CPSR bit | flag | source | encoding |
| --- | --- | --- | --- |
| 31 | N | CR0[LT] = CR bit 0 | `rlwinm rt, cr, 0, 0, 0` |
| 30 | Z | CR0[EQ] = CR bit 2 | `rlwinm rt, cr, 31, 1, 1` |
| 29 | C | XER[CA] = XER bit 2 | `rlwinm rt, xer, 0, 2, 2` |
| 28 | V | XER[OV] = XER bit 1 | `rlwinm rt, xer, 2, 3, 3` |

Then merge with read-modify-write of r30 (clear bits 0..3, OR each field in). `emitFlagsLogical`
must **preserve** C and V. Carry-producing arithmetic uses the `o.` forms plus `mfxer`;
`loadCarryToXer()` feeds guest C back into XER[CA] before `adde`/`subfe`.

**Trap:** `cmpw`/`cmp` destroys CR0. Never emit a compare whose operand can be r0 — scratch
holds junk; and never emit any compare between flag creation and flag harvest. (This silently
replaced the fresh CR0 of a successful `subfco` with `cmpw r11, r0` and produced a memset loop
that never terminated, plus CR0[EQ]=1 landing in CPSR bit 1 → IRQ mode.)

### 6.5 Self-modifying code (SMC)

- 4 096 LRU slots × 128-byte chunks; `jitPageSlot[page]` (64 KiB pages), per-slot
  epoch/gen tables; a store bumps the generation of the chunk it touches
  (`jitSmcNoteStore()`, called from `Memory::write`).
- Blocks record `slot` + `epoch` + `gen0/gen1` and are re-translated when those change.
- **`ArmJit::reset()` must initialise the SMC tables once** (`tablesReady`): if they stay
  `.bss`-zero, every page looks like it is watched by slot 0 and the entire cache is
  invalidated on the first store (symptom: `blocks ≈ dispatch`, `missGen ≈ dispatch`,
  `words` in the 100 000s). Any new code path that enables the JIT must call `reset()` first.

### 6.6 Instruction cache

- Emit code, then flush the exact range: `jit_sync_icache(addr, bytes)` — 32-byte-aligned
  `dcbst` + `icbi` per line. `icbi` is **not** free, but a missing or partial flush SIGILLs
  Dolphin within ~1 s. Do not "optimise" this without a working gate-2 result.
- Keep the flush per compiled block range; a single `icbi` per 4 KiB page (or none) is known
  to crash.

### 6.7 Host ABI and object offsets

| host | holds |
| --- | --- |
| r14–r28 | guest r0–r14 |
| r29 | retired cycles counter (returned by `jit_return`) |
| r30 | cached CPSR |
| r31 | `Interpreter*` |
| r0, r3–r12 | scratch (r0 is **never** a base/index operand) |
| r2, r13 | reserved — do not touch |
| CR0, CR1 | usable (CR2–CR4 are callee-saved by the ABI) |
| 8(r1) | free stack scratch slot |
| LR | saved at 116(r1) by `jit_enter`; frame is 112 bytes, saves 40..111 |

Object offsets (all verified against the tree, and enforced by static-asserts):
`registersUsr[0]` @ `0xA0` (`[15]` @ `0xDC`, the PC slot), `cpsr` @ `0x11C`, `halted` @ `0x08`.
Use `JIT_OFF_MEMBER(Interpreter, field)`; never hard-code. `Interpreter::cpsr` and `arm7`
are **private** — reach them through methods or generated-code offsets, not from file-scope
helpers.

---

## 7. Translation / exit / fallback protocol

- Block entry: `jit_enter(code, Interpreter*)`; prologue loads CPSR + guest r0–r14.
- Exits:
  - `exitNormal(pc)`: sync dirty guest registers, store CPSR, store the constant PC.
  - `exitNormalRegPC(reg)`: same with a register-held PC (BX/BLX, branches).
  - `exitNoWB(siteIdx)`: leave state untouched — used after a fallback ran the interpreter,
    which already wrote authoritative state (halted / mode change / PC change checks).
- Every exit jumps to the shared epilogue → `jit_return`, which returns r29 as the cycles
  retired by the block.
- Fallback (`emitFallback`): write state, store the instruction address in the PC slot, call
  `ArmJit::interpretOne(interp)`, then bail out if `halted`, if the CPSR mode/T bit changed
  (`JIT_EXIT_MODE_MASK`), or if the PC moved; otherwise adopt the interpreter's CPSR and
  reload registers.
- `ArmJit::run()` is entered from `Interpreter::stepOpcode()` with the between-instructions
  PC convention. It must keep the `pcNext >= 0x80000000` divergence trap (a guest PC inside
  MEM1 means the host jumped into its own code) and stays free of per-dispatch I/O (§8, T11).
- Decode table discipline: `armInsn()` switches on bits 27-26 and **must test** the
  sub-decoding bits. The LDM/STM-as-B/BL bug (`case 2` accepted bits 27-25 == 100) is the
  canonical example: `if (((opcode >> 25) & 7) != 5) return false;` before `armBranch`.

---

## 8. Trap catalogue (symptom → cause → fix)

| # | symptom | cause | fix |
| --- | --- | --- | --- |
| T1 | `cmp`/`bne` loop never terminates; CPSR bit 1 set → CPU drops to IRQ mode; guest PC drifts into data | flag harvest used wrong rotates (Z copied from N, V garbage) and a stray `cmp` clobbered CR0 | §6.4 table; audit every `Emitter::cmp` |
| T2 | registers or CR0 corrupted right after an arithmetic op | `cmp reg, 0` where the second operand resolved to r0 (scratch) | compare against a fresh `zero` register, never r0; keep compares out of flag live ranges |
| T3 | guest PC jumps megabytes away; LR = `next_pc`, PC = low half of a register list | LDM/STM decoded as `B`/`BL` | test bits 27-25 == 101 for B/BL (§7) |
| T4 | block cache thrashes: `blocks ≈ dispatch`, `missGen ≈ dispatch`, `words` in 100 000s | SMC tables left `.bss`-zero | initialise in `reset()` under `tablesReady`; call `reset()` before enabling |
| T5 | SIGILL after ~1 s; or OK for minutes then crashes | code cache not flushed / partially flushed | `jit_sync_icache` over the whole emitted range, every block |
| T6 | random register corruption under load | r0 used as a base/index; r2/r13 touched; CR2–CR4 clobbered | §6.7 |
| T7 | block truncated, exits missing, control flow falls through | exit-site arrays overflowed after adding a new per-instruction exit | `EXIT_SITE_MAX` (160) and the `exitCount` guard in `exitNormal` |
| T8 | JIT reaches a poll loop "early" and spins; ck.log shows JIT ahead of the interpreter at the same cycle count | block granularity vs the interpreter's instruction-granularity scheduling | §6.1 `jitLimit` + `schedCheck` |
| T9 | cycles drift by small amounts; flags of the next instruction wrong | fallback charged a flat 3 (or a wrong constant) instead of the interpreter's returned cost | charge the `interpretOne()` return value |
| T10 | ARM7 runs twice as fast / budgets wrong | ARM7 costs are half-rate: `runCoreNds` shifts them left by 1 | halve ARM7 budgets; keep ARM7 MUL on the fallback path |
| T11 | emulator stalls for seconds; `fps` collapses; runs look hung | SD I/O emitted into hot paths (the SD driver goes through emulated hardware) | probes/logs only on cold paths; drain bounded buffers once, not per dispatch |
| T12 | trace/ring dump shows nonsense PCs | ring index underflow (`write − 64` when `write < 64`), or ring records dispatches and was read as instructions | clamp the index; remember the ring unit |
| T13 | `run-dolphin.sh` reports "no such dol" | relative DOL paths resolve under `/home/user/NooDS-Wii/` | pass `jit.dol`, or an absolute path |
| T14 | `mcopy` cannot find the artifact | pulled from the wrong image — the DUT's image is `tmp/dolruns/<name>/user/Load/WiiSD.raw` | pull from `user/Load/`; `2>/dev/null` a missing file and check the run first |
| T15 | "the guest is stuck at PC X" | sampling alias: ck.log/timeline are periodic, a tight loop can be sampled at the same PC by chance | check a counter (e.g. the memset index) before declaring a hang |
| T16 | compile errors about private members | `Interpreter::cpsr`/`arm7` are private | add an accessor/method on `Interpreter`; helpers must be members |
| T17 | link/compile error about a helper | file-scope helpers cannot reach `Ctx`/`Interpreter` internals | make them members (`Interpreter::…`, `ArmJit::Ctx::…`) |
| T18 | first block of a frame interprets forever / deadline math wrong | deadline already passed → unsigned underflow | clamp `room` to 0 (`deadline > globalCycles ? … : 0`) |
| T19 | interpreter state and JIT state disagree about PC by ±2/±4 | pipeline conventions (§6.2) | use `interpretOne()` for the fallback, `flushPipeline()` semantics elsewhere |
| T20 | nothing works after a break; toolchain/rig "vanished" | `/opt` and apt packages do not persist between turns; some `tmp/` artifacts were lost | re-run §2.2; re-verify with `ls`; regenerate artifacts instead of trusting them |

---

## 9. Debugging ladder (bisect a divergence in minutes)

Work top-down; stop at the first rung that shows a difference and fix that.

1. **Boot sanity (30 s):** `dump=10` JIT run. Expect self-exit, `frames=10`, `fps` > 0, no
   SIGILL. If it hangs, pull `debug.log` and read the last `frame:` line and the ring tail —
   then go to rung 4.
2. **Timeline alignment:** with `timeline.log` enabled (`stepOpcode` first-hit probe for a
   short PC list in both variants) compare *globalCycles at first execution* of each PC.
   - All equal → the cost model and the scheduling contract are consistent so far.
   - A PC appears much earlier in the JIT → a block retired its cycles too cheaply, or a
     branch was mis-translated; dump that block (`blocks.bin`) and read it with
     `tools/ppcdump.py blocks.bin <guest_pc>`.
3. **Checkpoint bisect:** run both variants with `dump=45`, pull `ck.log` from each, print the
   first differing checkpoint (§4 gate 2b). The cycles delta and the PC pair tell you whether
   it is a cost problem (same PC, small delta) or a control-flow problem (different PCs).
4. **Single-step the suspect:** temporarily set `MAX_INSN_PER_BLOCK = 1` and
   `MAX_CYCLES_PER_BLOCK = 1`; the dispatcher then retires exactly one instruction per
   `run()` (already-proven behaviour) and you can compare against `interpretOne()` directly.
   Remember to restore the limits and rebuild.
5. **Read the emitted code:** raise the `blocks.bin` dump bound (`stats.blocksCompiled < N`),
   decode with `tools/ppcdump.py`, and check the instruction against §6 and the matching
   interpreter handler.
6. **Only then** run gates 1–4 in full and write down what passed.

Cheap tests that pay for themselves before any of the above:
- Flag round-trip: set CPSR, `mtcrf 0x80`, read CR0; then harvest back and compare. Any
  mismatch is T1/T2.
- Decode table: for every `case` arm, feed one opcode of each sub-encoding and record
  native/fallback; LDM/STM, B/BL, MRS/MSR, RRX, shifter-carry paths especially.
- SMC init: assert `jitPageSlot[i] == 0xFFFF` and that the first store does not bump a
  generation (§6.5).
- Scheduling: at frame 0, assert the JIT and interpreter agree on `globalCycles` and PC at
  the first VBLANK poll (§6.1).

---

## 10. Deliverables, evidence, handoff

1. `/home/user/deliverables/jit.dol` — the verified JIT build (record its SHA-256).
2. `/home/user/deliverables/handoff.md` — required sections:
   - status summary (what works, what is verified, what is not);
   - exact reproduction commands (build, SD image, Dolphin run, artifact pull, gates);
   - evidence index: file path + hash + which gate it belongs to;
   - performance table: JIT vs interpreter, same ROM/frame count, Dolphin numbers only;
   - known failures and the smallest next step for each (with the trap numbers from §8).
3. `/home/user/deliverables/evidence/gba-suite-results.csv` — schema and statuses in §4.
4. Optional: `noods-wii-jit.patch` (diff against the vendored revision) and `tools/` copies.
5. Roadmap / next-step ideas go in the handoff, not in code: do not start unrequested
   roadmap work (candidates seen so far: lazy flags, idle-loop detection, block linking,
   native THUMB shifts, ARM7 multiply timing, an instruction-level differential harness).

---

## 11. Status ledger (keep this short; update only from evidence)

_As of the last full session, 2026-10-01 (v07 written here):_

- Toolchain/build: **works** — devkitPPC r50 (gcc 16.1.0), libogc 3.1, libfat 2.1.0; both
  variants build; `jit.dol` and `NooDS-Wii-interp.dol` produced. (Binary artifacts were lost
  in a sandbox reset; sources retain every patch — verify with
  `grep -n "schedCheck\|jitLimit" NooDS-Wii/NooDS-Wii/jit.cpp`.)
- Both CPUs are JIT-compiled; the "ARM7 on interpreter" experiment was removed (it never
  reached frame 1: 7.6 M ticks for frame 0).
- **Fixed, pending re-verification:** flag harvest in `emitFlagsFromXer`/`emitFlagsLogical`
  (T1: wrong rlwinm rotates + stray `cmp r11, r0`). Root cause established by disassembling
  the emitted block; re-run gate 2b for the runtime proof.
- **Implemented, unverified:** instruction-granularity scheduling (`jitLimit` + `schedCheck`,
  §6.1). The last run with it enabled reached frame 0 only inside a 130 s budget — either a
  stall or just too short a budget; re-run with a longer timeout before judging.
- Gate 1: encoder harness green (113 emitted forms) during bring-up.
- Gate 2: **failing** (24 real state diffs + 257 `trace_*` artefacts) on the last full dump
  comparison; the flag fix targets the known cause of the loops/PC divergences.
- Gate 3: not run (needs the interactive DSU path).
- Deliverables: not yet produced.
- Open items: fallback cycle charge (T9), `autodump_diff.py` trace-noise filter, strip
  bring-up instrumentation (checkpoint/timeline/ring probes, `blocks.bin` bound,
  `jitperf.log`, frame-timing hook) before the final DOL.

---

## Appendix A — command cheat sheet

```bash
# bootstrap
sudo apt-get update && sudo apt-get install -y zstd dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri
bash /home/user/setup-devkitppc.sh
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH

# build
cd /home/user/NooDS-Wii && make -j4 JIT=1 && cp jit.dol /home/user/deliverables/jit.dol
make -j4 JIT=0                                  # NooDS-Wii-interp.dol

# run
cd /home/user && bash tools/mksd.sh tmp/rockwrestler.raw nds/rockwrestler.nds
printf 'path=/noods/rockwrestler.nds\njit=1\ndump=45\n' > /tmp/ab45.txt
mcopy -o -i tmp/rockwrestler.raw@@1M /tmp/ab45.txt ::/noods/autoboot.txt
timeout 300 bash tools/run-dolphin.sh jit.dol 280 /home/user/tmp/rockwrestler.raw 45

# pull artifacts
for f in autodump.txt autodump.raw debug.log timeline.log ck.log ring.log jitperf.log blocks.bin; do
  mcopy -n -o -i tmp/dolruns/jit/user/Load/WiiSD.raw@@1M ::/noods/$f tmp/dumps/jit/$f 2>/dev/null
done

# gates
tools/autodump.sh --diff tmp/dumps/interp tmp/dumps/jit
compare -metric AE jit.png interp.png null:
python3 tools/ppcdump.py blocks.bin 0x020001E4

# inspect a DOL
powerpc-eabi-nm -C jit.elf | grep -i armjit
```

## Appendix B — numbers that matter

- ROM hashes: `nds/rockwrestler.nds` MD5 `dfd1770daba69955031c0699d33b1dc6` (39 433 B,
  entry/ram `0x02000100`, ARM9 size `0x8EE0`; ARM7 `0x03800100`, size `0x660`).
  `gba/suite.gba` SHA-256 `8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`
  (524 288 B).
- SD image: 128 MiB FAT16, MBR partition 1 at 1 MiB, type `0x0E`; files at `::/noods/`.
- Frame: `ndsCycles` ≈ 408 960/frame; interpreter baseline `fps=8`, `arm9 cycles = 74 354 040`
  at 45 frames.
- Host ABI/offsets: §6.7 (`0xA0`, `0xDC`, `0x11C`, `0x08`; frame 112, LR 116, saves 40..111).
- Cache/SMC: `CODE_WORDS 2<<20`, `BLOCK_COUNT 16384`, SMC 4096 slots × 128-byte chunks,
  64 KiB pages.
- Timing: see §3.2.

## Appendix C — external sources (verified this session)

- devkitPPC packages: `https://wii.leseratte10.de/devkitPro/` — `devkitPPC/r50 (2026-05-03)`
  (gcc 16.1.0, binutils 2.46.0), `libogc 3.1 (2026-05-03)`, `libfat 2.1.0`,
  `other-stuff/gamecube-tools` (ships `elf2dol`). `pkg.devkitpro.org` = 403.
- Missing package files can be fetched by name through
  `https://wii.leseratte10.de/devkitPro/file.php/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst`
  (worked: HTTP 200, 584 KB) even when no index lists them.
- ROM provenance: rockwrestler `https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds`;
  suite `https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba`.
