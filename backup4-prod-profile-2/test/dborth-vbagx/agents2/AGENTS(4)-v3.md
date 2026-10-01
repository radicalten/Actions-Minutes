# AGENTS.md (v06) — NooDS-Wii ARM→PPC JIT

Consolidated project runbook and coding-agent operating rules. Develop, build, and verify
the ARM9/ARM7→PowerPC dynamic recompiler in NooDS-Wii, a Wii homebrew NDS/GBA emulator.
**This file supersedes v05 and is the single project/agent runbook.** Read §5 before
touching JIT code. Never claim a result without inspecting evidence from a fresh run.

## 1. Working and agent rules

- Protect existing user work: inspect `git status` before editing; do not overwrite
  unrelated changes or evidence.
- This sandbox has **no PTY and no stdin control**. Never launch interactive REPLs; use
  one-shot modes: `codex exec`, `claude -p`, `opencode run`, or `pi -p`.
- Make delegated prompts self-contained: read this file first, state task/scope, approval
  boundaries, constraints, output location, and definition of done. Resolve questions before
  launching an agent.
- Check agent CLI availability. Do not install global tools or packages without user
  approval; never put keys, passwords, or tokens in prompts, arguments, or process I/O. Have
  the user configure authentication locally.
- Prefer sandboxed `--full-auto` for scoped workspace edits. Reserve `--yolo` for disposable
  `/home/user/tmp/` scratch repos with no secrets or production config.
- For long jobs, launch a one-shot agent with a background process; monitor with
  `get_process_output` (`wait_for: exit` or `log`), not `ps`/`sleep` loops. Wait calls cap
  at 180 seconds; do not treat a timeout as completion.
- Send a short start update for background work; update on milestones, failure/user action,
  completion, or if stopping a process. Report commands, outcomes, and fresh artifacts.
- Parallel agents must use isolated branches/worktrees under `/home/user`; never share a
  writable checkout. Avoid concurrent Dolphin tests in this low-memory sandbox.
- Bash has a 30-second default timeout. Set `1800` for foreground coding-agent runs; keep
  each long Dolphin call under about 25 minutes.
- If a user specifies an agent/tool, honor that choice. In orchestrator mode, do not
  silently replace a failed delegated agent by hand-coding its patch.
- Route autonomous changes through reviewable feature branches. Do not push to `main`,
  `production`, or `release/*` before human review.

## 2. Environment and build

- Debian 13, headless, ~2 vCPU and 1.9 GiB RAM, no GPU. `/opt` and apt packages may vanish
  between sessions; `/home/user` persists.
- Repository: `/home/user/NooDS-Wii` (sources in `NooDS-Wii/NooDS-Wii/`). Helper tools:
  `/home/user/tools`. ROMs: `/home/user/nds/rockwrestler.nds` and
  `/home/user/gba/suite.gba`. Deliverables: `/home/user/deliverables/`.
- If missing and installation is authorized, install system test dependencies: `sudo apt-get
  install -y zstd dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri`.
- If `/opt/devkitpro` is missing, use `/home/user/setup-devkitppc.sh` (idempotent; needs
  `zstd`). It installs devkitPPC r50/libogc 3.1/libfat into `/opt/devkitpro` using
  `https://wii.leseratte10.de/devkitPro/`. `pkg.devkitpro.org` returns 403; do not use
  `dkp-pacman` or improvise another source.
- Re-export the toolchain path in **every** shell call; shell environment does not persist:
  ```bash
  export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
  ```
- From the repo root, `make -j4` builds the default JIT `jit.dol`; `make JIT=0` builds
  `NooDS-Wii-interp.dol`. Objects are separated in `obj-jit1/` and `obj-jit0/`.
- The Makefile compiles `*.cpp`, `*.S`, and `*.s` with dependency generation. In assembly
  use register numbers (e.g. `stw 0, 4(1)`), not register names.
- NDS test ROM: rockwrestler MD5 `dfd1770daba69955031c0699d33b1dc6`. GBA regression ROM:
  `/home/user/gba/suite.gba`, from [endrift's GBA Test Suite
  fork](https://github.com/radicalten/gba-test-suite-mgba-emu/blob/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba), pinned to commit `e05e71367964d75d65d2b9dded7240608a097a10` (512 KiB; SHA-256
  `8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`). Verify the hash
  before comparing runs.

Quick build:
```bash
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
cd /home/user/NooDS-Wii && make -j4
```

## 3. Headless Dolphin test rig

