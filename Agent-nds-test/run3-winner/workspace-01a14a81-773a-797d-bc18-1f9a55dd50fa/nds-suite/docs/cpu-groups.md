# Four independently scored CPU groups — development schema3

The generated CPU vectors are now partitioned into **ARM7,ARM7Thumb,ARM9,ARM9Thumb**.
These are actual independently executed/scored groups, not cosmetic debugger filters.
The23 original upstream groups remain separately scored, so the full plan has27 groups.
No vector was added, removed or assigned a different CPU-qualified case ID by this change.

## Registry and current results

| ID | Group | Cases | PASS | FAIL |
|---|---|---:|---:|---:|
|0|ARM7|4816|4803|13|
|1|ARM7Thumb|3326|3316|10|
|2|ARM9|6113|5889|224|
|3|ARM9Thumb|3353|3257|96|

Current totals including the subsequent Thumb-memory batch: ARM7:8142 (8119/23), ARM9:9466 (9146/320).
Full supervised score is now **17/27 =62.96%**, with8 returned group failures,
2 native division crashes,0 unrun. Previously17/25=68%: both old CPU groups had
failures in both ARM and Thumb subsets, so splitting them adds2 failed groups.
**The lower percentage is a scoring-denominator change, not new vector failures.**

## Stable agent selectors

```sh
.venv/bin/python scripts/run_desmume.py --list-groups
.venv/bin/python scripts/run_desmume.py --group ARM7 --output reports/arm7-arm.json
.venv/bin/python scripts/run_desmume.py --group ARM7Thumb --output reports/arm7-thumb.json
.venv/bin/python scripts/run_desmume.py --group ARM9 --output reports/arm9-arm.json
.venv/bin/python scripts/run_desmume.py --group ARM9Thumb --output reports/arm9-thumb.json
.venv/bin/python scripts/run_resilient.py --output reports/development.json
```

Public selectors accept registry names (case-insensitive) or current numeric IDs.
Invalid public IDs/names are argument errors, never an empty successful plan.
Each isolated group has score denominator1. Unselected vectors have host status
`NOT_SELECTED`, not PASS or an attempted failure. `breakdown.cpu_groups` provides
membership (`planned`), actual plan membership (`selected`), pass/fail and
unexecuted/interrupted counts separately. CPU-wide top-screen counters aggregate
executed vectors; they are not four independent score denominators.

The ARM7 worker now waits for explicit ARM/Thumb commands, so selecting an ARM9
group no longer executes the entire ARM7 CPU profile at startup. Selecting a Thumb
group does not run the same CPU's ARM cases. Before original upstream tests the
worker is released into its normal service loop. Hard CPU hangs still require the
bounded host supervisor; the standalone ROM cannot recover a stuck CPU by magic.

## ISA classification

Manifest schema3 records `isa`, `isa_basis`, `cpu_group`, `group_id` for each full
CPU-profile row. `scripts/cpu_groups.py` defines classification deterministically:

- Thumb instruction cases are Thumb regardless of the ARM ABI wrapper used to call them.
- IRQ/raw-exception cases with a Thumb source state are Thumb system cases.
- An ARM MOVS/SUBS/LDM exception-return instruction remains **ARM**, even if its
  return target is Thumb. These are not mislabeled as Thumb opcodes.
- Other ARM-source system and privileged cases remain ARM.

All rows belong to exactly one category. This grouping describes selected tests,
not complete ISA coverage; the broader Thumb roadmap remains open.

## Protocol migration — do not mix old evidence

Group registry and score protocol are **version3**; trace is **version4**.
See [memory/provenance update](thumb-memory.md) for per-record history semantics.
Score mailbox retains18 words but now reports27 registered groups; ledger has54
words at023f0100. Seed marker changes to52534D35/4E445353 and contains62 words.
Old-schema seeds and seeds from a different base-ROM SHA-256 are rejected.

Old IDs0 (all ARM9 vectors) and1 (all ARM7 vectors) are replaced by the four groups
above. Each old upstream ID2–24 moves to4–26. Native division crashes are now22/23;
WRAM/VRAM/TCM are24/25/26. **Vector IDs remain unchanged**; identity is still(CPU,id).
Use names or `--list-groups`, not hard-coded old numeric IDs.

Trace addresses/capacity are unchanged from development v2:9828 records per CPU.
Header word13 indicates the ARM7 writer is quiescent for safe restore; word14 is the
selected vector count; word15 is the ISA mask (1=ARM,2=Thumb,3=both,0=none).
Completion refers to the selected plan, not all stored metadata rows. Host validation
checks plan membership/count and rejects execution records outside the selected ISA.
Partial/interrupted traces stay incomplete, not falsely corrupt or complete. Old
v1/v2/v3 traces are not restored into v4. The entire restore input is validated first.

Legacy ARM7 detail mailbox now describes the last completed ARM7 ISA group. Use
`score.cpu7` for aggregate counters, not those legacy last-group fields.

## Debugger and LCD

Bottom overview shows all four scored groups with per-group totals and first failure.
Existing per-CPU explorers retain detailed records and display each case's scored
group. Ledger pages cover00–13 and14–26. The offline debugger has four-category
filters; CSV includes `cpu_group`, `group_id`, `isa`, `isa_basis`.

## Executed verification

`reports/cpu-group-tests.json` has13 scenarios covering classification, all four
independent plans/scores, expectation-fault isolation in both directions, old-seed
and invalid-selector rejection, and historical-record restoration after two crashes.
Actual ARM-only infinite loops injected on ARM7 and ARM9 are classified as host
timeouts; after restart each CPU's Thumb group still executes all its selected
cases. Remaining baseline Thumb mismatches stay FAIL, not PASS.

All12 LCD pages and3 recovery scenarios pass their assertions after migration.
Thumb800/profile and prior ARM512/profile regression suites pass;18 trace/protocol
checks pass. QEMU subsets rerun successfully except the explicitly capability-limited
raw MPU oracle, still PARTIAL_UNSUPPORTED rather than PASS. Current full run retains
all17608 CPU records and160 instrumented assertion events across native restarts.

This remains coverage-first development, NOT a final release. Some older fixed-count
release regression scripts still target the historical layout and must be migrated
before packaging. Milestone10 remains the sole verified recovery ZIP. Next Thumb
coverage priorities: remaining memory/addressing forms, high-register/PC/SP
forms, branch ranges and stack-edge behavior.
