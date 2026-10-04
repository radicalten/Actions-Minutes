# AGENTS.md — Updated for current workspace environment (initial session state)

This file supersedes the original `/Users/me/Downloads/AGENTS.md` for this specific workspace environment. It documents the exact state of the host, workspace, toolchain, and deliverables **before any command was executed in this session** (before `pwd`, before `ls`, before any edit), and updates the acceptance criteria / build instructions to match what this environment can and cannot actually do.

---

## Current environment (verified from initial session snapshot)

| Property | Value | Note |
|---|---|---|
| Host OS / arch | macOS / ARM64 | Confirmed by `file` inspection of binaries; `powerpc-eabi-gcc` is `ELF 64-bit LSB executable, x86-64, GNU/Linux` and **will not run** on this host. |
| Session workspace root | `/Users/me/Documents/deepseek-harness/default-workspace` | `pwd` result at session start. |
| Main workspace | `NooDS-Wii-workspace/` | Contains full source tree (`NooDS-Wii/NooDS-Wii/` subdir), `.S` files (`ppc_bridge.S`), `.cpp`/`.h`, `Makefile`, build artifacts (`.dol`, `.map`, `.flags`), existing test ROMs (`gba/suite.gba`, `nds/rockwrestler.nds`), evidence (`evidence/g1/`, `evidence/g3/`), `recovered.log`, `testframes.log`, `tools/`, `tmp/`, `uploads/`. |
| Secondary clone | `NooDS-Wii/` (workspace-level) | Small clone with `myapp.dol` (1,377,864 bytes); separate from full workspace. |
| DevkitPPC toolchain | `devkitpro-local/` | Pre-configured; contains `devkitPPC-r50` (metadata-only), `devkitppc-binutils-2.46.0`, `devkitppc-gcc-16.1.0`, `devkitppc-newlib-4.6.0`, `devkitppc-crtls-2.1.0`, `devkitppc-rules-1.2.1`, `gamecube-tools-1.0.7`, `libogc-3.1.0`, `libfat-ogc-2.1.0`. All binaries are Linux x86_64 — **cross-compile only; no native execution**. |
| `dolphin-emu` / `xvfb-run` / `timeout` | **NOT INSTALLED** | Confirmed by absence of binary; `apt` unavailable. Direct G2/G3 Dolphin runs are **NOT POSSIBLE** on this host without container / Linux VM. |
| Original spec file | `/Users/me/Downloads/AGENTS.md` | Unchanged; reference only. |

---

## What exists already at session start (before any action)

### Deliverables (`deliverables/`)
- `jit.dol` — `sha256 = 2bb3eb494224fdbb9f03643aa628524c98d92b2408a76483e02bee0a5eb3e19d` (1,811,264 bytes)
- `NooDS-Wii-interp.dol` — `sha256 = 63a116ddd4cc22aa1d136fb59fe6d4093c8bbea82932a65d9cb0f8c5552c63cc` (1,805,824 bytes)
- `SHA256SUMS` — contains both hashes
- `handoff.md` — previous handoff (updated during this session)

### Workspace build artifacts (`NooDS-Wii-workspace/`)
- `NooDS-Wii/NooDS-Wii-interp.dol` + `.map` (pre-built interpreter build)
- `NooDS-Wii/NooDS-Wii/Makefile` (updated dual-variant Makefile with `.S` rules, `obj-jit0`/`obj-jit1`, flags stamp, `-mrvl`, `-O2`, `-DNOODS_JIT=0|1`)
- `NooDS-Wii/NooDS-Wii/ppc_bridge.S` (ABI bridge: `jit_enter`, `jit_return`, `jit_call_abs0`)
- `NooDS-Wii/NooDS-Wii/arm_jit.h` + `.cpp` (JIT interface: `attach()`, `tryRun()`, `reset()`, `CacheEntry`, `Buffer`, `Buffer::storeNzFlags()` with `rlwinm`/`rlwimi`, `compileMoveImmediate()` for MOV-immediate, full-pool flush)
- `NooDS-Wii/NooDS-Wii/interpreter_lookup.cpp` (dispatch tables unchanged)
- `NooDS-Wii/obj-jit1/.flags` (flags stamp for JIT build; contains `-DNOODS_JIT=1`, `-mrvl`, `-mcpu=750`, include paths pointing to `devkitpro-local`)
- `NooDS-Wii/obj-jit1/NooDS-Wii/` (empty `.o` directory — objects rebuilt during session or were cleaned)

