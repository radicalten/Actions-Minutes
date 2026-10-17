> **v1.0 target: coverage first.** See [release plan](docs/v1.0-release.md).
> Original project contributions are MIT-licensed; upstream redistribution permission still blocks public release.

> **CPU GROUP MIGRATION:** [ARM7 / ARM7Thumb / ARM9 / ARM9Thumb](docs/cpu-groups.md)
> are now separately executed/scored. Registry/score schema3, trace4;27 groups,
> current17/27=62.96% with unchanged vector results. Prefer group names over old IDs.

> **DEVELOPMENT — NOT FINAL:** The working ROM now has9466 ARM9 /8142 ARM7 cases
> and trace protocol v4. Milestone10 is only the last packaged recovery release.
> Read [AGENTS.md](AGENTS.md) and [current development status](docs/release-development.md)
> before running automation. Older release counts/layouts below are historical;
> full current-release regression/package refresh is pending.
> Latest addition: [216 Thumb PC-relative cases per CPU](docs/thumb-pc.md), all passing.

# Agent handoff — milestone 10

## Deliverable and scope

`suite.nds` is a real devkitARM bare-metal C++/assembly NDS ROM. Release bundle:
`../nds-suite-milestone10.zip`. Keep only the latest verified milestone archive;
remove its predecessor only after the new package passes release guards. See
`reports/summary.md` for ROM hash and current evidence, `readme.md` for usage.
**The exhaustive project is not complete.** Only the mGBA carry table is source-ported;
all 23 Rockwrestler asserting groups are automated, not all passing.

User priorities: emulator-first; use GBATEK; keep expanding opcode coverage;
actual ARM7-specific execution as well as ARM9 correctness/system behavior;
detailed lower-screen diagnostics; score at completion; continue after failures.
Latest choice: **both ARM9 instruction correctness and system behavior, correctness first**.
Do not weaken expectations to match an emulator or call a finite matrix exhaustive.

## Current results

- ARM9 6,498 vectors: **6,178 PASS / 320 FAIL**.
- ARM7 5,174 vectors: **5,151 PASS / 23 FAIL**.
- Isolated/checkpointed full plan: **17/25 = 68%**, six returned group failures,
  two native SIGFPE division crashes (groups20/21), zero unrun after supervision.
- ARM9: 64 region-mirror +96 AP-alias discrepancies; 32 Thumb UDF nonentries;
  64 data-abort +64 prefetch-abort nonentries. MPU-enable readback is set on DS.
- ARM7: unchanged 12 odd LDRH +8 odd LDRSH +3 PC/pipeline discrepancies.
- 117 instrumented C++ sites, 160 observed events; original groups are internally
  fail-fast. Uninstrumented paths remain group-only/unknown.

Read `docs/compare-operand2.md` first, then `docs/arm9-milestone8.md`. It specifies exact matrices, safety, failure
meanings and oracle limits. `docs/remaining-cases.json` is the open-work ledger.
Historical milestone-6/7 documents retain explicitly labeled historical counts.

## Milestone 10 addition

256 TST/TEQ/CMP/CMN register-controlled operand2 cases per CPU: ARM9 IDs6243–6498,
ARM7 IDs4919–5174. All pass on DS and the ARM946/ti925t portable oracle. New scripts:
`compare_operand2.py`, `run_qemu_compare2.py`, `test_compare2.py`. Nine scenarios
verify ELF addresses/identical helper bytes,16 primary and16 NZCV mutations per CPU,
and212 unintended source-write failures per CPU from equivalent flag-setting ALU
opcode mutations.44 mutated cases coincidentally preserve the source value and
are not counted as detected faults. Total host checks36. Next substantial ARM9
expansion must address trace capacity first. Raw exceptions/timing are unchanged.

## Historical milestone 9 addition

256 register-controlled ADC/SBC/AND/EOR operand2 cases per CPU, all PASS on DS and
portable ARM946/ti925t oracle. New IDs ARM9:5987–6242, ARM7:4663–4918.
Generator `register_operand2.py`; `test_operand2.py` verifies actual ELF addresses,
byte-identical helpers across CPUs,16 primary and16 NZCV mutations per CPU, and
64 lost carry-input cases per CPU via actual ADC→ADD/SBC→SUB instruction mutations.
Nine new scenarios;34 total host tests. Raw exceptions/timing are not expanded by
this matrix. Older IDs below remain stable.