- `mksd.sh OUT.raw files…` creates a 128 MiB FAT16 SD image (MBR type `0x0E`, partition at 1
  MiB); files are placed under `::/noods/`. Inspect with `mdir -i img@@1M ::/` and `mtype -i
  img@@1M ::/file`.
- `run-dolphin.sh DOL SECS SD.raw [capture-secs…]` runs Dolphin 2503 under Xvfb :99,
  OGL/llvmpipe and HLE. It writes a fresh Dolphin config, copies SD to/from
  `$DATA/Load/WiiSD.raw`, and produces `SD.raw.out`, `cap-<t>.png`, and `dolphin.log`.
- ALSA errors may be harmless. Dolphin poweroff/writeback can take minutes; budget time and
  inspect the SD output before deciding a run is stuck.
- `xdotool` input does not reach Dolphin. For interactive `suite.gba` tests, autoboot the
  ROM and use the existing DSU/cemuhook UDP 26760 input path (`DSUClient.ini` plus
  `WiimoteNew.ini` or `GCPadNew.ini`). Verify navigation works before starting. If input is
  unavailable, stop and report the blocker; do not infer or fabricate results.
- Emulator timing is relative to Dolphin, not real Gekko/cache performance. Do not claim
  real-hardware timing or behavior from headless Dolphin.

## 4. Verification — required after every JIT change

A compile alone is not a verified JIT fix. Run all three checks; use fresh binaries and
artifacts, and report skipped/failed checks explicitly.

1. **Differential jittest (instruction-level oracle).** From the repo root, build the app
   and harness; in this sandbox the test rig is under `/home/user/tools`:
   ```bash
   export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
   cd /home/user/NooDS-Wii && make -j4 && /home/user/tools/jittest/build.sh
   cd /home/user/tools/jittest
   ../mksd.sh sd.raw ~/nds/rockwrestler.nds
   ../run-dolphin.sh jittest.dol 300 sd.raw
   mtype -i sd.raw.out@@1M ::/jittest.txt
   ```
   Expect `TOTAL FAILURES: 0`. Optional: put numeric `seed.txt` on the SD. On a mismatch,
   inspect opcodes, initial registers, and diff words; state layout is usr `[0..15]`, fiq
   `[16..22]`, svc `23–24`, abt `25–26`, irq `27–28`, und `29–30`, CPSR `31`, SPSR `32–36`.
2. **End-to-end autodump.** Run `tools/autodump.sh ROM JIT(0|1) FRAMES OUTDIR [timeout]`
   once with JIT and once with interpreter, using the same ROM/frame count. It writes
   `sd:/noods/autoboot.txt` settings (`path`, `jit=N`, `dump=N`), `autodump.txt`
   (FPS/registers/JIT stats), and `autodump.raw`; compare JIT and interpreter dumps.
3. **Screenshot comparison.** Autoboot without `dump=`, run the JIT and interpreter builds
   for equivalent frames (reference: `run-dolphin.sh jit.dol 50 sd.raw 45`), then compare
   fresh captures with `compare -metric AE <jit.png> <interp.png> null:`. Rockwrestler
   reference: `AE 0`.

### GBA suite regression (interactive)

- Run the full suite after each emulator-code change (skip only docs/build-only changes and
  state that explicitly). It supplements, but does not replace, the three JIT checks above.
  Use the same DOL variant/config for comparisons (`jit.dol` is the current default build).
- Stage `/home/user/gba/suite.gba` on the SD image and autoboot it in a fresh NooDS-Wii DOL
  using the existing `path` and `jit=N` settings. Omit `dump=` so the suite stays
  interactive.
- Suite UI (`src/main.c`): Up/Down selects a group; A runs it and displays per-test PASS/FAIL
  markers. In the result list, Up/Down selects, Left/Right pages, A opens details, and B
  returns. Run all groups: `memory`, `io-read`, `timing`, `timers`, `timer-irq`, `shifter`,
  `carry`, `multiply-long`, `bios-math`, `dma`, `sio-read`, `sio-timing`, `misc-edge`, and
  `video`. Do not count booting or opening the menu as a pass. Record results in
  `/home/user/deliverables/evidence/gba-suite-results.csv`, one row per test, with columns:
  `run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,status,notes,evidence`.
- Use a unique `run_id` and stable `test_id`s (menu path plus displayed label/order).
  Statuses: `PASS`, `FAIL`, `SKIP`, `HANG`, or `NOT_RUN`. Include the commit, DOL hash,
  suite hash, and evidence path. Append results per run; compare by `test_id` and report
  totals, regressions, and improvements. The first complete run establishes the baseline.
  Do not assume all tests pass.