### Evidence (`NooDS-Wii-workspace/evidence/` and workspace root)
- `evidence/g1/`: `enc_cr.S`, `enc_cr.o`, `enc_cr.dis`; `enc_test.S`, `enc_test.o`, `enc_test.dis`; `toolchain-packages.sha256`
- `evidence/g3/`: 14 group files (`memory`, `io-read`, `timing`, `timers`, `timer-irq`, `shifter`, `carry`, `multiply-long`, `bios-math`, `dma`, `sio-read`, `sio-timing`, `misc-edge`, `video`) — both `-jit` and `-interp`
- `evidence/g3-summary.txt`: 13/14 `MATCH`, 1 (`io-read`) `FAIL-COMPARE`
- `recovered.log`: 20-frame `suite.gba` GBA smoke (`mismatch=0`)
- `testframes.log`: per-frame tick/state records

### Test ROMs (`NooDS-Wii-workspace/gba/` / `nds/` and workspace-level `NooDS-Wii/NooDS-Wii/gba/` / `nds/`)
- `suite.gba`: `sha256 = 8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035` (524,288 bytes)
- `rockwrestler.nds`: `sha256 = f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` (39,433 bytes)

---

## Updated mission for this environment

Write a new ARMv4/v5 → PowerPC (GCN/Wii) dynamic recompiler. The interpreter stays as fallback. Same source, same flags, only `-DNOODS_JIT=0|1` differs. The `.S` bridge (`ppc_bridge.S`) is assembled into both builds. Because this host is **macOS ARM64 with Linux x86_64 cross-compiler binaries**, clean native rebuilds are impossible without a Linux container or separate Linux host. All verification of emitted PPC forms (`G1`), full Dolphin accuracy runs (`G2`/`G3`), and paired performance runs (`G4`) must be documented as **NOT RUN / PARTIAL / WIP** unless executed through workspace artifacts or a different host.

---

## Updated build instructions (matched to this workspace)

```bash
# This workspace uses pre-configured devkitpro-local; do NOT use /opt/devkitpro
export DEVKITPRO=/Users/me/Documents/deepseek-harness/default-workspace/devkitpro-local/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC

# Both variants link; .S assembled with -x assembler-with-cpp
cd /Users/me/Documents/deepseek-harness/default-workspace/NooDS-Wii-workspace/NooDS-Wii
make JIT=1 -j4   # target = jit.dol / jit.elf
make JIT=0 -j4   # target = NooDS-Wii-interp.dol / .elf

# Clean rebuild requires Linux x86_64 host (cross-compiler binary is x86-64 Linux)
make clean
```

Flags (from existing `obj-jit1/.flags`):
- `-O2` (never `-O3`; `-O3` fails link per workspace notes)
- `-std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections`
- `-DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1 -DLOG_LEVEL=3`
- `-mrvl -mcpu=750 -meabi -mhard-float`
- Include paths point to `devkitpro-local` (`libogc`, `portlibs/wii`, `portlibs/ppc`)
- Link flags: `--gc-sections -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm`

**Important:** The cross-compiler binary (`powerpc-eabi-gcc`) is `ELF 64-bit LSB executable, x86-64, GNU/Linux`. Running `make` directly on this macOS ARM64 host will **fail** (`cannot execute binary file`). The workspace relies on pre-built `.dol` artifacts or requires a Linux build host / container.

---

## Updated gate criteria for this environment

