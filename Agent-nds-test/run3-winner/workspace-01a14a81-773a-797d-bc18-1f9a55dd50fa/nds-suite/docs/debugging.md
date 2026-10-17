# Granular execution diagnostics — milestone 10

## What a vector PASS means

Every generated vector invokes a named assembly helper with `a`, `b`, and `initial` inputs. It checks **one primary result** exactly, plus **one auxiliary result** under `mask` when that mask is nonzero. The auxiliary can be flags, another data word, a guard, or a writeback delta—not always CPSR. `aux_kind`, where provided, identifies it. A zero mask means auxiliary **not checked**, not an extra pass.

The 6,498 ARM9 vectors cover 546 helper families; the 5,174 ARM7 vectors cover 454. Executed primary / masked auxiliary comparisons are 6,498 / 6,174 on ARM9 and 5,174 / 4,841 on ARM7. These include failures, not additional group scores.

The shared IDs 1–3,390 are unchanged. ARM9-only LDRD/STRD IDs are 3,391–3,486. ARM9 extension IDs continue through 6,498. ARM7-specific IDs are 3,391–5,174. See [raw-exception design](arm9-milestone8.md) for raw-exception sentinel/error-bit meanings. IDs above the shared prefix are CPU-qualified: schema-2 `src/generated/debug-manifest.json` has `vectors` (ARM9) and `arm7_vectors` (ARM7). See [arm7-specific.md](arm7-specific.md) for the 23 retained reference mismatches and execution proof. Runtime inputs, expectations and masks are captured in the trace, so deliberately altered ROM expectations are reported as actually executed rather than silently replaced by the static manifest.

## On the DS

Left/Right changes the **12** lower-screen pages. Pages **10 and 11** are ARM9/ARM7 explorers:

- **Up/Down:** select family, or select a case within the current family.
- **A:** toggle family/case selection.
- **B:** jump to the next failed case, across families; wraps around.
- Displays planned/pass/fail/unfinished counts for the selected family, case ID/state, inputs, expected/observed primary and auxiliary results, mask, XOR differences, helper PC, and prior-boot provenance.
- Unfinished means NOT_RUN or IN PROGRESS, not PASS. Observed values are withheld until a record is complete.

Page **12** is the original C++ assertion journal. Up/Down selects an event; B selects the next failure. It shows group, runtime assertion ID, source line, and the failure predicate's expected zero / observed value. Source expressions appear in the offline report. The main score remains group-based and does not change during browsing.

## Host debugger

```sh
python3 scripts/run_resilient.py --output reports/resilient.json
# A nonzero suite status is expected on the current backend; still run the export:
python3 scripts/export_debug.py reports/resilient.json --output reports/debugger.html
```

Open `reports/debugger.html` in a browser or the workspace viewer. It is self-contained, requires no network, and offers CPU/outcome/family filters, text search, pagination, and expandable case details. `debugger.csv` contains every CPU row; `debugger-assertions.json` contains source-linked C++ events. Assembly source paths/line numbers and helper PCs let an emulator developer locate instructions in `artifacts/arm9.elf` / `arm7.elf` and the generated assembly. The PC is the helper entry, **not** a claim to identify the precise faulting instruction.

Runner JSON contains per-family summaries and **every** vector mismatch in `breakdown`. Raw traces are immutable, SHA-256-named binary files referenced by `diagnostics`. Keep those files with the JSON; the export verifies their hashes. Project-relative trace paths remain portable with the project tree.

### Original-group limitations

The build instruments **117 single-line C++ assertion sites** without changing the failure expression, ID, or vendor source. Each expression is evaluated **once**, and the raw failure predicate (often Boolean, sometimes a bitmask) is journaled before calling the original failure hook. The recorded full run contains **160 execution events** because some sites occur in loops. This is not “160 of 117 tests”: sites and events are different units.

The journal does **not** separately capture all MMIO operands used by a predicate. Assembly assertions, multiline/direct failure paths, and not-yet-reached assertions are **group-only/unknown**. Original groups remain internally fail-fast. A successful group never manufactures PASS events for uninstrumented checks. Instrumentation changes scheduling and is not a cycle-accurate hardware reference.

## Memory protocol

All words are little-endian u32. Score protocol remains v2.

| Address | Reserved area |
|---|---|
| `0x02300000` | ARM9 trace, 256 KiB slot |
| `0x02340000` | ARM7 trace, 256 KiB slot |
| `0x02380000` | Assertion journal, 64-byte header + up to 4,096 × 20-byte records |
| `0x023ed000` | Resume restore acknowledgment |

CPU header is 16 words / 64 bytes:

0 magic `0x43545235`; 1 version 1; 2 CPU (9/7); 3 planned vectors; 4 executed; 5 passed; 6 failed; 7 active ID; 8 complete; 9 record stride **40 bytes**; 10 manifest CRC32; 11 historical-import flag; 12–15 reserved.

Record for ID `i` starts at `base + 64 + (i-1)*40`. Its ten words are:

`state, observed_primary, observed_aux, helper_pc, a, b, initial, expected_primary, expected_aux, mask`.

States: 0 NOT_RUN, 1 IN PROGRESS (host export: INTERRUPTED when sampled unfinished), 2 PASS, 3 FAIL. Expected fields and PC are written before entry; observed values precede the final state commit. There is capacity for 6,552 records per slot. No instruction-level trace or abort handler is implied.

Assertion header: magic `0x41535235`, version 1, capacity, committed event count, dropped-event count, then 11 reserved words. Each five-word event is `group, assertion_id, original_source_line, observed_failure_predicate, status` (1 PASS / 2 FAIL). Expected predicate is zero. Overflow is explicit, never silently counted as PASS. Source catalog: `src/generated/assertions.json`.

## Recovery and provenance

A BEGIN checkpoint references immutable trace/journal snapshots. After a crash, the resumed ROM publishes score state **6 (RESTORE_WAIT)**. The worker verifies saved hashes, restores the prior evidence while the emulator is paused between frames, marks CPU traces historical, then writes `0x52455335` to `0x023ed000`.

ARM7 runs its CPU vectors at boot. The worker only overwrites its buffer after the fresh ARM7 run has completed, and only imports an old ARM7 trace when its group was already resolved. If a fresh ARM7 is still writing, restoration is refused and reported; it is never raced. Fresh boot snapshots remain fresh observations. Per-boot checkpoint references preserve original evidence on the host even when in-ROM restoration is unavailable.

Only evidence captured **before** a native process crash can survive it. The fatal group's events after the last BEGIN checkpoint are unavailable; do not infer they ran or passed. A frame timeout where the worker remains alive can export the in-progress vector and unrun tail. The resume barrier has a bounded wait; other emulator adapters must implement its handshake to restore detailed evidence.

## Executed tests

`test_diagnostics.py` exercises three simultaneous ARM9 failures (including a new LDRD case), two ARM7 failures, next-failure/family/case navigation, an actual infinite-loop helper with INTERRUPTED and NOT_RUN records, and historical CPU/journal restoration after native crashes. `test_ui.py` covers all 12 pages with real DS key input and score/top-screen invariance. Existing negative and recovery suites remain enabled.

Release evidence may be deduplicated by `scripts/compact_evidence.py` into `reports/blobs/<sha256>.bin`. JSON paths are updated and byte hashes retained; decoding still verifies each blob.

Capacity:6552 records per CPU;54 ARM9 and1378 ARM7 slots remain. A larger expansion must update the trace layout rather than drop records.
