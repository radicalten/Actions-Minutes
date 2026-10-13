# NooDS-Wii native JIT — Stage 1 handoff (sources to upload)

> The continuation handoff for a fresh agent is **`AGENTS.md` at the workspace root** (one level up
> from this folder). If you upload only this folder, also copy that `AGENTS.md` next to these files.

**Why this folder exists:** the workspace snapshot the platform generates (`changes.zip`) is capped at
~128 MB and, on the last snapshot, it silently omitted the subdirectory `repo/NooDS-Wii/NooDS-Wii/`
— i.e. exactly the modified C++ sources. This folder is a small (~2 MB), top-level copy that cannot
be dropped, so the source files are always available. The workspace was also trimmed (stale harness
binaries removed, devkitPPC moved out of `/home/user`) so future snapshots fit the cap and include the
repo tree normally.

## Contents (byte-identical to the working tree in `/home/user/repo`)

| File | sha256 |
|---|---|
| `NooDS-Wii/NooDS-Wii/arm_jit.cpp` | `e2d055294f76e5a36797751cad3435dc8716580c431c8806b301296228ccac93` |
| `NooDS-Wii/NooDS-Wii/arm_jit.h` | `f3e95678c4d7b087824c8e113e3c079d209f9d766cca9e29877b0141d708cf16` |
| `NooDS-Wii/NooDS-Wii/ipc.h` | `12b61b0df28e06729fd2d8f8e7ff986bce8bfba74279e69dfa9bc33355ebb5d0` |
| `NooDS-Wii/jit.dol` (rebuilt browser JIT) | `cf53857c4d8fe12e6b7c6fc07ce57d89e16f342247ea7062c429f836c7f83515` |
| `noods_jit_stage1.patch` (`git diff` of the 3 sources vs `92e634a`) | `0a8c9cfc3bf824d47a292ed2e9d93aa0403c793d4ca99071dbdfcd5f3126d49d` |

## How to apply

Option A — drop-in copy (paths mirror the repo layout):

```
cp -r handoff/NooDS-Wii/  <your-repo>/        # arm_jit.cpp, arm_jit.h, ipc.h, jit.dol
```

Option B — patch (verified to apply cleanly to pristine `92e634a`, `git apply --check` PASS):

```
cd <your-repo> && git apply /path/to/noods_jit_stage1.patch
```

To also regenerate the browser DOL after applying:

```
export DEVKITPRO=<your devkitpro>   # devkitPPC r50 + libogc
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
cd <your-repo>/NooDS-Wii
make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1     # ELF/DOL build
bash tools/build_browser_jit.sh                     # -> jit.dol (expect the sha256 above)
bash tools/check_g1_encoder.sh                      # 44/44 encoder oracle
```

Nothing else in the tree is modified: `git status` shows only these 3 sources + `jit.dol` (plus the
untracked `evidence/toolchain_bootstrap_result.txt`). `interpreter.cpp` is pristine.

## What is in this change (ARM7 ARMv4T + ARM9 ARMv5TE, ARM + Thumb)

1. **PC-destination load block terminator** — `isPcDestLoadTerminatorArm` /
   `emitPcDestLoadTerminatorArm`: native `LDR/LDRB/LDRH PC,[…]` (pre-index, no writeback) as a block
   terminator (computed branch / jump table). Fast path mirrors `ldrOf/ldrbOf/ldrhOf` (misalign
   rotate, ARM9 T-bit from bit 0, inline refill when the target page is mapped and word-clean; else
   the instruction falls back to the interpreter handler). Not-taken = linear path, `+1` cycle;
   taken `+5`. Wired through `compileBlock` (count/nativeCount/prefixCycles/blockPure/hasTerm),
   `tryExecuteBlock`, and the `executeArmCore` Rd15 fallback gate.
2. **ARM7 IPCSYNC fast path** (`NOODS_JIT_IPCSYNC_FAST`, default 1) — `LDRH@0x04000180` reads the
   live `ipc.ipcSync[1]` (new `friend class ArmPpcJit` in `ipc.h`), plus the generic ARM7 halfword
   load gate.
3. **Generalized STRH stores** — `isNativeStoreArm`/`emitNativeStoreArm`: STRH pre/post-index,
   ±writeback, ±unshifted register offset, including `P=0,W=0` (NooDS `strhPt*` post-updates Rn even
   when W=0; confirmed by parity). New `stwbrx` encoder `0x7D003D2C`, verified against
   `powerpc-eabi-as -mgekko`.
4. Removed a duplicated HOTPC `fbSiteRecord` double-count. HOTPC profiling stays opt-in (default 0).

**Parked / deliberately disabled:** accepting `STR`/`STRB` natively still diverges under block
compilation (see the `isNativeStoreArm` comment in `arm_jit.cpp`); only the STRH subset is enabled.
Do not re-enable without fixing the end-of-block `pipeline[0]`/`R15` staleness.

## Verification (all on the frozen tree, harness = PPC-Linux + qemu-ppc)

- Interpreter-parity, `MISMATCHES=0`: 120 f and 300 f `rockwrestler.nds` (3 checkpoints), 35 f dense
  14-checkpoint sweep, `suite.gba` 60 f (GBA path intact), 5 f smoke.
- Fallback reduction (120 f, `harness/runs/nds_jit_p0.log` vs pre-change `nds_jit120.log`):
  `fbInsn7 2,050,058 → 558`, `fbInsn9 152,906 → 59,028`, `blkInsn7 12.6 M → 14.6 M`, `blkFail = 0`.
- Residual ARM9 fallbacks (59,028) are ≈ the directly-dispatched `Bcc` sites at 02000284
  (`1AFFFFF9`, 40,376) + 020001BC (`0AFFFFFC`, 18,843) — next candidate for native coverage.
- Real Wii targets rebuilt: `make -j2 JIT=1 BLOCKS=1` DOL ok and `jit.dol` sha256 above.

Logs referenced above live in `/home/user/harness/runs/` (`nds_jit_p0.log`, `nds_jit300_p0.log`,
`bis_interp.log` vs `exp_p0.log`/`bis_final.log`, `gba_jit_p0.log`, `nds_hot_final.log`).
Harness timings (~25 ms/f) are harness-only and are **not** Wii FPS.