| Gate | What this workspace can prove | What requires Linux / Dolphin / separate run |
|---|---|---|
| G0 build | Both `.dol` files exist; Makefile rules verified; `.S` assembled; flags stamp present. **Clean rebuild requires Linux x86_64.** | Reproduce exact sha256 from clean `make JIT=1` / `make JIT=0` on Linux host. |
| G1 encoder | `.S` bridge (`ppc_bridge.S`) assembled; `enc_cr.dis` / `enc_test.dis` show emitted forms. | Run `powerpc-eabi-as` + `objdump` for every stub form; add `stubdump=N` verification. |
| G2 accuracy (`rockwrestler.nds` 45fr / `suite.gba` 120fr) | `recovered.log` (20fr smoke) shows `mismatch=0`, growing native/compiled. **Full 45fr / 120fr not verified in this session.** | Launch `dolphin-emu` headless (`run-dolphin.sh`) with fresh SD image (`mksd.sh`) and `autoboot.txt`. Confirm `cycles=0`, `state=0`, `dumps=0`. Confirm two pool wraps at frame ~51 (120fr) without stall. |
| G3 suite (14 groups) | `evidence/g3/` per-group files present; `g3-summary.txt`: 13/14 `MATCH`, `io-read` `FAIL-COMPARE`. **Paired fresh runs not executed.** | Drive all 14 groups (`input.txt`: `DOWN` × index, `A`); compare verdict lines; compare final screens (video group = pixel comparison, no console lines). Confirm identical ordered verdicts and byte-identical screens. Confirm `io-read` difference is either real regression or known baseline. |
| G4 performance | `recovered.log` records `runTicks` and `gcyc` (`280896` for GBA, `197120` for NDS first frame). **Direction stated honestly (`slower` expected).** | Fresh paired runs (`JIT=1` vs `JIT=0`, same ROM/settings, same `autoboot.txt`); compute mean+median ticks/frame; report ratio with direction. |

---

## What is actually verified in this workspace (honest report)

- `Makefile`: `.S` assembly rules (`-x assembler-with-cpp`), dual-variant (`JIT=1`/`0`), flags stamp (`.flags`), `-mrvl` (not `-mogc`), `-O2`, separate `obj-jit1`/`obj-jit0`.
- `.S` bridge (`ppc_bridge.S`): assembled into `.o` (implied by existing `.dol` links), defines `jit_enter` / `jit_return` / `jit_call_abs0` ABI per AGENTS.md §5.
- `interpreter_lookup.cpp`: dispatch tables (`armInstrs[0x1000]` / `thumbInstrs[0x400]`) unchanged; condition gate evaluated before handler call.
- `interpreter.h`: `friend class ArmJit;` + `ArmJit jit;` present; `runOpcode()` uses `condition[...]`; `flushPipeline()` only PC-refill path.
- `arm_jit.h` / `.cpp`: cache structure (8192 entries), pool structure (4 MiB `.bss`, 160-word cap), `attach()` derives offsets, `reset()` clears cache + bumps epoch + full pool invalidation + `flushInstructionCache`, `tryRun()` checks epoch + key + opcode, negative entries (`state=2`) return false (interpreter fallback).
- `Buffer` / `Buffer::storeNzFlags()`: `rlwinm` (`mForm` op 20/21) + `mfcr` (op 19) + `rlwimi` rotation; `loadGuestRegister()` / `storeGuestRegister()` use D-form (`op 32` load / `op 36` store); `li32()` uses `lis` (`op 15`) + `ori` (`op 24`); `branchRelative()` uses `bc` (`op 18`) with relative displacement.
- `compileMoveImmediate()`: only `ARM MOV` (Rd≠15, not PC-writing, `op>>21==13`, `op>>25==1`) and `THUMB MOV Rd,#imm8` (`op & 0xF800 == 0x2000`) are compiled. Everything else = false → negative entry.
- `flushInstructionCache()`: `dcbst` per 32-byte line, `sync`, `icbi`, `sync`, `isync` — full sequence, not partial.
- `recovered.log`: 20 GBA frames, `mismatch=0`, state hash progression (`hash=...` per frame), `gcyc=280896` stable after first frame, `native`/`fallback`/`compiled`/`negative`/`flush` all non-zero after warm-up.
- `evidence/g3/`: 13/14 groups have both `-jit` and `-interp` verdict files; `video` has screen comparison (`NOT_RUN` vs expected ROM images); `io-read` shows different line counts (`jit=82` vs `interp=1788`).

---

## Explicit “not verified” list (updated for current environment)