## Previous additions and stable IDs

Shared 1–3390 unchanged. ARM9 3391–3486 retains LDRD/STRD. New ARM9:
3487–4446 DSP960;4447–5402 privileged956;5403–5498 IRQ96;
5499–5730 CP15232;5731–5794 SWI64;5795–5858 UDF64;
5859–5922 dataabort64;5923–5986 prefetchabort64.
ARM7 3391–3578 retains CPU-specific188;3579–4534 privileged956;
4535–4630 IRQ96;4631–4662 nestedIRQ32. Identity is **(CPU,ID)**.

Generators: `arm9_dsp.py`, parameterized `arm7_privileged.py`/`arm7_irq.py`,
`arm9_cp15.py`, `arm9_exceptions.py`, `arm7_nested.py`; integration in
`generate_v5.py` and `generate_arm7.py`. Build regenerates assembly, checker,
manifest and UI metadata. No ARMv5 instructions are dispatched on ARM7.

Raw exceptions use temporary low ITCM vectors and restore control/ITCM/vector words,
caller and exception-bank state. Abort scratch is explicitly reserved. Raw auxiliary
bits8=count error,9=MPU-enable zero,10=control/ITCM restore error,11=vector restore
error. `B00000DE` is a masked **sentinel**, not observed CPSR when no handler ran.
Prefetch return deliberately clears T/redirects to ARM cleanup; don't describe it as
an unchanged hardware-return path. IRQ callbacks are NDS BIOS-mediated; ARM7 nesting
checks timer3→timer2, not priority, exact raw LR, general nesting or cycle timing.

## QEMU evidence

- `run_qemu.py`: shared3390 PASS ARM946 + ti925t ARMv4T proxy.
- `run_qemu_v5.py`: portable ARM9 **2012 PASS** (96+960+956).
- `run_qemu_privileged.py`: ARM7 portable956 PASS on both models.
- `run_qemu_cp15.py`: **208 PASS** region/AP aliases; excludes24 bitmap cases.
- `run_qemu_exceptions.py`: **128 PASS /128 UNSUPPORTED**, status
  `PARTIAL_UNSUPPORTED`, exit1 on recorded QEMU10.0.13. The unadapted ITCM-control
  access causes undefined before the first SWI. Oracle-only assembly substitutes
  NOP/zero moves for that register and uses low RAM vectors. SWI/UDF pass; MPU-enable
  reads zero, so abort expectations remain independently unverified. ROM unchanged.
- `run_qemu_rock.py`: eleven original CPU groups PASS ARM946.

No exact ARM7TDMI QEMU model here. Integrator/CP is not DS. No physical hardware,
cache/TCM enforcement or timing validation. Do not silently convert unsupported
QEMU cases into PASS, or modify NDS expectations to match QEMU's missing MPU.

## Environment / reproducibility

Workspace files persist; installed packages, `/opt` tools, `.venv`, processes and
shell exports may not. Review `bash scripts/setup.sh`, activate `.venv`, set
`DEVKITARM=/opt/devkitpro/devkitARM`, `DEVKITPRO=/opt/devkitpro`.
Recorded devkitARM r68/GCC16.1.0, ndstool2.3.1, QEMU10.0.13,
py-desmume0.0.9/bundled DeSmuME0.9.12. Setup installs signed current packages, not
an archived exact environment. A signing-key HTTP403 was resolved using curl
user-agent `dkp apt`; don't bypass signature verification.

## Verification/release sequence

```sh
bash scripts/build.sh
python3 scripts/run_qemu.py
python3 scripts/run_qemu_v5.py
python3 scripts/run_qemu_privileged.py
python3 scripts/run_qemu_cp15.py
python3 scripts/run_qemu_operand2.py
python3 scripts/run_qemu_compare2.py
python3 scripts/run_qemu_exceptions.py   # expected exit1 PARTIAL_UNSUPPORTED here
python3 scripts/run_qemu_rock.py
python3 scripts/run_desmume.py --output reports/desmume.json
python3 scripts/run_resilient.py --output reports/resilient.json --screenshot reports/score-screen.png
python3 scripts/run_desmume.py --sequential --output reports/sequential.json
python3 scripts/run_desmume.py --group 0 --output reports/cpu9.json
python3 scripts/run_desmume.py --group 1 --output reports/cpu7.json
python3 scripts/test_negative.py
python3 scripts/test_recovery.py
python3 scripts/test_ui.py
python3 scripts/test_diagnostics.py
python3 scripts/test_arm7_specific.py
python3 scripts/test_privileged.py
python3 scripts/test_milestone8.py
python3 scripts/test_operand2.py
python3 scripts/test_compare2.py
python3 scripts/compact_evidence.py
python3 scripts/test_host.py 2>&1 | tee reports/host-tests.log
python3 scripts/export_debug.py reports/resilient.json
python3 scripts/package.py
```

