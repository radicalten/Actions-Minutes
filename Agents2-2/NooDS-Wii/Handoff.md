# Handoff — NooDS-Wii ARM → Wii PowerPC JIT

**Date:** 2026-10-10 (end of the JIT performance session)
**Repo:** `/home/user/NooDS-Wii` (git, branch `main`, HEAD `fdcb968`, no remote configured)
**Read first:** `AGENTS.md` (standing directives, layout, gate commands, dead ends, open items)

## 1. Where things stand

- **Working tree builds a faster JIT that passes all three gates.** The uncommitted diff is the session's JIT work: native loads and STRH, exact R15 sync in helper slow paths, and the latest addition, an inline same-page branch exit for ARM B/BL (`emitInlineBranchArm`). See §3.
- **Not committed.** The user will commit. Nothing has been pushed.
- **Performance** (frame 45, rockwrestler, same session, wall-clock `accum_us`):

  | Build | accum_us |
  |---|---:|
  | Interpreter (`JIT=0 BLOCKS=0`) | 4,568,589 |
  | Baseline before this change (exact R15 sync) | 4,913,380 |
  | **Current JIT (`JIT=1 BLOCKS=1`)** | **4,586,792** |

  The change is about 6.6% faster than the baseline. The JIT is still about 0.4% slower than the interpreter. Do not claim a JIT speedup over the interpreter.

## 2. Gate status on the current build

| Gate | Status | Evidence in repo |
|---|---|---|
| G1 — PPC encoder oracle | **PASS**, 52/52 (required 44) | `NooDS-Wii/evidence/g1_encoder_oracle_br1_2026-10-10.log` |
| G2 — rockwrestler frames 15/30/45 vs interpreter | **PASS**, `mismatch_count` 0; memory and framebuffer hashes match | `NooDS-Wii/evidence/g2_compare_br1.json`, runs in `g2_g2_br1/` and `g2_rw_interp/` |
| G3 — mGBA suite vs interpreter | **PASS**, 13 suites, 6,998 tests, 3,506 passed, 0 mismatches, gbaout SHA `97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff` | `NooDS-Wii/evidence/g3_compare_br1.json`, run in `g3_br1/` |

The current build's SHA-256 is `38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e`. It matches `/tmp/br1.dol`, which was gated. No DOL is kept in the tree. Rebuild and check the hash before relying on it.

The previous baseline (`final4`) also passes G2 and G3. Its compare files are `g2_compare_final4.json` and `g3_compare_final4.json`.

## 3. Uncommitted changes

```text
  M NooDS-Wii/NooDS-Wii/arm_jit.cpp     native LDR/LDRH/LDRB/STRH, helper R15 sync, inline same-page branch exit (+343/−10 across both files)
  M NooDS-Wii/NooDS-Wii/arm_jit.h       declarations; emitInlineBranchArm must stay static
  M NooDS-Wii/tools/*.sh, *.py (7)      mode 100644 → 100755 only (no content change)
?? NooDS-Wii/evidence/...              new evidence (see §5)
?? NooDS-Wii/evidence/experiments_2026-10-10.md
```

Check with `git -C /home/user/NooDS-Wii status --short`. The mode change is a decision for the user: commit it or revert it.

## 4. Experiments tried and rejected this session

Full table with run directories and numbers: `NooDS-Wii/evidence/experiments_2026-10-10.md`. Short version:

- **Native self-loop chaining** (a loop-back without returning to the dispatcher when the loop calls no helper). Correct once two bugs were fixed. It never fires on rockwrestler, because every self-loop iteration calls an I/O helper. Result: 4,612,914 µs, which is slower than the current build. Sources are in `/home/user/perf/chain_experiment/`, outside the repo.
- **Dispatch-count reduction:** inline branch exits remove ARM7 branch dispatches but not ARM9 self-loop dispatches. Chaining would be needed for that.
- **Native halfword loads and PC-base LDR:** pass G2, no speedup. Off.

## 5. Cleanup done on 2026-10-10

- Removed regenerable outputs: root-level DOL/ELF/MAP copies, `build_*` directories.
- Removed Dolphin logs and the scratch G2 run directories. Their results are summarized in `experiments_2026-10-10.md`.
- Kept: compare JSONs for `br1`, `final4` and the earlier `rw_jit1_blocks1`; `result.txt`, `g2_state.log`, memory and framebuffer dumps for the four retained G2 runs; G3 `gbaout_*` logs and mGBA summaries; G1 logs; G-1 `vbagx_*` notes.
- **Note:** `result.txt` `STATUS` is the emulator's run status, not parity. Parity is in the compare JSONs.
- The G3 `result.txt` files were removed along with the Dolphin run folders. The G3 evidence that gates (compare JSON, gbaout log, mGBA summary) is kept.

## 6. Corrections to the earlier documents

The earlier `AGENTS.md` and `Handoff.md` described a different state and cited files that are not in the repo. Specifically:

- `deliverables/*.dol`, `WORKSPACE_MANIFEST.md`, `local_changes_2026-10-10.patch`, `evidence/poll_skip_2026-10-10.md`, `evidence/g2_compare_fin_def45.json`, `evidence/g3_compare_dflt.json` do not exist.
- `NOODS_JIT_IDLE_SKIP` is not present in this tree.
- The G0 build logs, G4–G6 summaries and base-build hash cited earlier are not in the repo. Re-run the gates rather than trusting those hashes.
- The DISPSTAT / VCOUNT / IPCSYNC read fast path is in `memory.cpp` at HEAD. That claim holds.
- `NOODS_JIT_POLL_SKIP` is in `arm_jit.h` with default 0. That claim holds.

## 7. How to resume

```bash
cd /home/user/NooDS-Wii/NooDS-Wii
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC \
       PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/sbin:/sbin:/usr/games:$PATH
# If /opt/devkitpro is missing: sudo tools/bootstrap_toolchain.sh

make clean JIT=1 BLOCKS=1 && make -j2 JIT=1 BLOCKS=1
sha256sum NooDS-Wii_jit1_blocks1.dol          # expect 38fa9481ea770e28caa588c382f2db00eec54949b41019e01ebd6c7275eccc0e
bash tools/check_g1_encoder.sh                  # G1
```

Then run G2 and G3 as in `AGENTS.md` §3 before any further change.

## 8. Open items

1. **ARM7 `LDR pc`:** these loads are not native. They go through the interpreter fallback. Worth about 4% of profiled time. Needs exact ARM7 pipeline and cycle reproduction. Not started.
2. **`bctrl` LR-clobber audit:** re-check each `bctrl` site with `grep -n bctrl NooDS-Wii/NooDS-Wii/arm_jit.cpp`. The line numbers recorded earlier are stale.
3. **Further speed:** remaining cost is per-dispatch overhead and I/O helper calls in self-loops. Large gains need a design that keeps IRQ and scheduler behaviour correct across helpers. Agree the approach with the user first.