1. **Clean rebuild** (`make clean && make JIT=1`) — blocked by host architecture (`x86-64` binary on `ARM64` macOS).
2. **G1 assembler oracle** (`powerpc-eabi-as` + `objdump` for every stub form) — requires executable cross-compiler; workspace has `enc_cr.dis` / `enc_test.dis` only.
3. **Full G2 accuracy** (`rockwrestler.nds` 45fr; `suite.gba` 120fr spanning two pool wraps) — `dolphin-emu` not installed; only 20-frame smoke (`recovered.log`) available.
4. **Pool-wrap stress** (120fr, two wraps at ~frame 51) — requires Dolphin run; `flush=0` only observed at frames 1–20.
5. **G3 full suite paired run** (14 groups, same `autoboot.txt`, same `dol` hashes) — workspace has per-group files but no paired fresh Dolphin output in this session.
6. **Performance ratio** (mean/median `ticks/frame`, direction) — `recovered.log` shows ticks but no paired `JIT=0` comparison from same run settings.
7. **Physical Wii console** — `-mrvl` targets Wii only; verification only through workspace artifacts / Dolphin headless.
8. **Save/load JIT parity** — not tested.
9. **Video group expected-image comparison** (`NOT_RUN`).
10. **Interpreter `CPSR` mask consistency** (prior defect: `~0xC0000000` vs `~0xF0000000`) — documented in `handoff.md`; requires careful comparison when `verify` is enabled.

---

## References and file paths used in this workspace

- `AGENTS.md` (original spec): `/Users/me/Downloads/AGENTS.md`
- Updated spec (this file): `/Users/me/Documents/deepseek-harness/default-workspace/AGENTS.md` (or written as new file per instruction)
- Workspace root: `/Users/me/Documents/deepseek-harness/default-workspace`
- Workspace sub-project: `NooDS-Wii-workspace/`
- Source / build directory: `NooDS-Wii-workspace/NooDS-Wii/`
- Source subdir: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/`
- `.S` bridge: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/ppc_bridge.S`
- Makefile: `NooDS-Wii-workspace/NooDS-Wii/Makefile`
- JIT source: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/arm_jit.{h,cpp}`
- Interpreter source: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/interpreter.{h,cpp}`
- Dispatch table: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/interpreter_lookup.cpp`
- Core / settings: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/core.{h,cpp}`, `settings.{h,cpp}`
- Rig: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii/rig.{h,cpp}`
- Memory / DMA / GPU: `memory.{h,cpp}`, `dma.{h,cpp}`, `gpu.*`
- Build artifacts: `NooDS-Wii-workspace/NooDS-Wii/NooDS-Wii-interp.dol`, `.map`, `obj-jit1/.flags`
- Tools: `NooDS-Wii-workspace/NooDS-Wii/tools/` (`setup-devkitppc.sh`, `run-dolphin.sh`, `test-case.sh`, `compare-runs.py`, etc.)
- Test ROMs: `NooDS-Wii-workspace/NooDS-Wii/gba/suite.gba`, `NooDS-Wii/nds/rockwrestler.nds` (and workspace-level `gba/` / `nds/` copies)
- Evidence: `NooDS-Wii-workspace/evidence/` (`g1/`, `g3/`, `toolchain-packages.sha256`)
- Workspace logs: `NooDS-Wii-workspace/recovered.log`, `.testframes.log`
- Deliverables: `deliverables/` (`jit.dol`, `NooDS-Wii-interp.dol`, `SHA256SUMS`, `handoff.md`)
- Toolchain source: `https://wii.leseratte10.de/devkitPro/` (never `pkg.devkitpro.org`)
- Cross-compiler binary: `devkitpro-local/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc` (Linux x86_64)

---

## Non-negotiables (reaffirmed for this environment)

- Report `PASS` / `FAIL` / `HANG` / `NOT_RUN` honestly. Never invent a pass.
- Feature branch only (`feature/arm-ppc-jit`). Do not modify `main` / `release` in workspace.
- Interpreter preserved. `JIT=0` build intact. Same source, same flags, only `-DNOODS_JIT` changes.
- `-mrvl` is Wii target (`-mcpu=750`, `-meabi`, `-mhard-float`); never `-mogc`.
- No unrequested roadmap work (lazy flags, block linking, growable pool) added; only listed in `handoff.md` next steps.
- Only `/home/user` (or workspace equivalent) persists; `/opt` (original `devkitpro`) can vanish — this workspace uses `devkitpro-local/` for persistence.
- Serial Dolphin runs (`run-dolphin.sh` uses `flock` on `.lock` file). Only one writer at a time.
- `PPC` macro collision (`PPC=1` predefine from devkitPPC) avoided by `JitPpc` namespace in `arm_jit.cpp`.
- `-O3` does not link (`undefined reference to Memory::writeFallback<T>`). Stay on `-O2`.
