# Architecture, generators, IDs, and traces

## Target profiles

The Nintendo DS ROM contains two CPU test profiles:

- **ARM7:** ARM7TDMI / ARMv4T, generated ARM and Thumb cases in a separate CPU-specific profile.
- **ARM9:** ARM946E-S / ARMv5TE, generated ARM and Thumb cases in the ARM9 profile.

Each profile is split into independently selectable ARM and Thumb groups. CPU target, ISA group, and vector identity are explicit; `ID` alone is not globally unique.

## Generation path

The build pipeline is `scripts/build.sh` → generated sources/manifests → cross-compiled ARM7/ARM9 images → `ndstool`-packaged `suite.nds`.

- `scripts/generate_cpu.py` builds the shared vector prefix, currently 3,390 entries per profile.
- `scripts/generate_v5.py` appends ARM9-only cases, currently 7,414 profile-specific entries.
- `scripts/generate_arm7.py` appends ARM7-specific cases, currently 6,090 profile-specific entries.
- `scripts/thumb_branch.py` provides both the original branch-boundary matrix and the expanded displacement sweep; both profile generators include them.
- `src/generated/debug-manifest.json` records CPU-qualified IDs, expectations, masks, ISA group, source, and coverage metadata. Generated vector files are also checked into this development tree.

All branch vectors append after earlier vectors without renumbering them:

| Profile | Boundary matrix | Displacement sweep | Total branch vectors |
|---|---:|---:|---:|
| ARM9 | 9,467–9,786 (320) | 9,787–10,804 (1,018) | 1,338 |
| ARM7 | 8,143–8,462 (320) | 8,463–9,480 (1,018) | 1,338 |

Use `(CPU, ID)` when recording a vector result.

## Trace v4 memory map

Canonical values are in `src/trace_layout.h`; `scripts/trace_layout.py` reads that header rather than maintaining an independent address map.

| Region | Base | Reserved size | Contents |
|---|---:|---:|---|
| ARM9 trace | `0x02300000` | `0x6c000` bytes | Header + up to 11,057 × 40-byte records |
| ARM7 trace | `0x0236c000` | `0x6c000` bytes | Header + up to 11,057 × 40-byte records |
| Assertion journal | `0x023d8000` | `0x14040` bytes | 64-byte header + up to 4,096 × 20-byte events |
| Restore acknowledgement | `0x023ed000` | one word | Host/emulator restore handshake |
| Group mailbox / ledger / UI | `0x023f0000` onward | fixed protocol areas | Score, groups, navigation, and display metadata |

Each trace slot holds 11,057 records: `(0x6c000 − 64) // 40`. Twenty-four bytes remain after the maximum records. ARM9 uses 10,804 records, leaving **253 slots**; ARM7 uses 9,480, leaving **1,577 slots**. The ARM9 and ARM7 slots are adjacent and do not overlap. The assertion journal ends at `0x023ec040`, **0xfc0 bytes before** the restore-acknowledgement gate at `0x023ed000`.

The record format is unchanged, so the trace protocol remains v4. A CPU trace uses a 16-word (64-byte) header and 10-word (40-byte) records. The record includes run state/provenance, observed primary and auxiliary values, function PC, input operands, initial flags, expected primary/auxiliary values, and comparison mask. The host validates protocol, CPU identity, manifest fingerprint, planned ISA selection, count, record state, and completion counters before interpreting a result.

`src/trace.cpp` rejects invalid IDs, oversized plans, and malformed begin/end sequences. A missing or incomplete record is not reported as PASS. The per-record provenance bit distinguishes live work from records restored after a supervised emulator restart.

The expanded layout was exercised with the rebuilt ROM: its full ARM9 trace declared 10,804 rows with no overflow; the ARM7 trace declared 9,480. A host C++ guard-page test wrote through all 11,057 capacity slots and checked that the next write is rejected. Corrupt-header, truncation, restore-preflight, and mixed-provenance tests passed. The journal boundary remains below the restore gate.

The build instrumented 117 C++ assertion sites. This is not a count of assembly assertions or CPU vectors and does not imply that each source predicate was validated on hardware.

## Tool responsibilities

- **Host tests** check deterministic source/manifest invariants and guard scripts. They do not execute the DS CPU vectors.
- **QEMU tests** execute portable CPU code in named QEMU CPU models. `arm946` is relevant to the ARM9 profile; `ti925t` is an ARMv4T proxy only.
- **DeSmuME** executes the packaged NDS ROM and supplies group/trace observations. It is emulator evidence, not a physical DS run.
- **Hardware testing** has not been performed for the current snapshot.

The current ROM's stable SHA-256 and test outcomes are listed in the [handbook](README.md) and [coverage results](coverage-matrix.md).