Expected group nonpasses exit1; don't chain with `&&` and skip later checks.
Harness exceptions are not legitimate test outcomes. Negative tests precede
`test_diagnostics.py`; host tests consume final evidence. Rebuild twice, compare
ROM hashes, and run release guards before packaging. All injection ROMs are
private temporary copies. Source hashes cover the distributed unmodified ROM.

`test_milestone8.py` has eight scenarios: baseline/restore checks,220 helper-level
primary mutations, sticky-Q auxiliary mutation,96 ARM9 missing IRQ counters,
64 raw SWI missing counts, baseline32 nested IRQs,32 missing second IRQ counts.
Earlier 15 privileged expectation mutations, IRQ gating/counter, ARM7 attribution,
hang/recovery and 12-page real-key tests remain. Inspect actual report statuses.

`compact_evidence.py` pools identical byte-hashed blobs under `reports/blobs` and
updates JSON paths, preserving all referenced bytes. Keep blobs with the reports.
The combined manifest fingerprint and trace SHA-256 protect against stale restore.

## Runtime invariants

| Address/range | Purpose |
|---|---|
|02000100..<021f0000|ARM9 image; linker and post-build guard|
|021f0000..021f0fff|4KiB reserved raw-abort scratch|
|02200000..<02300000|ARM7 main-RAM image|
|0380f000 /0380ff00|ARM7 private-WRAM normal/IRQ stacks|
|02300000 /02340000|Separate256KiB CPU trace slots,6552 records each|
|02380000|C++ assertion journal|
|023ed000|diagnostic restoration barrier|
|023ee000|ARM7 reset request/ack/release/ready|
|023ef000 /023ef100|legacy CPU details|
|023f0000|scorev2,18 words|
|023f0080|UI metadata,9 words|
|023f0100|25 group state/cause pairs|

Trace capacity remaining:54 ARM9 /1378 ARM7. Expand the trace protocol/layout before adding more than54 ARM9 cases; never truncate. Each record is40 bytes; vector table
ABI is seven words `fn,a,b,initial,result,flags,mask`. Schema2 debug manifest has
separate full CPU lists. Duplicate table tuples exist (AP0 across regions); locate
mutation rows by table offset/ID rather than assuming every tuple is unique.

Both screens: top score; 12 lower pages. Pages9/10 (zero-based) are CPU explorers,
page11 journal. Left/Right page; Up/Down browse; A family/case; B next failure.
Use `emu.cycle(False)` for preserved keys and two idle frames for final LCD scanout.

Returned failures unwind only within valid nonnested `suite_call`; cleanup restores
ARM9 baseline and ARM7 cooperative service before IPC/WRAM/VRAM. Hard hangs/native
crashes require host restart. BEGIN checkpoints must identify the active group.
Restored traces are visibly historical. CPU7 reruns on fresh boot: import its old
trace only after that run completes and only when its original group was resolved.
No physical timing equivalence after relocation/instrumentation/resumption.

## Next priorities

1. Obtain an independent MPU-access-fault oracle/hardware baseline, preserving the
   distinction between QEMU model capability and DS backend behavior.
2. ARM7 BIOS-owned raw exception observation strategy; FIQ stimulus, IRQ raw LR and
   priority/deeper nesting; complete legal register/encoding/alias/bus matrices.
3. Cache/TCM enforcement and more ARM9 doubleword/interworking edges; timing only
   with controlled reference. UMAAL is ARMv6, not a missing legal NDS opcode.
4. More assembly assertion instrumentation and relevant MMIO operands, avoiding
   duplicate volatile reads; remaining source-level mGBA groups.
5. Additional emulator adapters and actual DS baseline.
6. Resolve Rockwrestler redistribution permission before public publication: its
   pinned snapshot lacks LICENSE/COPYING. Preserve MIT carry notice and NOTICE.md.
