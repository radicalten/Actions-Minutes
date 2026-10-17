# Coverage matrix and current results

This document records coverage and outcomes for ROM SHA-256 `fa7509bfafb6cfffaba83ec591efad698953b997c073bce7cf08f19d1a0142b4`. Counts are **vectors**, not instruction encodings, distinct opcodes, or assertions.

## Profile totals

| CPU profile | Shared | Profile-specific | Total | DeSmuME PASS | DeSmuME FAIL |
|---|---:|---:|---:|---:|---:|
| ARM7TDMI profile | 3,390 | 6,090 | **9,480** | **9,457** | **23** |
| ARM946E-S profile | 3,390 | 7,414 | **10,804** | **10,484** | **320** |

The 3,390 shared IDs are identical across profiles. Profile-specific vectors are appended; earlier IDs were not renumbered by the branch sweep. Always qualify an ID with its CPU.

## Four CPU/ISA execution groups

The full isolated run used `py-desmume 0.0.9`, booting the ROM separately for each registered group. Vector results were extracted from trace v4 and checked against the generated manifest.

| Group | CPU profile | ISA | Selected vectors | PASS | FAIL | Result |
|---|---|---|---:|---:|---:|---|
| ARM7 | ARM7TDMI | ARM | 4,816 | 4,803 | 13 | FAIL |
| ARM7Thumb | ARM7TDMI | Thumb | 4,664 | 4,654 | 10 | FAIL |
| ARM9 | ARM946E-S | ARM | 6,113 | 5,889 | 224 | FAIL |
| ARM9Thumb | ARM946E-S | Thumb | 4,691 | 4,595 | 96 | FAIL |

All **1,338 branch vectors per profile** passed in the focused ARM7Thumb and ARM9Thumb DeSmuME runs. The observed group failures remain 13/10 on ARM7 and 224/96 on ARM9; the branch expansion introduced no new mismatches. These are retained emulator observations, not adjudications of whether an architectural expectation, emulator behavior, or machine-specific behavior is responsible.

## Full 27-group runs

The isolated full run was performed with:

```sh
.venv/bin/python scripts/run_desmume.py \
  --output reports/development-after-thumb-sweep.json
```

Observed result: **17 PASS, 8 FAIL, 2 EMULATOR_CRASH**, with all 27 groups attempted. The two crashes occurred in DIV 64/32 and DIV 64/64. They are unresolved emulator/harness outcomes, not PASS and not vector FAIL counts.

The checkpointed/recovery path was also run:

```sh
.venv/bin/python scripts/run_resilient.py --output reports/development.json
```

It recovered from the same two emulator crashes and completed the group ledger. It preserves the same 17 PASS, 8 FAIL, and 2 crash outcomes; its aggregate score reports 17 passed and 10 failed because crashes are included in the non-pass count. The final CPU totals were ARM9 **10,484 PASS / 320 FAIL** and ARM7 **9,457 PASS / 23 FAIL**. The recovery run validates diagnostic restoration/provenance, not a clean full-suite PASS.

| Group IDs | Result | Notes |
|---|---|---|
| 0–3 | FAIL | Four CPU/ISA groups; detailed counts above. |
| 4–14 | PASS | Eleven portable CPU instruction groups. |
| 15 | FAIL | LDM / STM group. |
| 16 | PASS | IPCSYNC. |
| 17 | FAIL | IPCFIFO. |
| 18 | PASS | IPCFIFO IRQ. |
| 19–20 | PASS | SQRT 32 and SQRT 64. |
| 21 | FAIL | DIV 32/32. |
| 22–23 | EMULATOR_CRASH | DIV 64/32 and DIV 64/64. |
| 24–25 | PASS | WRAM CNT and VRAM CNT. |
| 26 | FAIL | TCM. |

A full-run shell exit status of 1 is expected for these observed failures/crashes; inspect the JSON/XML and trace rather than treating the run as a successful release result.

## Thumb families represented in both profiles

| Family | Vectors per profile | Scope |
|---|---:|---|
| Thumb register ALU edges | 800 | Selected shift/count edges, aliases, and carry-sensitive operations. |
| Aligned Thumb memory | 1,248 | Selected loads/stores, offsets, aliases, and guard checks; not a full region/alignment/abort matrix. |
| Thumb SP/address generation | 192 | Selected SP adjustment and SP-relative address cases. |
| Thumb PC-relative ADD/literal loads | 216 | Selected PC alignment/offset/register cases and static literals. |
| Earlier shared Thumb vectors | 753 | Original common matrix. |
| Other profile-specific Thumb/system vectors | 117 ARM7 / 144 ARM9 | Earlier CPU-specific and system/exception/IRQ vectors. |
| Thumb conditional/unconditional branches | **1,338** | 320 boundary/condition cases plus a 1,018-case displacement sweep; see [branch sweep design](thumb-branch-boundaries.md). |

These are defined matrices, not exhaustive ISA enumeration. The branch sweep encodes and audits all 256 legal conditional-B displacements. It executes both EQ states for most offsets; for −4 and −6 bytes it executes only EQ-false because a taken path loops. At −2 bytes the branch target aliases fallthrough. Unconditional B has 32 stratified interior offsets plus the original signed-range/zero boundaries; it does not sweep every legal immediate.

## Branch IDs and trace capacity

| Profile | Boundary IDs | Sweep IDs | Total after build | Trace capacity | Remaining slots |
|---|---:|---:|---:|---:|---:|
| ARM7 | 8,143–8,462 (320) | 8,463–9,480 (1,018) | 9,480 | 11,057 | 1,577 |
| ARM9 | 9,467–9,786 (320) | 9,787–10,804 (1,018) | 10,804 | 11,057 | **253** |

The trace slots were expanded to `0x6c000` bytes each. The assertion journal remains below the fixed restore-acknowledgement address; runtime capacity, full-trace, restore, and provenance checks passed. See [trace layout](architecture-and-traces.md). Do not append further vectors beyond the remaining capacity without another reviewed layout change.

## Evidence limits

The focused branch suite passed its DeSmuME ROM tests, QEMU `arm946` comparison, and QEMU `ti925t` proxy comparison. The proxy is not an exact ARM7TDMI model. No physical DS test was run; QEMU does not model DS BIOS, MMIO, memory-controller, or timing behavior. These results do not make the old release gate READY.