- If DSU navigation fails or a result is ambiguous, stop and ask for help or record
  `NOT_RUN`/`HANG` with a note. Never infer or fabricate a result.

For known NDS/JIT references, rockwrestler should show its menu and green ARM7 light.
Non-JIT work still requires relevant fresh tests and evidence; never report a pass from stale
artifacts.

## 5. JIT architecture and invariants

Read this entire section before changing JIT code. The JIT has silent-failure invariants;
keep analysis, codegen, interpreter state, and tests consistent.

- Source map: `jit_ppc_emitter.h` encodes PPC and patches branches; `jit.h/.cpp` implements
  `ArmJit` analysis/emission; `jit_asm.S` supplies `jit_enter`, `jit_return`, and
  `jit_sync_icache`; `interpreter.*` owns dispatch/fallback; `memory.h` tracks `jitGen`;
  `core.cpp` selects interpreter/JIT loops; `main.cpp` owns settings/menu/autoboot.
- Host mapping: guest r0–r14 → PPC r14–r28; r29 = cycles, r30 = cached CPSR, r31 =
  `Interpreter*`; scratch r0, r3–r12. Never use r0 as a D-form/X-form load/store or `addi`
  base; never touch r2/r13. Only CR0/CR1 are available; CR2–CR4 are callee-saved. `8(r1)` is
  a free stack scratch slot.
- Keep `armJit` as the last `Interpreter` field; offsets must remain below 32 KiB. Between
  blocks `registersUsr[15] = next_pc + 2·size`; JIT does not maintain `pipeline[]`.
  `saveState` refreshes the pipeline; `interpretOne` flushes it.
- Every exit writes changed registers and r30→CPSR through `exitWB`, unless a fallback
  already wrote state and uses `exitNoWB`.
- Fallback: store state; set `r15 = cur + 2·size`; call handler; add return cycles to r29.
  Exit if r15 changed, `halted != 0`, or `(cpsr ^ r30) & 0x3F`; otherwise reload CPSR and
  registers.
- `analyzeArm/Thumb` and emission must agree. Add native instructions as a new `kind`; do
  not re-decode differently. Match interpreter cycles exactly; conditional instructions add
  1 before the condition check and charge `cost−1` inside.
- PPC flags: `mtcrf 0x80,r30` maps CR0 LT=N, GT=Z, EQ=C, SO=V. SUB carry is PPC CA (no
  inversion); use `o.` forms plus `mfxer` where required.
- Code buffers/block pools must be static `.bss` in MEM1, POD-only (avoid initializers
  moving pools into `.data`); helpers must remain within `bl` range. Call `jit_sync_icache`
  after emission.
- Stores must bump `jitGen` both in emitted code and `Memory::write`; `loadState` must call
  `armJit.reset()`.

## 6. Pitfalls, roadmap, and deliverables

- Differential failures may expose interpreter bugs. Check behavior against the ARM
  reference before blaming the JIT. Fixed examples in `interpreter_alu.cpp`: RSCS mask,
  THUMB NEG V, and ARM7 multiply timing/UB. LDRT/STRT (post-index W=1) uses interpreter
  fallback.
- Keep randomized harness index registers small/non-negative for scaled offsets; otherwise
  stores can escape the restored 64 KiB region and cause false failures.
- `libfat` commits file contents on `fclose`; for logs that must survive a kill, reopen and
  close per write. `new Core` for a GBA ROM on the jittest DOL hangs on the main thread; use
  autodump for automated noninteractive GBA tests. Run the interactive suite in the normal
  NooDS-Wii DOL with DSU input. MEM2 is a bump allocator; only one `Core` fits per session.
- Grep before using `sed -n "$(grep …),+Np"`; it fails when grep returns no or multiple
  lines.
- Next work is in `01_JIT_PROGRESS.md` §6 (cycle-budget block linking, idle-loop detection,
  native THUMB shifts/MSR/BX, lazy flags, then real-hardware profiling). Do not start
  unrequested roadmap work; rerun §4 after each JIT step.
- Deliverables go in `/home/user/deliverables/`: `jit.dol`, write-up, versioned runbook,
  `noods-wii-jit.patch`, `tools/`, and `evidence/`. Use `NN_TOPIC.md` and increment
  `AGENTS_NN.md` per revision; keep the runbook about 100–200 lines. Do not overwrite
  existing deliverables or advance the version without checking scope.
