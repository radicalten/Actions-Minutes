# NooDS-Wii native JIT — handoff (Stage 2 final)

Read `AGENTS.md` (repo root) first. It has the current state, the environment setup, the build and
test commands, and the open items. This folder holds only what is new: the changed sources.
Nothing here is committed or pushed; the changes are in the working tree.

## Contents

| File | sha256 | Status |
|---|---|---|
| `NooDS-Wii/NooDS-Wii/arm_jit.cpp` | `41b850838924badec8e7a9f341e3f493aa5938bef27276b5a46418ffb3230b37` | Full current JIT source (Stage 1 + Stage 2). Identical to the repo file. |
| `NooDS-Wii/NooDS-Wii/arm_jit.h` | `f3e95678c4d7b087824c8e113e3c079d209f9d766cca9e29877b0141d708cf16` | Unchanged from Stage 1. |
| `NooDS-Wii/NooDS-Wii/ipc.h` | `12b61b0df28e06729fd2d8f8e7ff986bce8bfba74279e69dfa9bc33355ebb5d0` | Unchanged from Stage 1. |

The two shipped DOLs are not copied here. They are in the repo at `NooDS-Wii/`:

| DOL (repo path) | sha256 | What it is |
|---|---|---|
| `NooDS-Wii/jit.dol` | `2e505a160be548a88d57bd9850dbd0a322629f4c87c5e7886311a76f4d1ff6da` | Browser build, JIT on (`JIT=1 BLOCKS=1`). |
| `NooDS-Wii/interp.dol` | `913c647cd501c9b43392f041a09f5806c8d4e45f9f1eac72a62da75d67ab9771` | Browser build, interpreter only (`NOODS_FORCE_BROWSER=1`). |

No patch files are shipped. The sources are the deliverable.

## What changed in Stage 2

1. **ARM B/BL with a condition may start a block** (`isBranchHeadArm`). The condition is evaluated natively
   on both edges, or through `execBranchArm` when the target leaves the page.
2. **Timer reads inside blocks see the current clock.** `emitHelperR15Sync` publishes the block prefix to
   `Core::globalCycles` around helper calls. This fixed the earlier ARM7 immediate-offset LDRH divergence
   and the late frame-167 and frame-900 mismatches.
3. **Time-add offset fix.** `Core` is larger than 32 KB, so `offsetof(Core, globalCycles)` was truncated
   by `int16_t`. It is now loaded with `emitLoadImm32`.
4. **ARM7 IPCSYNC fast path is on by default** and is skipped in GBA mode (`Core::gbaMode` check).

## Verification summary

Full details and caveats are in `AGENTS.md` §4.

- Harness interpreter parity (`MISMATCHES=0`): rockwrestler frames 1–60 and 120, suite.gba frames 1–60,
  rockwrestler 900 frames, suite.gba 900 frames.
- G0 builds (JIT/BLOCKS 0/0, 1/0, 1/1); G1 encoder oracle 52/52; G2 NDS and GBA comparisons, pools 0 and
  8192, all `PASS` (the NDS stub-JIT pool-8192 wrap case is `NOT_RUN_WRAP`); G3 suite logs byte-identical
  across variants, with both JIT comparisons `PASS`.
- Dolphin browser check: both DOLs reach `FILE_BROWSER active dir=sd:/`, and a 60-second run is clean.

Not run: real Wii hardware, the browser UI on a Wii, and performance measurement on hardware.
Harness numbers are not Wii FPS, and this handoff claims no speedup.

Known discrepancy: the gate run recorded `d2048cf…` for the browser `jit.dol`, but a clean rebuild from
the same source gives `2e505a…`, which is what ships. See `AGENTS.md` §4.
